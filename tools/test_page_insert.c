#include "editfmt.h"
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

int main(void) {
    test_fmt();
    if (failed)
        return 1;
    return 0;
}