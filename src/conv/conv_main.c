#include "w64toolui.h"
#include "w64reu.h"
#include "w64tools.h"
#include <cbm.h>
#include <c64.h>

static int stash_word(unsigned int w) {
    if (tool_stash_byte((unsigned char)w) < 0)
        return -1;
    return tool_stash_byte((unsigned char)(w >> 8));
}

static int seq_store(unsigned int *wrote_out) {
    char spec[28];
    char line[80];
    unsigned char eof = 0;
    unsigned int addr = 0x0801;
    unsigned int line_no;
    unsigned int seen = 0;
    unsigned int wrote = 0;
    int rc;
    int err;
    int n;

    tool_file_spec(spec, 0, in_name, ",S,R");
    if (tool_open_ch(2, read_drive, spec) != 0) {
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);
    tool_stash_reset();
    if (stash_word(0x0801) < 0)
        return -1;
    for (;;) {
        cbm_k_chkin(2);
        rc = tool_read_seq_line(&eof, line, &err);
        if (rc < 0) {
            if (err == -2) {
                char msg[10];
                tool_set_str(msg, "READ ");
                tool_add_hex2(msg, last_st);
                tool_finish(CONV_DISK, msg);
            } else
                tool_finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        if (rc > 0)
            break;
        if (seen < 65535u)
            seen++;
        tool_note_progress(10, "PRG ", seen);
        cbm_k_chkin(2);
        n = conv_tokenize(line, &line_no, tok, 79);
        if (n == -1)
            continue;
        if (n == -2) {
            tool_finish(CONV_NO_NUM, "NO NUM");
            return -1;
        }
        if (n == -3) {
            tool_finish(CONV_NUM_BIG, "NUM BIG");
            return -1;
        }
        if (n < 0) {
            tool_finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        {
            unsigned int next = addr + 4u + (unsigned int)n + 1u;
            int j;
            if (next < addr) {
                tool_finish(CONV_TOO_BIG, "PRG TOO BIG");
                return -1;
            }
            if (stash_word(next) < 0 || stash_word(line_no) < 0)
                return -1;
            for (j = 0; j < n; j++) {
                if (tool_stash_byte(tok[j]) < 0)
                    return -1;
            }
            if (tool_stash_byte(0) < 0)
                return -1;
            addr = next;
            wrote++;
        }
    }
    if (stash_word(0) < 0)
        return -1;
    tool_stash_flush();
    tool_close_ch(2);
    *wrote_out = wrote;
    return 0;
}

static int prg_store(unsigned int *wrote_out) {
    char spec[28];
    unsigned char eof = 0;
    unsigned int addr = 0x0801;
    unsigned int link;
    unsigned int line_no;
    int lo, hi;
    unsigned int wrote = 0;
    int trunc = 0;
    int ml = 0;
    int first = 1;

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
                if (cut || cut_stmt)
                    trunc++;
                if (!first && tool_stash_byte(13) < 0)
                    return -1;
                for (j = 0; j < len; j++) {
                    if (tool_stash_byte((unsigned char)text_line[j]) < 0)
                        return -1;
                }
                first = 0;
            }
        }
        addr = link;
        wrote++;
        tool_note_progress(10, "SEQ ", wrote);
        cbm_k_chkin(2);
    }

    ml = tool_trailing_ml(&eof);
    tool_stash_flush();
    tool_close_ch(2);
    {
        char msg[32];
        tool_set_str(msg, "SEQ ");
        tool_add_dec(msg, wrote);
        if (trunc || ml) {
            tool_add_str(msg, " TRUNC ");
            tool_add_dec(msg, (unsigned int)trunc);
            tool_add_str(msg, " ML ");
            tool_add_dec(msg, (unsigned int)ml);
        }
        tool_put_row(12, msg);
    }
    *wrote_out = wrote;
    return 0;
}

static void seq_to_prg(void) {
    char probe[28];
    char spec[28];
    unsigned int wrote = 0;
    int replace;

    tool_file_spec(probe, 0, out_name, ",P,R");
    replace = tool_confirm_overwrite(probe);
    if (!replace)
        return;
    if (seq_store(&wrote) < 0)
        return;
    tool_file_spec(spec, replace, out_name, ",P,W");
    if (tool_flush_at(spec, 0, tool_stored()) < 0)
        return;
    if (!tool_drive_status_ok())
        return;
    if (wrote == 0)
        tool_put_row(12, "EMPTY PRG");
    else {
        char msg[20];
        tool_set_str(msg, "WROTE ");
        tool_add_dec(msg, wrote);
        tool_put_row(12, msg);
    }
    tool_file_spec(probe, 0, out_name, ",P,R");
    tool_remember_output(probe);
}

static void prg_to_seq(void) {
    char probe[28];
    char spec[28];
    unsigned int wrote = 0;
    int replace;

    tool_file_spec(probe, 0, out_name, ",S,R");
    replace = tool_confirm_overwrite(probe);
    if (!replace)
        return;
    if (prg_store(&wrote) < 0)
        return;
    tool_file_spec(spec, replace, out_name, ",S,W");
    if (tool_flush_at(spec, 0, tool_stored()) < 0)
        return;
    if (!tool_drive_status_ok())
        return;
    tool_file_spec(probe, 0, out_name, ",S,R");
    tool_remember_output(probe);
}

static void title_at(const char *s) {
    unsigned char x = (40 - 8) / 2;
    unsigned char *screen = (unsigned char *)0x0400;
    unsigned char *color = (unsigned char *)0xD800;
    unsigned int pos = x;

    while (*s && x < 39) {
        screen[pos] = (unsigned char)*s++;
        color[pos] = COL_GREEN;
        pos++;
        x++;
    }
}

static void draw_menu(void) {
    tool_clrscr();
    title_at("W64CONV");
    tool_paint_reu();
    tool_put_row(2, "1 SEQ TO PRG");
    tool_put_row(3, "2 PRG TO SEQ");
    tool_put_row(4, "? HELP");
}

static void show_help(void) {
    tool_clrscr();
    tool_put_row(0, "W64CONV HELP");
    tool_paint_reu();
    tool_put_row(2, "1. SEQ > PRG - CONVERTS SEQ TO PRG");
    tool_put_row(3, "2. PRG > SEQ - CONVERTS PRG TO SEQ");
    tool_put_row(4, "?  HELP - SHOWS THIS HELP SCREEN");
    tool_put_row(6, "* ERRORS *");
    tool_put_row(7, "NO NUM:    LINE HAS NO NUMBER");
    tool_put_row(8, "NUM BIG:   PAST 63999");
    tool_put_row(9, "LINE LONG: PAST 79 CHARACTERS");
    tool_put_row(10, "TOO BIG:   REU FULL");
    tool_put_row(11, "PRG TOO BIG: 16-BIT LINK");
    tool_put_row(12, "OPEN FAIL: DRIVE DID NOT OPEN");
    tool_put_row(13, "BAD NAME:  EMPTY OR , : * ? @");
    tool_put_row(14, "DISK:      SEE THE DOS TEXT");
    tool_put_row(16, "STOP EXITS THIS PAGE");
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
    w64_reu_probe();
    reu_bytes = w64_reu_bytes();
    for (;;) {
        int mode;

        verify_code = 0;
        draw_menu();
        mode = menu_key();
        if (mode < 0) {
            verify_code = CONV_CANCEL;
            tool_put_row(12, "CANCELLED");
            tool_stay();
        }
        if (!tool_reu_ok(reu_bytes)) {
            tool_put_err(12, "REU 128KB REQUIRED");
            continue;
        }
        if (tool_prepare(mode == 2 ? 'P' : 'S') < 0) {
            if (verify_code != 0 && !tool_another())
                return 0;
            continue;
        }
        if (mode == 1)
            seq_to_prg();
        else
            prg_to_seq();
        if (!tool_another())
            return 0;
    }
}
