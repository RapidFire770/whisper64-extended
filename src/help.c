#include "help.h"
#include "screen.h"

static void help_page(int page) {
    clrscr();
    if (page == 0) {
        cputs_at(0, 0, "WHISPER64 - HELP", COL_YELLOW);
        cputs_at(0, 2, "FUNCTION KEYS:", COL_CYAN);
        cputs_at(2, 3, "F1 - LOAD FILE (DIRECTORY)", COL_WHITE);
        cputs_at(2, 4, "F2 - SAVE FILE", COL_WHITE);
        cputs_at(2, 5, "F3 - SELECT DRIVE (8-15)", COL_WHITE);
        cputs_at(2, 6, "F4 - BASIC MODE & RENUMBER", COL_WHITE);
        cputs_at(2, 7, "F5 - FIND TEXT", COL_WHITE);
        cputs_at(2, 8, "F6 - FIND & REPLACE", COL_WHITE);
        cputs_at(2, 9, "F7 - FIND NEXT", COL_WHITE);
        cputs_at(2, 10, "F8 - THIS HELP SCREEN", COL_WHITE);

        cputs_at(0, 11, "EDITING:", COL_CYAN);
        cputs_at(2, 12, "CTRL+K - TOGGLE MARK MODE", COL_WHITE);
        cputs_at(2, 13, "CTRL+C - COPY MARKED TEXT", COL_WHITE);
        cputs_at(2, 14, "CTRL+V - PASTE TEXT", COL_WHITE);
        cputs_at(2, 15, "CTRL+Z - UNDO LAST CHANGE", COL_WHITE);
        cputs_at(2, 16, "CTRL+Y - REDO LAST CHANGE", COL_WHITE);
        cputs_at(2, 17, "CTRL+G - GOTO LINE NUMBER", COL_WHITE);
        cputs_at(2, 18, "CTRL+W - NEW FILE", COL_WHITE);
        cputs_at(2, 19, "HOME - GO TO TOP", COL_WHITE);
        cputs_at(2, 20, "ARROWS - MOVE CURSOR", COL_WHITE);

        cputs_at(0, 21, "BASIC MODE (F4)", COL_CYAN);
        cputs_at(2, 22, "CTRL+D - 40/80 COLUMNS", COL_WHITE);
        cputs_at(0, 23, "<> PAGE", COL_CYAN);
        return;
    }

    cputs_at(0, 0, "WHISPER64 - HELP 2", COL_YELLOW);
    cputs_at(0, 2, "W64TOOLS", COL_CYAN);
    cputs_at(2, 3, "1 SEQ>PRG   2 PRG>SEQ", COL_WHITE);
    cputs_at(2, 4, "3 RENUMBER  4 LABELS", COL_WHITE);
    cputs_at(2, 6, "CTRL+N NEXT  CTRL+B BACK", COL_WHITE);
    cputs_at(2, 7, "CTRL+G TITLE LINE", COL_WHITE);
    cputs_at(2, 9, "F4 RENUMBERS THIS PAGE", COL_WHITE);
    cputs_at(2, 10, "BASIC LINE STOPS AT 79", COL_WHITE);
    cputs_at(0, 23, "LEFT PAGE  STOP EXITS", COL_CYAN);
}

void show_help() {
    int page = 0;

    for (;;) {
        char c;
        help_page(page);
        c = cgetc();
        if (c == KEY_RIGHT)
            page = 1;
        else if (c == KEY_LEFT)
            page = 0;
        else if (c == KEY_STOP || c == KEY_RETURN)
            break;
    }
    update_cursor();
}
