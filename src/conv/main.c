#include "w64tools.h"
#include <cbm.h>
#include <ctype.h>
#include <string.h>

/* This SDK has no conio.h. 40-column screen codes match the bytes we store. */
#define SCREEN_RAM ((unsigned char *)0x0400)
#define COL_GREEN 5
#define COL_YELLOW 7
#define KEY_DOWN 17
#define KEY_UP 145

static void cputc_at(unsigned char x, unsigned char y, char c, unsigned char color) {
    unsigned int pos = (unsigned int)y * 40 + x;
    SCREEN_RAM[pos] = (unsigned char)c;
    COLOR_RAM[pos] = color;
}

static void clrscr(void) {
    unsigned int i;
    for (i = 0; i < 1000; i++) {
        SCREEN_RAM[i] = ' ';
        COLOR_RAM[i] = COL_GREEN;
    }
}

static int cgetc(void) {
    unsigned char c;
    while (!(c = cbm_k_getin()))
        ;
    return c;
}

static void cputs_at(unsigned char x, unsigned char y, const char *s,
                     unsigned char color) {
    while (*s && x < 39) {
        cputc_at(x, y, *s++, color);
        x++;
    }
}

/* Kept out of zero page. The Ultimate test reads these after main parks. */
volatile unsigned char verify_buf[256];
volatile unsigned long verify_len;
volatile unsigned long verify_sum;
volatile unsigned char verify_code __attribute__((section(".bss")));

static void stay(void) __attribute__((noreturn));
static void stay(void) {
    for (;;)
        ;
}

static unsigned char read_drive;
static unsigned char write_drive;
static char in_name[17];
static char out_name[17];
static unsigned char tok[80];
static char text_line[80];

/* A 1541 keeps one data file open. Bytes wait here until the input is closed.
   The menu, renumber code, and the BASIC return path sit in front of this
   array. Keep __heap_start at or below $C800. */
#define OUT_CAP 20096
static unsigned char out_bytes[OUT_CAP];
static unsigned int out_used;

static void put_row(unsigned char row, const char *msg) {
    unsigned char i;

    for (i = 0; i < 39; i++)
        cputc_at(i, row, ' ', COL_GREEN);
    cputs_at(0, row, msg, COL_GREEN);
}

/* One shared formatter. sprintf pulls in about 4KB of printf. */
static void set_str(char *dst, const char *src) {
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

static void add_str(char *dst, const char *src) {
    while (*dst)
        dst++;
    set_str(dst, src);
}

static void add_dec(char *dst, unsigned int v) {
    char rev[5];
    unsigned char r = 0;

    while (*dst)
        dst++;
    if (v == 0)
        rev[r++] = '0';
    while (v) {
        rev[r++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (r)
        *dst++ = rev[--r];
    *dst = '\0';
}

static void add_hex2(char *dst, unsigned char v) {
    static const char hex[] = "0123456789ABCDEF";

    while (*dst)
        dst++;
    *dst++ = hex[v >> 4];
    *dst++ = hex[v & 0x0f];
    *dst = '\0';
}

/* replace == 2 writes the DOS overwrite prefix. tail is ",S,R" or similar. */
static void file_spec(char *dst, int replace, const char *name, const char *tail) {
    dst[0] = '\0';
    if (replace == 2)
        add_str(dst, "@0:");
    add_str(dst, name);
    add_str(dst, tail);
}

static int name_ok(const char *s) {
    while (*s) {
        if (*s == ',' || *s == ':' || *s == '*' || *s == '?' || *s == '@')
            return 0;
        s++;
    }
    return 1;
}

static int read_field(unsigned char row, const char *label, char *buf, int cap) {
    int n = 0;
    int ch;
    int label_len = 0;

    while (label[label_len])
        label_len++;

    put_row(row, label);
    for (;;) {
        ch = cgetc();
        if (ch == 3) {
            verify_code = CONV_CANCEL;
            return -1;
        }
        if (ch == 13) {
            buf[n] = '\0';
            return n;
        }
        if (ch == 20 && n > 0) {
            n--;
            cputc_at((unsigned char)(label_len + n), row, ' ', COL_GREEN);
            continue;
        }
        if (ch >= 32 && ch < 128 && n < cap - 1) {
            buf[n++] = (char)ch;
            cputc_at((unsigned char)(label_len + n - 1), row, (char)ch, COL_GREEN);
        }
    }
}

static int ask_drive(unsigned char row, const char *label, unsigned char *dest) {
    char buf[4];
    int n;
    int i;
    unsigned int v;

    for (;;) {
        n = read_field(row, label, buf, 4);
        if (n < 0)
            return -1;
        if (n == 0) {
            *dest = 8;
            return 0;
        }
        v = 0;
        for (i = 0; buf[i]; i++) {
            if (!isdigit((unsigned char)buf[i])) {
                v = 99;
                break;
            }
            v = v * 10u + (unsigned int)(buf[i] - '0');
        }
        if (v >= 8 && v <= 15) {
            *dest = (unsigned char)v;
            return 0;
        }
        put_row(20, "USE 8-15");
    }
}

static int open_ch(unsigned char lfn, unsigned char drive, const char *spec) {
    cbm_k_setlfs(lfn, drive, 2);
    cbm_k_setnam(spec);
    if (cbm_k_open() != 0) {
        cbm_k_close(lfn);
        return -1;
    }
    return 0;
}

static void close_ch(unsigned char lfn) {
    cbm_k_clrch();
    cbm_k_close(lfn);
}

static void finish(unsigned char code, const char *msg);

static int stash(unsigned char b) {
    if (out_used >= OUT_CAP) {
        finish(CONV_TOO_BIG, "TOO BIG");
        return -1;
    }
    out_bytes[out_used++] = b;
    return 0;
}

static int stash_word(unsigned int w) {
    if (stash((unsigned char)w) < 0)
        return -1;
    return stash((unsigned char)(w >> 8));
}

static int flush_out(const char *spec) {
    unsigned int i;

    if (open_ch(3, write_drive, spec) != 0) {
        finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkout(3);
    for (i = 0; i < out_used; i++)
        cbm_k_chrout(out_bytes[i]);
    close_ch(3);
    return 0;
}

/* KERNAL STATUS at $90 is OR-ed in. A write leaves bits that the next read would keep. */
static unsigned char last_st;

static int chrin_fresh(void) {
    *(unsigned char *)0x90 = 0;
    return cbm_k_chrin();
}

static int read_byte(unsigned char lfn_ready, unsigned char *eof) {
    int c;
    int st;

    (void)lfn_ready;
    if (*eof)
        return -1;
    c = chrin_fresh();
    st = cbm_k_readst();
    last_st = (unsigned char)st;
    if (st & 0x80)
        return -2;
    /* EOF bit: this byte counts. A bare timeout is the read past the end. */
    if (st & 0x40) {
        *eof = 1;
        return c & 0xFF;
    }
    if (st & 0x02) {
        *eof = 1;
        return -1;
    }
    if (st)
        return -2;
    return c & 0xFF;
}

static void finish(unsigned char code, const char *msg) {
    cbm_k_clrch();
    cbm_k_close(2);
    cbm_k_close(3);
    verify_code = code;
    put_row(12, msg);
}

static int drive_status_ok(void) {
    char st[40];
    int j = 0;
    int c;

    cbm_k_clrch();
    cbm_k_close(2);
    cbm_k_close(3);
    cbm_k_setlfs(15, write_drive, 15);
    cbm_k_setnam("");
    if (cbm_k_open() != 0) {
        cbm_k_close(15);
        return 1;
    }
    cbm_k_chkin(15);
    while (j < 39) {
        c = chrin_fresh();
        if (cbm_k_readst() & 0x40)
            break;
        if (c == 13)
            break;
        st[j++] = (char)c;
    }
    st[j] = '\0';
    cbm_k_clrch();
    cbm_k_close(15);
    if (st[0] == '0' && (st[1] == '0' || st[1] == '1'))
        return 1;
    finish(CONV_DISK, st);
    return 0;
}

/* KERNAL OPEN returns 0 when the drive answers. A missing file is DOS 62. */
static int dos_missing(const char *spec) {
    char st[4];
    int j = 0;
    int c;

    if (open_ch(2, write_drive, spec) != 0)
        return 1;
    /* A read forces the drive to resolve the name. OPEN alone can leave 73. */
    cbm_k_chkin(2);
    c = chrin_fresh();
    (void)c;
    (void)cbm_k_readst();
    cbm_k_clrch();
    cbm_k_setlfs(15, write_drive, 15);
    cbm_k_setnam("");
    if (cbm_k_open() != 0) {
        cbm_k_close(15);
        close_ch(2);
        return 0;
    }
    cbm_k_chkin(15);
    while (j < 3) {
        c = chrin_fresh();
        if (cbm_k_readst() & 0x40)
            break;
        if (c == ',' || c == 13)
            break;
        st[j++] = (char)c;
    }
    st[j] = '\0';
    cbm_k_clrch();
    cbm_k_close(15);
    close_ch(2);
    return st[0] == '6' && st[1] == '2';
}

static int confirm_overwrite(const char *spec) {
    int ch;

    if (dos_missing(spec))
        return 1;
    put_row(9, "OVERWRITE? Y/N");
    ch = cgetc();
    if (ch != 'Y' && ch != 'y') {
        finish(CONV_CANCEL, "CANCELLED");
        return 0;
    }
    return 2;
}

/* 0 got a line, 1 clean EOF, -1 error already stored in *err. */
static int read_seq_line(unsigned char *eof, char *line, int *err) {
    int n = 0;
    int c;

    *err = 0;
    for (;;) {
        c = read_byte(2, eof);
        if (c == -2) {
            *err = -2;
            return -1;
        }
        if (c < 0) {
            if (n == 0)
                return 1;
            line[n] = '\0';
            return 0;
        }
        if (c == 13 || c == 10) {
            line[n] = '\0';
            return 0;
        }
        if (n >= 79) {
            *err = -4;
            return -1;
        }
        line[n++] = (char)c;
    }
}

static void note_progress(unsigned char row, const char *label, int n) {
    char msg[20];

    cbm_k_clrch();
    set_str(msg, label);
    add_dec(msg, (unsigned int)n);
    put_row(row, msg);
}

static int seq_pass(int writing) {
    char spec[28];
    char line[80];
    unsigned char eof = 0;
    unsigned int addr = 0x0801;
    unsigned int line_no;
    int seen = 0;
    int wrote = 0;
    int rc;
    int err;
    int n;

    file_spec(spec, 0, in_name, ",S,R");
    if (open_ch(2, read_drive, spec) != 0) {
        finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);

    if (writing) {
        out_used = 0;
        if (stash_word(0x0801) < 0)
            return -1;
    }

    for (;;) {
        cbm_k_chkin(2);
        rc = read_seq_line(&eof, line, &err);
        if (rc < 0) {
            if (err == -2) {
                char msg[10];
                set_str(msg, "READ ");
                add_hex2(msg, last_st);
                finish(CONV_DISK, msg);
            } else
                finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        if (rc > 0)
            break;
        seen++;
        note_progress(10, writing ? "PRG " : "CHECK ", seen);
        cbm_k_chkin(2);
        n = conv_tokenize(line, &line_no, tok, 79);
        if (n == -1)
            continue;
        if (n == -2) {
            finish(CONV_NO_NUM, "NO NUM");
            return -1;
        }
        if (n == -3) {
            finish(CONV_NUM_BIG, "NUM BIG");
            return -1;
        }
        if (n < 0) {
            finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        {
            unsigned int next = addr + 4 + (unsigned int)n + 1;
            int j;
            if (next < addr) {
                finish(CONV_TOO_BIG, "PRG TOO BIG");
                return -1;
            }
            if (writing) {
                if (stash_word(next) < 0 || stash_word(line_no) < 0)
                    return -1;
                for (j = 0; j < n; j++) {
                    if (stash(tok[j]) < 0)
                        return -1;
                }
                if (stash(0) < 0)
                    return -1;
            }
            addr = next;
            wrote++;
        }
    }

    if (writing && stash_word(0) < 0)
        return -1;
    close_ch(2);
    return wrote;
}

static int trailing_ml(unsigned char *eof) {
    int b;
    int b2;

    if (*eof)
        return 0;
    b = read_byte(2, eof);
    if (b < 0)
        return 0;
    if (b == 13 && *eof) {
        b2 = read_byte(2, eof);
        if (b2 < 0)
            return 0;
        return 1;
    }
    return 1;
}

static int prg_pass(int writing) {
    char spec[28];
    unsigned char eof = 0;
    unsigned int addr = 0x0801;
    unsigned int link;
    unsigned int line_no;
    int lo, hi;
    int wrote = 0;
    int trunc = 0;
    int ml = 0;
    int first = 1;

    file_spec(spec, 0, in_name, ",P,R");
    if (open_ch(2, read_drive, spec) != 0) {
        finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);
    if (writing)
        out_used = 0;
    lo = read_byte(2, &eof);
    hi = read_byte(2, &eof);
    if (lo != 0x01 || hi != 0x08) {
        finish(CONV_NOT_BASIC, "NOT BASIC PRG");
        return -1;
    }

    for (;;) {
        int remain;
        int slen = 0;
        int b;
        char msg[28];

        lo = read_byte(2, &eof);
        hi = read_byte(2, &eof);
        if (lo < 0 || hi < 0) {
            finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        link = (unsigned int)(lo | (hi << 8));
        if (link == 0)
            break;
        if (link <= addr || (unsigned int)(link - addr) < 5 ||
            (unsigned int)(link - addr) > 256) {
            finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        lo = read_byte(2, &eof);
        hi = read_byte(2, &eof);
        if (lo < 0 || hi < 0) {
            finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        line_no = (unsigned int)(lo | (hi << 8));
        if (line_no > 63999u) {
            finish(CONV_NUM_BIG, "NUM BIG");
            return -1;
        }
        remain = (int)(link - addr) - 4;
        slen = 0;
        {
            int cut_stmt = 0;
            while (remain > 1) {
                b = read_byte(2, &eof);
                if (b < 0) {
                    finish(CONV_BAD_BASIC, "BAD BASIC PRG");
                    return -1;
                }
                if (slen < 79)
                    tok[slen++] = (unsigned char)b;
                else
                    cut_stmt = 1;
                remain--;
            }
            b = read_byte(2, &eof);
            if (b != 0) {
                finish(CONV_BAD_BASIC, "BAD BASIC PRG");
                return -1;
            }
            {
                int cut = 0;
                int len = conv_format_line(line_no, tok, slen, text_line, 80, &cut);
                int j;
                if (cut || cut_stmt)
                    trunc++;
                if (writing) {
                    if (!first && stash(13) < 0)
                        return -1;
                    for (j = 0; j < len; j++) {
                        if (stash((unsigned char)text_line[j]) < 0)
                            return -1;
                    }
                    first = 0;
                }
            }
        }
        addr = link;
        wrote++;
        cbm_k_clrch();
        set_str(msg, writing ? "SEQ " : "CHECK ");
        add_dec(msg, (unsigned int)wrote);
        put_row(10, msg);
        cbm_k_chkin(2);
    }

    ml = trailing_ml(&eof);
    close_ch(2);
    if (!writing)
        return wrote;
    {
        char msg[32];
        set_str(msg, "SEQ ");
        add_dec(msg, (unsigned int)wrote);
        if (trunc || ml) {
            add_str(msg, " TRUNC ");
            add_dec(msg, (unsigned int)trunc);
            add_str(msg, " ML ");
            add_dec(msg, (unsigned int)ml);
        }
        put_row(12, msg);
    }
    return wrote;
}

static void remember_output(const char *spec) {
    unsigned char eof = 0;
    unsigned long sum = 0;
    unsigned long n = 0;
    int b;

    verify_len = 0;
    verify_sum = 0;
    if (open_ch(2, write_drive, spec) != 0) {
        finish(CONV_OPEN_FAIL, "READBACK FAIL");
        return;
    }
    cbm_k_chkin(2);
    for (;;) {
        b = read_byte(2, &eof);
        if (b == -2) {
            char msg[20];
            close_ch(2);
            verify_len = n;
            verify_sum = sum;
            set_str(msg, "BACK ");
            add_hex2(msg, last_st);
            add_str(msg, " ");
            add_dec(msg, (unsigned int)n);
            finish(CONV_DISK, msg);
            return;
        }
        if (b < 0)
            break;
        if (n < 256)
            verify_buf[n] = (unsigned char)b;
        sum += (unsigned char)b;
        n++;
    }
    close_ch(2);
    verify_len = n;
    verify_sum = sum;
    verify_code = CONV_OK;
}

static void seq_to_prg(void) {
    char probe[28];
    char spec[28];
    int wrote;
    int replace;

    file_spec(probe, 0, out_name, ",P,R");
    replace = confirm_overwrite(probe);
    if (!replace)
        return;
    if (seq_pass(0) < 0)
        return;
    wrote = seq_pass(1);
    if (wrote < 0)
        return;
    file_spec(spec, replace, out_name, ",P,W");
    if (flush_out(spec) < 0)
        return;
    if (!drive_status_ok())
        return;
    if (wrote == 0)
        put_row(12, "EMPTY PRG");
    else {
        char msg[20];
        set_str(msg, "WROTE ");
        add_dec(msg, (unsigned int)wrote);
        put_row(12, msg);
    }
    file_spec(probe, 0, out_name, ",P,R");
    remember_output(probe);
}

static void prg_to_seq(void) {
    char probe[28];
    char spec[28];
    int wrote;
    int replace;

    file_spec(probe, 0, out_name, ",S,R");
    replace = confirm_overwrite(probe);
    if (!replace)
        return;
    if (prg_pass(0) < 0)
        return;
    wrote = prg_pass(1);
    if (wrote < 0)
        return;
    file_spec(spec, replace, out_name, ",S,W");
    if (flush_out(spec) < 0)
        return;
    if (!drive_status_ok())
        return;
    file_spec(probe, 0, out_name, ",S,R");
    remember_output(probe);
}

#define TEXT_CAP (OUT_CAP - 2048)
#define DIR_BYTES 18
#define DIR_MAX 144

static void show_tool_err(int rc) {
    if (rc == 1 || rc == 2)
        finish(CONV_NUM_BIG, "NUM BIG");
    else if (rc == 3)
        finish(CONV_TOO_BIG, "TOO BIG");
    else if (rc == 4)
        finish(CONV_LINE_LONG, "LINE LONG");
    else if (rc == 6)
        finish(CONV_BAD_BASIC, "NO LABEL");
    else if (rc == 7)
        finish(CONV_BAD_NAME, "BAD NAME");
    else if (rc == 8)
        finish(CONV_BAD_BASIC, "DUP LABEL");
    else
        finish(CONV_BAD_BASIC, "NO NUM");
}

static int read_text(unsigned int cap) {
    char spec[28];
    unsigned char eof = 0;

    file_spec(spec, 0, in_name, ",S,R");
    if (open_ch(2, read_drive, spec) != 0) {
        finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);
    out_used = 0;
    for (;;) {
        int c = read_byte(2, &eof);

        if (c == -2) {
            char msg[12];
            set_str(msg, "READ ");
            add_hex2(msg, last_st);
            finish(CONV_DISK, msg);
            return -1;
        }
        if (c < 0)
            break;
        if (out_used >= cap) {
            finish(CONV_TOO_BIG, "TOO BIG");
            return -1;
        }
        out_bytes[out_used++] = (unsigned char)c;
    }
    close_ch(2);
    return 0;
}

static int write_named(const char *kind, int replace) {
    char spec[28];

    file_spec(spec, replace, out_name, ",");
    add_str(spec, kind);
    if (flush_out(spec) < 0)
        return -1;
    if (!drive_status_ok())
        return -1;
    return 0;
}

static void renumber_job(unsigned int step) {
    char probe[28];
    int replace;
    int err = 0;
    struct RenumEnt *map = (struct RenumEnt *)(out_bytes + TEXT_CAP);
    int cap = 2048 / (int)sizeof(struct RenumEnt);

    file_spec(probe, 0, out_name, ",S,R");
    replace = confirm_overwrite(probe);
    if (!replace)
        return;
    if (read_text(TEXT_CAP) < 0)
        return;
    if (renumber_seq(out_bytes, &out_used, TEXT_CAP, map, cap, step, &err)) {
        show_tool_err(err);
        return;
    }
    if (write_named("S,W", replace) < 0)
        return;
    put_row(12, "RENUMBERED");
    file_spec(probe, 0, out_name, ",S,R");
    remember_output(probe);
}

static int put_prg(unsigned int *sp, unsigned int room, unsigned char b) {
    if (*sp >= room) {
        finish(CONV_TOO_BIG, "TOO BIG");
        return -1;
    }
    out_bytes[(*sp)++] = b;
    return 0;
}

static int put_prg_word(unsigned int *sp, unsigned int room, unsigned int w) {
    if (put_prg(sp, room, (unsigned char)w) < 0)
        return -1;
    return put_prg(sp, room, (unsigned char)(w >> 8));
}

/* Text sits at the top of out_bytes. The PRG is written from the start. */
static int tokenize_buffer(unsigned int text_len) {
    unsigned char *src = out_bytes + (OUT_CAP - text_len);
    unsigned int room = OUT_CAP - text_len;
    unsigned int sp = 0;
    unsigned int i = 0;
    unsigned int addr = 0x0801;
    int wrote = 0;

    if (text_len)
        memmove(src, out_bytes, text_len);
    if (put_prg_word(&sp, room, 0x0801) < 0)
        return -1;
    while (i <= text_len) {
        char line[80];
        int n = 0;
        int past = 0;
        int slen;
        unsigned int line_no;
        unsigned int next;
        int j;

        if (i == text_len)
            break;
        while (i < text_len && src[i] != '\r') {
            if (n >= 79)
                past = 1;
            else
                line[n++] = (char)src[i];
            i++;
        }
        if (i < text_len && src[i] == '\r')
            i++;
        if (past) {
            finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        line[n] = '\0';
        slen = conv_tokenize(line, &line_no, tok, 79);
        if (slen == -1)
            continue;
        if (slen == -2) {
            finish(CONV_NO_NUM, "NO NUM");
            return -1;
        }
        if (slen == -3) {
            finish(CONV_NUM_BIG, "NUM BIG");
            return -1;
        }
        if (slen < 0) {
            finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        next = addr + 4u + (unsigned int)slen + 1u;
        if (next < addr) {
            finish(CONV_TOO_BIG, "TOO BIG");
            return -1;
        }
        if (put_prg_word(&sp, room, next) < 0 || put_prg_word(&sp, room, line_no) < 0)
            return -1;
        for (j = 0; j < slen; j++) {
            if (put_prg(&sp, room, tok[j]) < 0)
                return -1;
        }
        if (put_prg(&sp, room, 0) < 0)
            return -1;
        addr = next;
        wrote++;
    }
    if (put_prg_word(&sp, room, 0) < 0)
        return -1;
    out_used = sp;
    return wrote;
}

static void label_job(void) {
    char probe[28];
    int replace;
    int err = 0;
    int wrote;
    unsigned int text_len;
    struct LabelEnt *map = (struct LabelEnt *)(out_bytes + TEXT_CAP);
    int cap = 2048 / (int)sizeof(struct LabelEnt);

    file_spec(probe, 0, out_name, ",P,R");
    replace = confirm_overwrite(probe);
    if (!replace)
        return;
    if (read_text(TEXT_CAP) < 0)
        return;
    if (labels_to_numbered(out_bytes, &out_used, TEXT_CAP, map, cap, 10, &err)) {
        show_tool_err(err);
        return;
    }
    text_len = out_used;
    wrote = tokenize_buffer(text_len);
    if (wrote < 0)
        return;
    if (write_named("P,W", replace) < 0)
        return;
    if (wrote == 0)
        put_row(12, "EMPTY PRG");
    else {
        char msg[20];
        set_str(msg, "WROTE ");
        add_dec(msg, (unsigned int)wrote);
        put_row(12, msg);
    }
    file_spec(probe, 0, out_name, ",P,R");
    remember_output(probe);
}

static int has_word(const char *s, const char *w) {
    while (*s) {
        const char *a = s;
        const char *b = w;

        while (*b && *a == *b) {
            a++;
            b++;
        }
        if (*b == '\0')
            return 1;
        s++;
    }
    return 0;
}

static char type_of(const char *s) {
    if (has_word(s, "DEL"))
        return 'D';
    if (has_word(s, "SEQ"))
        return 'S';
    if (has_word(s, "PRG"))
        return 'P';
    if (has_word(s, "USR"))
        return 'U';
    if (has_word(s, "REL"))
        return 'R';
    return 0;
}

static int load_dir(void) {
    unsigned char eof = 0;
    int nent = 0;
    int header = 1;

    cbm_k_setlfs(2, read_drive, 0);
    cbm_k_setnam("$");
    if (cbm_k_open() != 0) {
        cbm_k_close(2);
        finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);
    read_byte(2, &eof);
    read_byte(2, &eof);
    while (nent < DIR_MAX && !eof) {
        int link_lo = read_byte(2, &eof);
        int link_hi = read_byte(2, &eof);
        int blo;
        int bhi;
        unsigned int blocks;
        char full[40];
        int fp = 0;
        int c;
        int inq = 0;
        int np = 0;
        int i;
        char name[17];
        char type;
        unsigned char *ent;

        if (link_lo < 0 || link_hi < 0)
            break;
        if (link_lo == 0 && link_hi == 0)
            break;
        blo = read_byte(2, &eof);
        bhi = read_byte(2, &eof);
        if (blo < 0 || bhi < 0)
            break;
        blocks = (unsigned int)blo + ((unsigned int)bhi << 8);
        while (fp < 39) {
            c = read_byte(2, &eof);
            if (c <= 0)
                break;
            full[fp++] = (char)c;
        }
        full[fp] = '\0';
        if (header && blocks == 0) {
            header = 0;
            continue;
        }
        header = 0;
        for (i = 0; i < fp; i++) {
            if (full[i] == '"') {
                if (!inq)
                    inq = 1;
                else {
                    inq = 2;
                    break;
                }
            } else if (inq == 1 && np < 16)
                name[np++] = full[i];
        }
        name[np] = '\0';
        if (np == 0)
            break;
        if (inq != 2)
            continue;
        type = type_of(full + i + 1);
        if (!type)
            continue;
        ent = out_bytes + (unsigned int)nent * DIR_BYTES;
        memset(ent, 0, DIR_BYTES);
        memcpy(ent, name, (unsigned int)np);
        ent[16] = 0;
        ent[17] = (unsigned char)type;
        nent++;
    }
    close_ch(2);
    return nent;
}

static void draw_title(void) {
    clrscr();
    cputs_at((40 - 8) / 2, 0, "W64TOOLS", COL_GREEN);
}

static void draw_menu(void) {
    draw_title();
    put_row(2, "1 SEQ TO PRG");
    put_row(3, "2 PRG TO SEQ");
    put_row(4, "3 RENUMBER SEQ");
    put_row(5, "4 LABELS TO PRG");
    put_row(6, "? HELP");
}

static void show_help(void) {
    clrscr();
    put_row(0, "W64TOOLS HELP");
    put_row(2, "1 SEQ>PRG  2 PRG>SEQ");
    put_row(3, "3 RENUMBER 4 LABELS");
    put_row(5, "* ERRORS *");
    put_row(6, "NO NUM:    LINE HAS NO NUMBER");
    put_row(7, "NUM BIG:   PAST 63999");
    put_row(8, "LINE LONG: PAST 79 CHARACTERS");
    put_row(9, "TOO BIG:   BUFFER IS FULL");
    put_row(10, "OPEN FAIL: DRIVE DID NOT OPEN");
    put_row(11, "BAD NAME:  EMPTY OR , : * ? @");
    put_row(12, "NO LABEL:  GOTO NAME MISSING");
    put_row(13, "DUP LABEL: SAME NAME TWICE");
    put_row(14, "DISK:      SEE THE DOS TEXT");
    put_row(16, "STOP EXITS THIS PAGE");
    cgetc();
}

static int menu_key(void) {
    for (;;) {
        int ch = cgetc();

        if (ch == 3)
            return -1;
        if (ch == '?') {
            show_help();
            draw_menu();
            continue;
        }
        if (ch >= '1' && ch <= '4')
            return ch - '0';
    }
}

static int pick_file(int mode) {
    unsigned char which[DIR_MAX];
    int nent;
    int nshow = 0;
    int sel = 0;
    int i;
    char want = (mode == 2) ? 'P' : 'S';

    nent = load_dir();
    if (nent < 0)
        return -1;
    for (i = 0; i < nent; i++) {
        if (out_bytes[(unsigned int)i * DIR_BYTES + 17] == (unsigned char)want)
            which[nshow++] = (unsigned char)i;
    }
    if (nshow == 0) {
        put_row(12, "NO FILES");
        cgetc();
        return -1;
    }
    draw_title();
    for (;;) {
        int top = 0;
        int row;
        int ch;

        if (sel >= 18)
            top = sel - 17;
        for (row = 0; row < 18; row++) {
            int ix = top + row;
            unsigned char y = (unsigned char)(2 + row);

            if (ix < nshow) {
                unsigned char *ent = out_bytes + (unsigned int)which[ix] * DIR_BYTES;
                unsigned char col = (ix == sel) ? COL_YELLOW : COL_GREEN;
                unsigned char x;

                for (x = 0; x < 39; x++)
                    cputc_at(x, y, ' ', col);
                cputs_at(0, y, (const char *)ent, col);
            } else
                put_row(y, "");
        }
        ch = cgetc();
        if (ch == 3)
            return -1;
        if (ch == 13) {
            unsigned char *ent = out_bytes + (unsigned int)which[sel] * DIR_BYTES;
            set_str(in_name, (const char *)ent);
            return 0;
        }
        if (ch == KEY_DOWN && sel + 1 < nshow)
            sel++;
        if (ch == KEY_UP && sel > 0)
            sel--;
    }
}

static int parse_step(const char *s, unsigned int *step) {
    unsigned int v = 0;

    if (*s == '\0') {
        *step = 10;
        return 0;
    }
    while (*s) {
        unsigned int d;

        if (!isdigit((unsigned char)*s))
            return -1;
        d = (unsigned int)(*s - '0');
        if (v > 63999u / 10 || (v == 63999u / 10 && d > 63999u % 10))
            return -1;
        v = v * 10u + d;
        s++;
    }
    *step = v;
    return 0;
}

static int another(void) {
    int ch;

    put_row(22, "ANOTHER? Y/N");
    for (;;) {
        ch = cgetc();
        if (ch == 'Y' || ch == 'y') {
            verify_code = 0;
            return 1;
        }
        if (ch == 'N' || ch == 'n' || ch == 3) {
            *(unsigned char *)0xD020 = 14;
            *(unsigned char *)0xD021 = 6;
            clrscr();
            return 0;
        }
    }
}

int main(void) {
    *(unsigned char *)0xD020 = 12;
    *(unsigned char *)0xD021 = 0;
    for (;;) {
        int mode;
        unsigned int step = 10;

        /* A Stop in a field sets CANCEL. Do not let that linger into the next directory. */
        verify_code = 0;
        draw_menu();
        mode = menu_key();
        if (mode < 0) {
            verify_code = CONV_CANCEL;
            put_row(12, "CANCELLED");
            stay();
        }
        if (ask_drive(8, "READ DRIVE (8-15): ", &read_drive) < 0)
            continue;
        if (ask_drive(10, "WRITE DRIVE (8-15): ", &write_drive) < 0)
            continue;
        if (pick_file(mode) < 0) {
            if (verify_code != 0 && !another())
                return 0;
            continue;
        }
        if (read_field(20, "OUTPUT FILENAME: ", out_name, 17) < 0)
            continue;
        if (out_name[0] == '\0')
            set_str(out_name, in_name);
        if (out_name[0] == '\0' || !name_ok(out_name)) {
            finish(CONV_BAD_NAME, "BAD NAME");
            if (!another())
                return 0;
            continue;
        }
        if (mode == 3) {
            char buf[8];
            int n = read_field(21, "STEP: ", buf, 8);

            if (n < 0)
                continue;
            if (parse_step(buf, &step) < 0) {
                finish(CONV_NUM_BIG, "NUM BIG");
                if (!another())
                    return 0;
                continue;
            }
        }
        if (mode == 1)
            seq_to_prg();
        else if (mode == 2)
            prg_to_seq();
        else if (mode == 3)
            renumber_job(step);
        else
            label_job();
        if (!another())
            return 0;
    }
}
