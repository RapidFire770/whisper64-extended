#ifndef W64CONV_H
#define W64CONV_H

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

/* >=0 statement length. -1 blank. -2 no number. -3 above 63999.
   -4 the statement would not fit in cap bytes. */
int conv_tokenize(const char *line, unsigned int *line_no,
                  unsigned char *out, int cap);

/* Writes the editor text for one statement, capped at cap-1 characters.
   *truncated is set when a byte did not fit. Returns the length. */
int conv_format_line(unsigned int line_no, const unsigned char *stmt, int slen,
                     char *text, int cap, int *truncated);

/* Volatile so a whole-program link keeps the bytes the Ultimate test reads. */
extern volatile unsigned char verify_buf[256];
extern volatile unsigned long verify_len;
extern volatile unsigned long verify_sum;
extern volatile unsigned char verify_code;

#endif
