/* This file is compiled without LTO because inlining the title clear into
 * main makes ld.lld crash in the register allocator. */

#include "w64toolui.h"
#include <c64.h>

#define SCREEN_RAM ((unsigned char *)0x0400)

void tool_restore_title(void) {
    unsigned char name[8];
    unsigned char i;
    unsigned char x = (40 - 8) / 2;

    for (i = 0; i < 8; i++)
        name[i] = SCREEN_RAM[x + i];
    tool_clrscr();
    for (i = 0; i < 8; i++) {
        unsigned int pos = (unsigned int)x + i;

        SCREEN_RAM[pos] = name[i];
        COLOR_RAM[pos] = COL_GREEN;
    }
    tool_paint_reu();
}