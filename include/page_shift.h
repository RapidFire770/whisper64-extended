#ifndef PAGE_SHIFT_H
#define PAGE_SHIFT_H

#ifndef MAX_LINE_LENGTH
#define MAX_LINE_LENGTH 80
#endif
#ifndef LINES_PER_PAGE
#define LINES_PER_PAGE 48
#endif

int page_insert_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, const char *text, char *displaced);

#endif