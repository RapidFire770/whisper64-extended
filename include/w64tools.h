#ifndef W64TOOLS_H
#define W64TOOLS_H

#define CONV_IDLE      0
#define CONV_OK        1
#define CONV_NOT_BASIC 2
#define CONV_BAD_BASIC 3
#define CONV_NO_NUM    4
#define CONV_NUM_BIG   5
#define CONV_LINE_LONG 6
#define CONV_OPEN_FAIL 7
#define CONV_DISK      8
#define CONV_TOO_BIG   9
#define CONV_BAD_NAME  10
#define CONV_CANCEL    11

#define OUT_CAP 20096

/* >=0 statement length. -1 blank. -2 no number. -3 above 63999.
   -4 the statement would not fit in cap bytes. */
int conv_tokenize(const char *line, unsigned int *line_no,
                  unsigned char *out, int cap);

/* Length of a V2 keyword at s[i], or 0. A following letter rejects a
   keyword that ends in a letter or digit. TAB( and SPC( keep the paren. */
int conv_keyword_len(const unsigned char *s, unsigned int len, unsigned int i);

/* Writes the editor text for one statement, capped at cap-1 characters.
   *truncated is set when a byte did not fit. Returns the length. */
int conv_format_line(unsigned int line_no, const unsigned char *stmt, int slen,
                     char *text, int cap, int *truncated);

struct RenumEnt {
    unsigned int old_num;
    unsigned int new_num;
};

int renumber_seq(unsigned char *text, unsigned int *used, unsigned int text_cap,
                 struct RenumEnt *map, int map_cap,
                 unsigned int step, int *err, int *err_row);

struct LabelEnt {
    char name[12];
    unsigned int num;
};

int labels_to_numbered(unsigned char *text, unsigned int *used,
                       unsigned int text_cap,
                       struct LabelEnt *map, int map_cap,
                       unsigned int step, int *err, int *err_row);

int renumber_seq_store(unsigned long text_len, unsigned long reu_size,
                       unsigned int step, unsigned long *out_at,
                       unsigned long *out_len, int *err, int *err_row);
/* *ceiling is the first map byte, or reu_size when the map is empty. */
int labels_to_numbered_store(unsigned long text_len, unsigned long reu_size,
                             unsigned int step, unsigned long *out_at,
                             unsigned long *out_len, int *err, int *err_row,
                             unsigned long *ceiling);
/* *wrote is the BASIC line count. reu_size is the exclusive end of free
   space (the first map byte when a map is still resident).
   A 16-bit link wrap returns 9. A collision with that end returns 3. */
int tokenize_store(unsigned long seq_at, unsigned long seq_len,
                   unsigned long reu_size, unsigned long *out_at,
                   unsigned long *out_len, int *wrote, int *err, int *err_row);

/* Volatile so a whole-program link keeps the bytes the Ultimate test reads. */
extern volatile unsigned char verify_buf[256];
extern volatile unsigned long verify_len;
extern volatile unsigned long verify_sum;
extern volatile unsigned char verify_code;

#endif
