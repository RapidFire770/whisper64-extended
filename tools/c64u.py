#!/usr/bin/env python3
"""
C64 Ultimate REST-API driver.

Drives the running editor on the developer's C64U (http://192.168.100.193)
via the KERNAL keyboard buffer ($0277, count $C6), and reads back screen +
color RAM so we can SEE the per-keystroke state that py65 cannot reproduce.

Usage (see functions called from __main__):
    python3 tools/c64u.py cursor     # whisper64 cursor-move regression
    python3 tools/c64u.py dump       # print the current 40-column screen

Keyboard buffer: $0277 (10 bytes), count at $C6. The editor's main-loop GETIN
consumes them. One buffer load = up to 10 keys; the loop runs thousands of
times per frame, so we sleep briefly between injections to let it drain.

Screen-code note: UPPERCASE A-Z ($41-$5A) and space ($20) match PETSCII, but
digits/punctuation differ. We decode leniently (see decode_row).
"""
import sys
import time
import urllib.request

HOST = "http://192.168.100.193"
SCREEN = 0x0400
COLOR = 0xD800
KBUF = 0x0277
KCOUNT = 0xC6

# PETSCII key codes for the KERNAL keyboard buffer (what GETIN returns).
K_DOWN = 0x11
K_UP = 0x91
K_RETURN = 0x0D
K_HOME = 0x13
K_LEFT = 0x9D
K_RIGHT = 0x1D
K_F4 = 0x8A
PRG = "build/whisper64.prg"


def _put(path, data=None):
    url = HOST + path
    req = urllib.request.Request(url, data=data, method="PUT")
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read()


def _get(path):
    url = HOST + path
    with urllib.request.urlopen(url, timeout=10) as r:
        return r.read()


def reset():
    _put("/v1/machine:reset")
    time.sleep(2.5)


def load_and_run(prg_path):
    with open(prg_path, "rb") as f:
        data = f.read()
    req = urllib.request.Request(HOST + "/v1/runners:run_prg",
                                 data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=15) as r:
        r.read()
    time.sleep(2.0)


def read_mem(addr, length):
    """Read `length` bytes at `addr`; return bytes (binary response)."""
    return _get(f"/v1/machine:readmem?address=0x{addr:X}&length={length}")


def write_mem(addr, hex_bytes):
    _put(f"/v1/machine:writemem?address=0x{addr:X}&data={hex_bytes}")


def inject_keys(codes, settle=0.35):
    """Push up to 10 PETSCII keycodes into the KERNAL keyboard buffer."""
    for chunk in range(0, len(codes), 10):
        batch = codes[chunk:chunk + 10]
        data = "".join(f"{c:02X}" for c in batch)
        write_mem(KBUF, data)
        write_mem(KCOUNT, f"{len(batch):02X}")
        time.sleep(settle)


def decode_screen_row(raw, row):
    """Decode 40 screen bytes for `row` to a readable string (lenient)."""
    base = row * 40
    out = []
    for b in raw[base:base + 40]:
        b &= 0xFF
        # Loose screen-code -> ASCII: space $20, A-Z $41-$5A pass through.
        if b == 0x20:
            out.append(" ")
        elif 0x41 <= b <= 0x5A:
            out.append(chr(b))
        elif 0x30 <= b <= 0x39:  # digits: screen codes $30-$39 == PETSCII digits
            out.append(chr(b))
        elif b == 0x00:
            out.append(".")
        elif 0x80 & b:  # reverse video (cursor) -> mark
            out.append("[#%02X]" % b)
        else:
            out.append("<%02X>" % b)
    return "".join(out)


def dump_rows(rows=range(0, 25), label=""):
    scr = read_mem(SCREEN, 25 * 40)
    col = read_mem(COLOR, 25 * 40)
    if label:
        print(f"--- {label} ---")
    for r in rows:
        text = decode_screen_row(scr, r)
        colors = " ".join(f"{col[r * 40 + i] & 0x0F:X}" for i in range(40))
        print(f"  r{r:2d}: {text}")
        print(f"       {colors}")
    print()
    return scr, col


def row_text(raw, row):
    return bytes(raw[row * 40:(row + 1) * 40])


def boot_editor():
    reset()
    load_and_run(PRG)
    scr, _ = dump_rows(rows=range(24, 25), label="after run")
    if b"REU" in row_text(scr, 24):
        inject_keys([0x20], settle=0.6)
        dump_rows(rows=range(24, 25), label="reu dismissed")


def cells_changed(before, after):
    out = []
    for i, (a, b) in enumerate(zip(before, after)):
        if a != b:
            out.append((i // 40, i % 40))
    return out


def allowed_move(diffs, rows):
    """Title line:col is columns 8-16 of row 0. Cursor cells sit on `rows`."""
    for r, c in diffs:
        if r == 0 and 8 <= c <= 16:
            continue
        if r in rows:
            continue
        return False
    return True


def cmd_cursor():
    boot_editor()
    inject_keys([K_F4], settle=0.6)
    scr, col = dump_rows(label="basic on")
    status = row_text(scr, 24)
    if b"BASIC" not in status:
        print("FAIL basic status missing")
        return 1
    # PRINT on row 1. HOME leaves the cursor on the P.
    inject_keys(list(b"PRINT") + [K_RETURN, K_HOME], settle=0.8)
    before_s, before_c = dump_rows(label="before right")
    inject_keys([K_RIGHT], settle=0.5)
    after_s, after_c = dump_rows(label="after right")
    if row_text(after_s, 24) != row_text(before_s, 24):
        print("FAIL right cleared status row")
        return 1
    if not allowed_move(cells_changed(before_s, after_s), {1}):
        print("FAIL right rewrote rows other than the cursor line")
        return 1
    if not allowed_move(cells_changed(before_c, after_c), {1}):
        print("FAIL right rewrote colors outside the cursor line")
        return 1
    # P is screen column 3. Leaving it restores purple (4). R is column 4.
    if after_s[1 * 40 + 3] != 0x50 or (after_c[1 * 40 + 3] & 0x0F) != 4:
        print("FAIL P did not return to purple")
        return 1
    if (after_s[1 * 40 + 4] & 0x80) == 0 or (after_c[1 * 40 + 4] & 0x0F) != 1:
        print("FAIL cursor did not land on R")
        return 1
    print("ok")
    return 0


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "dump"
    if cmd == "cursor":
        sys.exit(cmd_cursor())
    elif cmd == "dump":
        dump_rows()
    elif cmd == "down":
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        for i in range(n):
            inject_keys([K_DOWN], settle=0.5)
            dump_rows(label=f"DOWN #{i+1}")
    elif cmd == "up":
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        for i in range(n):
            inject_keys([K_UP], settle=0.5)
            dump_rows(label=f"UP #{i+1}")
    elif cmd == "home":
        inject_keys([K_HOME], settle=0.5)
        dump_rows(label="HOME")
    else:
        print(f"unknown command: {cmd}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
