#include "page_shift.h"
#include <string.h>

static void copy_text(char *dst, const char *text) {
    int i = 0;

    while (text[i] && i < MAX_LINE_LENGTH - 1) {
        dst[i] = text[i];
        i++;
    }
    dst[i] = '\0';
}

int page_insert_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, const char *text, char *displaced) {
    if (*count < LINES_PER_PAGE) {
        memmove(page[row + 1], page[row],
                (size_t)(*count - row) * MAX_LINE_LENGTH);
        copy_text(page[row], text);
        (*count)++;
        return 0;
    }

    copy_text(displaced, page[LINES_PER_PAGE - 1]);
    if (row < LINES_PER_PAGE - 1) {
        memmove(page[row + 1], page[row],
                (size_t)(LINES_PER_PAGE - 1 - row) * MAX_LINE_LENGTH);
    }
    copy_text(page[row], text);
    return 1;
}