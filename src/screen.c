#include "screen.h"
#include "screen80.h"
#include "editor_state.h"

void clrscr() {
    if (screen_mode == MODE_80COL) {
        clrscr_80();
        return;
    }
    int i;
    for (i = 0; i < 1000; i++) {
        SCREEN_RAM[i] = ' ';
        COLOR_RAM[i] = COL_WHITE;
    }
}

char cgetc() {
    char c;
    while (!(c = mouse_read_key()));
    return c;
}

void cputc_at(int x, int y, char c, unsigned char color) {
    if (screen_mode == MODE_80COL) {
        cputc_at_80(x, y, c, color);
        return;
    }
    int pos = y * SCREEN_WIDTH + x;
    SCREEN_RAM[pos] = c;
    COLOR_RAM[pos] = color;
}

void cputs_at(int x, int y, const char *s, unsigned char color) {
    if (screen_mode == MODE_80COL) {
        cputs_at_80(x, y, s, color);
        return;
    }
    int pos = y * SCREEN_WIDTH + x;
    while (*s) {
        SCREEN_RAM[pos] = *s++;
        COLOR_RAM[pos] = color;
        pos++;
    }
}

int is_basic_keyword(const char *word) {
    int i;
    for (i = 0; basic_keywords[i] != NULL; i++) {
        if (strcmp(word, basic_keywords[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int column_marked(int line_num, int col, int len) {
    int start_y, end_y;

    if (!mark_active)
        return 0;

    start_y = mark_start_y < mark_end_y ? mark_start_y : mark_end_y;
    end_y = mark_start_y < mark_end_y ? mark_end_y : mark_start_y;
    if (line_num < start_y || line_num > end_y)
        return 0;

    if (start_y == end_y) {
        int start_x = mark_start_x < mark_end_x ? mark_start_x : mark_end_x;
        int end_x = mark_start_x < mark_end_x ? mark_end_x : mark_start_x;
        return col >= start_x && col < end_x;
    }
    if (line_num == start_y)
        return col >= (line_num == mark_start_y ? mark_start_x : 0);
    if (line_num == end_y)
        return col < (line_num == mark_end_y ? mark_end_x : len);
    return 1;
}

/* Two 4px glyphs share a hires cell. Purple beats yellow beats white. */
static uint8_t cell_rank(uint8_t color) {
    if (color == COL_PURPLE) return 3;
    if (color == COL_YELLOW) return 2;
    if (color == COL_WHITE) return 1;
    return 0;
}

static void paint_cell(uint8_t *cell_color, int screen_col, uint8_t color) {
    int cell;

    if (screen_col < 0 || screen_col >= 80)
        return;
    cell = screen_col >> 1;
    if (cell_rank(color) >= cell_rank(cell_color[cell]))
        cell_color[cell] = color;
}

static void paint_line_colors(int line_num, int len, uint8_t *cell_color) {
    char word[20];
    int word_len = 0;
    int keyword_start = -1;
    int i, j, n;

    n = len;
    if (n > edit_width)
        n = edit_width;

    for (i = 0; i < n; i++) {
        char c = lines[line_num][i];

        if (column_marked(line_num, i, len))
            paint_cell(cell_color, 3 + i, COL_YELLOW);

        if (basic_mode && (isupper(c) || c == '$' || c == '%')) {
            if (word_len == 0)
                keyword_start = i;
            if (word_len < 19)
                word[word_len++] = c;
        } else if (word_len > 0) {
            word[word_len] = '\0';
            if (basic_mode && is_basic_keyword(word)) {
                for (j = 0; j < word_len; j++)
                    paint_cell(cell_color, 3 + keyword_start + j, COL_PURPLE);
            }
            word_len = 0;
        }
    }

    if (word_len > 0) {
        word[word_len] = '\0';
        if (basic_mode && is_basic_keyword(word)) {
            for (j = 0; j < word_len; j++)
                paint_cell(cell_color, 3 + keyword_start + j, COL_PURPLE);
        }
    }
}

void draw_line_number(int screen_row, int line_num) {
    int pos = screen_row * SCREEN_WIDTH;

    if (line_num < num_lines) {
        SCREEN_RAM[pos] = (line_num / 10) + '0';
        SCREEN_RAM[pos + 1] = (line_num % 10) + '0';
        SCREEN_RAM[pos + 2] = ':';
        COLOR_RAM[pos] = COL_CYAN;
        COLOR_RAM[pos + 1] = COL_CYAN;
        COLOR_RAM[pos + 2] = COL_CYAN;
    } else {
        cputc_at(0, screen_row, ' ', COL_WHITE);
        cputc_at(1, screen_row, ' ', COL_WHITE);
        cputc_at(2, screen_row, ' ', COL_WHITE);
    }
}

void draw_text_line(int screen_row, int line_num) {
    int ew = edit_width;

    if (screen_mode == MODE_80COL) {
        char rowbuf[80];
        uint8_t cell_color[40];
        int i, len;

        for (i = 0; i < 40; i++)
            cell_color[i] = COL_WHITE;

        if (line_num < num_lines) {
            rowbuf[0] = (line_num / 10) + '0';
            rowbuf[1] = (line_num % 10) + '0';
            rowbuf[2] = ':';
            len = strlen(lines[line_num]);
            for (i = 0; i < ew && i < len; i++)
                rowbuf[3 + i] = lines[line_num][i];
            for (; i < ew; i++)
                rowbuf[3 + i] = ' ';
            if (basic_mode || mark_active)
                paint_line_colors(line_num, len, cell_color);
            cell_color[0] = COL_CYAN;
        } else {
            memset(rowbuf, ' ', 3 + ew);
        }

        render_line_80(screen_row, rowbuf, 3 + ew, cell_color);
        return;
    }

    // 40-col path
    {
        int i, j;
        char word[20];
        int word_len = 0;
        unsigned char color;
        int keyword_start = -1;

        if (line_num < num_lines) {
            int len = strlen(lines[line_num]);
            int is_marked;

            for (i = 0; i < ew; i++) {
                color = COL_WHITE;
                is_marked = column_marked(line_num, i, len);

                if (i < len) {
                    char c = lines[line_num][i];

                    if (basic_mode && (isupper(c) || c == '$' || c == '%')) {
                        if (word_len == 0) keyword_start = i;
                        if (word_len < 19) {
                            word[word_len++] = c;
                        }
                    } else {
                        if (word_len > 0) {
                            word[word_len] = '\0';
                            if (basic_mode && is_basic_keyword(word)) {
                                for (j = 0; j < word_len; j++) {
                                    cputc_at(3 + keyword_start + j, screen_row,
                                             lines[line_num][keyword_start + j], COL_PURPLE);
                                }
                            }
                            word_len = 0;
                        }
                    }

                    if (is_marked) {
                        color = COL_YELLOW;
                    }
                    cputc_at(3 + i, screen_row, c, color);
                } else {
                    cputc_at(3 + i, screen_row, ' ', COL_WHITE);
                }
            }

            if (word_len > 0) {
                word[word_len] = '\0';
                if (basic_mode && is_basic_keyword(word)) {
                    for (j = 0; j < word_len; j++) {
                        cputc_at(3 + keyword_start + j, screen_row,
                                 lines[line_num][keyword_start + j], COL_PURPLE);
                    }
                }
            }
        } else {
            for (i = 0; i < ew; i++) {
                cputc_at(3 + i, screen_row, ' ', COL_WHITE);
            }
        }
    }
}

void redraw_screen() {
    int i;
    char title[80];
    int global_line = current_page * LINES_PER_PAGE + cursor_y + 1;
    int sw = screen_width;

    if (current_filename[0] != '\0') {
        sprintf(title, "%.8s", current_filename);
        cputs_at(0, 0, title, COL_YELLOW);
    } else {
        cputs_at(0, 0, "WHISPER", COL_YELLOW);
    }

    char pos_info[15];
    sprintf(pos_info, " %d:%d", global_line, cursor_x + 1);
    cputs_at(8, 0, pos_info, COL_GREEN);

    int next_x = 17;
    if (basic_mode) {
        cputs_at(next_x, 0, "[BAS]", COL_PURPLE);
        next_x += 5;
    }

    if (mark_active) {
        cputs_at(next_x, 0, "[M]", COL_GREEN);
        next_x += 3;
    }

    int drive_pos = sw - 10;
    char drive_info[10];
    sprintf(drive_info, " D:%d", current_drive);
    cputs_at(drive_pos, 0, drive_info, COL_CYAN);

    int page_pos = sw - 18;
    if (num_pages > 1) {
        char page_info[10];
        sprintf(page_info, " P%d/%d", current_page + 1, num_pages);
        cputs_at(page_pos, 0, page_info, COL_CYAN);
    }

    if (screen_mode == MODE_80COL) {
        cputs_at(sw - 4, 0, "80C", COL_GREEN);
    }

    // Clear gap in title bar
    for (i = next_x; i < page_pos; i++) {
        cputc_at(i, 0, ' ', COL_YELLOW);
    }

    for (i = 0; i < EDIT_HEIGHT; i++) {
        // In 80-col mode, draw_text_line includes line numbers
        if (screen_mode != MODE_80COL) {
            draw_line_number(i + 1, scroll_offset + i);
        }
        draw_text_line(i + 1, scroll_offset + i);
    }

    // Clear status bar
    for (i = 0; i < sw; i++) {
        cputc_at(i, 24, ' ', COL_CYAN);
    }
}

void draw_cursor() {
    int screen_y = cursor_y - scroll_offset + 1;
    int screen_x = cursor_x + 3;

    if (screen_y >= 1 && screen_y <= EDIT_HEIGHT) {
        if (screen_mode == MODE_80COL) {
            // In bitmap mode, draw cursor by inverting the character cell
            // Use reverse-video effect: redraw char with inverted colors
            int len = strlen(lines[cursor_y]);
            char c = (cursor_x < len) ? lines[cursor_y][cursor_x] : ' ';
            // Draw with black-on-white (inverted) by using a special approach
            // In bitmap mode: XOR the bitmap bytes for this character position
            uint8_t *bmp;
            int cell_col = screen_x >> 1;
            int side = screen_x & 1;
            uint8_t xor_mask = (side == 0) ? 0xF0 : 0x0F;

            bmp = BITMAP_BASE + (uint16_t)screen_y * 320 + (uint16_t)cell_col * 8;

            // Uses outer begin_draw from update_cursor (draw_depth > 0)
            bmp[0] ^= xor_mask;
            bmp[1] ^= xor_mask;
            bmp[2] ^= xor_mask;
            bmp[3] ^= xor_mask;
            bmp[4] ^= xor_mask;
            bmp[5] ^= xor_mask;
            bmp[6] ^= xor_mask;
            bmp[7] ^= xor_mask;
        } else {
            int pos = screen_y * SCREEN_WIDTH + screen_x;
            int len = strlen(lines[cursor_y]);

            if (cursor_x < len) {
                SCREEN_RAM[pos] = lines[cursor_y][cursor_x] + 128;
                COLOR_RAM[pos] = COL_WHITE;
            } else {
                SCREEN_RAM[pos] = CURSOR_CHAR;
                COLOR_RAM[pos] = COL_WHITE;
            }
        }
    }
}

// Fast update: only redraws the current line + title bar position + cursor
// Use for typing operations (insert/delete char) instead of full redraw
void update_current_line(void) {
    int screen_y = cursor_y - scroll_offset + 1;

    if (screen_mode == MODE_80COL) screen80_begin_draw();

    // Update position in title bar
    {
        char pos_info[15];
        int global_line = current_page * LINES_PER_PAGE + cursor_y + 1;
        sprintf(pos_info, " %d:%d  ", global_line, cursor_x + 1);
        cputs_at(8, 0, pos_info, COL_GREEN);
    }

    // Redraw only the current line
    if (screen_y >= 1 && screen_y <= EDIT_HEIGHT) {
        if (screen_mode != MODE_80COL) {
            draw_line_number(screen_y, cursor_y);
        }
        draw_text_line(screen_y, cursor_y);
    }

    draw_cursor();

    if (screen_mode == MODE_80COL) screen80_end_draw();
}

void update_cursor() {
    if (mouse_is_enabled()) {
        mouse_hide_cursor();
    }

    // Batch all bitmap + color writes under one ROM banking operation
    if (screen_mode == MODE_80COL) screen80_begin_draw();

    redraw_screen();
    draw_cursor();

    if (screen_mode == MODE_80COL) screen80_end_draw();

    if (mouse_is_enabled()) {
        mouse_draw_cursor();
    }
}

void show_message(const char *msg, unsigned char col) {
    int i;
    int sw = screen_width;

    if (screen_mode == MODE_80COL) {
        screen80_begin_draw();
        for (i = 0; i < sw; i++) {
            cputc_at_80(i, 24, ' ', col);
        }
        cputs_at_80(0, 24, msg, col);
        if (basic_mode) {
            cputs_at_80(sw - 8, 24, "[BASIC]", COL_PURPLE);
        }
        screen80_end_draw();
    } else {
        for (i = 0; i < sw; i++) {
            cputc_at(i, 24, ' ', col);
        }
        cputs_at(0, 24, msg, col);
        if (basic_mode) {
            cputs_at(sw - 8, 24, "[BASIC]", COL_PURPLE);
        }
    }
}
