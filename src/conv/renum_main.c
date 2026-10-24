#include "w64toolui.h"
#include "w64reu.h"
#include "w64tools.h"
#include <cbm.h>

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
    title_at("W64RENUM");
    tool_paint_reu();
    tool_put_row(2, "1 RENUMBER SEQ");
    tool_put_row(3, "? HELP");
}

static void show_help(void) {
    tool_clrscr();
    tool_put_row(0, "W64RENUM HELP");
    tool_paint_reu();
    tool_put_row(2, "1. RENUMBER - RENUMBERS LINES IN SEQ");
    tool_put_row(3, "STEP EMPTY IS 10");
    tool_put_row(4, "?  HELP - SHOWS THIS HELP SCREEN");
    tool_put_row(6, "* ERRORS *");
    tool_put_row(7, "NO NUM:    LINE HAS NO NUMBER");
    tool_put_row(8, "NUM BIG:   PAST 63999");
    tool_put_row(9, "LINE LONG: PAST 79 CHARACTERS");
    tool_put_row(10, "TOO BIG:   REU FULL");
    tool_put_row(11, "OPEN FAIL: DRIVE DID NOT OPEN");
    tool_put_row(12, "BAD NAME:  EMPTY OR , : * ? @");
    tool_put_row(13, "DISK:      SEE THE DOS TEXT");
    tool_put_row(15, "STOP EXITS THIS PAGE");
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
        if (ch == '1')
            return 1;
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
        if (mode < 0) {
            verify_code = CONV_CANCEL;
            tool_put_row(12, "CANCELLED");
            tool_stay();
        }
        if (!tool_reu_ok(reu_bytes)) {
            tool_put_err(12, "REU 128KB REQUIRED");
            continue;
        }
        if (tool_prepare('S') < 0) {
            if (verify_code != 0 && !tool_another())
                return 0;
            continue;
        }
        {
            char buf[8];
            unsigned int step;

            if (tool_read_field(21, "STEP: ", buf, 8) < 0) {
                if (verify_code != 0 && !tool_another())
                    return 0;
                continue;
            }
            if (tool_parse_step(buf, &step) < 0) {
                tool_finish(CONV_NUM_BIG, "NUM BIG");
                if (!tool_another())
                    return 0;
                continue;
            }
            renumber_job(step);
        }
        if (!tool_another())
            return 0;
    }
}
