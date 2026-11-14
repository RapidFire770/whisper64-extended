#include "editor.h"
#include "editor_state.h"
#include "screen.h"
#include "reu.h"
#include "editfmt.h"
#include "page_shift.h"
#include "undo.h"

static char spill_line[MAX_LINE_LENGTH];
static char spill_next[MAX_LINE_LENGTH];

void save_current_page_to_temp(void) {
    char temp_name[20];
    int i, len;
    
    if (!page_modified) {
        return;
    }
    
    if (reu_is_available()) {
        reu_save_page(current_page);
        page_modified = 0;
        return;
    }
    
    edit_set(temp_name, "@0:");
    edit_add(temp_name, TEMP_FILE);
    edit_add(temp_name, ".P");
    edit_add_dec(temp_name, (unsigned int)current_page, 0);
    edit_add(temp_name, ",S,W");
    
    cbm_k_setlfs(2, current_drive, 2);
    cbm_k_setnam(temp_name);
    
    if (cbm_k_open() == 0) {
        cbm_k_chkout(2);
        
        for (i = 0; i < num_lines; i++) {
            len = strlen(lines[i]);
            for (int j = 0; j < len; j++) {
                cbm_k_chrout(lines[i][j]);
            }
            cbm_k_chrout(13);
        }
        
        cbm_k_clrch();
        cbm_k_close(2);
        page_modified = 0;
    }
}

void load_page(int page_num) {
    char temp_name[20];
    int i, pos;
    unsigned char ch;
    unsigned char st;
    
    if (page_num == current_page) return;
    if (page_num < 0 || page_num >= num_pages) return;
    
    save_current_page_to_temp();
    
    current_page = page_num;
    memset(lines, 0, sizeof(lines));
    num_lines = 1;
    
    if (reu_is_available()) {
        int loaded = reu_load_page(page_num);
        if (loaded > 0) {
            num_lines = loaded;
            page_modified = 0;
            return;
        }
    }
    
    edit_set(temp_name, TEMP_FILE);
    edit_add(temp_name, ".P");
    edit_add_dec(temp_name, (unsigned int)page_num, 0);
    edit_add(temp_name, ",S,R");
    
    cbm_k_setlfs(2, current_drive, 2);
    cbm_k_setnam(temp_name);
    
    if (cbm_k_open() == 0) {
        i = 0;
        pos = 0;
        cbm_k_chkin(2);
        *(volatile unsigned char *)0x90 = 0;
        
        while (i < LINES_PER_PAGE) {
            ch = cbm_k_chrin();
            st = cbm_k_readst();
            if (st & 0x40) {
                if (ch != '\r' && ch != '\n') {
                    if (pos < MAX_LINE_LENGTH - 1)
                        lines[i][pos++] = ch;
                }
                lines[i][pos] = '\0';
                i++;
                pos = 0;
                break;
            }
            
            if (ch == '\r' || ch == '\n') {
                lines[i][pos] = '\0';
                i++;
                pos = 0;
            } else if (pos < MAX_LINE_LENGTH - 1) {
                lines[i][pos++] = ch;
            }
        }
        
        if (pos > 0) {
            lines[i][pos] = '\0';
            i++;
        }
        
        cbm_k_clrch();
        cbm_k_close(2);
        
        if (i > 0) {
            num_lines = i;
        }
    }
    
    if (num_lines < 1) {
        num_lines = 1;
    }
    
    page_modified = 0;
}

void check_page_boundary(void) {
    if (cursor_y >= num_lines && cursor_y >= LINES_PER_PAGE) {
        if (current_page < num_pages - 1) {
            load_page(current_page + 1);
            cursor_y = 0;
            cursor_x = 0;
            scroll_offset = 0;
        }
    } else if (cursor_y < 0 && current_page > 0) {
        load_page(current_page - 1);
        cursor_y = num_lines - 1;
        if (cursor_y < 0) cursor_y = 0;
        scroll_offset = cursor_y - EDIT_HEIGHT + 1;
        if (scroll_offset < 0) scroll_offset = 0;
    }
}

void init_editor(void) {
    clrscr();
    memset(lines, 0, sizeof(lines));
    num_lines = 1;
    total_lines = 1;
    current_page = 0;
    num_pages = 1;
    cursor_x = 0;
    cursor_y = 0;
    scroll_offset = 0;
    search_term[0] = '\0';
    replace_term[0] = '\0';
    mark_active = 0;
    clipboard_lines = 0;
    current_filename[0] = '\0';
    page_modified = 0;
    basic_mode = 0;
    
    POKE(0xD020, 0);
    POKE(0xD021, 0);
}

void insert_char(char c) {
    int len = strlen(lines[cursor_y]);
    
    if (len < MAX_LINE_LENGTH - 1) {
        memmove(&lines[cursor_y][cursor_x + 1], 
                &lines[cursor_y][cursor_x], 
                len - cursor_x + 1);
        
        lines[cursor_y][cursor_x] = c;
        cursor_x++;
        page_modified = 1;
    } else if (basic_mode) {
        show_message("BASIC LINE FULL", COL_YELLOW);
    }
}

void delete_char(void) {
    int len = strlen(lines[cursor_y]);
    
    if (cursor_x > 0) {
        memmove(&lines[cursor_y][cursor_x - 1], 
                &lines[cursor_y][cursor_x], 
                len - cursor_x + 1);
        cursor_x--;
        page_modified = 1;
    } else if (cursor_y > 0) {
        int prev_len = strlen(lines[cursor_y - 1]);
        if (prev_len + len < MAX_LINE_LENGTH) {
            strcat(lines[cursor_y - 1], lines[cursor_y]);
            
            memmove(&lines[cursor_y], &lines[cursor_y + 1], 
                    (num_lines - cursor_y - 1) * sizeof(lines[0]));
            lines[num_lines - 1][0] = '\0';
            num_lines--;
            total_lines--;
            
            cursor_y--;
            cursor_x = prev_len;
            page_modified = 1;
            
            if (scroll_offset > 0 && cursor_y < scroll_offset) {
                scroll_offset--;
            }
        }
    }
}

static void copy_tail(char *dst, const char *src) {
    int i = 0;

    while (src[i] && i < MAX_LINE_LENGTH - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int spill_blocked(void) {
    int p;

    if (!reu_is_available())
        return 0;
    for (p = current_page + 1; p < num_pages; p++) {
        int n = reu_page_lines(p);

        if (n > 0 && n < LINES_PER_PAGE)
            return 0;
    }
    return num_pages >= reu_max_page_count();
}

static void open_carried_page(const char *text) {
    memset(lines, 0, sizeof lines);
    copy_tail(lines[0], text);
    num_lines = 1;
    num_pages = current_page + 1;
    page_modified = 1;
}

static int push_spill(void) {
    int page = current_page + 1;
    int home = -1;

    for (;;) {
        if (page >= num_pages) {
            save_current_page_to_temp();
            current_page = page;
            open_carried_page(spill_line);
            save_current_page_to_temp();
            if (home < 0)
                home = page;
            return home;
        }
        load_page(page);
        if (reu_is_available() && reu_page_lines(page) == 0) {
            page++;
            continue;
        }
        if (home < 0)
            home = page;
        if (page_insert_line(lines, &num_lines, 0, spill_line, spill_next)) {
            copy_tail(spill_line, spill_next);
            page_modified = 1;
            page++;
            continue;
        }
        page_modified = 1;
        return home;
    }
}

int new_line(void) {
    int origin;
    int follow;
    int home;

    copy_tail(spill_line, &lines[cursor_y][cursor_x]);
    if (num_lines < LINES_PER_PAGE) {
        lines[cursor_y][cursor_x] = '\0';
        page_insert_line(lines, &num_lines, cursor_y + 1, spill_line, spill_next);
        total_lines++;
        cursor_y++;
        cursor_x = 0;
        page_modified = 1;
        if (cursor_y - scroll_offset >= EDIT_HEIGHT)
            scroll_offset++;
        return 1;
    }

    if (spill_blocked()) {
        show_message("REU FULL", COL_YELLOW);
        return 0;
    }

    origin = current_page;
    follow = cursor_y == num_lines - 1;
    lines[cursor_y][cursor_x] = '\0';
    if (!follow) {
        page_insert_line(lines, &num_lines, cursor_y + 1, spill_line, spill_next);
        copy_tail(spill_line, spill_next);
        cursor_y++;
        cursor_x = 0;
        if (cursor_y - scroll_offset >= EDIT_HEIGHT)
            scroll_offset++;
    }
    page_modified = 1;
    total_lines++;
    mark_active = 0;
    home = push_spill();
    clear_undo();
    if (follow) {
        if (current_page != home)
            load_page(home);
        cursor_x = 0;
        cursor_y = 0;
        scroll_offset = 0;
    } else if (current_page != origin) {
        load_page(origin);
    }
    return 1;
}