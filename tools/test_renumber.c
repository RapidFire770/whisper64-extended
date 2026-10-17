#include "w64conv.h"
#include <stdio.h>
#include <string.h>

static int failed;

static void show_bytes(const char *label, const unsigned char *s, unsigned int n) {
    unsigned int i;

    if (n > 200)
        n = 200;
    printf("  %s (%u) ", label, n);
    for (i = 0; i < n; i++) {
        unsigned char c = s[i];
        if (c == '\r')
            fputs("\\r", stdout);
        else if (c >= 32 && c < 127)
            putchar(c);
        else
            printf("\\x%02x", c);
    }
    putchar('\n');
}

static void expect_text(const char *name, const char *in, const char *want,
                        unsigned int step) {
    unsigned char buf[4096];
    struct RenumEnt map[32];
    unsigned int used = (unsigned int)strlen(in);
    unsigned int want_len = (unsigned int)strlen(want);
    int err = 99;
    int rc;

    memset(buf, 0xa5, sizeof buf);
    memcpy(buf, in, used);
    rc = renumber_seq(buf, &used, (unsigned int)sizeof buf, map, 32, step, &err);
    if (rc != 0 || err != 0 || used != want_len || memcmp(buf, want, want_len) != 0) {
        printf("FAIL %s rc=%d err=%d\n", name, rc, err);
        show_bytes("got", buf, used);
        show_bytes("want", (const unsigned char *)want, want_len);
        failed = 1;
    }
}

static void expect_err(const char *name, const unsigned char *in, unsigned int used,
                       unsigned int cap, struct RenumEnt *map, int map_cap,
                       unsigned int step, int want) {
    unsigned char buf[4096];
    unsigned char orig[4096];
    unsigned int got_used = used;
    int err = 99;
    int rc;

    memset(buf, 0xa5, sizeof buf);
    memcpy(buf, in, used);
    memcpy(orig, in, used);
    rc = renumber_seq(buf, &got_used, cap, map, map_cap, step, &err);
    if (rc != want || err != want || got_used != used || memcmp(buf, orig, used) != 0) {
        printf("FAIL %s rc=%d err=%d used=%u\n", name, rc, err, got_used);
        show_bytes("got", buf, got_used);
        show_bytes("orig", orig, used);
        failed = 1;
    }
}

static void expect_err_str(const char *name, const char *in, unsigned int step,
                           int want) {
    struct RenumEnt map[32];

    expect_err(name, (const unsigned char *)in, (unsigned int)strlen(in), 4096,
               map, 32, step, want);
}

int main(void) {
    unsigned char many[4096];
    struct RenumEnt big[512];
    struct RenumEnt map[32];
    unsigned int i;
    unsigned int used;
    unsigned char long_line[80];
    unsigned int len;

    expect_text("goto self", "5 GOTO 5\r20 END", "10 GOTO 10\r20 END", 10);
    expect_text("on goto list", "10 ON A GOTO 10, 30\r30 END",
                "10 ON A GOTO 10, 20\r20 END", 10);
    expect_text("rem", "10 REM GOTO 99\r", "10 REM GOTO 99\r", 10);
    expect_text("quotes", "10 PRINT \"GOTO 99\"", "10 PRINT \"GOTO 99\"", 10);
    expect_err_str("missing target", "10 GOTO 40\r20 END", 10, 5);
    expect_err_str("step 0", "10 END", 0, 1);
    expect_err_str("step 64000", "10 END", 64000, 1);

    used = 0;
    for (i = 0; i < 513; i++) {
        many[used++] = '1';
        if (i + 1 < 513)
            many[used++] = '\r';
    }
    expect_err("513 lines", many, used, 4096, big, 512, 10, 3);

    expect_err_str("step 30000", "1\r1\r1", 30000, 2);
    expect_text("go to", "5 GO TO 5", "10 GO TO 10", 10);
    expect_text("forgoto", "10 FORGOTO 40\r20 END", "10 FORGOTO 40\r20 END", 10);
    expect_text("swap", "30 GOTO 10\r10 END", "10 GOTO 20\r20 END", 10);
    expect_text("then digits", "10 THEN5\r5 END", "10 THEN20\r20 END", 10);
    expect_text("go sub", "5 GO SUB 5", "10 GO SUB 10", 10);
    expect_text("remember", "10 REMEMBER:GOTO 5\r5 END",
                "10 REMEMBER:GOTO 20\r20 END", 10);

    /* Line number would change, so a commit-before-check would show up. */
    expect_err_str("missing on first", "5 GOTO 40\r20 END", 10, 5);
    expect_err_str("missing in list", "5 GOTO 5, 40", 10, 5);

    used = 0;
    {
        int err = 99;
        int rc = renumber_seq(many, &used, 4096, map, 32, 10, &err);
        if (rc != 0 || err != 0 || used != 0) {
            printf("FAIL empty rc=%d err=%d used=%u\n", rc, err, used);
            failed = 1;
        }
    }

    len = 0;
    long_line[len++] = '5';
    long_line[len++] = ' ';
    while (len < 79)
        long_line[len++] = 'X';
    expect_err("line 79 grows", long_line, len, 4096, map, 32, 10, 4);

    len = 0;
    while (len < 79)
        long_line[len++] = 'X';
    long_line[len++] = '\r';
    expect_err("line 79 keeps cr", long_line, len, 4096, map, 32, 10, 0);

    len = 0;
    while (len < 80)
        long_line[len++] = 'Y';
    expect_err("line 80", long_line, 80, 4096, map, 32, 10, 4);
    expect_err("cap", (const unsigned char *)"5 END", 5, 5, map, 32, 10, 4);
    {
        unsigned char exact[6];
        unsigned int n = 5;
        int err = 99;
        int rc;

        memcpy(exact, "5 END", 5);
        rc = renumber_seq(exact, &n, 6, map, 32, 10, &err);
        if (rc != 0 || err != 0 || n != 6 || memcmp(exact, "10 END", 6) != 0) {
            printf("FAIL exact cap rc=%d err=%d used=%u\n", rc, err, n);
            failed = 1;
        }
    }

    expect_text("shrink grow", "999 END\r1 END\r22 END", "10 END\r20 END\r30 END", 10);
    expect_text("second grows", "10 END\r5 END", "10 END\r20 END", 10);
    expect_text("empty lines", "5 END\r\r6 END", "10 END\r\r20 END", 10);
    expect_text("unnumbered ref", "GOTO 5\r5 END", "GOTO 10\r10 END", 10);
    expect_text("no lf split", "5 END\n20 END", "10 END\n20 END", 10);
    expect_text("comma nospace", "10 ON A GOTO 5,6\r5 END\r6 END",
                "10 ON A GOTO 20,30\r20 END\r30 END", 10);
    expect_text("else", "10 IF A THEN 5 ELSE 6\r5 END\r6 END",
                "10 IF A THEN 20 ELSE 30\r20 END\r30 END", 10);
    expect_text("quote then goto", "10 PRINT \"GOTO 99\":GOTO 5\r5 END",
                "10 PRINT \"GOTO 99\":GOTO 20\r20 END", 10);
    expect_text("rem mid", "10 PRINT X:REM GOTO 5\r5 END",
                "10 PRINT X:REM GOTO 5\r20 END", 10);
    expect_text("data ref", "10 DATA GOTO 5\r5 END", "10 DATA GOTO 20\r20 END", 10);
    expect_text("leading spaces", "  5 END", "  10 END", 10);
    expect_text("not numbered", "10PRINT\r5 END", "10PRINT\r10 END", 10);
    expect_text("dup old", "10 END\r10 GOTO 10", "10 END\r20 GOTO 10", 10);
    expect_text("step 63999", "1 END", "63999 END", 63999);
    expect_err_str("old too big", "64000 END", 10, 2);
    expect_err_str("two at 63999", "1\r2", 63999, 2);
    expect_err_str("later old big", "10 END\r64000 END", 10, 2);

    if (failed)
        return 1;
    printf("ok\n");
    return 0;
}
