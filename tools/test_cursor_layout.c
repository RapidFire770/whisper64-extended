#include "cursor_layout.h"
#include <stdio.h>

static int failed;

static void expect_int(const char *name, int got, int want) {
    if (got == want)
        return;
    printf("FAIL %s got %d want %d\n", name, got, want);
    failed = 1;
}

int main(void) {
    int lens[3];
    int part, col;

    expect_int("empty", text_rows_for(0, 37), 1);
    expect_int("exact 37", text_rows_for(37, 37), 1);
    expect_int("38", text_rows_for(38, 37), 2);
    expect_int("74", text_rows_for(74, 37), 2);
    expect_int("75", text_rows_for(75, 37), 3);
    expect_int("79", text_rows_for(79, 37), 3);
    expect_int("exact 77", text_rows_for(77, 77), 1);
    expect_int("78 at 77", text_rows_for(78, 77), 2);

    split_col_for(0, 37, &part, &col);
    expect_int("part0", part, 0);
    expect_int("col0", col, 0);
    split_col_for(37, 37, &part, &col);
    expect_int("part37", part, 1);
    expect_int("col37", col, 0);
    split_col_for(74, 37, &part, &col);
    expect_int("part74", part, 2);
    expect_int("col74", col, 0);

    expect_int("phantom off", phantom_row_for(37, 37, 0, 37), 0);
    expect_int("phantom cx0", phantom_row_for(37, 37, 1, 0), 0);
    expect_int("phantom inside", phantom_row_for(37, 37, 1, 36), 0);
    expect_int("phantom 37", phantom_row_for(37, 37, 1, 37), 1);
    expect_int("phantom short end", phantom_row_for(10, 37, 1, 10), 0);
    expect_int("phantom 74", phantom_row_for(74, 37, 1, 74), 1);
    expect_int("phantom 73", phantom_row_for(74, 37, 1, 73), 0);
    expect_int("phantom 77", phantom_row_for(77, 77, 1, 77), 1);

    lens[0] = 37;
    lens[1] = 10;
    lens[2] = 80;
    expect_int("scroll 0-2", scroll_rows(lens, 3, 0, 2, 37), 2);
    expect_int("scroll 0-3", scroll_rows(lens, 3, 0, 3, 37), 5);
    expect_int("scroll flipped", scroll_rows(lens, 3, 3, 0, 37), 5);
    expect_int("scroll same", scroll_rows(lens, 3, 1, 1, 37), 0);

    if (failed) {
        printf("FAILED\n");
        return 1;
    }
    printf("ok\n");
    return 0;
}
