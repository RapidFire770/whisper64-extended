#!/usr/bin/env python3
"""Run w64conv on the C64 Ultimate and compare the file it re-reads.

Resets the machine and mounts a temporary D64 on the drive at bus id 8.
The previous mount is restored on the way out.
"""
import json
import os
import subprocess
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c64u
import test_basic_prg as oracle

HOST = c64u.HOST
CONV_OK = 1
CONV_NOT_BASIC = 2
CONV_BAD_BASIC = 3


def sector_count(track):
    if track <= 17:
        return 21
    if track <= 24:
        return 19
    if track <= 30:
        return 18
    return 17


def sector_offset(track, sector):
    off = 0
    for t in range(1, track):
        off += sector_count(t)
    return (off + sector) * 256


def put_file(image, index, file_type, name, data):
    """Place one file on track 1. file_type is 0x81 SEQ or 0x82 PRG."""
    if len(data) > 254:
        raise SystemExit("test file needs more than one sector")
    track, sector = 1, index
    off = sector_offset(track, sector)
    image[off] = 0
    image[off + 1] = len(data) + 1
    image[off + 2:off + 2 + len(data)] = data

    bam = sector_offset(18, 0)
    entry = bam + 4  # track 1
    image[entry] = (image[entry] - 1) & 0xFF
    image[entry + 1] &= ~(1 << sector)

    slot = sector_offset(18, 1) + 2 + index * 32
    image[slot] = file_type
    image[slot + 1] = track
    image[slot + 2] = sector
    raw = name.encode("ascii")
    padded = raw + bytes([0xA0]) * (16 - len(raw))
    image[slot + 3:slot + 19] = padded
    image[slot + 28] = 1
    image[slot + 29] = 0


def build_image(path, files):
    subprocess.check_call(
        ["c1541", "-format", "w64conv,cv", "d64", path],
        stdout=subprocess.DEVNULL,
    )
    with open(path, "rb") as f:
        image = bytearray(f.read())
    for i, (kind, name, data) in enumerate(files):
        put_file(image, i, kind, name, data)
    with open(path, "wb") as f:
        f.write(image)


def _retry(fn, tries=4, pause=0.6):
    last = None
    for _ in range(tries):
        try:
            return fn()
        except Exception as err:
            last = err
            time.sleep(pause)
    raise last


def install_retries():
    get, put, load = c64u._get, c64u._put, c64u.load_and_run

    def get_retry(path):
        return _retry(lambda: get(path))

    def put_retry(path, data=None):
        return _retry(lambda: put(path, data))

    def load_retry(path):
        return _retry(lambda: load(path), tries=3, pause=1.0)

    c64u._get = get_retry
    c64u._put = put_retry
    c64u.load_and_run = load_retry


def api(method, path, data=None, timeout=30):
    req = urllib.request.Request(
        HOST + path, data=data, method=method,
        headers={"Content-Type": "application/octet-stream"} if data else {},
    )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def drives():
    return json.loads(api("GET", "/v1/drives"))


def bus8_drive(info):
    for item in info["drives"]:
        for name, drive in item.items():
            if drive.get("bus_id") == 8:
                return name, drive
    raise SystemExit("no drive at bus id 8")


def mount_bytes(drive_name, image):
    api("POST", f"/v1/drives/{drive_name}:mount?type=d64&mode=readwrite", image)


def restore_mount(drive_name, drive):
    image = drive.get("image_file") or ""
    folder = drive.get("image_path") or ""
    if not image:
        api("PUT", f"/v1/drives/{drive_name}:remove")
        return
    if image.startswith("/"):
        remote = image
    elif folder:
        remote = folder.rstrip("/") + "/" + image
    else:
        remote = image
    quoted = urllib.parse.quote(remote, safe="/")
    api("PUT", f"/v1/drives/{drive_name}:mount?image={quoted}&type=d64&mode=readwrite")
    _, now = bus8_drive(drives())
    got = now.get("image_file") or ""
    if got != remote and not got.endswith("/" + image.lstrip("/")):
        raise SystemExit(f"restore left {got!r}, wanted {remote!r}")


def map_addr(symbol):
    """Linker rows look like: '    be50     be50        1     1                 basic_mode'."""
    with open("build/w64tools.map") as f:
        for line in f:
            parts = line.split()
            if parts and parts[-1] == symbol:
                return int(parts[0], 16)
    raise SystemExit("missing symbol " + symbol)


def mem(addr, length):
    last = None
    for _ in range(4):
        try:
            return c64u.read_mem(addr, length)
        except Exception as err:
            last = err
            time.sleep(0.4)
    raise last


def u32(addr):
    return int.from_bytes(mem(addr, 4), "little")


def screen_text():
    raw = mem(0x0400, 25 * 40)
    rows = []
    for r in range(16):
        rows.append(c64u.decode_screen_row(raw, r))
    return "\n".join(rows)


def inject_text(text):
    codes = [ord(ch) for ch in text] + [0x0D]
    c64u.inject_keys(codes, settle=0.5)


def run_job(mode, downs, out_name, timeout=45):
    c64u.reset()
    c64u.load_and_run("build/w64tools.prg")
    deadline = time.time() + 15
    while time.time() < deadline:
        raw = mem(0x0400, 40)
        if b"W64TOOLS" in raw:
            break
        time.sleep(0.25)
    else:
        raise SystemExit("converter did not start\n" + screen_text())
    c64u.inject_keys([ord(mode)], settle=0.5)
    inject_text("")
    inject_text("")
    deadline = time.time() + 15
    while time.time() < deadline:
        row = c64u.decode_screen_row(mem(0x0400, 400), 2)
        if "SEQ TO PRG" not in row and row.strip(" ."):
            break
        time.sleep(0.25)
    else:
        raise SystemExit("directory did not list\n" + screen_text())
    for _ in range(downs):
        c64u.inject_keys([0x11], settle=0.5)
    c64u.inject_keys([0x0D], settle=0.5)
    inject_text(out_name)
    code_addr = map_addr("verify_code")
    deadline = time.time() + timeout
    code = 0
    while time.time() < deadline:
        code = mem(code_addr, 1)[0]
        if code != 0:
            return code
        row = c64u.decode_screen_row(mem(0x0400, 400), 9)
        if "OVERWRITE" in row:
            raise SystemExit("overwrite prompt\n" + screen_text())
        time.sleep(0.25)
    raise SystemExit("timed out\n" + screen_text())


def verify_dump():
    try:
        length = u32(map_addr("verify_len"))
        total = u32(map_addr("verify_sum"))
        raw = mem(map_addr("verify_buf"), 64)
        return f"len {length} sum {total:#x} buf {raw.hex()}\n"
    except Exception as err:
        return f"verify dump failed: {err}\n"


def expect_bytes(expected):
    length = u32(map_addr("verify_len"))
    total = u32(map_addr("verify_sum"))
    got = mem(map_addr("verify_buf"), min(256, len(expected)))
    if length != len(expected) or total != sum(expected) or bytes(got) != expected:
        raise SystemExit(
            f"mismatch len {length} vs {len(expected)} sum {total:#x} vs {sum(expected):#x}\n"
            + got.hex() + "\n" + expected.hex() + "\n"
            + screen_text()
        )


def main():
    install_retries()
    info = drives()
    name, previous = bus8_drive(info)
    image_path = "/tmp/w64conv-test.d64"
    sample = ['10 PRINT "HI"', "20 GOTO 10"]
    sample_prg = oracle.build_prg(sample)
    sample_seq = oracle.seq_text(sample)
    # PRG to SEQ writes a CR only between lines.
    tokens_seq = "\r".join(sample).encode("latin1")
    bad_link = bytes([0x01, 0x08, 0x01, 0x08, 0x0A, 0x00, 0x00, 0x00, 0x00])
    not_basic = bytes([0x00, 0x08, 0x00, 0x00])
    failed = False
    try:
        build_image(image_path, [
            (0x81, "SAMPLE", sample_seq),
            (0x82, "TOKENS", sample_prg),
            (0x82, "BADLINK", bad_link),
            (0x82, "NOTBASIC", not_basic),
        ])
        with open(image_path, "rb") as f:
            mount_bytes(name, f.read())
        print("job SAMPLE", flush=True)
        code = run_job("1", 0, "OUT1")
        if code != CONV_OK:
            raise SystemExit(f"SEQ to PRG code {code}\n" + verify_dump() + screen_text())
        expect_bytes(sample_prg)

        print("job TOKENS", flush=True)
        code = run_job("2", 0, "OUT2")
        if code != CONV_OK:
            raise SystemExit(f"PRG to SEQ code {code}\n" + screen_text())
        expect_bytes(tokens_seq)

        print("job BADLINK", flush=True)
        code = run_job("2", 1, "OUT3")
        if code != CONV_BAD_BASIC:
            raise SystemExit(f"bad link code {code}\n" + screen_text())

        print("job NOTBASIC", flush=True)
        code = run_job("2", 2, "OUT4")
        if code != CONV_NOT_BASIC:
            raise SystemExit(f"not basic code {code}\n" + screen_text())
        print("ultimate ok")
    except BaseException:
        failed = True
        raise
    finally:
        try:
            restore_mount(name, previous)
        except BaseException as err:
            print("restore failed:", err, file=sys.stderr)
            if not failed:
                raise


if __name__ == "__main__":
    main()
