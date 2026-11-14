#!/usr/bin/env python3
"""
C64 Ultimate REST-API driver.

Drives the running editor on the developer's C64U (http://192.168.100.193)
via the KERNAL keyboard buffer ($0277, count $C6), and reads back screen +
color RAM so we can SEE the per-keystroke state that py65 cannot reproduce.

Usage (see functions called from __main__):
    python3 tools/c64u.py cursor     # whisper64 cursor-move regression
    python3 tools/c64u.py wrap       # wrapped-line typing band redraw
    python3 tools/c64u.py mark       # mark-mode redraw and keyword color
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
K_CTRL_K = 0x0B
K_F1 = 0x85
K_DELETE = 0x14
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
    data = _get(f"/v1/machine:readmem?address=0x{addr:X}&length={length}")
    # Ultimate reads put noise in the color high nibble; VIC uses the low nibble.
    if addr >= COLOR and addr < COLOR + 0x400:
        data = bytes(b & 0x0F for b in data)
    return data


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
    boot_editor()
    line = list(b"10 GOTO 10") + [K_RETURN]
    for _ in range(23):
        inject_keys(line, settle=0.6)
    inject_keys([K_HOME], settle=0.6)
    # settle 1.0: shorter pause left the status row blank after the line fill
    inject_keys([K_F4], settle=1.0)
    before_s, before_c = dump_rows(label="before downs")
    status = row_text(before_s, 24)
    if b"BASIC" not in status:
        print("FAIL scroll setup lost BASIC status")
        return 1
    inject_keys([K_DOWN] * 22, settle=0.8)
    mid_s, mid_c = dump_rows(label="down to last visible")
    if row_text(mid_s, 24) != status:
        print("FAIL in-window down cleared status")
        return 1
    inject_keys([K_DOWN], settle=0.6)
    after_s, after_c = dump_rows(label="scroll down")
    if row_text(after_s, 24) != status:
        print("FAIL scroll cleared status row")
        return 1

    def plain(raw, row):
        return bytes(b & 0x7F for b in raw[row * 40:(row + 1) * 40])

    for r in range(1, 23):
        if plain(after_s, r) != plain(mid_s, r + 1):
            print(f"FAIL scroll screen row {r} did not follow the line")
            return 1
        if bytes(after_c[r * 40:(r + 1) * 40]) != bytes(mid_c[(r + 1) * 40:(r + 2) * 40]):
            print(f"FAIL scroll color row {r}")
            return 1
    bottom = row_text(after_s, 23)
    if bottom[0] != 0x32 or bottom[1] != 0x33:
        print("FAIL bottom gutter is not 23")
        return 1
    if (after_c[22 * 40 + 6] & 0x0F) != 4:
        print("FAIL GOTO did not stay purple after the shift")
        return 1
    print("ok")
    return 0


def plain_row(raw, row):
    return bytes(b & 0x7F for b in raw[row * 40:(row + 1) * 40])


def row0_only_linecol_diff(before, after):
    """Row 0 may change only in columns 8-16 (title line:col)."""
    for c in range(40):
        if 8 <= c <= 16:
            continue
        if before[c] != after[c]:
            return False
    return True


def cmd_wrap():
    boot_editor()

    inject_keys([K_RETURN], settle=0.4)
    inject_keys(list(b"HI"), settle=0.4)
    inject_keys([K_UP], settle=0.4)
    inject_keys([0x58] * 36, settle=0.8)

    inject_keys([K_F4], settle=0.6)
    scr, _ = dump_rows(label="basic on after setup")
    r1 = row_text(scr, 1)
    if r1[:3] != bytes([0x30, 0x30, 0x3A]) or r1[3:39] != bytes([0x58] * 36):
        print("FAIL row 1 not 00: plus 36 X")
        return 1
    r2 = row_text(scr, 2)
    if r2[:3] != bytes([0x30, 0x31, 0x3A]) or r2[3:5] != b"HI":
        print("FAIL row 2 not 01:HI")
        return 1
    if b"BASIC" not in row_text(scr, 24):
        print("FAIL basic status missing after F4")
        return 1

    inject_keys([0x58], settle=0.5)
    scr, _ = dump_rows(label="phantom row")
    r1 = row_text(scr, 1)
    if r1[:3] != bytes([0x30, 0x30, 0x3A]) or plain_row(scr, 1)[3:40] != bytes([0x58] * 37):
        print("FAIL row 1 not 00: plus 37 X")
        return 1
    if plain_row(scr, 2)[:3] != bytes([0x20, 0x20, 0x20]):
        print("FAIL row 2 does not start with three spaces")
        return 1
    r3 = row_text(scr, 3)
    if r3[:3] != bytes([0x30, 0x31, 0x3A]) or r3[3:5] != b"HI":
        print("FAIL row 3 not 01:HI")
        return 1
    if b"BASIC" not in row_text(scr, 24):
        print("FAIL status lost on phantom row")
        return 1
    saved_phantom = scr

    inject_keys([0x58] * 3, settle=0.6)
    scr, _ = dump_rows(label="40 chars two screen rows")
    if row_text(scr, 24) != row_text(saved_phantom, 24):
        print("FAIL step 5 status row changed")
        return 1
    if row_text(scr, 3) != row_text(saved_phantom, 3):
        print("FAIL step 5 row 3 changed")
        return 1
    for r in range(4, 24):
        if row_text(scr, r) != row_text(saved_phantom, r):
            print(f"FAIL step 5 row {r} changed")
            return 1
    if not row0_only_linecol_diff(row_text(saved_phantom, 0), row_text(scr, 0)):
        print("FAIL step 5 row 0 changed outside line:col")
        return 1
    if plain_row(scr, 2)[3] != 0x58:
        print("FAIL step 5 row 2 col 3 not X")
        return 1
    saved_forty = scr

    inject_keys([K_DELETE] * 3, settle=0.5)
    scr, _ = dump_rows(label="delete to phantom len 37")
    if row_text(scr, 3) != row_text(saved_forty, 3):
        print("FAIL step 6 row 3 changed")
        return 1
    if row_text(scr, 24) != row_text(saved_forty, 24):
        print("FAIL step 6 status row changed")
        return 1
    if plain_row(scr, 2)[3] != 0x20:
        print("FAIL step 6 row 2 col 3 not space")
        return 1

    inject_keys([K_DELETE], settle=0.5)
    scr, _ = dump_rows(label="back to 36 one screen row")
    r2 = row_text(scr, 2)
    if r2[:3] != bytes([0x30, 0x31, 0x3A]) or r2[3:5] != b"HI":
        print("FAIL step 7 row 2 not 01:HI")
        return 1
    if b"BASIC" not in row_text(scr, 24):
        print("FAIL step 7 status missing")
        return 1

    print("ok")
    return 0


def cmd_mark():
    boot_editor()
    inject_keys([K_F4], settle=0.6)
    inject_keys(list(b"PRINT") + [K_RETURN] + list(b"XY") + [K_HOME], settle=0.8)
    inject_keys([K_CTRL_K], settle=0.5)
    before_s, before_c = dump_rows(label="mark on PRINT")
    if b"MARK" not in row_text(before_s, 24):
        print("FAIL mark status missing")
        return 1
    inject_keys([K_RIGHT] * 5, settle=0.6)
    after_s, after_c = dump_rows(label="marked PRINT")
    if row_text(after_s, 24) != row_text(before_s, 24):
        print("FAIL mark arrows cleared the status row")
        return 1
    if not allowed_move(cells_changed(before_s, after_s), {1}):
        print("FAIL mark arrows rewrote rows other than the PRINT line")
        return 1
    if not allowed_move(cells_changed(before_c, after_c), {1}):
        print("FAIL mark arrows rewrote colors outside the PRINT line")
        return 1
    for col in range(3, 8):
        if (after_c[40 + col] & 0x0F) != 7:
            print(f"FAIL PRINT column {col} is not yellow")
            return 1
    if plain_row(after_s, 2)[3:5] != b"XY":
        print("FAIL XY line changed while marking PRINT")
        return 1

    inject_keys([K_CTRL_K, K_DOWN, K_LEFT, K_CTRL_K], settle=0.8)
    before_s, before_c = dump_rows(label="anchor on Y")
    if b"MARK" not in row_text(before_s, 24):
        print("FAIL second mark status missing")
        return 1
    inject_keys([K_UP], settle=0.5)
    up_s, up_c = dump_rows(label="mark up onto R")
    if row_text(up_s, 24) != row_text(before_s, 24):
        print("FAIL upward mark cleared the status row")
        return 1
    if not allowed_move(cells_changed(before_s, up_s), {1, 2}):
        print("FAIL upward mark rewrote rows outside the two lines")
        return 1
    if (up_c[40 + 3] & 0x0F) != 4:
        print("FAIL unmarked P is not purple")
        return 1
    if (up_c[40 + 5] & 0x0F) != 7 or (up_c[40 + 6] & 0x0F) != 7:
        print("FAIL I or N is not yellow")
        return 1
    if (up_c[80 + 3] & 0x0F) != 7:
        print("FAIL X is not yellow")
        return 1
    if (up_c[80 + 4] & 0x0F) != 1:
        print("FAIL unmarked Y is not white")
        return 1

    boot_editor()
    inject_keys(list(b"ABC") + [K_HOME, K_CTRL_K], settle=0.6)
    inject_keys([K_RIGHT, K_RIGHT, K_RIGHT, 3, 22], settle=0.8)
    scr, _ = dump_rows(label="pasted ABC")
    row = plain_row(scr, 1)
    if row[:9] != bytes([0x30, 0x30, 0x3A, 0x41, 0x42, 0x43, 0x41, 0x42, 0x43]):
        print("FAIL paste did not draw ABCABC")
        return 1
    if b"PASTED" not in row_text(scr, 24):
        print("FAIL paste status missing")
        return 1
    print("ok")
    return 0


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "dump"
    if cmd == "cursor":
        sys.exit(cmd_cursor())
    elif cmd == "wrap":
        sys.exit(cmd_wrap())
    elif cmd == "mark":
        sys.exit(cmd_mark())
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
