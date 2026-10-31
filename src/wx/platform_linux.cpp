// Linux (wxGTK 3): an X11 screen grab or the XDG Screenshot portal, the
// GlobalShortcuts portal, a StatusNotifierItem tray icon with a D-Bus menu, and
// the single instance as an owned D-Bus name. All D-Bus work uses GLib's GDBus,
// which GTK already depends on, so nothing else is needed at run time; inside the
// Flatpak the portals and the tray watcher are the only services FreeSnip talks to.
#include "snip.h"
#include <wx/clipbrd.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <unistd.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace snip::platform {
namespace {

const char* const kAppId = "io.github.tcottrill.FreeSnip";
const char* const kAppPath = "/io/github/tcottrill/FreeSnip";
const char* const kPortal = "org.freedesktop.portal.Desktop";
const char* const kPortalPath = "/org/freedesktop/portal/desktop";

GDBusConnection* bus()
{
 static GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
 return connection;
}

wxString errorText(GError* error) { return error ? wxString::FromUTF8(error->message) : wxString(); }

std::function<void()> g_snip, g_show;

// ---- Single instance: the first copy owns io.github.tcottrill.FreeSnip -----------

void instanceCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                  GVariant*, GDBusMethodInvocation* invocation, gpointer)
{
 const std::string name = method;
 g_dbus_method_invocation_return_value(invocation, nullptr);
 wxTheApp->CallAfter([name] {
  if (name == "Snip" && g_snip) g_snip();
  else if (name == "Show" && g_show) g_show();
 });
}

void registerInstance()
{
 static GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(
  "<node><interface name='io.github.tcottrill.FreeSnip'><method name='Snip'/><method name='Show'/></interface></node>", nullptr);
 static const GDBusInterfaceVTable vtable{ instanceCall, nullptr, nullptr, {} };
 if (info) g_dbus_connection_register_object(bus(), kAppPath, info->interfaces[0], &vtable, nullptr, nullptr, nullptr);
}

// ---- Portal requests ---------------------------------------------------------------
// A portal method returns a Request object path at once and reports the result
// later in its Response signal. Requests live in a table, so a late reply to a
// cancelled request finds nothing.

using Response = std::function<void(guint32 code, GVariant* results, const wxString& error)>;
struct Pending { std::string path; guint signal = 0, timeout = 0; Response done; };
std::map<unsigned, Pending> g_requests;
unsigned g_nextRequest = 0;

std::string newToken()
{
 static unsigned n = 0;
 return "snip" + std::to_string(getpid()) + "_" + std::to_string(++n);
}

void unsubscribe(Pending& p)
{
 if (p.signal) g_dbus_connection_signal_unsubscribe(bus(), p.signal);
 p.signal = 0;
}

void dropRequest(unsigned id)
{
 auto it = g_requests.find(id);
 if (it == g_requests.end()) return;
 unsubscribe(it->second);
 if (it->second.timeout) g_source_remove(it->second.timeout);
 g_requests.erase(it);
}

void closeRequest(unsigned id)
{
 auto it = g_requests.find(id);
 if (it == g_requests.end()) return;
 if (!it->second.path.empty())
  g_dbus_connection_call(bus(), kPortal, it->second.path.c_str(), "org.freedesktop.portal.Request", "Close",
                         nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
 dropRequest(id);
}

void finishRequest(unsigned id, guint32 code, GVariant* results, const wxString& error)
{
 auto it = g_requests.find(id);
 if (it == g_requests.end()) return;
 Response done = std::move(it->second.done);
 dropRequest(id);
 if (done) done(code, results, error);
}

void onResponse(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params, gpointer data)
{
 guint32 code = 2;
 GVariant* results = nullptr;
 g_variant_get(params, "(u@a{sv})", &code, &results);
 finishRequest(GPOINTER_TO_UINT(data), code, results, {});
 if (results) g_variant_unref(results);
}

void listen(unsigned id, const std::string& path)
{
 Pending& p = g_requests[id];
 unsubscribe(p);
 p.path = path;
 p.signal = g_dbus_connection_signal_subscribe(bus(), nullptr, "org.freedesktop.portal.Request", "Response",
                                                path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE, onResponse,
                                                GUINT_TO_POINTER(id), nullptr);
}

std::string requestPath(const std::string& token)
{
 std::string sender = g_dbus_connection_get_unique_name(bus()) + 1;   // without the ':'
 for (char& c : sender) if (c == '.') c = '_';
 return std::string(kPortalPath) + "/request/" + sender + "/" + token;
}

void onCalled(GObject* source, GAsyncResult* result, gpointer data)
{
 const unsigned id = GPOINTER_TO_UINT(data);
 GError* error = nullptr;
 GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
 if (!reply) {
  const wxString text = errorText(error);
  g_error_free(error);
  finishRequest(id, 3, nullptr, text);
  return;
 }
 const char* handle = nullptr;
 g_variant_get(reply, "(&o)", &handle);
 auto it = g_requests.find(id);
 if (it != g_requests.end() && handle && it->second.path != handle) listen(id, handle);   // older portals
 g_variant_unref(reply);
}

// Calls a portal method; args must already hold the handle_token option. done
// gets code 0 (success), 1 (cancelled), 2 (failed) or 3 (the call itself failed).
unsigned portalRequest(const char* iface, const char* method, GVariant* args, const std::string& token,
                       guint timeoutMs, Response done)
{
 const unsigned id = ++g_nextRequest;
 g_requests[id].done = std::move(done);
 listen(id, requestPath(token));   // before the call, so no Response can be missed
 g_requests[id].timeout = g_timeout_add(timeoutMs, [](gpointer data) -> gboolean {
  const unsigned id = GPOINTER_TO_UINT(data);
  auto it = g_requests.find(id);
  if (it != g_requests.end()) it->second.timeout = 0;
  if (it != g_requests.end() && !it->second.path.empty())
   g_dbus_connection_call(bus(), kPortal, it->second.path.c_str(), "org.freedesktop.portal.Request", "Close",
                          nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
  finishRequest(id, 4, nullptr, {});
  return G_SOURCE_REMOVE;
 }, GUINT_TO_POINTER(id));
 g_dbus_connection_call(bus(), kPortal, kPortalPath, iface, method, args, G_VARIANT_TYPE("(o)"),
                        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, onCalled, GUINT_TO_POINTER(id));
 return id;
}

// ---- Desktop capture -----------------------------------------------------------------

unsigned g_shotRequest = 0, g_shotGeneration = 0;
std::function<void(DesktopShot, wxString)> g_shotDone;

void finishShot(DesktopShot shot, const wxString& error)
{
 g_shotRequest = 0;
 auto done = std::move(g_shotDone);
 g_shotDone = nullptr;
 if (done) done(std::move(shot), error);
}

std::string displayType() { return G_OBJECT_TYPE_NAME(gdk_display_get_default()); }

// XWayland cannot read the Wayland desktop: use the portal in any Wayland session,
// even when GTK itself runs on X11.
bool waylandSession()
{
 const char* type = std::getenv("XDG_SESSION_TYPE");
 const char* wayland = std::getenv("WAYLAND_DISPLAY");
 return displayType().find("Wayland") != std::string::npos || (type && !std::strcmp(type, "wayland")) || (wayland && *wayland);
}

wxImage fromPixbuf(GdkPixbuf* pixbuf)
{
 const int w = gdk_pixbuf_get_width(pixbuf), h = gdk_pixbuf_get_height(pixbuf);
 const int stride = gdk_pixbuf_get_rowstride(pixbuf), channels = gdk_pixbuf_get_n_channels(pixbuf);
 const guchar* src = gdk_pixbuf_get_pixels(pixbuf);
 wxImage image(w, h, false);
 unsigned char* dst = image.GetData();
 for (int y = 0; y < h; ++y) {
  const guchar* row = src + static_cast<size_t>(y) * stride;
  for (int x = 0; x < w; ++x, dst += 3) std::memcpy(dst, row + x * channels, 3);
 }
 return image;
}

// Monitors in logical coordinates, mapped onto the image. The image covers their
// union at one scale; a desktop that composed it differently gets an error rather
// than a misaligned frozen screen.
bool mapOutputs(DesktopShot& shot, wxString& error)
{
 GdkDisplay* display = gdk_display_get_default();
 wxRect bounds;
 for (int i = 0; i < gdk_display_get_n_monitors(display); ++i) {
  GdkRectangle g;
  gdk_monitor_get_geometry(gdk_display_get_monitor(display, i), &g);
  shot.outputs.push_back({ wxRect(g.x, g.y, g.width, g.height), {}, i });
  bounds = bounds.IsEmpty() ? shot.outputs.back().geometry : bounds.Union(shot.outputs.back().geometry);
 }
 const double sx = bounds.IsEmpty() ? 0 : double(shot.image.GetWidth()) / bounds.width;
 const double sy = bounds.IsEmpty() ? 0 : double(shot.image.GetHeight()) / bounds.height;
 if (bounds.IsEmpty() || std::abs(sx - sy) > 1.0 / std::min(bounds.width, bounds.height)) {
  error = "The desktop screenshot does not match the monitor layout. Use Capture > Desktop options for this "
          "layout, or retry after checking display scaling.";
  return false;
 }
 for (auto& o : shot.outputs) {
  const int x0 = int(std::lround((o.geometry.x - bounds.x) * sx)), y0 = int(std::lround((o.geometry.y - bounds.y) * sx));
  const int x1 = int(std::lround((o.geometry.GetRight() + 1 - bounds.x) * sx));
  const int y1 = int(std::lround((o.geometry.GetBottom() + 1 - bounds.y) * sx));
  o.pixels = wxRect(x0, y0, x1 - x0, y1 - y0);
  o.pixels.Intersect(wxRect(shot.image.GetSize()));
 }
 return true;
}

bool grabX11(DesktopShot& shot, wxString& error)
{
 GdkWindow* root = gdk_get_default_root_window();
 GdkPixbuf* pixbuf = gdk_pixbuf_get_from_window(root, 0, 0, gdk_window_get_width(root), gdk_window_get_height(root));
 if (!pixbuf) { error = "The display server did not return a screen image."; return false; }
 shot.image = fromPixbuf(pixbuf);
 g_object_unref(pixbuf);
 return mapOutputs(shot, error);
}

void screenshotResponse(CaptureMode mode, guint32 code, GVariant* results, const wxString& callError)
{
 DesktopShot shot;
 if (code == 1) { finishShot({}, {}); return; }
 if (code == 3) {
  finishShot({}, "Desktop screenshot service unavailable: " + callError +
                 "\nCheck that your desktop's XDG screenshot portal is installed and running.");
  return;
 }
 if (code == 4) {
  finishShot({}, "Screen capture timed out. Check your desktop's screenshot permission dialog and try again.");
  return;
 }
 const char* uri = nullptr;
 if (code != 0 || !results || !g_variant_lookup(results, "uri", "&s", &uri)) {
  finishShot({}, "The desktop could not capture a screenshot.");
  return;
 }
 gchar* path = g_filename_from_uri(uri, nullptr, nullptr);
 if (!path) { finishShot({}, "The desktop returned an invalid screenshot file."); return; }
 {
  wxLogNull quiet;
  shot.image.LoadFile(wxString::FromUTF8(path));
 }
 g_free(path);
 if (!shot.image.IsOk()) { finishShot({}, "Cannot open the screenshot returned by the desktop."); return; }
 wxString error;
 if (mode == CaptureMode::Rectangle && !mapOutputs(shot, error)) { finishShot({}, error); return; }
 finishShot(std::move(shot), {});
}

void requestScreenshot(CaptureMode mode, guint32 version, guint32 targets)
{
 if (mode == CaptureMode::ActiveWindow && (version < 3 || !(targets & 8))) {
  finishShot({}, "This desktop does not advertise direct active-window capture. Use Capture > Desktop options "
                 "to select a window with your desktop's picker.");
  return;
 }
 const std::string token = newToken();
 GVariantBuilder options;
 g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
 g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
 g_variant_builder_add(&options, "{sv}", "interactive", g_variant_new_boolean(mode == CaptureMode::DesktopOptions));
 g_variant_builder_add(&options, "{sv}", "modal", g_variant_new_boolean(FALSE));
 if (mode == CaptureMode::ActiveWindow) g_variant_builder_add(&options, "{sv}", "target", g_variant_new_uint32(8));
 else if (mode != CaptureMode::DesktopOptions && version >= 3 && (targets & 1))
  g_variant_builder_add(&options, "{sv}", "target", g_variant_new_uint32(1));
 g_shotRequest = portalRequest("org.freedesktop.portal.Screenshot", "Screenshot",
  g_variant_new("(sa{sv})", "", &options), token, 120000,
  [mode](guint32 code, GVariant* results, const wxString& error) { screenshotResponse(mode, code, results, error); });
}

struct PropertiesCall { unsigned generation; CaptureMode mode; };

void portalScreenshot(CaptureMode mode)
{
 if (mode == CaptureMode::DesktopOptions) { requestScreenshot(mode, 0, 0); return; }
 g_dbus_connection_call(bus(), kPortal, kPortalPath, "org.freedesktop.DBus.Properties", "GetAll",
  g_variant_new("(s)", "org.freedesktop.portal.Screenshot"), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE,
  10000, nullptr, [](GObject* source, GAsyncResult* result, gpointer data) {
   const PropertiesCall call = *static_cast<PropertiesCall*>(data);
   delete static_cast<PropertiesCall*>(data);
   GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, nullptr);
   guint32 version = 0, targets = 0;
   if (reply) {
    GVariant* values = g_variant_get_child_value(reply, 0);
    g_variant_lookup(values, "version", "u", &version);
    g_variant_lookup(values, "AvailableTargets", "u", &targets);
    g_variant_unref(values);
    g_variant_unref(reply);
   }
   if (call.generation == g_shotGeneration) requestScreenshot(call.mode, version, targets);
  }, new PropertiesCall{ g_shotGeneration, mode });
}

// ---- Global shortcut portal --------------------------------------------------------

std::string g_session;
unsigned g_shortcutRequest = 0;
guint g_activated = 0;
std::function<void(const wxString&)> g_status;

void shortcutStatus(const wxString& text) { if (g_status) g_status(text); }

void closeSession()
{
 if (g_shortcutRequest) closeRequest(g_shortcutRequest);
 g_shortcutRequest = 0;
 if (!g_session.empty())
  g_dbus_connection_call(bus(), kPortal, g_session.c_str(), "org.freedesktop.portal.Session", "Close",
                         nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
 g_session.clear();
}

wxString shortcutFailure(guint32 code, const wxString& fallback)
{
 if (code == 1) return "Shortcut setup cancelled";
 if (code == 3) return "Global shortcut setup is unavailable on this desktop. Assign the FreeSnip launcher in desktop keyboard settings.";
 if (code == 4) return "Shortcut setup timed out. You can also assign the FreeSnip launcher in desktop settings.";
 return fallback;
}

void bindShortcut()
{
 const std::string token = newToken();
 GVariantBuilder props;
 g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
 g_variant_builder_add(&props, "{sv}", "description", g_variant_new_string("Take a new snip"));
 g_variant_builder_add(&props, "{sv}", "preferred_trigger", g_variant_new_string("CTRL+ALT+s"));
 GVariantBuilder shortcuts;
 g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
 g_variant_builder_add(&shortcuts, "(sa{sv})", "snip", &props);
 GVariantBuilder options;
 g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
 g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
 g_shortcutRequest = portalRequest("org.freedesktop.portal.GlobalShortcuts", "BindShortcuts",
  g_variant_new("(oa(sa{sv})sa{sv})", g_session.c_str(), &shortcuts, "", &options), token, 120000,
  [](guint32 code, GVariant*, const wxString&) {
   g_shortcutRequest = 0;
   if (code != 0) { closeSession(); shortcutStatus(shortcutFailure(code, "Desktop shortcut setup failed")); return; }
   shortcutStatus("Global shortcut enabled. The desktop controls the assigned key.");
  });
}

void onActivated(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params, gpointer)
{
 const char* session = nullptr;
 const char* id = nullptr;
 g_variant_get_child(params, 0, "&o", &session);
 g_variant_get_child(params, 1, "&s", &id);
 if (!g_session.empty() && session && g_session == session && id && !std::strcmp(id, "snip"))
  wxTheApp->CallAfter([] { if (g_snip) g_snip(); });
}

// ---- Tray: StatusNotifierItem + com.canonical.dbusmenu --------------------------------

TrayActions g_tray;
wxString g_tooltip;
guint g_itemObject = 0, g_menuObject = 0;
guint32 g_menuRevision = 1;
struct MenuNode { TrayItem item; std::vector<int> children; };
std::map<int, MenuNode> g_menu;

const char kItemXml[] =
 "<node><interface name='org.kde.StatusNotifierItem'>"
 "<property name='Category' type='s' access='read'/>"
 "<property name='Id' type='s' access='read'/>"
 "<property name='Title' type='s' access='read'/>"
 "<property name='Status' type='s' access='read'/>"
 "<property name='WindowId' type='i' access='read'/>"
 "<property name='IconName' type='s' access='read'/>"
 "<property name='IconPixmap' type='a(iiay)' access='read'/>"
 "<property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
 "<property name='ItemIsMenu' type='b' access='read'/>"
 "<property name='Menu' type='o' access='read'/>"
 "<method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
 "<method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
 "<method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
 "<method name='Scroll'><arg type='i' direction='in'/><arg type='s' direction='in'/></method>"
 "<signal name='NewIcon'/><signal name='NewToolTip'/><signal name='NewStatus'><arg type='s'/></signal>"
 "</interface></node>";

const char kMenuXml[] =
 "<node><interface name='com.canonical.dbusmenu'>"
 "<property name='Version' type='u' access='read'/>"
 "<property name='TextDirection' type='s' access='read'/>"
 "<property name='Status' type='s' access='read'/>"
 "<property name='IconThemePath' type='as' access='read'/>"
 "<method name='GetLayout'><arg type='i' direction='in'/><arg type='i' direction='in'/><arg type='as' direction='in'/>"
 "<arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/></method>"
 "<method name='GetGroupProperties'><arg type='ai' direction='in'/><arg type='as' direction='in'/>"
 "<arg type='a(ia{sv})' direction='out'/></method>"
 "<method name='GetProperty'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
 "<method name='Event'><arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='in'/>"
 "<arg type='u' direction='in'/></method>"
 "<method name='EventGroup'><arg type='a(isvu)' direction='in'/><arg type='ai' direction='out'/></method>"
 "<method name='AboutToShow'><arg type='i' direction='in'/><arg type='b' direction='out'/></method>"
 "<method name='AboutToShowGroup'><arg type='ai' direction='in'/><arg type='ai' direction='out'/><arg type='ai' direction='out'/></method>"
 "<signal name='ItemsPropertiesUpdated'><arg type='a(ia{sv})'/><arg type='a(ia{sv})'/></signal>"
 "<signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
 "<signal name='ItemActivationRequested'><arg type='i'/><arg type='u'/></signal>"
 "</interface></node>";

// Icon sizes for hosts that cannot find the themed icon (a build run outside the Flatpak).
GVariant* iconPixmaps()
{
 GVariantBuilder list;
 g_variant_builder_init(&list, G_VARIANT_TYPE("a(iiay)"));
 for (int s : { 16, 22, 24, 32, 48, 64 }) {
  const wxImage image = appIconImage(s);
  std::vector<guchar> argb(static_cast<size_t>(s) * s * 4);
  for (int y = 0, i = 0; y < s; ++y)
   for (int x = 0; x < s; ++x, i += 4) {
    argb[i] = image.GetAlpha(x, y);
    argb[i + 1] = image.GetRed(x, y);
    argb[i + 2] = image.GetGreen(x, y);
    argb[i + 3] = image.GetBlue(x, y);
   }
  g_variant_builder_add(&list, "(ii@ay)", s, s,
                        g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, argb.data(), argb.size(), 1));
 }
 return g_variant_builder_end(&list);
}

GVariant* itemProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* name, GError**, gpointer)
{
 const std::string p = name;
 if (p == "Category") return g_variant_new_string("ApplicationStatus");
 if (p == "Id") return g_variant_new_string(kAppId);
 if (p == "Title") return g_variant_new_string("FreeSnip");
 if (p == "Status") return g_variant_new_string("Active");
 if (p == "WindowId") return g_variant_new_int32(0);
 if (p == "IconName") return g_variant_new_string(std::getenv("FLATPAK_ID") ? kAppId : "");   // exported by the Flatpak
 if (p == "IconPixmap") return iconPixmaps();
 if (p == "ToolTip") {
  GVariantBuilder none;
  g_variant_builder_init(&none, G_VARIANT_TYPE("a(iiay)"));
  return g_variant_new("(sa(iiay)ss)", "", &none, "FreeSnip", g_tooltip.utf8_str().data());
 }
 if (p == "ItemIsMenu") return g_variant_new_boolean(FALSE);
 if (p == "Menu") return g_variant_new_object_path("/MenuBar");
 return nullptr;
}

void itemCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
              GVariant*, GDBusMethodInvocation* invocation, gpointer)
{
 if (!std::strcmp(method, "Activate")) wxTheApp->CallAfter([] { if (g_tray.click) g_tray.click(); });
 g_dbus_method_invocation_return_value(invocation, nullptr);
}

void rebuildMenu()
{
 g_menu.clear();
 g_menu[0];
 int separator = 100000;
 std::function<void(int, const std::vector<TrayItem>&)> add = [&](int parent, const std::vector<TrayItem>& items) {
  for (const auto& item : items) {
   const int id = item.label.empty() ? separator++ : item.id;
   g_menu[parent].children.push_back(id);
   g_menu[id].item = item;
   add(id, item.submenu);
  }
 };
 if (g_tray.menu) add(0, g_tray.menu());
}

GVariant* menuProperties(int id)
{
 GVariantBuilder props;
 g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
 const MenuNode& node = g_menu[id];
 if (id != 0) {
  if (node.item.label.empty()) {
   g_variant_builder_add(&props, "{sv}", "type", g_variant_new_string("separator"));
  } else {
   wxString label = node.item.label.BeforeFirst('\t');   // no shortcut column in dbusmenu
   label.Replace("_", "__");                              // '_' marks a mnemonic
   g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(label.utf8_str().data()));
   if (node.item.checkable) {
    g_variant_builder_add(&props, "{sv}", "toggle-type", g_variant_new_string("checkmark"));
    g_variant_builder_add(&props, "{sv}", "toggle-state", g_variant_new_int32(node.item.checked ? 1 : 0));
   }
  }
 }
 if (!node.children.empty()) g_variant_builder_add(&props, "{sv}", "children-display", g_variant_new_string("submenu"));
 return g_variant_builder_end(&props);
}

GVariant* menuLayout(int id, int depth)
{
 GVariantBuilder children;
 g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
 if (depth != 0)
  for (int child : g_menu[id].children)
   g_variant_builder_add(&children, "v", menuLayout(child, depth < 0 ? -1 : depth - 1));
 return g_variant_new("(i@a{sv}@av)", id, menuProperties(id), g_variant_builder_end(&children));
}

void menuEvent(int id, const char* event)
{
 if (!std::strcmp(event, "clicked")) wxTheApp->CallAfter([id] { if (g_tray.chosen) g_tray.chosen(id); });
}

void menuCall(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
              GVariant* params, GDBusMethodInvocation* invocation, gpointer)
{
 const std::string m = method;
 if (m == "GetLayout") {
  gint32 parent = 0, depth = -1;
  g_variant_get(params, "(ii@as)", &parent, &depth, nullptr);
  rebuildMenu();
  if (!g_menu.count(parent)) {
   g_dbus_method_invocation_return_dbus_error(invocation, "com.canonical.dbusmenu.Error", "No such menu item");
   return;
  }
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", g_menuRevision, menuLayout(parent, depth)));
 } else if (m == "GetGroupProperties") {
  GVariantIter* ids = nullptr;
  g_variant_get(params, "(ai@as)", &ids, nullptr);
  if (g_menu.empty()) rebuildMenu();
  GVariantBuilder list;
  g_variant_builder_init(&list, G_VARIANT_TYPE("a(ia{sv})"));
  gint32 id = 0;
  bool any = false;
  while (g_variant_iter_next(ids, "i", &id)) {
   any = true;
   if (g_menu.count(id)) g_variant_builder_add(&list, "(i@a{sv})", id, menuProperties(id));
  }
  g_variant_iter_free(ids);
  if (!any) for (const auto& node : g_menu) g_variant_builder_add(&list, "(i@a{sv})", node.first, menuProperties(node.first));
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&list)));
 } else if (m == "GetProperty") {
  gint32 id = 0;
  const char* name = nullptr;
  g_variant_get(params, "(i&s)", &id, &name);
  if (g_menu.empty()) rebuildMenu();
  GVariant* props = menuProperties(g_menu.count(id) ? id : 0);
  GVariant* value = g_variant_lookup_value(props, name, nullptr);
  g_variant_unref(g_variant_ref_sink(props));
  if (!value) {
   g_dbus_method_invocation_return_dbus_error(invocation, "com.canonical.dbusmenu.Error", "No such property");
   return;
  }
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
  g_variant_unref(value);
 } else if (m == "Event") {
  gint32 id = 0;
  const char* event = nullptr;
  g_variant_get(params, "(i&svu)", &id, &event, nullptr, nullptr);
  menuEvent(id, event);
  g_dbus_method_invocation_return_value(invocation, nullptr);
 } else if (m == "EventGroup") {
  GVariantIter* events = nullptr;
  g_variant_get(params, "(a(isvu))", &events);
  gint32 id = 0;
  const char* event = nullptr;
  while (g_variant_iter_next(events, "(i&svu)", &id, &event, nullptr, nullptr)) menuEvent(id, event);
  g_variant_iter_free(events);
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
 } else if (m == "AboutToShow") {
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
 } else if (m == "AboutToShowGroup") {
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai@ai)", g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0),
                                                                 g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
 } else {
  g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownMethod", "Unknown method");
 }
}

GVariant* menuProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* name, GError**, gpointer)
{
 const std::string p = name;
 if (p == "Version") return g_variant_new_uint32(3);
 if (p == "TextDirection") return g_variant_new_string("ltr");
 if (p == "Status") return g_variant_new_string("normal");
 if (p == "IconThemePath") return g_variant_new_strv(nullptr, 0);
 return nullptr;
}

} // namespace

bool otherInstance(const wxString& command)
{
 if (!bus()) return false;   // no session bus: run without the single-instance check
 GVariant* reply = g_dbus_connection_call_sync(bus(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
  "RequestName", g_variant_new("(su)", kAppId, 4u /* DO_NOT_QUEUE */), G_VARIANT_TYPE("(u)"),
  G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
 guint32 result = 1;
 if (reply) { g_variant_get(reply, "(u)", &result); g_variant_unref(reply); }
 if (result != 3) {   // primary owner (or the name cannot be owned here): this is the instance
  registerInstance();
  return false;
 }
 if (command != "tray") {
  GVariant* done = g_dbus_connection_call_sync(bus(), kAppId, kAppPath, "io.github.tcottrill.FreeSnip",
   command == "snip" ? "Snip" : "Show", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  if (done) g_variant_unref(done);
 }
 return true;
}

void startShortcuts(std::function<void()> snip, std::function<void()> show)
{
 g_snip = std::move(snip);
 g_show = std::move(show);
}

void stopShortcuts()
{
 closeSession();
 if (g_activated && bus()) g_dbus_connection_signal_unsubscribe(bus(), g_activated);
 g_activated = 0;
 g_status = nullptr;
 g_snip = g_show = nullptr;
}

wxString shortcutNames() { return {}; }   // the desktop decides the keys

bool canSetupShortcut() { return true; }

void setupShortcut(std::function<void(const wxString&)> status)
{
 g_status = std::move(status);
 if (g_shortcutRequest) return;
 if (!bus()) { shortcutStatus("No desktop session bus is available."); return; }
 closeSession();
 if (!g_activated)
  g_activated = g_dbus_connection_signal_subscribe(bus(), nullptr, "org.freedesktop.portal.GlobalShortcuts", "Activated",
                                                   kPortalPath, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, onActivated, nullptr, nullptr);
 const std::string token = newToken(), sessionToken = newToken();
 GVariantBuilder options;
 g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
 g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
 g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(sessionToken.c_str()));
 shortcutStatus("Waiting for the desktop's shortcut dialog...");
 g_shortcutRequest = portalRequest("org.freedesktop.portal.GlobalShortcuts", "CreateSession",
  g_variant_new("(a{sv})", &options), token, 120000, [](guint32 code, GVariant* results, const wxString&) {
   g_shortcutRequest = 0;
   if (code != 0) { closeSession(); shortcutStatus(shortcutFailure(code, "Desktop shortcut setup failed")); return; }
   GVariant* handle = results ? g_variant_lookup_value(results, "session_handle", nullptr) : nullptr;
   const char* path = handle && (g_variant_is_of_type(handle, G_VARIANT_TYPE_STRING) || g_variant_is_of_type(handle, G_VARIANT_TYPE_OBJECT_PATH))
    ? g_variant_get_string(handle, nullptr) : "";
   g_session = path;
   if (handle) g_variant_unref(handle);
   if (g_session.rfind("/org/freedesktop/portal/desktop/session/", 0) != 0) {
    g_session.clear();
    shortcutStatus("Desktop returned an invalid shortcut session");
    return;
   }
   bindShortcut();
  });
}

void capture(CaptureMode mode, std::function<void(DesktopShot shot, wxString error)> done)
{
 cancelCapture();
 g_shotDone = std::move(done);
 const bool portal = waylandSession() || displayType().find("X11") == std::string::npos ||
                     mode == CaptureMode::ActiveWindow || mode == CaptureMode::DesktopOptions;
 if (!portal) {
  DesktopShot shot;
  wxString error;
  if (!grabX11(shot, error)) shot = DesktopShot{};
  finishShot(std::move(shot), error);
  return;
 }
 if (!bus()) { finishShot({}, "No desktop session bus is available."); return; }
 portalScreenshot(mode);
}

void cancelCapture()
{
 ++g_shotGeneration;
 if (g_shotRequest) closeRequest(g_shotRequest);
 g_shotRequest = 0;
 g_shotDone = nullptr;
}

bool copyImage(wxWindow*, const wxImage& image, const wxMemoryBuffer&)
{
 if (!image.IsOk() || !wxTheClipboard->Open()) return false;
 const bool ok = wxTheClipboard->SetData(new wxImageDataObject(image));   // image/png
 wxTheClipboard->Close();
 return ok;
}

bool startTray(TrayActions actions, const wxString& tooltip)
{
 if (!bus()) return false;
 // Without a watcher (a desktop with no tray) nobody would show the icon.
 GVariant* reply = g_dbus_connection_call_sync(bus(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
  "NameHasOwner", g_variant_new("(s)", "org.kde.StatusNotifierWatcher"), G_VARIANT_TYPE("(b)"),
  G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
 gboolean watcher = FALSE;
 if (reply) { g_variant_get(reply, "(b)", &watcher); g_variant_unref(reply); }
 if (!watcher) return false;

 g_tray = std::move(actions);
 g_tooltip = tooltip;
 static GDBusNodeInfo* item = g_dbus_node_info_new_for_xml(kItemXml, nullptr);
 static GDBusNodeInfo* menu = g_dbus_node_info_new_for_xml(kMenuXml, nullptr);
 static const GDBusInterfaceVTable itemTable{ itemCall, itemProperty, nullptr, {} };
 static const GDBusInterfaceVTable menuTable{ menuCall, menuProperty, nullptr, {} };
 g_itemObject = g_dbus_connection_register_object(bus(), "/StatusNotifierItem", item->interfaces[0], &itemTable, nullptr, nullptr, nullptr);
 g_menuObject = g_dbus_connection_register_object(bus(), "/MenuBar", menu->interfaces[0], &menuTable, nullptr, nullptr, nullptr);
 GError* error = nullptr;
 reply = g_dbus_connection_call_sync(bus(), "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
  "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem", g_variant_new("(s)", "/StatusNotifierItem"),
  nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
 if (reply) { g_variant_unref(reply); return g_itemObject && g_menuObject; }
 if (error) g_error_free(error);
 stopTray();
 return false;
}

void stopTray()
{
 if (g_itemObject) g_dbus_connection_unregister_object(bus(), g_itemObject);
 if (g_menuObject) g_dbus_connection_unregister_object(bus(), g_menuObject);
 g_itemObject = g_menuObject = 0;
 g_tray = {};
}

bool supportsStartup() { return false; }
bool startupEnabled() { return false; }
void setStartup(bool) {}

// Wayland gives clients no window positions: fullscreen on the output instead.
void placeSurface(wxTopLevelWindow* window, const Output& output)
{
 auto* gtk = GTK_WINDOW(window->GetHandle());
 if (output.monitor >= 0) gtk_window_fullscreen_on_monitor(gtk, gdk_screen_get_default(), output.monitor);
 else gtk_window_fullscreen(gtk);
}

void instantWindow(wxTopLevelWindow*) {}

void bringToFront(wxTopLevelWindow* window) { window->Raise(); }

}
