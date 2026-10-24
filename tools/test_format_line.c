#include "w64tools.h"
#include <stdio.h>
#include <string.h>

static int failed;

static void expect_src(const char *name, const char *src, const char *want,
                       int want_cut) {
    unsigned char stmt[256];
    char text[80];
    unsigned int num = 0;
    int cut = 99;
    int n;
    int len;

    n = conv_tokenize(src, &num, stmt, (int)sizeof stmt);
    if (n < 0) {
        printf("FAIL %s tokenize %d\n", name, n);
        failed++;
        return;
    }
    memset(text, 0xA5, sizeof text);
    len = conv_format_line(num, stmt, n, text, 80, &cut);
    if (want == NULL) {
        if (len >= 0 || cut != 1) {
            printf("FAIL %s expected refuse, len %d cut %d text %s\n",
                   name, len, cut, text);
            failed++;
        }
        return;
    }
    if (len < 0 || cut != want_cut || strcmp(text, want) != 0 ||
        len != (int)strlen(want) || len > 79) {
        printf("FAIL %s len %d cut %d\n  got  %s\n  want %s\n",
               name, len, cut, text, want);
        failed++;
    }
}

static void expect_raw(const char *name, unsigned int line_no,
                       const unsigned char *stmt, int slen, const char *want,
                       int want_cut) {
    char text[80];
    int cut = 99;
    int len;

    memset(text, 0xA5, sizeof text);
    len = conv_format_line(line_no, stmt, slen, text, 80, &cut);
    if (want == NULL) {
        if (len >= 0 || cut != 1) {
            printf("FAIL %s expected refuse, len %d cut %d\n", name, len, cut);
            failed++;
        }
        return;
    }
    if (len < 0 || cut != want_cut || strcmp(text, want) != 0) {
        printf("FAIL %s len %d cut %d\n  got  %s\n  want %s\n",
               name, len, cut, text, want);
        failed++;
    }
}

int main(void) {
    unsigned char packed[20];
    char long_rem[96];
    char long_quote[96];
    char long_data[96];

    expect_src("short", "10 PRINT \"A\"", "10 PRINT \"A\"", 0);
    expect_src("fits", "10 PRINT \"A\":PRINT \"B\"", "10 PRINT \"A\":PRINT \"B\"", 0);

    expect_src("border",
               "3 B1$=CHR$(180):B2$=CHR$(170):BL$=CHR$(108):BR$=CHR$(186):"
               "BB$=CHR$(175):TB$=CHR$(183):TL$=CHR$(111):TR$=CHR$(112)",
               "3 B1$=CHR$(180):B2$=CHR$(170):BL$=CHR$(108):BR$=CHR$(186):"
               "BB$=CHR$(175)",
               1);

    expect_src("menu",
               "213 PRINTCHR$(30):PRINT\"F1/SPACE TO ROLL AGAIN\":"
               "PRINT\"ANY KEY FOR SELECTION MENU\"",
               "213 PRINTCHR$(30):PRINT\"F1/SPACE TO ROLL AGAIN\"", 1);

    memset(packed, 0x99, sizeof packed);
    expect_raw("packed print", 10, packed, 20, NULL, 1);

    memset(long_rem, 'X', sizeof long_rem);
    memcpy(long_rem, "10 PRINT \"A\":REM ABC:", 21);
    long_rem[21 + 70] = '\0';
    expect_src("rem", long_rem, "10 PRINT \"A\"", 1);

    {
        char full[96];
        char want[80];
        memset(full, 'X', sizeof full);
        memcpy(full, "10 PRINT \"", 10);
        full[78] = '"';
        memcpy(want, full, 79);
        want[79] = '\0';
        memcpy(full + 79, ":REM HI", 8);
        expect_src("fits then colon", full, want, 1);
    }
    {
        char full[96];
        char want[80];
        memset(full, 'Y', sizeof full);
        memcpy(full, "10 A=1:PRINT \"", 14);
        full[78] = '"';
        memcpy(want, full, 79);
        want[79] = '\0';
        memcpy(full + 79, ":B=1", 5);
        expect_src("stmt ends at 79", full, want, 1);
    }

    memset(long_quote, 'Y', sizeof long_quote);
    memcpy(long_quote, "10 PRINT \":\":PRINT \"", 19);
    long_quote[19 + 60] = '"';
    long_quote[20 + 60] = '\0';
    expect_src("quote colon", long_quote, "10 PRINT \":\"", 1);

    memset(long_data, 'Z', sizeof long_data);
    memcpy(long_data, "10 DATA \"A:B\":PRINT \"", 21);
    long_data[21 + 60] = '"';
    long_data[22 + 60] = '\0';
    expect_src("data colon", long_data, "10 DATA \"A:B\"", 1);

    if (failed) {
        printf("%d failed\n", failed);
        return 1;
    }
    printf("ok\n");
    return 0;
}
