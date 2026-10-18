#!/usr/bin/env python3
"""Check W64TOOLS error text is red and menu, help, and cancel stay green.

Resets the Ultimate and mounts a one-file D64 on bus id 8. The previous
mount is restored on the way out.
"""
import sys
import time

import c64u
import test_w64conv_c64u as disk

GREEN = 5
RED = 2
HELP_LINE = "NO NUM:    LINE HAS NO NUMBER"


def screen():
    return c64u.read_mem(0x0400, 1000)


def colors(row):
    raw = c64u.read_mem(0xD800, 1000)
    return [b & 0x0F for b in raw[row * 40:row * 40 + 39]]


def expect(row, text, color):
    raw = screen()
    want = text.encode("ascii")
    got = raw[row * 40:row * 40 + len(want)]
    if got != want:
        raise SystemExit("row %d text %r wanted %r" % (row, got, want))
    cols = colors(row)
    n = len(want)
    if cols[:n] != [color] * n:
        raise SystemExit("row %d %r colors %s wanted %d" % (row, text, cols[:n], color))
    tail = [GREEN] * (39 - n)
    if cols[n:] != tail:
        raise SystemExit("row %d tail %s" % (row, cols[n:]))


def wait_row(row, prefix, timeout=15):
    want = prefix.encode("ascii")
    deadline = time.time() + timeout
    last = b""
    while time.time() < deadline:
        got = screen()[row * 40:row * 40 + 40]
        last = got
        if got.startswith(want):
            return
        time.sleep(0.25)
    raise SystemExit("row %d wanted %r got %r" % (row, prefix, last))


def wait_menu(timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = screen()
        if raw[16:24] == b"W64TOOLS" and raw[80:92] == b"1 SEQ TO PRG":
            return
        time.sleep(0.25)
    raise SystemExit("menu did not show")


def load_tool():
    c64u.reset()
    c64u.load_and_run("build/w64tools.prg")
    wait_menu()


def inject_text(text):
    c64u.inject_keys([ord(ch) for ch in text] + [0x0D], settle=0.5)


def main():
    disk.install_retries()
    info = disk.drives()
    name, previous = disk.bus8_drive(info)
    image_path = "/tmp/w64tools-red.d64"
    try:
        disk.build_image(image_path, [(0x81, "SAMPLE", b'10 PRINT "HI"\r')])
        with open(image_path, "rb") as f:
            disk.mount_bytes(name, f.read())

        load_tool()
        expect(2, "1 SEQ TO PRG", GREEN)
        print("menu green", flush=True)

        c64u.inject_keys([ord("?")], settle=0.5)
        wait_row(0, "W64TOOLS HELP")
        expect(6, HELP_LINE, GREEN)
        print("help green", flush=True)
        c64u.inject_keys([0x0D], settle=0.5)
        wait_menu()

        c64u.inject_keys([ord("1")], settle=0.5)
        inject_text("1")
        wait_row(20, "USE 8-15")
        expect(20, "USE 8-15", RED)
        print("use 8-15 red", flush=True)
        c64u.inject_keys([0x03], settle=0.5)
        wait_menu()

        c64u.inject_keys([ord("2")], settle=0.5)
        inject_text("")
        inject_text("")
        wait_row(12, "NO FILES")
        expect(12, "NO FILES", RED)
        print("no files red", flush=True)
        c64u.inject_keys([0x0D], settle=0.5)
        wait_menu()

        c64u.inject_keys([ord("1")], settle=0.5)
        inject_text("")
        inject_text("")
        wait_row(2, "SAMPLE")
        c64u.inject_keys([0x0D], settle=0.5)
        wait_row(20, "OUTPUT FILENAME:")
        inject_text("@")
        wait_row(12, "BAD NAME")
        expect(12, "BAD NAME", RED)
        print("bad name red", flush=True)

        load_tool()
        c64u.inject_keys([0x03], settle=0.5)
        wait_row(12, "CANCELLED")
        expect(12, "CANCELLED", GREEN)
        print("cancelled green", flush=True)
        c64u.reset()
    finally:
        disk.restore_mount(name, previous)


if __name__ == "__main__":
    try:
        main()
    except Exception as err:
        print(err, file=sys.stderr)
        sys.exit(1)
