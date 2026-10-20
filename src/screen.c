#include "screen.h"
#include "screen80.h"
#include "editor_state.h"
#include "cursor_layout.h"

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

/* Lowercase charset (CRT CHROUT $0E). A-Z already match $41-$5A.
   @ [ ] a-z and the bar sit elsewhere. { } \ ^ _ ` ~ are not in
   the ROM, so those bytes stay distinct from the brackets. */
static unsigned char to_screen(unsigned char c) {
    if (c == 0x40) return 0x00;
    if (c == 0x5B) return 0x1B;
    if (c == 0x5D) return 0x1D;
    if (c == 0x7C) return 0x5D;
    if (c >= 0x61 && c <= 0x7A) return (unsigned char)(c - 0x60);
    return c;
}

void cputc_at(int x, int y, char c, unsigned char color) {
    if (screen_mode == MODE_80COL) {
        cputc_at_80(x, y, c, color);
        return;
    }
    int pos = y * SCREEN_WIDTH + x;
    SCREEN_RAM[pos] = to_screen((unsigned char)c);
    COLOR_RAM[pos] = color;
}

void cputs_at(int x, int y, const char *s, unsigned char color) {
    if (screen_mode == MODE_80COL) {
        cputs_at_80(x, y, s, color);
        return;
    }
    int pos = y * SCREEN_WIDTH + x;
    while (*s) {
        SCREEN_RAM[pos] = to_screen((unsigned char)*s++);
        COLOR_RAM[pos] = color;
        pos++;
    }
}

unsigned char is_basic_keyword(const char *word) {
    int i;
    for (i = 0; basic_keywords[i] != NULL; i++) {
        if (strcmp(word, basic_keywords[i]) == 0)
            return basic_colors[i];
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

/* Two 4px glyphs share a hires cell. Keyword colors beat yellow, which beats white. */
static uint8_t cell_rank(uint8_t color) {
    if (color == COL_PURPLE || color == COL_BLUE) return 3;
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

static void flush_word(const char *word, int word_len, int start,
                       unsigned char *color, int *rem) {
    unsigned char col;
    int j;
    char tmp[16];

    if (word_len <= 0 || word_len > 15)
        return;
    memcpy(tmp, word, word_len);
    tmp[word_len] = '\0';
    col = is_basic_keyword(tmp);
    if (!col)
        return;
    for (j = 0; j < word_len; j++)
        color[start + j] = col;
    if (strcmp(tmp, "REM") == 0)
        *rem = 1;
}

/* color[i] is 0, or the keyword color. Quotes and a finished REM stay 0. */
static void scan_keywords(const char *line, int len, unsigned char *color) {
    char word[16];
    int word_len = 0;
    int start = 0;
    int quote = 0;
    int rem = 0;
    int i;

    for (i = 0; i < len; i++)
        color[i] = 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)line[i];

        if (rem)
            break;
        if (c == '"') {
            flush_word(word, word_len, start, color, &rem);
            word_len = 0;
            quote = !quote;
            continue;
        }
        if (quote)
            continue;
        if (c == '(') {
            int tab = word_len == 3 && word[0] == 'T' && word[1] == 'A' && word[2] == 'B';
            int spc = word_len == 3 && word[0] == 'S' && word[1] == 'P' && word[2] == 'C';
            if (tab || spc) {
                word[word_len++] = '(';
                flush_word(word, word_len, start, color, &rem);
            } else if (word_len > 0) {
                flush_word(word, word_len, start, color, &rem);
            }
            word_len = 0;
            continue;
        }
        if (isupper(c) || c == '$' || c == '%' || c == '#') {
            if (word_len == 0)
                start = i;
            if (word_len < 15)
                word[word_len++] = c;
            else
                word_len = 16;
        } else if (word_len > 0) {
            flush_word(word, word_len, start, color, &rem);
            word_len = 0;
        }
    }
    if (!rem)
        flush_word(word, word_len, start, color, &rem);
}

/* off is the first buffer column drawn on this screen row. */
static void paint_line_colors(int line_num, int len, int off, uint8_t *cell_color) {
    unsigned char color[MAX_LINE_LENGTH];
    int i;
    int ew = edit_width;

    for (i = 0; i < len; i++) {
        if (i >= off && i < off + ew && column_marked(line_num, i, len))
            paint_cell(cell_color, 3 + i - off, COL_YELLOW);
    }
    if (!basic_mode)
        return;
    scan_keywords(lines[line_num], len, color);
    for (i = 0; i < len; i++) {
        if (color[i] && i >= off && i < off + ew)
            paint_cell(cell_color, 3 + i - off, color[i]);
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

static int text_rows(int len) {
    return text_rows_for(len, edit_width);
}

static void split_col(int pos, int *part, int *col) {
    split_col_for(pos, edit_width, part, col);
}

static int phantom_row(int line) {
    int len = 0;

    if (line < num_lines)
        len = (int)strlen(lines[line]);
    return phantom_row_for(len, edit_width,
                           line == cursor_y && line < num_lines, cursor_x);
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
    unsigned char color[MAX_LINE_LENGTH];
    int i;
    int ew = edit_width;

    if (!basic_mode)
        return;
    scan_keywords(lines[line_num], len, color);
    for (i = 0; i < len; i++) {
        if (color[i] && i >= off && i < off + ew)
            cputc_at(3 + i - off, screen_row, lines[line_num][i], color[i]);
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

static void draw_title_pos(void) {
    char pos_info[12];
    int global_line = current_page * LINES_PER_PAGE + cursor_y + 1;
    int n;

    sprintf(pos_info, " %d:%d", global_line, cursor_x + 1);
    n = (int)strlen(pos_info);
    while (n < 9)
        pos_info[n++] = ' ';
    pos_info[n] = '\0';
    cputs_at(8, 0, pos_info, COL_GREEN);
}

static unsigned char cursor_drawn;
static unsigned char cursor_color;
static unsigned char cursor_sx, cursor_sy;

static void draw_editor_band(int first, int last) {
    int row = 1;
    int line = scroll_offset;

    while (row <= EDIT_HEIGHT) {
        if (line < num_lines) {
            int len = (int)strlen(lines[line]);
            int rows = text_rows(len) + phantom_row(line);
            int part;
            int off = 0;

            for (part = 0; part < rows && row <= EDIT_HEIGHT; part++) {
                if (row >= first && row <= last) {
                    if (screen_mode != MODE_80COL) {
                        if (part == 0)
                            draw_line_number(row, line);
                        else
                            clear_gutter(row);
                    }
                    draw_slice(row, line, off, part == 0);
                }
                off += edit_width;
                row++;
            }
            line++;
        } else {
            if (row >= first && row <= last) {
                if (screen_mode != MODE_80COL)
                    draw_line_number(row, line);
                draw_slice(row, line, 0, 0);
            }
            row++;
            line++;
        }
    }
}

void redraw_screen() {
    int i;
    char title[80];
    int sw = screen_width;

    cursor_drawn = 0;

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

    draw_title_pos();

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
    draw_editor_band(1, EDIT_HEIGHT);

    // Clear status bar
    for (i = 0; i < sw; i++) {
        cputc_at(i, 24, ' ', COL_CYAN);
    }
}

static void xor_cursor_cell(unsigned char sx, unsigned char sy) {
    uint8_t *bmp;
    int cell_col = sx >> 1;
    int side = sx & 1;
    uint8_t xor_mask = (side == 0) ? 0xF0 : 0x0F;

    bmp = BITMAP_BASE + (uint16_t)sy * 320 + (uint16_t)cell_col * 8;
    bmp[0] ^= xor_mask;
    bmp[1] ^= xor_mask;
    bmp[2] ^= xor_mask;
    bmp[3] ^= xor_mask;
    bmp[4] ^= xor_mask;
    bmp[5] ^= xor_mask;
    bmp[6] ^= xor_mask;
    bmp[7] ^= xor_mask;
}

static void erase_cursor(void) {
    int pos;

    if (!cursor_drawn)
        return;
    if (screen_mode == MODE_80COL) {
        xor_cursor_cell(cursor_sx, cursor_sy);
    } else {
        pos = cursor_sy * SCREEN_WIDTH + cursor_sx;
        SCREEN_RAM[pos] &= 0x7F;
        COLOR_RAM[pos] = cursor_color;
    }
    cursor_drawn = 0;
}

void draw_cursor() {
    int screen_y, screen_x;

    cursor_screen(&screen_x, &screen_y);

    if (screen_y < 1 || screen_y > EDIT_HEIGHT) {
        cursor_drawn = 0;
        return;
    }

    cursor_sx = (unsigned char)screen_x;
    cursor_sy = (unsigned char)screen_y;
    cursor_drawn = 1;

    if (screen_mode == MODE_80COL) {
        xor_cursor_cell(cursor_sx, cursor_sy);
    } else {
        int pos = screen_y * SCREEN_WIDTH + screen_x;
        int len = strlen(lines[cursor_y]);

        cursor_color = COLOR_RAM[pos];
        if (cursor_x < len) {
            SCREEN_RAM[pos] = to_screen((unsigned char)lines[cursor_y][cursor_x]) | 0x80;
            COLOR_RAM[pos] = COL_WHITE;
        } else {
            SCREEN_RAM[pos] = CURSOR_CHAR;
            COLOR_RAM[pos] = COL_WHITE;
        }
    }
}

static int layout_changed(int old_x, int old_y) {
    int old_len = 0;
    int new_len = 0;
    int w = edit_width;
    int before, after;

    if (old_y < num_lines)
        old_len = (int)strlen(lines[old_y]);
    before = phantom_row_for(old_len, w, old_y < num_lines, old_x);
    after = phantom_row_for(old_len, w, old_y == cursor_y && old_y < num_lines,
                            cursor_x);
    if (before != after)
        return 1;
    if (old_y == cursor_y)
        return 0;
    if (cursor_y < num_lines)
        new_len = (int)strlen(lines[cursor_y]);
    return phantom_row_for(new_len, w, cursor_y < num_lines, cursor_x) != 0;
}

static void paint_cursor_only(void) {
    if (screen_mode == MODE_80COL)
        screen80_begin_draw();
    erase_cursor();
    draw_title_pos();
    draw_cursor();
    if (screen_mode == MODE_80COL)
        screen80_end_draw();
}

void refresh_move(int old_x, int old_y, int old_scroll) {
    if (!mark_active && old_x == cursor_x && old_y == cursor_y
        && old_scroll == scroll_offset)
        return;
    if (mark_active) {
        update_cursor();
        return;
    }
    fit_cursor();
    if (layout_changed(old_x, old_y) || scroll_offset != old_scroll) {
        update_cursor();
        return;
    }
    paint_cursor_only();
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

    draw_title_pos();

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
