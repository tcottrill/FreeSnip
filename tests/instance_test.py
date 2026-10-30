import os
import pathlib
import subprocess
import sys
import tempfile
import time

with tempfile.TemporaryDirectory(prefix="snip-instance-") as tmp:
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_RUNTIME_DIR=tmp)
    first = subprocess.Popen([sys.argv[1], "--tray"], env=env,
                             stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        socket = pathlib.Path(tmp) / "tcottrill-snip" / "instance"
        deadline = time.monotonic() + 5
        while not socket.exists() and time.monotonic() < deadline:
            if first.poll() is not None:
                raise RuntimeError(first.stderr.read().decode())
            time.sleep(0.02)
        assert socket.exists(), "First instance never opened its socket"
        second = subprocess.run([sys.argv[1], "--tray"], env=env, timeout=5,
                                capture_output=True)
        assert second.returncode == 0, second.stderr.decode()
        assert first.poll() is None, "First instance unexpectedly exited"
        print("Second launch reached running instance; primary stayed alive")
    finally:
        first.terminate()
        try:
            first.wait(timeout=5)
        except subprocess.TimeoutExpired:
            first.kill()
            first.wait()
