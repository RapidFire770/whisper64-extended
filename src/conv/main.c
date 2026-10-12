#include "w64conv.h"
#include <cbm.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* This SDK has no conio.h. 40-column screen codes match the bytes we store. */
#define SCREEN_RAM ((unsigned char *)0x0400)
#define COL_WHITE 1

static void cputc_at(unsigned char x, unsigned char y, char c) {
    unsigned int pos = (unsigned int)y * 40 + x;
    SCREEN_RAM[pos] = (unsigned char)c;
    COLOR_RAM[pos] = COL_WHITE;
}

static void clrscr(void) {
    unsigned int i;
    for (i = 0; i < 1000; i++) {
        SCREEN_RAM[i] = ' ';
        COLOR_RAM[i] = COL_WHITE;
    }
}

static int cgetc(void) {
    unsigned char c;
    while (!(c = cbm_k_getin()))
        ;
    return c;
}

static void cputs_at(unsigned char x, unsigned char y, const char *s) {
    while (*s && x < 39) {
        cputc_at(x, y, *s++);
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

static unsigned char drive_num;
static char in_name[17];
static char out_name[17];
static unsigned char tok[80];
static char text_line[80];

/* A 1541 keeps one data file open. Bytes wait here until the input is closed. */
#define OUT_CAP 32768
static unsigned char out_bytes[OUT_CAP];
static unsigned int out_used;

static void put_row(unsigned char row, const char *msg) {
    unsigned char i;

    for (i = 0; i < 39; i++)
        cputc_at(i, row, ' ');
    cputs_at(0, row, msg);
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
    int label_len = (int)strlen(label);

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
            cputc_at((unsigned char)(label_len + n), row, ' ');
            continue;
        }
        if (ch >= 32 && ch < 128 && n < cap - 1) {
            buf[n++] = (char)ch;
            cputc_at((unsigned char)(label_len + n - 1), row, (char)ch);
        }
    }
}

static int ask_drive(void) {
    char buf[4];
    int n;
    unsigned int v;

    for (;;) {
        n = read_field(2, "DRIVE ", buf, 4);
        if (n < 0)
            return -1;
        if (n == 0) {
            drive_num = 8;
            return 0;
        }
        v = 0;
        for (n = 0; buf[n]; n++) {
            if (!isdigit((unsigned char)buf[n])) {
                v = 0;
                break;
            }
            v = v * 10 + (unsigned int)(buf[n] - '0');
        }
        if (v >= 8 && v <= 15) {
            drive_num = (unsigned char)v;
            return 0;
        }
        put_row(3, "USE 8-15");
    }
}

static int open_ch(unsigned char lfn, const char *spec) {
    cbm_k_setlfs(lfn, drive_num, 2);
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

    if (open_ch(3, spec) != 0) {
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
    cbm_k_setlfs(15, drive_num, 15);
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

    if (open_ch(2, spec) != 0)
        return 1;
    /* A read forces the drive to resolve the name. OPEN alone can leave 73. */
    cbm_k_chkin(2);
    c = chrin_fresh();
    (void)c;
    (void)cbm_k_readst();
    cbm_k_clrch();
    cbm_k_setlfs(15, drive_num, 15);
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

static void note_progress(unsigned char row, const char *fmt, int n) {
    char msg[20];

    cbm_k_clrch();
    sprintf(msg, fmt, n);
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

    sprintf(spec, "%s,S,R", in_name);
    if (open_ch(2, spec) != 0) {
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
                sprintf(msg, "READ %02X", last_st);
                finish(CONV_DISK, msg);
            } else
                finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        if (rc > 0)
            break;
        seen++;
        note_progress(10, writing ? "PRG %d" : "CHECK %d", seen);
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

    sprintf(spec, "%s,P,R", in_name);
    if (open_ch(2, spec) != 0) {
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
        sprintf(msg, writing ? "SEQ %d" : "CHECK %d", wrote);
        put_row(10, msg);
        cbm_k_chkin(2);
    }

    ml = trailing_ml(&eof);
    close_ch(2);
    if (!writing)
        return wrote;
    {
        char msg[28];
        if (trunc || ml)
            sprintf(msg, "SEQ %d TRUNC %d ML %d", wrote, trunc, ml);
        else
            sprintf(msg, "SEQ %d", wrote);
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
    if (open_ch(2, spec) != 0) {
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
            sprintf(msg, "BACK %02X %u", last_st, (unsigned)n);
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

    sprintf(probe, "%s,P,R", out_name);
    replace = confirm_overwrite(probe);
    if (!replace)
        return;
    if (seq_pass(0) < 0)
        return;
    wrote = seq_pass(1);
    if (wrote < 0)
        return;
    if (replace == 2)
        sprintf(spec, "@0:%s,P,W", out_name);
    else
        sprintf(spec, "%s,P,W", out_name);
    if (flush_out(spec) < 0)
        return;
    if (!drive_status_ok())
        return;
    if (wrote == 0)
        put_row(12, "EMPTY PRG");
    else {
        char msg[20];
        sprintf(msg, "WROTE %d", wrote);
        put_row(12, msg);
    }
    sprintf(probe, "%s,P,R", out_name);
    remember_output(probe);
}

static void prg_to_seq(void) {
    char probe[28];
    char spec[28];
    int wrote;
    int replace;

    sprintf(probe, "%s,S,R", out_name);
    replace = confirm_overwrite(probe);
    if (!replace)
        return;
    if (prg_pass(0) < 0)
        return;
    wrote = prg_pass(1);
    if (wrote < 0)
        return;
    if (replace == 2)
        sprintf(spec, "@0:%s,S,W", out_name);
    else
        sprintf(spec, "%s,S,W", out_name);
    if (flush_out(spec) < 0)
        return;
    if (!drive_status_ok())
        return;
    sprintf(probe, "%s,S,R", out_name);
    remember_output(probe);
}

int main(void) {
    int ch;

    clrscr();
    put_row(0, "W64CONV");
    if (ask_drive() < 0) {
        put_row(12, "CANCELLED");
        stay();
    }
    put_row(4, "1 SEQ TO PRG");
    put_row(5, "2 PRG TO SEQ");
    put_row(6, "WHICH?");
    for (;;) {
        ch = cgetc();
        if (ch == 3) {
            verify_code = CONV_CANCEL;
            put_row(12, "CANCELLED");
            stay();
        }
        if (ch == '1' || ch == '2')
            break;
    }
    if (read_field(7, "IN ", in_name, 17) < 0)
        stay();
    if (in_name[0] == '\0' || !name_ok(in_name)) {
        finish(CONV_BAD_NAME, "BAD NAME");
        stay();
    }
    if (read_field(8, "OUT ", out_name, 17) < 0)
        stay();
    if (out_name[0] == '\0' || !name_ok(out_name)) {
        finish(CONV_BAD_NAME, "BAD NAME");
        stay();
    }
    if (ch == '1')
        seq_to_prg();
    else
        prg_to_seq();
    stay();
}
