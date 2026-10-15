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
    while (!(c = cbm_k_getin()));
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

static void paint_span(uint8_t *cell_color, int start, int word_len, int off) {
    int j;
    int ew = edit_width;

    for (j = 0; j < word_len; j++) {
        int src = start + j;
        if (src >= off && src < off + ew)
            paint_cell(cell_color, 3 + src - off, COL_PURPLE);
    }
}

/* off is the first buffer column drawn on this screen row. */
static void paint_line_colors(int line_num, int len, int off, uint8_t *cell_color) {
    char word[20];
    int word_len = 0;
    int keyword_start = -1;
    int i, ew;

    ew = edit_width;
    for (i = 0; i < len; i++) {
        char c = lines[line_num][i];

        if (i >= off && i < off + ew && column_marked(line_num, i, len))
            paint_cell(cell_color, 3 + i - off, COL_YELLOW);

        if (basic_mode && (isupper(c) || c == '$' || c == '%')) {
            if (word_len == 0)
                keyword_start = i;
            if (word_len < 19)
                word[word_len++] = c;
        } else if (word_len > 0) {
            word[word_len] = '\0';
            if (basic_mode && is_basic_keyword(word))
                paint_span(cell_color, keyword_start, word_len, off);
            word_len = 0;
        }
    }

    if (word_len > 0) {
        word[word_len] = '\0';
        if (basic_mode && is_basic_keyword(word))
            paint_span(cell_color, keyword_start, word_len, off);
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

/* edit_width is 37 or 77. A line is at most 79, so three rows is the cap.
   Subtract instead of dividing: the compiler's divide helpers do not fit. */
static int text_rows(int len) {
    int limit = edit_width;

    if (len <= limit)
        return 1;
    limit += edit_width;
    if (len <= limit)
        return 2;
    return 3;
}

static void split_col(int pos, int *part, int *col) {
    int w = edit_width;

    /* Two compares, not a loop. A loop here becomes a divide. */
    if (pos < w) {
        *part = 0;
        *col = pos;
        return;
    }
    pos -= w;
    if (pos < w) {
        *part = 1;
        *col = pos;
        return;
    }
    *part = 2;
    *col = pos - w;
}

/* The cursor past a full row sits on the next screen row, before the next line. */
static int phantom_row(int line) {
    int len;
    int part, col;

    if (line != cursor_y || line >= num_lines || cursor_x <= 0)
        return 0;
    len = strlen(lines[line]);
    if (cursor_x < len)
        return 0;
    split_col(cursor_x, &part, &col);
    return col == 0;
}

static void cursor_screen(int *sx, int *sy) {
    int row = 1;
    int i;
    int part, col;

    for (i = scroll_offset; i < cursor_y; i++) {
        int len = 0;
        if (i < num_lines)
            len = strlen(lines[i]);
        row += text_rows(len);
    }
    split_col(cursor_x, &part, &col);
    row += part;
    *sy = row;
    *sx = col + 3;
}

static void fit_cursor(void) {
    int sx, sy;

    if (cursor_y < scroll_offset)
        scroll_offset = cursor_y;
    for (;;) {
        cursor_screen(&sx, &sy);
        if (sy <= EDIT_HEIGHT)
            break;
        if (scroll_offset >= cursor_y)
            break;
        scroll_offset++;
    }
}

static int line_is_simple(void) {
    int i;
    int len = 0;

    if (cursor_y < num_lines)
        len = strlen(lines[cursor_y]);
    if (len >= edit_width || cursor_x >= edit_width)
        return 0;
    for (i = scroll_offset; i < cursor_y; i++) {
        if (i < num_lines && (int)strlen(lines[i]) > edit_width)
            return 0;
    }
    return 1;
}

static void clear_gutter(int screen_row) {
    cputc_at(0, screen_row, ' ', COL_CYAN);
    cputc_at(1, screen_row, ' ', COL_CYAN);
    cputc_at(2, screen_row, ' ', COL_CYAN);
}

static void paint_40_keyword(int screen_row, int line_num, int len, int off) {
    char word[20];
    int word_len = 0;
    int keyword_start = -1;
    int i, j;
    int ew = edit_width;

    if (!basic_mode)
        return;

    for (i = 0; i < len; i++) {
        char c = lines[line_num][i];

        if (isupper(c) || c == '$' || c == '%') {
            if (word_len == 0)
                keyword_start = i;
            if (word_len < 19)
                word[word_len++] = c;
        } else if (word_len > 0) {
            word[word_len] = '\0';
            if (is_basic_keyword(word)) {
                for (j = 0; j < word_len; j++) {
                    int src = keyword_start + j;
                    if (src >= off && src < off + ew) {
                        cputc_at(3 + src - off, screen_row,
                                 lines[line_num][src], COL_PURPLE);
                    }
                }
            }
            word_len = 0;
        }
    }

    if (word_len > 0) {
        word[word_len] = '\0';
        if (is_basic_keyword(word)) {
            for (j = 0; j < word_len; j++) {
                int src = keyword_start + j;
                if (src >= off && src < off + ew) {
                    cputc_at(3 + src - off, screen_row,
                             lines[line_num][src], COL_PURPLE);
                }
            }
        }
    }
}

/* show_num is the first screen row of an editor line. Later rows continue it. */
static void draw_slice(int screen_row, int line_num, int off, int show_num) {
    int ew = edit_width;
    int len = 0;
    int i;

    if (line_num < num_lines)
        len = strlen(lines[line_num]);

    if (screen_mode == MODE_80COL) {
        char rowbuf[80];
        uint8_t cell_color[40];

        for (i = 0; i < 40; i++)
            cell_color[i] = COL_WHITE;

        if (show_num && line_num < num_lines) {
            rowbuf[0] = (line_num / 10) + '0';
            rowbuf[1] = (line_num % 10) + '0';
            rowbuf[2] = ':';
        } else {
            rowbuf[0] = ' ';
            rowbuf[1] = ' ';
            rowbuf[2] = ' ';
        }
        for (i = 0; i < ew; i++) {
            int src = off + i;
            if (src < len)
                rowbuf[3 + i] = lines[line_num][src];
            else
                rowbuf[3 + i] = ' ';
        }
        if (line_num < num_lines && (basic_mode || mark_active))
            paint_line_colors(line_num, len, off, cell_color);
        if (line_num < num_lines)
            cell_color[0] = COL_CYAN;

        render_line_80(screen_row, rowbuf, 3 + ew, cell_color);
        return;
    }

    for (i = 0; i < ew; i++) {
        int src = off + i;
        unsigned char color = COL_WHITE;
        char c = ' ';

        if (src < len) {
            c = lines[line_num][src];
            if (column_marked(line_num, src, len))
                color = COL_YELLOW;
        }
        cputc_at(3 + i, screen_row, c, color);
    }
    if (line_num < num_lines)
        paint_40_keyword(screen_row, line_num, len, off);
}

void draw_text_line(int screen_row, int line_num) {
    draw_slice(screen_row, line_num, 0, 1);
}

void redraw_screen() {
    int i;
    char title[80];
    int global_line = current_page * LINES_PER_PAGE + cursor_y + 1;
    int sw = screen_width;

    /* Help and the directory leave a longer string on row 0.
       The fields below are shorter than that string. */
    for (i = 0; i < sw; i++)
        cputc_at(i, 0, ' ', COL_YELLOW);

    if (current_filename[0] != '\0') {
        sprintf(title, "%.8s", current_filename);
        cputs_at(0, 0, title, COL_YELLOW);
    } else {
        cputs_at(0, 0, "WHISPER", COL_YELLOW);
    }

    char pos_info[12];
    int n;

    /* Always 9 columns. A shorter line:col must cover the previous one. */
    sprintf(pos_info, " %d:%d", global_line, cursor_x + 1);
    n = strlen(pos_info);
    while (n < 9)
        pos_info[n++] = ' ';
    pos_info[n] = '\0';
    cputs_at(8, 0, pos_info, COL_GREEN);

    int next_x = 17;
    if (basic_mode) {
        cputs_at(next_x, 0, "[BAS]", COL_PURPLE);
        next_x += 5;
    }

    if (mark_active) {
        cputs_at(next_x, 0, "[M]", COL_GREEN);
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

    fit_cursor();
    {
        int row = 1;
        int line = scroll_offset;

        while (row <= EDIT_HEIGHT) {
            if (line < num_lines) {
                int len = strlen(lines[line]);
                int rows = text_rows(len) + phantom_row(line);
                int part;
                int off = 0;

                for (part = 0; part < rows && row <= EDIT_HEIGHT; part++) {
                    if (screen_mode != MODE_80COL) {
                        if (part == 0)
                            draw_line_number(row, line);
                        else
                            clear_gutter(row);
                    }
                    draw_slice(row, line, off, part == 0);
                    off += edit_width;
                    row++;
                }
                line++;
            } else {
                if (screen_mode != MODE_80COL)
                    draw_line_number(row, line);
                draw_slice(row, line, 0, 0);
                row++;
            }
        }
    }

    // Clear status bar
    for (i = 0; i < sw; i++) {
        cputc_at(i, 24, ' ', COL_CYAN);
    }
}

void draw_cursor() {
    int screen_y, screen_x;

    cursor_screen(&screen_x, &screen_y);

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

// Fast update: only redraws the current line + title bar position + cursor.
// A wrapped line, or a wrap above it, has to rebuild the rows under it.
void update_current_line(void) {
    int screen_y;

    if (!line_is_simple()) {
        update_cursor();
        return;
    }

    screen_y = cursor_y - scroll_offset + 1;

    if (screen_mode == MODE_80COL) screen80_begin_draw();

    // Update position in title bar
    {
        char pos_info[12];
        int global_line = current_page * LINES_PER_PAGE + cursor_y + 1;
        int n;

        sprintf(pos_info, " %d:%d", global_line, cursor_x + 1);
        n = strlen(pos_info);
        while (n < 9)
            pos_info[n++] = ' ';
        pos_info[n] = '\0';
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
    if (screen_mode == MODE_80COL) screen80_begin_draw();

    redraw_screen();
    draw_cursor();

    if (screen_mode == MODE_80COL) screen80_end_draw();
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
