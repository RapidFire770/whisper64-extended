#include "clipboard.h"
#include "editor_state.h"
#include "editor.h"
#include "screen.h"
#include "undo.h"

void mark_toggle() {
    if (!mark_active) {
        mark_active = 1;
        mark_start_x = cursor_x;
        mark_start_y = cursor_y;
        mark_end_x = cursor_x;
        mark_end_y = cursor_y;
        update_cursor();
        show_message("MARK ON - ARROWS, CTRL+C=COPY", COL_GREEN);
    } else {
        mark_active = 0;
        update_cursor();
        show_message("MARK OFF", COL_RED);
    }
}

/* Same bytes as strncpy of n, then a NUL at n. Keeps that helper out. */
static void copy_n(char *dst, const char *src, int n) {
    int i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            while (++i < n)
                dst[i] = '\0';
            break;
        }
    }
    dst[n] = '\0';
}

void copy_marked() {
    int i;
    int start_y = mark_start_y < mark_end_y ? mark_start_y : mark_end_y;
    int end_y = mark_start_y < mark_end_y ? mark_end_y : mark_start_y;
    
    if (!mark_active) {
        show_message("NO MARK - PRESS CTRL+K", COL_RED);
        return;
    }
    
    clipboard_lines = 0;
    
    for (i = start_y; i <= end_y && clipboard_lines < 2; i++) {
        if (start_y == end_y) {
            int start_x = mark_start_x < mark_end_x ? mark_start_x : mark_end_x;
            int end_x = mark_start_x < mark_end_x ? mark_end_x : mark_start_x;
            copy_n(clipboard[clipboard_lines], &lines[i][start_x], end_x - start_x);
        } else if (i == start_y || i == end_y) {
            int x = (i == mark_start_y) ? mark_start_x : mark_end_x;
            if (i == start_y)
                strcpy(clipboard[clipboard_lines], &lines[i][x]);
            else {
                copy_n(clipboard[clipboard_lines], lines[i], x);
            }
        } else {
            strcpy(clipboard[clipboard_lines], lines[i]);
        }
        clipboard_lines++;
    }
    
    show_message("COPIED - CTRL+V TO PASTE", COL_GREEN);
}

void paste_clipboard() {
    int i, j;
    
    if (clipboard_lines == 0) {
        show_message("CLIPBOARD EMPTY", COL_RED);
        return;
    }
    
    for (i = 0; i < clipboard_lines; i++) {
        int inserted = 0;

        for (j = 0; clipboard[i][j]; j++) {
            int column = cursor_x;

            insert_char(clipboard[i][j]);
            if (cursor_x > column)
                inserted++;
        }
        if (i < clipboard_lines - 1) {
            if (!new_line()) {
                while (inserted > 0) {
                    delete_char();
                    inserted--;
                }
                update_cursor();
                show_message("REU FULL", COL_YELLOW);
                return;
            }
            clear_undo();
        }
    }

    update_cursor();
    show_message("TEXT PASTED", COL_GREEN);
}