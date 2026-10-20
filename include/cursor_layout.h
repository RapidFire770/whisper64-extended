#ifndef CURSOR_LAYOUT_H
#define CURSOR_LAYOUT_H

/* edit_width is 37 or 77. Compare twice. Do not divide by width. */

static inline int text_rows_for(int len, int width) {
    int limit = width;

    if (len <= limit)
        return 1;
    limit += width;
    if (len <= limit)
        return 2;
    return 3;
}

static inline void split_col_for(int pos, int width, int *part, int *col) {
    if (pos < width) {
        *part = 0;
        *col = pos;
        return;
    }
    pos -= width;
    if (pos < width) {
        *part = 1;
        *col = pos;
        return;
    }
    *part = 2;
    *col = pos - width;
}

/* Extra screen row when the cursor sits just past a line that ended
   on a row boundary. Other lines pass on_cursor as 0. */
static inline int phantom_row_for(int len, int width, int on_cursor, int cx) {
    int part, col;

    if (!on_cursor || cx <= 0 || cx < len)
        return 0;
    split_col_for(cx, width, &part, &col);
    return col == 0;
}

static inline int scroll_rows(const int *lens, int n, int from, int to, int width) {
    int lo = from;
    int hi = to;
    int rows = 0;
    int i;

    if (hi < lo) {
        lo = to;
        hi = from;
    }
    for (i = lo; i < hi; i++) {
        int len = 0;
        if (i >= 0 && i < n)
            len = lens[i];
        rows += text_rows_for(len, width);
    }
    return rows;
}

#endif
