"""Exercise real Wayland configure/attach/commit/release against a mock presenter."""
import array
import hashlib
import os
import pathlib
import signal
import socket
import struct
import subprocess
import sys
import tempfile

runtime = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="proroot-wayland-") as temp:
    os.chmod(temp, 0o700)
    bridge_path = str(pathlib.Path(temp) / "present.sock")
    env = dict(os.environ, XDG_RUNTIME_DIR=temp, WAYLAND_DISPLAY="wayland-test")
    # Reuse the socket name after teardown to cover lock/socket cleanup.
    for attempt in range(2):
        server = demo = connection = None
        listener = socket.socket(socket.AF_UNIX)
        listener.bind(bridge_path)
        listener.listen(1)
        listener.settimeout(10)
        try:
            server = subprocess.Popen([str(runtime / "proroot-wayland"), bridge_path, "wayland-test", "192", "288"], env=env)
            demo = subprocess.Popen([str(runtime / "proroot-wayland-demo")], env=env)
            connection, _ = listener.accept()
            connection.settimeout(10)
            digests = set()
            for sequence in range(1, 9):
                header, ancillary, flags, _ = connection.recvmsg(32, socket.CMSG_SPACE(4), socket.MSG_WAITALL)
                assert len(header) == 32 and not flags & socket.MSG_CTRUNC
                magic, version, width, height, stride, fmt, size, serial = struct.unpack("=8I", header)
                assert (magic, version, width, height, stride, fmt, size, serial) == (0x4C575250, 1, 192, 288, 768, 1, 221184, sequence)
                received = array.array("i")
                for level, kind, data in ancillary:
                    assert (level, kind) == (socket.SOL_SOCKET, socket.SCM_RIGHTS)
                    received.frombytes(data)
                assert len(received) == 1
                fd = received[0]
                try:
                    import fcntl
                    assert fcntl.fcntl(fd, fcntl.F_GET_SEALS) & fcntl.F_SEAL_WRITE
                    pixels = os.pread(fd, size, 0)
                    assert len(pixels) == size
                    assert struct.unpack_from("=I", pixels)[0] == 0xFF10221D
                    digests.add(hashlib.sha256(pixels).hexdigest())
                finally:
                    os.close(fd)
                connection.sendall(struct.pack("=I", serial))
            assert len(digests) == 8, "Linux client animation did not change"
            print(f"PASS cycle {attempt + 1}: configure, 8 SHM frames, sealed FD, pixels, ACK/release")
        finally:
            if demo:
                demo.terminate()
                demo.wait(timeout=10)
            if connection:
                connection.close()
            if server:
                server.send_signal(signal.SIGTERM)
                server.wait(timeout=10)
            listener.close()
            os.unlink(bridge_path)
