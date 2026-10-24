#include "w64toolui.h"
#include "w64reu.h"
#include "w64tools.h"
#include <c64.h>
#include <cbm.h>
#include <ctype.h>
#include <string.h>

/* This SDK has no conio.h. 40-column screen codes match the bytes we store. */
#define SCREEN_RAM ((unsigned char *)0x0400)
#define KEY_DOWN 17
#define KEY_UP 145

/* Kept out of zero page. The Ultimate test reads these after main parks. */
volatile unsigned char verify_buf[256];
volatile unsigned long verify_len;
volatile unsigned long verify_sum;
volatile unsigned char verify_code __attribute__((section(".bss")));

unsigned char read_drive;
unsigned char write_drive;
char in_name[17];
char out_name[17];
unsigned char tok[80];
char text_line[80];
unsigned long reu_bytes;
unsigned char last_st;

static unsigned char dir_ents[DIR_MAX * DIR_BYTES];

static unsigned char reu_chunk[256];
static unsigned int reu_chunk_n;
static unsigned long reu_pos;

static void cputc_at(unsigned char x, unsigned char y, char c, unsigned char color) {
    unsigned int pos = (unsigned int)y * 40 + x;
    SCREEN_RAM[pos] = (unsigned char)c;
    COLOR_RAM[pos] = color;
}

/* PETSCII 14. Title bytes $41-$5A are capitals in the lowercase/uppercase set. */
void tool_lowercase(void) {
    cbm_k_chrout(14);
}

void tool_clrscr(void) {
    unsigned int i;
    for (i = 0; i < 1000; i++) {
        SCREEN_RAM[i] = ' ';
        COLOR_RAM[i] = COL_GREEN;
    }
}

int tool_cgetc(void) {
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

static void paint_row(unsigned char row, const char *msg, unsigned char color) {
    unsigned char i;

    for (i = 0; i < 39; i++)
        cputc_at(i, row, ' ', COL_GREEN);
    cputs_at(0, row, msg, color);
}

void tool_put_row(unsigned char row, const char *msg) {
    paint_row(row, msg, COL_GREEN);
}

void tool_put_err(unsigned char row, const char *msg) {
    paint_row(row, msg, COL_RED);
}

void tool_center_row(unsigned char row, const char *msg) {
    unsigned char len = 0;
    unsigned char x = 0;
    unsigned char i;

    while (msg[len])
        len++;
    for (i = 0; i < 39; i++)
        cputc_at(i, row, ' ', COL_GREEN);
    if (len < 40)
        x = (unsigned char)((40 - len) >> 1);
    cputs_at(x, row, msg, COL_GREEN);
}

/* One shared formatter. sprintf pulls in about 4KB of printf. */
void tool_set_str(char *dst, const char *src) {
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

void tool_add_str(char *dst, const char *src) {
    while (*dst)
        dst++;
    tool_set_str(dst, src);
}

void tool_add_dec(char *dst, unsigned int v) {
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

void tool_add_hex2(char *dst, unsigned char v) {
    static const char hex[] = "0123456789ABCDEF";

    while (*dst)
        dst++;
    *dst++ = hex[v >> 4];
    *dst++ = hex[v & 0x0f];
    *dst = '\0';
}

/* replace == 2 writes the DOS overwrite prefix. tail is ",S,R" or similar. */
void tool_file_spec(char *dst, int replace, const char *name, const char *tail) {
    dst[0] = '\0';
    if (replace == 2)
        tool_add_str(dst, "@0:");
    tool_add_str(dst, name);
    tool_add_str(dst, tail);
}

int tool_name_ok(const char *s) {
    while (*s) {
        if (*s == ',' || *s == ':' || *s == '*' || *s == '?' || *s == '@')
            return 0;
        s++;
    }
    return 1;
}

int tool_read_field(unsigned char row, const char *label, char *buf, int cap) {
    int n = 0;
    int ch;
    int label_len = 0;

    while (label[label_len])
        label_len++;

    tool_put_row(row, label);
    for (;;) {
        ch = tool_cgetc();
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

int tool_ask_drive(unsigned char row, const char *label, unsigned char *dest) {
    char buf[4];
    int n;
    int i;
    unsigned int v;

    for (;;) {
        n = tool_read_field(row, label, buf, 4);
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
        tool_put_err(20, "USE 8-15");
    }
}

int tool_open_ch(unsigned char lfn, unsigned char drive, const char *spec) {
    cbm_k_setlfs(lfn, drive, 2);
    cbm_k_setnam(spec);
    if (cbm_k_open() != 0) {
        cbm_k_close(lfn);
        return -1;
    }
    return 0;
}

void tool_close_ch(unsigned char lfn) {
    cbm_k_clrch();
    cbm_k_close(lfn);
}

/* KERNAL STATUS at $90 is OR-ed in. A write leaves bits that the next read would keep. */
static int chrin_fresh(void) {
    *(unsigned char *)0x90 = 0;
    return cbm_k_chrin();
}

int tool_read_byte(unsigned char lfn_ready, unsigned char *eof) {
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

void tool_finish(unsigned char code, const char *msg) {
    cbm_k_clrch();
    cbm_k_close(2);
    cbm_k_close(3);
    verify_code = code;
    if (code == CONV_CANCEL)
        tool_put_row(18, msg);
    else
        tool_put_err(18, msg);
}

int tool_drive_status_ok(void) {
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
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return 0;
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
    tool_finish(CONV_DISK, st);
    return 0;
}

/* KERNAL OPEN returns 0 when the drive answers. A missing file is DOS 62. */
static int dos_missing(const char *spec) {
    char st[4];
    int j = 0;
    int c;

    if (tool_open_ch(2, write_drive, spec) != 0)
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
        tool_close_ch(2);
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
    tool_close_ch(2);
    return st[0] == '6' && st[1] == '2';
}

int tool_confirm_overwrite(const char *spec) {
    int ch;

    if (dos_missing(spec))
        return 1;
    tool_put_row(17, "OVERWRITE? Y/N");
    ch = tool_cgetc();
    if (ch != 'Y' && ch != 'y') {
        tool_finish(CONV_CANCEL, "CANCELLED");
        return 0;
    }
    return 2;
}

/* 0 got a line, 1 clean EOF, -1 error already stored in *err. */
int tool_read_seq_line(unsigned char *eof, char *line, int *err) {
    int n = 0;
    int c;

    *err = 0;
    for (;;) {
        c = tool_read_byte(2, eof);
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

void tool_note_progress(unsigned char row, const char *label, unsigned int n) {
    char msg[20];

    cbm_k_clrch();
    tool_set_str(msg, label);
    tool_add_dec(msg, n);
    tool_put_row(row, msg);
}

void tool_paint_reu(void) {
    char msg[16];
    unsigned int kb;

    if (reu_bytes == 0) {
        tool_center_row(1, "REU: NONE");
        return;
    }
    tool_set_str(msg, "REU: ");
    kb = (unsigned int)(reu_bytes >> 10);
    tool_add_dec(msg, kb);
    tool_add_str(msg, "KB");
    tool_center_row(1, msg);
}

void tool_stash_reset(void) {
    reu_pos = 0;
    reu_chunk_n = 0;
}

int tool_stash_byte(unsigned char b) {
    if (reu_pos + (unsigned long)reu_chunk_n >= reu_bytes) {
        tool_finish(CONV_TOO_BIG, "TOO BIG");
        return -1;
    }
    reu_chunk[reu_chunk_n++] = b;
    if (reu_chunk_n == 256) {
        w64_store_put(reu_pos, reu_chunk, 256);
        reu_pos += 256;
        reu_chunk_n = 0;
    }
    return 0;
}

void tool_stash_flush(void) {
    if (reu_chunk_n == 0)
        return;
    w64_store_put(reu_pos, reu_chunk, reu_chunk_n);
    reu_pos += reu_chunk_n;
    reu_chunk_n = 0;
}

unsigned long tool_stored(void) {
    return reu_pos;
}

int tool_flush_at(const char *spec, unsigned long start, unsigned long nbytes) {
    unsigned char chunk[256];
    unsigned long off = 0;

    if (tool_open_ch(3, write_drive, spec) != 0) {
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkout(3);
    while (off < nbytes) {
        unsigned int n = 256;
        unsigned int i;
        unsigned long left = nbytes - off;

        if (left < n)
            n = (unsigned int)left;
        w64_store_get(start + off, chunk, n);
        for (i = 0; i < n; i++)
            cbm_k_chrout(chunk[i]);
        off += n;
    }
    tool_close_ch(3);
    return 0;
}

void tool_remember_output(const char *spec) {
    unsigned char eof = 0;
    unsigned long sum = 0;
    unsigned long n = 0;
    int b;

    verify_len = 0;
    verify_sum = 0;
    if (tool_open_ch(2, write_drive, spec) != 0) {
        tool_finish(CONV_OPEN_FAIL, "READBACK FAIL");
        return;
    }
    cbm_k_chkin(2);
    for (;;) {
        b = tool_read_byte(2, &eof);
        if (b == -2) {
            char msg[20];
            tool_close_ch(2);
            verify_len = n;
            verify_sum = sum;
            tool_set_str(msg, "BACK ");
            tool_add_hex2(msg, last_st);
            tool_add_str(msg, " ");
            tool_add_dec(msg, (unsigned int)n);
            tool_finish(CONV_DISK, msg);
            return;
        }
        if (b < 0)
            break;
        if (n < 256)
            verify_buf[n] = (unsigned char)b;
        sum += (unsigned char)b;
        n++;
    }
    tool_close_ch(2);
    verify_len = n;
    verify_sum = sum;
    verify_code = CONV_OK;
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
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);
    tool_read_byte(2, &eof);
    tool_read_byte(2, &eof);
    while (nent < DIR_MAX && !eof) {
        int link_lo = tool_read_byte(2, &eof);
        int link_hi = tool_read_byte(2, &eof);
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
        blo = tool_read_byte(2, &eof);
        bhi = tool_read_byte(2, &eof);
        if (blo < 0 || bhi < 0)
            break;
        blocks = (unsigned int)blo + ((unsigned int)bhi << 8);
        while (fp < 39) {
            c = tool_read_byte(2, &eof);
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
        ent = dir_ents + (unsigned int)nent * DIR_BYTES;
        memset(ent, 0, DIR_BYTES);
        memcpy(ent, name, (unsigned int)np);
        ent[16] = 0;
        ent[17] = (unsigned char)type;
        nent++;
    }
    tool_close_ch(2);
    return nent;
}

/* The menu painted the disk name in these columns. The file list keeps it. */
static void draw_title(void) {
    unsigned char name[8];
    unsigned char i;
    unsigned char x = (40 - 8) / 2;

    for (i = 0; i < 8; i++)
        name[i] = SCREEN_RAM[x + i];
    tool_clrscr();
    for (i = 0; i < 8; i++)
        cputc_at((unsigned char)(x + i), 0, (char)name[i], COL_GREEN);
    tool_paint_reu();
}

int tool_pick_file(char want) {
    unsigned char which[DIR_MAX];
    int nent;
    int nshow = 0;
    int sel = 0;
    int i;

    nent = load_dir();
    if (nent < 0)
        return -1;
    for (i = 0; i < nent; i++) {
        if (dir_ents[(unsigned int)i * DIR_BYTES + 17] == (unsigned char)want)
            which[nshow++] = (unsigned char)i;
    }
    if (nshow == 0) {
        tool_put_err(12, "NO FILES");
        tool_cgetc();
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
                unsigned char *ent = dir_ents + (unsigned int)which[ix] * DIR_BYTES;
                unsigned char col = (ix == sel) ? COL_YELLOW : COL_GREEN;
                unsigned char x;

                for (x = 0; x < 39; x++)
                    cputc_at(x, y, ' ', col);
                cputs_at(0, y, (const char *)ent, col);
            } else
                tool_put_row(y, "");
        }
        ch = tool_cgetc();
        if (ch == 3)
            return -1;
        if (ch == 13) {
            unsigned char *ent = dir_ents + (unsigned int)which[sel] * DIR_BYTES;
            tool_set_str(in_name, (const char *)ent);
            return 0;
        }
        if (ch == KEY_DOWN && sel + 1 < nshow)
            sel++;
        if (ch == KEY_UP && sel > 0)
            sel--;
    }
}

int tool_parse_step(const char *s, unsigned int *step) {
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

int tool_another(void) {
    int ch;

    tool_put_row(22, "ANOTHER? Y/N");
    for (;;) {
        ch = tool_cgetc();
        if (ch == 'Y' || ch == 'y') {
            verify_code = 0;
            return 1;
        }
        if (ch == 'N' || ch == 'n' || ch == 3) {
            *(unsigned char *)0xD020 = 14;
            *(unsigned char *)0xD021 = 6;
            tool_clrscr();
            return 0;
        }
    }
}

void tool_stay(void) {
    for (;;)
        ;
}

void tool_show_err(int rc, int row) {
    char msg[20];

    if (rc == 1 || rc == 2)
        tool_finish(CONV_NUM_BIG, "NUM BIG");
    else if (rc == 3)
        tool_finish(CONV_TOO_BIG, "TOO BIG");
    else if (rc == 4) {
        if (row) {
            tool_set_str(msg, "LINE LONG ");
            tool_add_dec(msg, (unsigned int)row);
            tool_finish(CONV_LINE_LONG, msg);
        } else
            tool_finish(CONV_LINE_LONG, "LINE LONG");
    } else if (rc == 6) {
        if (row) {
            tool_set_str(msg, "NO LABEL ");
            tool_add_dec(msg, (unsigned int)row);
            tool_finish(CONV_BAD_BASIC, msg);
        } else
            tool_finish(CONV_BAD_BASIC, "NO LABEL");
    } else if (rc == 7)
        tool_finish(CONV_BAD_NAME, "BAD NAME");
    else if (rc == 8)
        tool_finish(CONV_BAD_BASIC, "DUP LABEL");
    else if (rc == 9)
        tool_finish(CONV_TOO_BIG, "PRG TOO BIG");
    else
        tool_finish(CONV_BAD_BASIC, "NO NUM");
}

static void clear_dir_rows(void) {
    unsigned char y;

    for (y = 2; y < 20; y++)
        tool_put_row(y, "");
}

int tool_prepare(char want) {
    if (tool_ask_drive(8, "READ DRIVE (8-15): ", &read_drive) < 0)
        return -1;
    if (tool_ask_drive(10, "WRITE DRIVE (8-15): ", &write_drive) < 0)
        return -1;
    if (tool_pick_file(want) < 0)
        return -1;
    if (tool_read_field(20, "OUTPUT FILENAME: ", out_name, 17) < 0)
        return -1;
    if (out_name[0] == '\0')
        tool_set_str(out_name, in_name);
    clear_dir_rows();
    if (out_name[0] == '\0' || !tool_name_ok(out_name)) {
        tool_finish(CONV_BAD_NAME, "BAD NAME");
        return -1;
    }
    return 0;
}

int tool_trailing_ml(unsigned char *eof) {
    int b;
    int b2;

    if (*eof)
        return 0;
    b = tool_read_byte(2, eof);
    if (b < 0)
        return 0;
    if (b == 13 && *eof) {
        b2 = tool_read_byte(2, eof);
        if (b2 < 0)
            return 0;
        return 1;
    }
    return 1;
}
