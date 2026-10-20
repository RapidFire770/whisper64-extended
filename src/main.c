/*
 * Whisper64 for Commodore 64
 * 
 * KEYS: F1=LOAD F2=SAVE F3=DRIVE F5=FIND F6=REPLACE F7=NEXT F8=HELP
 *       CTRL+K=MARK CTRL+C=COPY CTRL+V=PASTE
 * 
 * Compile with LLVM-MOS: cmake --build build
 */

#include "whisper64.h"
#include "editor_state.h"
#include "editor.h"
#include "screen.h"
#include "file_ops.h"
#include "search.h"
#include "clipboard.h"
#include "help.h"
#include "undo.h"
#include "goto.h"
#include "reu.h"
#include "screen80.h"

int main(void) {
    char c;

    init_editor();
    reu_init();
    screen80_init();  // Generate 4x8 font from ROM (always, cheap to do)
    update_cursor();

    if (reu_is_available()) {
        char msg[40];
        sprintf(msg, "REU: %luKB  MAX %d PAGES", reu_get_size() / 1024, reu_max_page_count());
        show_message(msg, COL_GREEN);
        cgetc();
    }
    
    show_message("F1=LOAD F2=SAVE F4=BASIC CTRL+D=80COL", COL_CYAN);
    
    while (1) {
        c = cbm_k_getin();
        if (c == 0) continue;
        
        // Toggle 80-column mode with Ctrl+D (4)
        if (c == 4) {
            if (screen_mode == MODE_80COL) {
                screen80_disable();
                edit_width = EDIT_WIDTH;
                screen_width = SCREEN_WIDTH;
                clrscr();
                update_cursor();
                show_message("40-COLUMN MODE", COL_GREEN);
            } else {
                screen80_enable();
                edit_width = EDIT_WIDTH_80;
                screen_width = SCREEN_WIDTH_80;
                update_cursor();
                show_message("80-COL MODE - CTRL+D TO TOGGLE", COL_GREEN);
            }
            continue;
        }

        // Function keys
        if (c == KEY_F1) {
            show_directory();
        } else if (c == KEY_F2) {
            save_file();
            update_cursor();
        } else if (c == KEY_F3) {
            select_drive();
        } else if (c == KEY_F4) {
            if (!basic_mode) {
                basic_mode = 1;
                update_cursor();
                show_message("BASIC MODE ON", COL_GREEN);
            } else {
                basic_mode = 0;
                update_cursor();
                show_message("BASIC MODE OFF", COL_RED);
            }
        } else if (c == KEY_F5) {
            search_text();
        } else if (c == KEY_F6) {
            find_and_replace();
        } else if (c == KEY_F7) {
            if (search_term[0]) {
                search_next();
            } else {
                show_message("NO SEARCH - USE F5 FIRST", COL_RED);
            }
        } else if (c == KEY_F8) {
            show_help();
        }
        // Control key combinations
        else if (c == 11) {  // Control+K for mark
            mark_toggle();
        } else if (c == 3 && cursor_y < num_lines) {  // Control+C for copy
            copy_marked();
        } else if (c == 22 && cursor_y < num_lines) {  // Control+V for paste
            paste_clipboard();
        } else if (c == 26) {  // Control+Z for undo
            undo_last_action();
        } else if (c == 25) {  // Control+Y for redo
            redo_last_action();
        } else if (c == 7) {  // Control+G for goto line
            goto_line();
        } else if (c == KEY_CTRL_N) {
            if (current_page < num_pages - 1) {
                load_page(current_page + 1);
                cursor_y = 0;
                cursor_x = 0;
                scroll_offset = 0;
                update_cursor();
            }
        } else if (c == KEY_CTRL_B) {
            if (current_page > 0) {
                load_page(current_page - 1);
                cursor_y = 0;
                cursor_x = 0;
                scroll_offset = 0;
                update_cursor();
            }
        } else if (c == 23) {  // Control+W new file
            new_file();
        }
        // Navigation and editing
        else if (c == KEY_RETURN) {
            save_undo_state();
            new_line();
            update_cursor();
        } else if (c == KEY_DELETE) {
            int at_break = (cursor_x == 0 && cursor_y > 0);
            int wrapped = 0;

            if (cursor_y < num_lines) {
                int len = strlen(lines[cursor_y]);
                /* A full row, or the blank row past it, has to be rebuilt. */
                wrapped = (len >= edit_width || cursor_x >= edit_width);
            }
            save_undo_state();
            delete_char();
            if (at_break || wrapped)
                update_cursor();
            else
                update_current_line();
        } else if (c == KEY_LEFT) {
            int old_x = cursor_x;
            int old_y = cursor_y;
            int old_scroll = scroll_offset;

            if (cursor_x > 0) {
                cursor_x--;
            } else if (cursor_y > 0) {
                cursor_y--;
                cursor_x = strlen(lines[cursor_y]);
                if (cursor_y < scroll_offset) {
                    scroll_offset--;
                }
            }
            if (mark_active) {
                mark_end_x = cursor_x;
                mark_end_y = cursor_y;
            }
            refresh_move(old_x, old_y, old_scroll);
        } else if (c == KEY_RIGHT) {
            int old_x = cursor_x;
            int old_y = cursor_y;
            int old_scroll = scroll_offset;

            if (cursor_x < strlen(lines[cursor_y])) {
                cursor_x++;
            } else if (cursor_y < num_lines - 1) {
                cursor_y++;
                cursor_x = 0;
                if (cursor_y - scroll_offset >= EDIT_HEIGHT) {
                    scroll_offset++;
                }
            }
            if (mark_active) {
                mark_end_x = cursor_x;
                mark_end_y = cursor_y;
            }
            refresh_move(old_x, old_y, old_scroll);
        } else if (c == KEY_UP) {
            int old_x = cursor_x;
            int old_y = cursor_y;
            int old_scroll = scroll_offset;
            int page = current_page;

            if (cursor_y > 0) {
                cursor_y--;
                if (cursor_x > strlen(lines[cursor_y])) {
                    cursor_x = strlen(lines[cursor_y]);
                }
                if (cursor_y < scroll_offset) {
                    scroll_offset--;
                }
            } else if (current_page > 0) {
                // Move to previous page
                load_page(current_page - 1);
                cursor_y = num_lines - 1;
                scroll_offset = cursor_y - EDIT_HEIGHT + 1;
                if (scroll_offset < 0) scroll_offset = 0;
                if (cursor_x > strlen(lines[cursor_y])) {
                    cursor_x = strlen(lines[cursor_y]);
                }
            }
            if (mark_active) {
                mark_end_x = cursor_x;
                mark_end_y = cursor_y;
            }
            if (current_page != page)
                update_cursor();
            else
                refresh_move(old_x, old_y, old_scroll);
        } else if (c == KEY_DOWN) {
            int old_x = cursor_x;
            int old_y = cursor_y;
            int old_scroll = scroll_offset;
            int page = current_page;

            if (cursor_y < num_lines - 1) {
                cursor_y++;
                if (cursor_x > strlen(lines[cursor_y])) {
                    cursor_x = strlen(lines[cursor_y]);
                }
                if (cursor_y - scroll_offset >= EDIT_HEIGHT) {
                    scroll_offset++;
                }
            } else if (current_page < num_pages - 1) {
                // Move to next page
                load_page(current_page + 1);
                cursor_y = 0;
                cursor_x = 0;
                scroll_offset = 0;
            }
            if (mark_active) {
                mark_end_x = cursor_x;
                mark_end_y = cursor_y;
            }
            if (current_page != page)
                update_cursor();
            else
                refresh_move(old_x, old_y, old_scroll);
        } else if (c == KEY_HOME) {
            // Go to absolute start (page 0, line 0)
            if (current_page != 0) {
                load_page(0);
            }
            cursor_x = 0;
            cursor_y = 0;
            scroll_offset = 0;
            update_cursor();
        }
        else if (c >= 32 && c < 128) {
            int before = cursor_x;

            /* A full line is not stored. Redrawing it would wipe the
               BASIC LINE FULL message: that line is wrapped, so the
               redraw clears the status row. */
            if ((int)strlen(lines[cursor_y]) < MAX_LINE_LENGTH - 1)
                save_undo_state();
            insert_char(c);
            if (cursor_x != before)
                update_current_line();
        }
    }
    
    return 0;
}