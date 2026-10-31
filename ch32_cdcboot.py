"""
CH32V203 USB CDC bootloader upload for PlatformIO.

1. If the bootloader is already enumerated (1A86:FE0E), use it.
2. Else, if the app is running (1A86:FE0D), send a 1200bps touch and wait
   for the bootloader to appear.
3. Erase / write / CRC-check the app image at 0x2000, then start it.

The bootloader itself (env:bootloader) is installed once through the
factory BootROM: hold BOOT0, reset, then `pio run -e bootloader -t upload`.

platformio.ini:
    upload_protocol = ch32-cdcboot
    extra_scripts = pre:ch32_cdcboot.py
"""

import os
import struct
import sys
import time
import zlib

VID = 0x1A86
PID_APP = 0xFE0D
PID_BOOT = 0xFE0E
PAGE = 256


def _find_port(pid):
    from serial.tools.list_ports import comports
    for p in comports():
        if p.vid == VID and p.pid == pid:
            return p.device
    return None


def _wait_port(pid, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        port = _find_port(pid)
        if port:
            return port
        time.sleep(0.1)
    return None


def _open(port):
    import serial
    # udev may still be applying permissions right after enumeration
    for _ in range(30):
        try:
            return serial.Serial(port, 115200, timeout=5)
        except serial.SerialException:
            time.sleep(0.1)
    return serial.Serial(port, 115200, timeout=5)


class Boot:
    def __init__(self, ser):
        self.ser = ser

    def _read(self, n):
        data = self.ser.read(n)
        if len(data) != n:
            raise IOError("bootloader timeout (got %d of %d bytes)" % (len(data), n))
        return data

    def cmd(self, op, addr=0, length=0, data=b"", reply=0):
        self.ser.write(struct.pack("<cIH", op, addr, length) + data)
        status = self._read(1)
        if status == b"E":
            raise IOError("bootloader error %d on %r @0x%X" % (self._read(1)[0], op, addr))
        if status != b"K":
            raise IOError("bad reply %r" % status)
        return self._read(reply) if reply else b""


def _flash(firmware):
    if firmware.endswith(".elf"):
        bin_path = firmware[:-4] + ".bin"
        if os.path.isfile(bin_path):
            firmware = bin_path
    image = open(firmware, "rb").read()
    image += b"\xff" * (-len(image) % PAGE)

    port = _find_port(PID_BOOT)
    if not port:
        app = _find_port(PID_APP)
        if not app:
            print("No board found (1A86:FE0D app or 1A86:FE0E bootloader). Double-tap RESET to start the bootloader.")
            return 1
        import serial
        print("Triggering bootloader via 1200bps touch on %s" % app)
        serial.Serial(app, 1200).close()
        port = _wait_port(PID_BOOT, 8.0)
        if not port:
            print("Bootloader did not appear. Double-tap RESET and retry.")
            return 1

    print("Bootloader on %s" % port)
    ser = _open(port)
    ser.reset_input_buffer()
    b = Boot(ser)

    info = b.cmd(b"I", reply=23)
    if info[:13] != b"CH32V203BOOT1":
        print("Unexpected bootloader id %r" % info[:13])
        return 1
    app_base, flash_end, page = struct.unpack("<IIH", info[13:23])
    if len(image) > flash_end - app_base:
        print("Image too large: %d > %d bytes" % (len(image), flash_end - app_base))
        return 1

    t0 = time.monotonic()
    for off in range(0, len(image), 0x4000):
        b.cmd(b"E", app_base + off, min(0x4000, len(image) - off))
    for off in range(0, len(image), page):
        b.cmd(b"W", app_base + off, page, image[off:off + page])
        sys.stdout.write("\rWriting %d / %d bytes" % (off + page, len(image)))
        sys.stdout.flush()
    print()

    crc = struct.unpack("<I", b.cmd(b"C", app_base | 0x80000000, len(image) // page, reply=4))[0]
    if crc != zlib.crc32(image):
        print("CRC mismatch: device %08X, image %08X" % (crc, zlib.crc32(image)))
        return 1
    print("Verify OK (CRC32 %08X), %.1f s" % (crc, time.monotonic() - t0))

    b.cmd(b"G")
    ser.close()
    print("Started app")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: %s <firmware.bin|elf>" % sys.argv[0], file=sys.stderr)
        sys.exit(2)
    sys.exit(_flash(sys.argv[1]))
else:
    Import("env")  # type: ignore  # noqa: F821
    _proto = env.GetProjectOption("upload_protocol", "")  # type: ignore
    if _proto == "ch32-cdcboot":
        env.Replace(UPLOAD_PROTOCOL="custom")  # type: ignore
        _script = os.path.join(env.subst("$PROJECT_DIR"), "ch32_cdcboot.py")  # type: ignore
        env.Replace(UPLOADCMD='"%s" "%s" "$SOURCE"' % (sys.executable, _script))  # type: ignore
    elif _proto == "wchisp-bootrom":
        # One-time bootloader install through the factory BootROM (BOOT0 held)
        env.Replace(UPLOAD_PROTOCOL="custom")  # type: ignore
        _wchisp = os.environ.get("WCHISP") or os.path.join(
            env.PioPlatform().get_package_dir("tool-wchisp") or "", "wchisp"  # type: ignore
        )
        env.Replace(UPLOADCMD='"%s" flash "$BUILD_DIR/${PROGNAME}.bin"' % _wchisp)  # type: ignore
