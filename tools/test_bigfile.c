#include "w64tools.h"
#include "w64reu.h"
#include <stdio.h>
#include <string.h>

static unsigned char mem[256 * 1024];
static int failed;

void w64_store_get(unsigned long addr, void *dst, unsigned int n) {
    memcpy(dst, mem + addr, n);
}

void w64_store_put(unsigned long addr, const void *src, unsigned int n) {
    memcpy(mem + addr, src, n);
}

static void fail(const char *name) {
    printf("FAIL %s\n", name);
    failed = 1;
}

static void check_parity(void) {
    const char *in = "1 GOTO 2\r2 END\r";
    const char *want = "10 GOTO 20\r20 END\r";
    unsigned char buf[64];
    struct RenumEnt map[8];
    unsigned int used = (unsigned int)strlen(in);
    unsigned long out_at = 0, out_len = 0;
    int err = 99, row = 77, rc;

    memset(buf, 0, sizeof buf);
    memcpy(buf, in, used);
    rc = renumber_seq(buf, &used, sizeof buf, map, 8, 10, &err, &row);
    if (rc != 0 || used != strlen(want) || memcmp(buf, want, used) != 0)
        fail("ram parity");
    memset(mem, 0, sizeof mem);
    memcpy(mem, in, strlen(in));
    err = 99;
    row = 77;
    rc = renumber_seq_store(strlen(in), sizeof mem, 10, &out_at, &out_len, &err, &row);
    if (rc != 0 || err != 0 || out_len != strlen(want) ||
        memcmp(mem + out_at, want, out_len) != 0)
        fail("store parity");
}

static void check_file_order(void) {
    const char *in = "30 GOTO 10\r10 END\r";
    const char *want = "10 GOTO 20\r20 END\r";
    unsigned char buf[64];
    struct RenumEnt map[8];
    unsigned int used = (unsigned int)strlen(in);
    unsigned long out_at = 0, out_len = 0;
    int err = 99, row = 77, rc;

    memset(buf, 0, sizeof buf);
    memcpy(buf, in, used);
    rc = renumber_seq(buf, &used, sizeof buf, map, 8, 10, &err, &row);
    if (rc != 0 || used != strlen(want) || memcmp(buf, want, used) != 0)
        fail("ram file order");
    memset(mem, 0, sizeof mem);
    memcpy(mem, in, strlen(in));
    err = 99;
    row = 77;
    rc = renumber_seq_store(strlen(in), sizeof mem, 10, &out_at, &out_len, &err, &row);
    if (rc != 0 || err != 0 || out_len != strlen(want) ||
        memcmp(mem + out_at, want, out_len) != 0)
        fail("store file order");
}

static void check_long(void) {
    unsigned long n = 0;
    unsigned int i;
    unsigned long out_at = 0, out_len = 0;
    int err = 99, row = 77, rc;
    char line[48];

    memset(mem, 0, sizeof mem);
    for (i = 1; i <= 2000; i++) {
        int k = sprintf(line, "%u REM THIS IS A LONG LINE OF TEXT\r", i);
        memcpy(mem + n, line, (unsigned int)k);
        n += (unsigned int)k;
    }
    memcpy(mem + n, "2001 GOTO 1\r", 12);
    n += 12;
    if (n <= OUT_CAP)
        fail("long setup");
    rc = renumber_seq_store(n, sizeof mem, 10, &out_at, &out_len, &err, &row);
    if (rc != 0 || err != 0 || memcmp(mem + out_at, "10 REM", 6) != 0)
        fail("long head");
    /* 2001 numbered lines at step 10. The last line keeps its CR. */
    if (out_len < 14 || memcmp(mem + out_at + out_len - 14, "20010 GOTO 10\r", 14) != 0)
        fail("long goto");
}

static void check_many(void) {
    unsigned long n = 0;
    int i;
    unsigned long out_at = 0, out_len = 0;
    int err = 99, row = 77, rc;

    memset(mem, 0, sizeof mem);
    for (i = 0; i < 600; i++) {
        memcpy(mem + n, "1 X\r", 4);
        n += 4;
    }
    rc = renumber_seq_store(n, sizeof mem, 10, &out_at, &out_len, &err, &row);
    if (rc != 0 || memcmp(mem + out_at, "10 X\r20 X\r", 10) != 0)
        fail("many");
}

static void check_missing(void) {
    unsigned long out_at = 0, out_len = 0;
    int err = 99, row = 77, rc;

    memset(mem, 0, sizeof mem);
    memcpy(mem, "10 GOTO 99\r", 11);
    rc = renumber_seq_store(11, sizeof mem, 10, &out_at, &out_len, &err, &row);
    if (rc != 5 || err != 5)
        fail("missing");
}

static void check_tight(void) {
    const char *in = "10 END\r20 END\r";
    unsigned long out_at = 1, out_len = 1;
    int err = 99, row = 77, rc;
    unsigned char orig[16];

    memset(mem, 0xA5, sizeof mem);
    memcpy(mem, in, strlen(in));
    memcpy(orig, in, strlen(in));
    rc = renumber_seq_store(strlen(in), strlen(in), 10, &out_at, &out_len, &err, &row);
    if (rc != 3 || err != 3 || memcmp(mem, orig, strlen(in)) != 0)
        fail("tight");
}

static void check_label_text(const char *ram_name, const char *store_name,
                             const char *in, const char *want) {
    unsigned char buf[128];
    unsigned char tail[2048];
    int cap = 2048 / (int)sizeof(struct LabelEnt);
    unsigned int used = (unsigned int)strlen(in);
    unsigned long out_at = 0, out_len = 0;
    int err = 99, row = 77, rc;

    memset(buf, 0, sizeof buf);
    memcpy(buf, in, used);
    rc = labels_to_numbered(buf, &used, sizeof buf,
                            (struct LabelEnt *)tail, cap, 10, &err, &row);
    if (rc != 0 || used != strlen(want) || memcmp(buf, want, used) != 0)
        fail(ram_name);
    memset(mem, 0, sizeof mem);
    memcpy(mem, in, strlen(in));
    err = 99;
    row = 77;
    rc = labels_to_numbered_store(strlen(in), sizeof mem, 10,
                                  &out_at, &out_len, &err, &row);
    if (rc != 0 || err != 0 || out_len != strlen(want) ||
        memcmp(mem + out_at, want, out_len) != 0)
        fail(store_name);
}

static void check_labels(void) {
    check_label_text("ram labels", "store labels",
                     "start:\rPRINT \"HI\"\rGOTO start",
                     "10 PRINT \"HI\"\r20 GOTO 10");
}

static void check_label_order(void) {
    check_label_text("ram label order", "store label order",
                     "c: END\ra: END\rb:\rGOTO b",
                     "10 END\r20 END\r30 GOTO 30");
}

static void check_label_blank(void) {
    check_label_text("ram label blank", "store label blank",
                     "a:\r\rGOTO a",
                     "10\r\r20 GOTO 10");
}

static void check_label_dup(void) {
    const char *in = "a: END\ra: END";
    unsigned char orig[32];
    unsigned long out_at = 0, out_len = 0;
    int err = 99, row = 77, rc;

    memset(mem, 0, sizeof mem);
    memcpy(mem, in, strlen(in));
    memcpy(orig, in, strlen(in));
    rc = labels_to_numbered_store(strlen(in), sizeof mem, 10,
                                  &out_at, &out_len, &err, &row);
    if (rc != 8 || err != 8 || memcmp(mem, orig, strlen(in)) != 0)
        fail("label dup");
}

static void check_label_dup_unsorted(void) {
    const char *in = "b: END\ra: END\rb: END";
    unsigned char orig[32];
    unsigned long out_at = 0, out_len = 0;
    int err = 99, row = 77, rc;

    memset(mem, 0, sizeof mem);
    memcpy(mem, in, strlen(in));
    memcpy(orig, in, strlen(in));
    rc = labels_to_numbered_store(strlen(in), sizeof mem, 10,
                                  &out_at, &out_len, &err, &row);
    if (rc != 8 || err != 8 || memcmp(mem, orig, strlen(in)) != 0)
        fail("label dup unsorted");
}

static void check_label_slot(void) {
    const char *in = "a: END";
    unsigned long out_at = 1, out_len = 1;
    int err = 99, row = 77, rc;
    unsigned char orig[16];

    memset(mem, 0xA5, sizeof mem);
    memcpy(mem, in, strlen(in));
    memcpy(orig, in, strlen(in));
    rc = labels_to_numbered_store(strlen(in), strlen(in), 10,
                                  &out_at, &out_len, &err, &row);
    if (rc != 3 || err != 3 || memcmp(mem, orig, strlen(in)) != 0)
        fail("label slot");
}

static void check_tokenize(void) {
    const unsigned char want[] = {0x01, 0x08, 0x07, 0x08, 0x0A, 0x00,
                                  0x80, 0x00, 0x00, 0x00};
    unsigned long out_at = 0, out_len = 0;
    int wrote = -1, err = 99, row = 77, rc;

    memset(mem, 0, sizeof mem);
    memcpy(mem, "10 END\r", 7);
    rc = tokenize_store(0, 7, 4096, &out_at, &out_len, &wrote, &err, &row);
    if (rc != 0 || err != 0 || wrote != 1 || out_at != 7 || out_len != 10 ||
        memcmp(mem + 7, want, 10) != 0)
        fail("tokenize end");

    memset(mem, 0, sizeof mem);
    memcpy(mem, "PRINT \"HI\"\r", 12);
    err = 99;
    rc = tokenize_store(0, 12, 4096, &out_at, &out_len, &wrote, &err, &row);
    if (rc != 5 || err != 5)
        fail("tokenize nonum");

    memset(mem, 0, sizeof mem);
    memcpy(mem, "10 END\r", 7);
    err = 99;
    rc = tokenize_store(0, 7, 11, &out_at, &out_len, &wrote, &err, &row);
    if (rc != 3 || err != 3)
        fail("tokenize tight");
}

static void check_label_tight(void) {
    const char *in = "10 END\r20 END\r";
    unsigned long out_at = 1, out_len = 1;
    int err = 99, row = 77, rc;
    unsigned char orig[16];

    memset(mem, 0xA5, sizeof mem);
    memcpy(mem, in, strlen(in));
    memcpy(orig, in, strlen(in));
    rc = labels_to_numbered_store(strlen(in), strlen(in), 10,
                                  &out_at, &out_len, &err, &row);
    if (rc != 3 || err != 3 || memcmp(mem, orig, strlen(in)) != 0)
        fail("label tight");
}

int main(void) {
    check_parity();
    check_file_order();
    check_long();
    check_many();
    check_missing();
    check_tight();
    check_labels();
    check_label_order();
    check_label_blank();
    check_label_dup();
    check_label_dup_unsorted();
    check_label_slot();
    check_label_tight();
    check_tokenize();
    return failed ? 1 : 0;
}
