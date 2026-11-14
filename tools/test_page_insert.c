#include "editfmt.h"
#include "page_shift.h"
#include <stdio.h>
#include <string.h>

static int failed;

static void fail(const char *name) {
    printf("FAIL %s\n", name);
    failed = 1;
}

static void expect_str(const char *name, const char *got, const char *want) {
    if (strcmp(got, want) != 0)
        fail(name);
}

static void test_fmt(void) {
    char buf[64];

    buf[0] = '\0';
    edit_add_dec(buf, 0, 0);
    expect_str("dec 0", buf, "0");

    buf[0] = '\0';
    edit_add_dec(buf, 12, 4);
    expect_str("dec width", buf, "  12");

    buf[0] = '\0';
    edit_add_dec(buf, 12345, 4);
    expect_str("dec long", buf, "12345");

    buf[0] = '\0';
    edit_add_field(buf, "HI", 16);
    expect_str("field hi", buf, "HI              ");

    buf[0] = '\0';
    edit_add_field(buf, "ABCDEFGHIJKLMNOP", 16);
    expect_str("field 16", buf, "ABCDEFGHIJKLMNOP");

    buf[0] = '\0';
    edit_add_n(buf, "ABCDEFGHIJ", 8);
    expect_str("add n", buf, "ABCDEFGH");

    buf[0] = '\0';
    edit_add_n(buf, "AB", 8);
    expect_str("add n short", buf, "AB");

    buf[0] = '\0';
    edit_set(buf, "REU: ");
    edit_add_dec(buf, 16320, 0);
    edit_add(buf, "KB  MAX ");
    edit_add_dec(buf, 4347, 0);
    edit_add(buf, " PAGES");
    expect_str("startup", buf, "REU: 16320KB  MAX 4347 PAGES");
}

static void fill_numbered(char page[][MAX_LINE_LENGTH], int n) {
    int i;
    for (i = 0; i < n; i++) {
        page[i][0] = 'L';
        page[i][1] = (char)('0' + (i / 10));
        page[i][2] = (char)('0' + (i % 10));
        page[i][3] = '\0';
    }
}

static int displaced_untouched(const unsigned char *d) {
    int i;
    for (i = 0; i < MAX_LINE_LENGTH; i++) {
        if (d[i] != 0xA5)
            return 0;
    }
    return 1;
}

static void test_pages(void) {
    char page[LINES_PER_PAGE][MAX_LINE_LENGTH];
    unsigned char displaced[MAX_LINE_LENGTH];
    int count;
    int rc;
    char big[81];
    int i;

    memset(page, 0, sizeof page);
    strcpy(page[0], "AA");
    strcpy(page[1], "BB");
    strcpy(page[2], "CC");
    count = 3;
    memset(displaced, 0xA5, sizeof displaced);
    rc = page_insert_line(page, &count, 1, "XX", (char *)displaced);
    if (rc != 0 || count != 4 || strcmp(page[0], "AA") != 0 ||
        strcmp(page[1], "XX") != 0 || strcmp(page[2], "BB") != 0 ||
        strcmp(page[3], "CC") != 0 || !displaced_untouched(displaced))
        fail("insert middle");

    memset(page, 0, sizeof page);
    strcpy(page[0], "AA");
    strcpy(page[1], "BB");
    strcpy(page[2], "CC");
    count = 3;
    memset(displaced, 0xA5, sizeof displaced);
    rc = page_insert_line(page, &count, 3, "XX", (char *)displaced);
    if (rc != 0 || count != 4 || strcmp(page[0], "AA") != 0 ||
        strcmp(page[1], "BB") != 0 || strcmp(page[2], "CC") != 0 ||
        strcmp(page[3], "XX") != 0 || !displaced_untouched(displaced))
        fail("append");

    memset(page, 0, sizeof page);
    strcpy(page[0], "AA");
    strcpy(page[1], "BB");
    count = 2;
    rc = page_insert_line(page, &count, 1, "", (char *)displaced);
    if (rc != 0 || count != 3 || strcmp(page[0], "AA") != 0 ||
        page[1][0] != '\0' || strcmp(page[2], "BB") != 0)
        fail("blank");

    memset(page, 0, sizeof page);
    fill_numbered(page, LINES_PER_PAGE);
    count = LINES_PER_PAGE;
    rc = page_insert_line(page, &count, 0, "NEW", (char *)displaced);
    if (rc != 1 || count != LINES_PER_PAGE || strcmp(page[0], "NEW") != 0 ||
        strcmp(page[1], "L00") != 0 || strcmp((char *)displaced, "L47") != 0)
        fail("full head");

    memset(page, 0, sizeof page);
    fill_numbered(page, LINES_PER_PAGE);
    count = LINES_PER_PAGE;
    rc = page_insert_line(page, &count, 47, "NEW", (char *)displaced);
    if (rc != 1 || count != LINES_PER_PAGE || strcmp(page[47], "NEW") != 0 ||
        strcmp((char *)displaced, "L47") != 0)
        fail("full last");
    for (i = 0; i < 47; i++) {
        char want[4];
        want[0] = 'L';
        want[1] = (char)('0' + (i / 10));
        want[2] = (char)('0' + (i % 10));
        want[3] = '\0';
        if (strcmp(page[i], want) != 0)
            fail("full last kept");
    }

    memset(page, 0, sizeof page);
    memset(big, 'A', 80);
    big[80] = '\0';
    count = 1;
    page[0][0] = '\0';
    rc = page_insert_line(page, &count, 0, big, (char *)displaced);
    if (rc != 0 || count != 2 || strlen(page[0]) != 79)
        fail("truncate");
    for (i = 0; i < 79; i++) {
        if (page[0][i] != 'A')
            fail("truncate bytes");
    }
}

int main(void) {
    test_fmt();
    test_pages();
    if (failed)
        return 1;
    return 0;
}