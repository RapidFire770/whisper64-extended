#include "w64tools.h"
#include <ctype.h>
#include <string.h>

/* Token order. $80 is END, $CB is GO. INPUT# is before INPUT.
   PRINT# is before PRINT. GOTO is before GO. Do not sort. */
static const char *keywords[] = {
    "END", "FOR", "NEXT", "DATA", "INPUT#", "INPUT", "DIM", "READ", "LET", "GOTO",
    "RUN", "IF", "RESTORE", "GOSUB", "RETURN", "REM", "STOP", "ON", "WAIT", "LOAD",
    "SAVE", "VERIFY", "DEF", "POKE", "PRINT#", "PRINT", "CONT", "LIST", "CLR", "CMD",
    "SYS", "OPEN", "CLOSE", "GET", "NEW", "TAB(", "TO", "FN", "SPC(", "THEN",
    "NOT", "STEP", "+", "-", "*", "/", "^", "AND", "OR", ">",
    "=", "<", "SGN", "INT", "ABS", "USR", "FRE", "POS", "SQR", "RND",
    "LOG", "EXP", "COS", "SIN", "TAN", "ATN", "PEEK", "LEN", "STR$", "VAL",
    "ASC", "CHR$", "LEFT$", "RIGHT$", "MID$", "GO",
    NULL
};

static int keyword_at(const unsigned char *s, const char *kw) {
    int n = 0;

    while (*kw) {
        if (*s != (unsigned char)*kw)
            return 0;
        s++;
        kw++;
        n++;
    }
    return n;
}

int conv_keyword_len(const unsigned char *s, unsigned int len, unsigned int i) {
    int k;

    if (i >= len)
        return 0;
    for (k = 0; keywords[k] != NULL; k++) {
        const char *kw = keywords[k];
        unsigned int n = 0;
        unsigned int p = i;
        unsigned char last = 0;

        while (kw[n]) {
            if (p >= len || s[p] != (unsigned char)kw[n])
                break;
            last = (unsigned char)kw[n];
            p++;
            n++;
        }
        if (kw[n])
            continue;
        if ((last >= 'A' && last <= 'Z') || (last >= '0' && last <= '9')) {
            if (p < len) {
                unsigned char c = s[p];

                if (c >= 'A' && c <= 'Z')
                    continue;
            }
        }
        return (int)n;
    }
    return 0;
}

int conv_tokenize(const char *line, unsigned int *line_no,
                  unsigned char *out, int cap) {
    const unsigned char *s;
    unsigned int num = 0;
    int n = 0;
    int quote = 0;
    int rem = 0;
    int data = 0;

    while (*line == ' ')
        line++;
    if (*line == '\0')
        return -1;
    if (!isdigit((unsigned char)*line))
        return -2;

    while (isdigit((unsigned char)*line)) {
        unsigned int digit = (unsigned char)*line - '0';
        if (num > 63999u / 10 || (num == 63999u / 10 && digit > 63999u % 10))
            return -3;
        num = num * 10 + digit;
        line++;
    }
    /* ROM crunch starts at the first non-space. A stored space would LIST as two. */
    while (*line == ' ')
        line++;

    s = (const unsigned char *)line;
    while (*s) {
        int k;

        /* A quote ends DATA only for a colon outside the quotes.
           The quote check has to run before the DATA check. */
        if (n >= cap)
            return -4;
        if (rem) {
            out[n++] = *s++;
            continue;
        }
        if (quote) {
            out[n++] = *s;
            if (*s == '"')
                quote = 0;
            s++;
            continue;
        }
        if (*s == '"') {
            out[n++] = '"';
            quote = 1;
            s++;
            continue;
        }
        if (data) {
            if (*s == ':')
                data = 0;
            out[n++] = *s++;
            continue;
        }
        if (*s == '?') {
            out[n++] = 0x99;
            s++;
            continue;
        }
        for (k = 0; keywords[k] != NULL; k++) {
            int len = keyword_at(s, keywords[k]);
            if (!len)
                continue;
            out[n++] = (unsigned char)(0x80 + k);
            s += len;
            if (out[n - 1] == 0x8F)
                rem = 1;
            else if (out[n - 1] == 0x83)
                data = 1;
            break;
        }
        if (keywords[k] == NULL)
            out[n++] = *s++;
    }

    *line_no = num;
    return n;
}

static int put_piece(char *text, int *len, int cap, const char *bytes, int n,
                     int *truncated) {
    int i;

    if (*len + n >= cap) {
        *truncated = 1;
        return -1;
    }
    for (i = 0; i < n; i++)
        text[*len + i] = bytes[i];
    *len += n;
    return 0;
}

int conv_format_line(unsigned int line_no, const unsigned char *stmt, int slen,
                     char *text, int cap, int *truncated) {
    char body[80];
    char num[8];
    int blen = 0;
    int state = 0;
    int i;
    int nlen;
    int pos;

    *truncated = 0;
    for (i = 0; i < slen; i++) {
        unsigned char b = stmt[i];
        const char *piece;
        char one[1];
        int plen;
        char kwbuf[8];

        if (state == 2 || state == 4 || state == 3 || state == 1) {
            one[0] = (char)b;
            piece = one;
            plen = 1;
            if (state == 4 && b == '"')
                state = 3;
            else if (state == 3 && b == '"')
                state = 4;
            else if (state == 3 && b == ':')
                state = 0;
            else if (state == 1 && b == '"')
                state = 0;
        } else if (b == '"') {
            state = 1;
            one[0] = '"';
            piece = one;
            plen = 1;
        } else if (b >= 0x80 && b <= 0xCB) {
            const char *kw = keywords[b - 0x80];
            plen = 0;
            while (kw[plen] && plen < 7) {
                kwbuf[plen] = kw[plen];
                plen++;
            }
            kwbuf[plen] = '\0';
            piece = kwbuf;
            if (b == 0x8F)
                state = 2;
            else if (b == 0x83)
                state = 3;
        } else {
            one[0] = (char)b;
            piece = one;
            plen = 1;
        }
        put_piece(body, &blen, 80, piece, plen, truncated);
    }
    body[blen] = '\0';

    nlen = 0;
    {
        unsigned int v = line_no;
        char rev[8];
        int r = 0;
        if (v == 0)
            rev[r++] = '0';
        while (v) {
            rev[r++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (r > 0)
            num[nlen++] = rev[--r];
    }
    num[nlen] = '\0';

    pos = 0;
    for (i = 0; i < nlen && pos < cap - 1; i++)
        text[pos++] = num[i];
    if (blen > 0 && body[0] != ' ' && pos < cap - 1)
        text[pos++] = ' ';
    for (i = 0; i < blen && pos < cap - 1; i++)
        text[pos++] = body[i];
    if (nlen + (blen > 0 && body[0] != ' ') + blen > cap - 1)
        *truncated = 1;
    text[pos] = '\0';
    return pos;
}
