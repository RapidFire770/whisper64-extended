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

static void label_job(void) {
    char probe[28];
    char spec[28];
    int replace;
    int err = 0;
    int row = 0;
    int wrote = 0;
    unsigned long out_at = 0;
    unsigned long out_len = 0;
    unsigned long ceiling = 0;
    unsigned long prg_at = 0;
    unsigned long prg_len = 0;

    tool_file_spec(probe, 0, out_name, ",P,R");
    replace = tool_confirm_overwrite(probe);
    if (!replace)
        return;
    if (read_seq_reu() < 0)
        return;
    if (labels_to_numbered_store(tool_stored(), reu_bytes, 10, &out_at, &out_len,
                                 &err, &row, &ceiling)) {
        tool_show_err(err, row);
        return;
    }
    if (tokenize_store(out_at, out_len, ceiling, &prg_at, &prg_len,
                       &wrote, &err, &row)) {
        tool_show_err(err, row);
        return;
    }
    tool_file_spec(spec, replace, out_name, ",P,W");
    if (tool_flush_at(spec, prg_at, prg_len) < 0)
        return;
    if (!tool_drive_status_ok())
        return;
    if (wrote == 0)
        tool_put_row(12, "EMPTY PRG");
    else {
        char msg[20];
        tool_set_str(msg, "WROTE ");
        tool_add_dec(msg, (unsigned int)wrote);
        tool_put_row(12, msg);
    }
    tool_file_spec(probe, 0, out_name, ",P,R");
    tool_remember_output(probe);
}

static void draw_menu(void) {
    tool_clrscr();
    tool_center_row(0, "W64LABEL");
    tool_paint_reu();
    tool_put_row(3, "1 LABELS TO PRG");
    tool_put_row(4, "? HELP");
}

static void show_help(void) {
    tool_clrscr();
    tool_center_row(0, "W64LABEL HELP");
    tool_paint_reu();
    tool_put_row(3, "1. LABELS - LABELED LINE SEQ TO PRG");
    tool_put_row(4, "STEP IS 10");
    tool_put_row(5, "?  HELP - SHOWS THIS HELP SCREEN");
    tool_put_row(7, "* ERRORS *");
    tool_put_row(8, "NO NUM:      LINE HAS NO NUMBER");
    tool_put_row(9, "NUM BIG:     PAST 63999");
    tool_put_row(10, "LINE LONG:   PAST 79 CHARACTERS");
    tool_put_row(11, "TOO BIG:     REU FULL");
    tool_put_row(12, "PRG TOO BIG:   16-BIT LINK");
    tool_put_row(13, "NO LABEL:    GOTO NAME MISSING");
    tool_put_row(14, "DUP LABEL:   SAME NAME TWICE");
    tool_put_row(15, "BAD NAME:    EMPTY OR , : * ? @");
    tool_put_row(16, "OPEN FAIL:   DRIVE DID NOT OPEN");
    tool_put_row(17, "DISK:        SEE THE DOS TEXT");
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
            tool_cgetc();
            continue;
        }
        if (tool_prepare('S') < 0) {
            if (verify_code != 0 && !tool_another())
                return 0;
            continue;
        }
        label_job();
        if (!tool_another())
            return 0;
    }
}
