#!/usr/bin/env python3
"""Linux integration test for Snip's D-Bus side, on a private session bus.

Fakes the XDG Screenshot portal and a StatusNotifierWatcher, then checks that Snip
registers its tray item and menu, captures through the portal from the tray menu,
hands --snip from a second launch to the running instance, and exits from the menu.

  dbus-run-session -- python3 tests/linux_dbus_test.py dist/linux/snip

Needs a display (Wayland or X11) and PyGObject. See docs/linux-build.md.
"""
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

import gi
gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib  # noqa: E402

SNIP = os.path.abspath(sys.argv[1])
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
context = GLib.MainContext.default()
failures = 0


def check(ok, name):
    global failures
    print(('PASS' if ok else 'FAIL') + ': ' + name, flush=True)
    if not ok:
        failures += 1


def pump(condition, seconds):
    end = time.time() + seconds
    while time.time() < end:
        while context.iteration(False):
            pass
        if condition():
            return True
        time.sleep(0.02)
    return condition()


def own(name):
    reply = bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'RequestName',
                          GLib.Variant('(su)', (name, 4)), GLib.VariantType('(u)'), Gio.DBusCallFlags.NONE, -1, None)
    assert reply.unpack()[0] == 1, name


# ---- fake screenshot portal ----
PORTAL_XML = """<node><interface name='org.freedesktop.portal.Screenshot'>
<method name='Screenshot'><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>
<property name='version' type='u' access='read'/>
</interface></node>"""
screenshots = []   # options of each Screenshot call
responses = []     # (code, results) to send, in order


def portal_call(conn, sender, path, iface, method, params, invocation):
    _, options = params.unpack()
    request = '/org/freedesktop/portal/desktop/request/%s/%s' % (sender[1:].replace('.', '_'), options['handle_token'])
    screenshots.append(options)
    invocation.return_value(GLib.Variant('(o)', (request,)))
    code, results = responses.pop(0) if responses else (2, {})

    def respond():
        conn.emit_signal(sender, request, 'org.freedesktop.portal.Request', 'Response',
                         GLib.Variant('(ua{sv})', (code, results)))
        return False
    GLib.timeout_add(100, respond)


def portal_property(conn, sender, path, iface, name):
    return GLib.Variant('u', 2)


# ---- fake tray watcher ----
WATCHER_XML = """<node><interface name='org.kde.StatusNotifierWatcher'>
<method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>
</interface></node>"""
items = []


def watcher_call(conn, sender, path, iface, method, params, invocation):
    items.append((sender, params.unpack()[0]))
    invocation.return_value(None)


portal_info = Gio.DBusNodeInfo.new_for_xml(PORTAL_XML)
watcher_info = Gio.DBusNodeInfo.new_for_xml(WATCHER_XML)
bus.register_object('/org/freedesktop/portal/desktop', portal_info.interfaces[0], portal_call, portal_property, None)
bus.register_object('/StatusNotifierWatcher', watcher_info.interfaces[0], watcher_call, None, None)
own('org.freedesktop.portal.Desktop')
own('org.kde.StatusNotifierWatcher')

def write_png(path, width, height, rgb):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + bytes(rgb) * width for _ in range(height))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


shot = os.path.join(tempfile.mkdtemp(), 'screen.png')
write_png(shot, 640, 400, (37, 99, 235))

snip = subprocess.Popen([SNIP, '--tray'])
check(pump(lambda: items, 15), 'tray item registered with the watcher')
sender, item_path = items[0] if items else ('', '')
check(item_path == '/StatusNotifierItem', 'registered by object path')
owner = bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'GetNameOwner',
                      GLib.Variant('(s)', ('io.github.tcottrill.Snip',)), None, Gio.DBusCallFlags.NONE, -1, None)
check(owner.unpack()[0] == sender, 'Snip owns io.github.tcottrill.Snip')


def call(path, iface, method, args, reply_type=None):
    return bus.call_sync(sender, path, iface, method, args, GLib.VariantType(reply_type) if reply_type else None,
                         Gio.DBusCallFlags.NONE, 5000, None).unpack()


props = call('/StatusNotifierItem', 'org.freedesktop.DBus.Properties', 'GetAll',
             GLib.Variant('(s)', ('org.kde.StatusNotifierItem',)))[0]
check(props.get('Menu') == '/MenuBar' and props.get('Id') == 'io.github.tcottrill.Snip', 'item properties')
check(len(props.get('IconPixmap', [])) >= 4, 'item icon pixmaps')

_, layout = call('/MenuBar', 'com.canonical.dbusmenu', 'GetLayout', GLib.Variant('(iias)', (0, -1, [])))
ids = {}


def walk(node, depth=0):
    item_id, item_props, children = node
    if 'label' in item_props:
        ids[item_props['label']] = item_id
    for child in children:
        walk(child, depth + 1)


walk(layout)
check({'New snip', 'Capture', 'Rectangle', 'Full desktop', 'Active window', 'Desktop options...',
       'Show Snip', 'Cancel capture', 'Exit'} <= set(ids), 'menu items: %s' % sorted(ids))


def click(label):
    call('/MenuBar', 'com.canonical.dbusmenu', 'Event',
         GLib.Variant('(isvu)', (ids[label], 'clicked', GLib.Variant('i', 0), 0)))


responses.append((0, {'uri': GLib.Variant('s', GLib.filename_to_uri(shot, None))}))
click('Full desktop')
check(pump(lambda: len(screenshots) == 1, 10), 'Full desktop asks the portal for a screenshot')
check(screenshots and not screenshots[0].get('interactive', True), 'non-interactive screenshot')
pump(lambda: False, 1.5)
check(snip.poll() is None, 'still running after the screenshot arrived')

responses.append((1, {}))   # the user cancels this one
second = subprocess.run([SNIP, '--snip'], timeout=15)
check(second.returncode == 0, 'the second launch exits successfully (exit code %s)' % second.returncode)
check(pump(lambda: len(screenshots) == 2, 10), 'a second --snip starts a capture in the running instance')
pump(lambda: False, 1)
check(snip.poll() is None, 'still running after a cancelled capture')

click('Exit')
try:
    snip.wait(10)
except subprocess.TimeoutExpired:
    snip.kill()
check(snip.returncode == 0, 'Exit in the tray menu quits (exit code %s)' % snip.returncode)
sys.exit(1 if failures else 0)
