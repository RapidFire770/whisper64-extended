#include "w64toolui.h"
#include "w64reu.h"
#include "w64tools.h"
#include <cbm.h>

static void reu_slide(unsigned long dst, unsigned long src, unsigned long n,
                      int from_high) {
    unsigned char chunk[256];
    unsigned long off;

    if (n == 0 || dst == src)
        return;
    if (from_high) {
        off = n;
        while (off) {
            unsigned int take = 256;

            if (off < take)
                take = (unsigned int)off;
            off -= take;
            w64_store_get(src + off, chunk, take);
            w64_store_put(dst + off, chunk, take);
        }
    } else {
        off = 0;
        while (off < n) {
            unsigned int take = 256;

            if (n - off < take)
                take = (unsigned int)(n - off);
            w64_store_get(src + off, chunk, take);
            w64_store_put(dst + off, chunk, take);
            off += take;
        }
    }
}

static int read_seq_reu(void) {
    char spec[28];
    unsigned char eof = 0;

    tool_file_spec(spec, 0, in_name, ",S,R");
    if (tool_open_ch(2, read_drive, spec) != 0) {
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);
    tool_stash_reset();
    for (;;) {
        int c = tool_read_byte(2, &eof);

        if (c == -2) {
            char msg[12];

            tool_set_str(msg, "READ ");
            tool_add_hex2(msg, last_st);
            tool_finish(CONV_DISK, msg);
            return -1;
        }
        if (c < 0)
            break;
        if (tool_stash_byte((unsigned char)c) < 0)
            return -1;
    }
    tool_stash_flush();
    tool_close_ch(2);
    return 0;
}

static void renumber_job(unsigned int step) {
    char probe[28];
    char spec[28];
    int replace;
    int err = 0;
    int row = 0;
    unsigned long out_at = 0;
    unsigned long out_len = 0;

    tool_file_spec(probe, 0, out_name, ",S,R");
    replace = tool_confirm_overwrite(probe);
    if (!replace)
        return;
    if (read_seq_reu() < 0)
        return;
    if (renumber_seq_store(tool_stored(), reu_bytes, step, &out_at, &out_len,
                           &err, &row)) {
        tool_show_err(err, row);
        return;
    }
    tool_file_spec(spec, replace, out_name, ",S,W");
    if (tool_flush_at(spec, out_at, out_len) < 0)
        return;
    if (!tool_drive_status_ok())
        return;
    tool_put_row(12, "RENUMBERED");
    tool_file_spec(probe, 0, out_name, ",S,R");
    tool_remember_output(probe);
}

static int put_tail_byte(unsigned long at, int b) {
    unsigned char c;

    if (at >= reu_bytes) {
        tool_finish(CONV_TOO_BIG, "TOO BIG");
        return -1;
    }
    c = (unsigned char)b;
    w64_store_put(at, &c, 1);
    return 0;
}

static int copy_tail(unsigned char *eof, unsigned long at, unsigned long *tail_len) {
    int b;
    int b2;
    unsigned long n = 0;

    *tail_len = 0;
    if (*eof)
        return 0;
    b = tool_read_byte(2, eof);
    if (b < 0)
        return 0;
    if (b == 13 && *eof) {
        b2 = tool_read_byte(2, eof);
        if (b2 < 0)
            return 0;
        if (put_tail_byte(at + n, b) < 0)
            return -1;
        n++;
        b = b2;
    }
    for (;;) {
        if (put_tail_byte(at + n, b) < 0)
            return -1;
        n++;
        if (*eof)
            break;
        b = tool_read_byte(2, eof);
        if (b == -2) {
            char msg[12];

            tool_set_str(msg, "READ ");
            tool_add_hex2(msg, last_st);
            tool_finish(CONV_DISK, msg);
            return -1;
        }
        if (b < 0)
            break;
    }
    *tail_len = n;
    return 0;
}

static int prg_to_text(unsigned long *text_len, unsigned long *tail_len) {
    char spec[28];
    unsigned char eof = 0;
    unsigned int addr = 0x0801;
    unsigned int link;
    unsigned int line_no;
    int lo, hi;
    unsigned int seen = 0;

    tool_file_spec(spec, 0, in_name, ",P,R");
    if (tool_open_ch(2, read_drive, spec) != 0) {
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);
    tool_stash_reset();
    lo = tool_read_byte(2, &eof);
    hi = tool_read_byte(2, &eof);
    if (lo != 0x01 || hi != 0x08) {
        tool_finish(CONV_NOT_BASIC, "NOT BASIC PRG");
        return -1;
    }
    for (;;) {
        int remain;
        int slen = 0;
        int b;

        cbm_k_chkin(2);
        lo = tool_read_byte(2, &eof);
        hi = tool_read_byte(2, &eof);
        if (lo < 0 || hi < 0) {
            tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        link = (unsigned int)(lo | (hi << 8));
        if (link == 0)
            break;
        if (link <= addr || (unsigned int)(link - addr) < 5 ||
            (unsigned int)(link - addr) > 256) {
            tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        lo = tool_read_byte(2, &eof);
        hi = tool_read_byte(2, &eof);
        if (lo < 0 || hi < 0) {
            tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        line_no = (unsigned int)(lo | (hi << 8));
        if (line_no > 63999u) {
            tool_finish(CONV_NUM_BIG, "NUM BIG");
            return -1;
        }
        remain = (int)(link - addr) - 4;
        slen = 0;
        {
            int cut_stmt = 0;

            while (remain > 1) {
                b = tool_read_byte(2, &eof);
                if (b < 0) {
                    tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
                    return -1;
                }
                if (slen < 79)
                    tok[slen++] = (unsigned char)b;
                else
                    cut_stmt = 1;
                remain--;
            }
            b = tool_read_byte(2, &eof);
            if (b != 0) {
                tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
                return -1;
            }
            {
                int cut = 0;
                int len = conv_format_line(line_no, tok, slen, text_line, 80, &cut);
                int j;

                if (len < 0 || cut || cut_stmt) {
                    char msg[20];

                    tool_set_str(msg, "LINE LONG ");
                    tool_add_dec(msg, line_no);
                    tool_finish(CONV_LINE_LONG, msg);
                    return -1;
                }
                for (j = 0; j < len; j++) {
                    if (tool_stash_byte((unsigned char)text_line[j]) < 0)
                        return -1;
                }
                if (tool_stash_byte(13) < 0)
                    return -1;
            }
        }
        addr = link;
        if (seen < 65535u)
            seen++;
        tool_note_progress(10, "PRG ", seen);
    }
    tool_stash_flush();
    *text_len = tool_stored();
    if (copy_tail(&eof, *text_len, tail_len) < 0)
        return -1;
    tool_close_ch(2);
    if (*text_len + *tail_len < *text_len) {
        tool_finish(CONV_TOO_BIG, "TOO BIG");
        return -1;
    }
    return 0;
}

static void renumber_prg(unsigned int step) {
    char probe[28];
    char spec[28];
    int replace;
    int err = 0;
    int row = 0;
    int wrote = 0;
    unsigned long text_len = 0;
    unsigned long tail_len = 0;
    unsigned long out_at = 0;
    unsigned long out_len = 0;
    unsigned long prg_at = 0;
    unsigned long prg_len = 0;
    unsigned long ceiling;

    tool_file_spec(probe, 0, out_name, ",P,R");
    replace = tool_confirm_overwrite(probe);
    if (!replace)
        return;
    if (prg_to_text(&text_len, &tail_len) < 0)
        return;
    if (text_len + tail_len > reu_bytes) {
        tool_finish(CONV_TOO_BIG, "TOO BIG");
        return;
    }
    ceiling = reu_bytes - tail_len;
    if (tail_len)
        reu_slide(ceiling, text_len, tail_len, 1);
    if (text_len) {
        if (renumber_seq_store(text_len, ceiling, step, &out_at, &out_len,
                               &err, &row)) {
            tool_show_err(err, row);
            return;
        }
    }
    if (tokenize_store(out_at, out_len, ceiling, &prg_at, &prg_len,
                       &wrote, &err, &row)) {
        tool_show_err(err, row);
        return;
    }
    if (tail_len)
        reu_slide(prg_at + prg_len, ceiling, tail_len, 0);
    tool_file_spec(spec, replace, out_name, ",P,W");
    if (tool_flush_at(spec, prg_at, prg_len + tail_len) < 0)
        return;
    if (!tool_drive_status_ok())
        return;
    tool_put_row(12, "RENUMBERED");
    tool_file_spec(probe, 0, out_name, ",P,R");
    tool_remember_output(probe);
}

static void draw_menu(void) {
    tool_clrscr();
    tool_center_row(0, "W64RENUM");
    tool_paint_reu();
    tool_put_row(3, "1 RENUMBER SEQ");
    tool_put_row(4, "2 RENUMBER PRG");
    tool_put_row(5, "? HELP");
    tool_put_row(6, "RUN/STOP EXITS THE PROGRAM");
}

static void show_help(void) {
    tool_clrscr();
    tool_center_row(0, "W64RENUM HELP");
    tool_paint_reu();
    tool_put_row(3, "1. SEQ - RENUMBERS LINES IN SEQ");
    tool_put_row(4, "2. PRG - RENUMBERS A PRG, KEEPS ML");
    tool_put_row(5, "STEP EMPTY IS 10");
    tool_put_row(6, "?  HELP - SHOWS THIS HELP SCREEN");
    tool_put_row(7, "* ERRORS *");
    tool_put_row(8, "NO NUM:      LINE HAS NO NUMBER");
    tool_put_row(9, "NUM BIG:     PAST 63999");
    tool_put_row(10, "LINE LONG:   PAST 79 CHARACTERS");
    tool_put_row(11, "TOO BIG:     REU FULL");
    tool_put_row(12, "OPEN FAIL:   DRIVE DID NOT OPEN");
    tool_put_row(13, "BAD NAME:    EMPTY OR , : * ? @");
    tool_put_row(14, "DISK:        SEE THE DOS TEXT");
    tool_put_row(23, "STOP EXITS THIS PAGE");
    tool_cgetc();
}

static int menu_key(void) {
    for (;;) {
        int ch = tool_cgetc();

        if (ch == 3)
            return -1;
        if (ch == '?') {
            show_help();
            draw_menu();
            continue;
        }
        if (ch == '1' || ch == '2')
            return ch - '0';
    }
}

int main(void) {
    *(unsigned char *)0xD020 = 12;
    *(unsigned char *)0xD021 = 0;
    tool_lowercase();
    w64_reu_probe();
    reu_bytes = w64_reu_bytes();
    for (;;) {
        int mode;

        verify_code = 0;
        draw_menu();
        mode = menu_key();
        if (mode < 0)
            tool_exit();
        if (!tool_reu_ok(reu_bytes)) {
            tool_put_err(12, "REU 128KB REQUIRED");
            tool_cgetc();
            continue;
        }
        if (tool_prepare(mode == 1 ? 'S' : 'P') < 0) {
            if (verify_code != 0)
                tool_another();
            continue;
        }
        {
            char buf[8];
            unsigned int step;

            if (tool_read_field(21, "STEP: ", buf, 8) < 0) {
                if (verify_code != 0)
                    tool_another();
                continue;
            }
            if (tool_parse_step(buf, &step) < 0) {
                tool_finish(CONV_NUM_BIG, "NUM BIG");
                tool_another();
                continue;
            }
            if (mode == 1)
                renumber_job(step);
            else
                renumber_prg(step);
        }
        tool_another();
    }
}
