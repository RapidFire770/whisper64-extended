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

__attribute__((noinline))
static void move_bytes(char *dst, const char *src, unsigned int n) {
    if (n == 0 || dst == src)
        return;
    if (dst > src) {
        dst += n;
        src += n;
        while (n != 0) {
            n--;
            dst--;
            src--;
            *dst = *src;
        }
        return;
    }
    while (n != 0) {
        *dst = *src;
        dst++;
        src++;
        n--;
    }
}

int page_insert_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, const char *text, char *displaced) {
    if (*count < LINES_PER_PAGE) {
        move_bytes(page[row + 1], page[row],
                   (unsigned int)(*count - row) * MAX_LINE_LENGTH);
        copy_text(page[row], text);
        (*count)++;
        return 0;
    }

    copy_text(displaced, page[LINES_PER_PAGE - 1]);
    if (row < LINES_PER_PAGE - 1) {
        move_bytes(page[row + 1], page[row],
                   (unsigned int)(LINES_PER_PAGE - 1 - row) * MAX_LINE_LENGTH);
    }
    copy_text(page[row], text);
    return 1;
}

int page_remove_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, char *removed) {
    int n;

    if (*count < 1 || row < 0 || row >= *count)
        return 1;
    copy_text(removed, page[row]);
    n = *count - row - 1;
    if (n > 0) {
        move_bytes(page[row], page[row + 1],
                   (unsigned int)n * MAX_LINE_LENGTH);
    }
    page[*count - 1][0] = '\0';
    (*count)--;
    return 0;
}