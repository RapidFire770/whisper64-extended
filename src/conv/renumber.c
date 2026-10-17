#include "w64tools.h"
#include <ctype.h>
#include <string.h>

static int is_letter(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static const char *refs[] = {
    "GO SUB", "GO TO", "GOSUB", "GOTO", "THEN", "ELSE", NULL
};

static int word_at(const unsigned char *s, unsigned int len, unsigned int i,
                   const char *kw) {
    unsigned int n = 0;
    unsigned int k;

    while (kw[n])
        n++;
    if (i > 0 && is_letter(s[i - 1]))
        return 0;
    if (n > len - i)
        return 0;
    for (k = 0; k < n; k++) {
        if (s[i + k] != (unsigned char)kw[k])
            return 0;
    }
    if (i + n < len && is_letter(s[i + n]))
        return 0;
    return (int)n;
}

static int ref_at(const unsigned char *s, unsigned int len, unsigned int i) {
    int k;

    for (k = 0; refs[k] != NULL; k++) {
        int n = word_at(s, len, i, refs[k]);
        if (n)
            return n;
    }
    return 0;
}

static int line_num_at(const unsigned char *s, unsigned int len,
                       unsigned int *num, unsigned int *digits,
                       unsigned int *after) {
    unsigned int i = 0;
    unsigned int v = 0;
    int over = 0;

    while (i < len && s[i] == ' ')
        i++;
    if (i >= len || !isdigit((unsigned char)s[i]))
        return 0;
    *digits = i;
    while (i < len && isdigit((unsigned char)s[i])) {
        unsigned int d = (unsigned char)s[i] - '0';
        if (!over) {
            if (v > 63999u / 10 || (v == 63999u / 10 && d > 63999u % 10))
                over = 1;
            else
                v = v * 10u + d;
        }
        i++;
    }
    if (i < len && s[i] != ' ')
        return 0;
    *after = i;
    if (over)
        return -1;
    *num = v;
    return 1;
}

static int put_span(char *line, int *n, const unsigned char *s, unsigned int sn) {
    unsigned int i;

    if (sn > 79u || (unsigned int)*n > 79u - sn)
        return 4;
    for (i = 0; i < sn; i++)
        line[(*n)++] = (char)s[i];
    return 0;
}

static int put_num(char *line, int *n, unsigned int v) {
    char rev[6];
    int r = 0;

    if (v == 0)
        rev[r++] = '0';
    while (v) {
        rev[r++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    if (*n > 79 - r)
        return 4;
    while (r > 0)
        line[(*n)++] = rev[--r];
    return 0;
}

static int find_old(struct RenumEnt *map, int nmap, unsigned int old) {
    int i;

    for (i = 0; i < nmap; i++) {
        if (map[i].old_num == old)
            return i;
    }
    return -1;
}

static int read_num(const unsigned char *s, unsigned int len, unsigned int i,
                    unsigned int *num, unsigned int *end) {
    unsigned int v = 0;

    if (i >= len || !isdigit((unsigned char)s[i]))
        return 0;
    while (i < len && isdigit((unsigned char)s[i])) {
        unsigned int d = (unsigned char)s[i] - '0';
        if (v > 63999u / 10 || (v == 63999u / 10 && d > 63999u % 10))
            return -1;
        v = v * 10u + d;
        i++;
    }
    *num = v;
    *end = i;
    return 1;
}

static int emit_refs(const unsigned char *s, unsigned int len, unsigned int *ip,
                     char *line, int *n, struct RenumEnt *map, int nmap) {
    for (;;) {
        unsigned int at = *ip;
        unsigned int num = 0;
        unsigned int end = 0;
        unsigned int j;
        unsigned int k;
        int kind;
        int idx;

        while (*ip < len && s[*ip] == ' ')
            (*ip)++;
        if (*ip >= len || !isdigit((unsigned char)s[*ip])) {
            *ip = at;
            return 0;
        }
        if (put_span(line, n, s + at, *ip - at))
            return 4;
        kind = read_num(s, len, *ip, &num, &end);
        if (kind <= 0)
            return 5;
        idx = find_old(map, nmap, num);
        if (idx < 0)
            return 5;
        if (put_num(line, n, map[idx].new_num))
            return 4;
        *ip = end;

        j = *ip;
        while (j < len && s[j] == ' ')
            j++;
        if (j >= len || s[j] != ',')
            return 0;
        k = j + 1;
        while (k < len && s[k] == ' ')
            k++;
        if (k >= len || !isdigit((unsigned char)s[k]))
            return 0;
        if (put_span(line, n, s + *ip, k - *ip))
            return 4;
        *ip = k;
    }
}

static int build_line(const unsigned char *s, unsigned int len, int numbered,
                      unsigned int new_num, unsigned int digits,
                      unsigned int after, struct RenumEnt *map, int nmap,
                      char *line, int *out_len) {
    int n = 0;
    unsigned int i = 0;
    int quote = 0;

    if (numbered) {
        /* Leading number is this line's new number, not a GOTO target. */
        if (put_span(line, &n, s, digits))
            return 4;
        if (put_num(line, &n, new_num))
            return 4;
        i = after;
    }
    while (i < len) {
        int kn;

        if (quote) {
            if (put_span(line, &n, s + i, 1))
                return 4;
            if (s[i] == '"')
                quote = 0;
            i++;
            continue;
        }
        if (s[i] == '"') {
            if (put_span(line, &n, s + i, 1))
                return 4;
            quote = 1;
            i++;
            continue;
        }
        if (word_at(s, len, i, "REM")) {
            if (put_span(line, &n, s + i, len - i))
                return 4;
            break;
        }
        kn = ref_at(s, len, i);
        if (kn) {
            int rc;

            if (put_span(line, &n, s + i, (unsigned int)kn))
                return 4;
            i += (unsigned int)kn;
            rc = emit_refs(s, len, &i, line, &n, map, nmap);
            if (rc)
                return rc;
            continue;
        }
        if (put_span(line, &n, s + i, 1))
            return 4;
        i++;
    }
    line[n] = '\0';
    *out_len = n;
    return 0;
}

static unsigned int line_end(const unsigned char *text, unsigned int used,
                             unsigned int pos) {
    while (pos < used && text[pos] != '\r')
        pos++;
    return pos;
}

static int fit_line(unsigned char *text, unsigned int *used, unsigned int text_cap,
                    unsigned int pos, unsigned int end, const char *built, int new_len) {
    unsigned int len = end - pos;

    if ((unsigned int)new_len > len) {
        unsigned int growth = (unsigned int)new_len - len;

        if (*used > text_cap || growth > text_cap - *used)
            return 4;
        if (*used > end)
            memmove(text + end + growth, text + end, *used - end);
        *used += growth;
    } else if ((unsigned int)new_len < len) {
        unsigned int shrink = len - (unsigned int)new_len;

        if (*used > end)
            memmove(text + (end - shrink), text + end, *used - end);
        *used -= shrink;
    }
    {
        int c;

        for (c = 0; c < new_len; c++)
            text[pos + (unsigned int)c] = (unsigned char)built[c];
    }
    return 0;
}

int renumber_seq(unsigned char *text, unsigned int *used, unsigned int text_cap,
                 struct RenumEnt *map, int map_cap,
                 unsigned int step, int *err) {
    unsigned int pos;
    unsigned int next;
    int nmap;
    int rc;
    int past;
    int map_i;

    rc = 0;
    if (step == 0 || step > 63999u) {
        rc = 1;
        goto done;
    }
    if (*used == 0)
        goto done;

    nmap = 0;
    next = step;
    past = 0;
    pos = 0;
    while (pos < *used) {
        unsigned int end = line_end(text, *used, pos);
        unsigned int len = end - pos;
        unsigned int old = 0;
        unsigned int digits = 0;
        unsigned int after = 0;
        int kind = line_num_at(text + pos, len, &old, &digits, &after);

        if (kind < 0) {
            rc = 2;
            goto done;
        }
        if (kind > 0) {
            if (nmap >= map_cap) {
                rc = 3;
                goto done;
            }
            if (past) {
                rc = 2;
                goto done;
            }
            map[nmap].old_num = old;
            map[nmap].new_num = next;
            nmap++;
            /* 16-bit unsigned wraps. Reject the next number before adding step. */
            if (next > 63999u - step)
                past = 1;
            else
                next += step;
        }
        pos = end;
        if (pos < *used)
            pos++;
    }

    pos = 0;
    map_i = 0;
    while (pos < *used) {
        unsigned int end = line_end(text, *used, pos);
        unsigned int len = end - pos;
        unsigned int old = 0;
        unsigned int digits = 0;
        unsigned int after = 0;
        int has_cr = end < *used;
        int new_len = 0;
        int kind;
        char line[80];

        kind = line_num_at(text + pos, len, &old, &digits, &after);
        if (kind < 0) {
            rc = 2;
            goto done;
        }
        rc = build_line(text + pos, len, kind > 0,
                        kind > 0 ? map[map_i].new_num : 0,
                        digits, after, map, nmap, line, &new_len);
        if (rc)
            goto done;
        if (kind > 0)
            map_i++;

        rc = fit_line(text, used, text_cap, pos, end, line, new_len);
        if (rc)
            goto done;
        pos += (unsigned int)new_len;
        if (has_cr)
            pos++;
    }
    rc = 0;

done:
    if (err)
        *err = rc;
    return rc;
}

static int name_char(unsigned char c) {
    return is_letter(c) || (c >= '0' && c <= '9');
}

/* 1 label, 0 not, -1 the name is longer than 11 and ends with ':'. */
static int take_label(const unsigned char *s, unsigned int len,
                      unsigned int *start, unsigned int *nlen, unsigned int *stmt) {
    unsigned int i = 0;
    unsigned int n = 0;

    while (i < len && s[i] == ' ')
        i++;
    if (i >= len || !is_letter(s[i]))
        return 0;
    *start = i;
    while (i < len && name_char(s[i])) {
        n++;
        i++;
    }
    if (i >= len || s[i] != ':')
        return 0;
    if (n > 11)
        return -1;
    i++;
    if (i < len && s[i] == ' ')
        i++;
    *nlen = n;
    *stmt = i;
    return 1;
}

static int same_name(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int find_label(struct LabelEnt *map, int nmap, const char *name) {
    int i;

    for (i = 0; i < nmap; i++) {
        if (same_name(map[i].name, name))
            return i;
    }
    return -1;
}

/* A kept line number at or above the counter jumps the counter past it.
   A fresh number takes the counter. 16-bit add is checked before it wraps. */
static int take_number(int numbered, unsigned int old, unsigned int *counter,
                       int *past, unsigned int step, unsigned int *out) {
    if (numbered) {
        *out = old;
        if (old >= *counter) {
            if (old > 63999u - step)
                *past = 1;
            else
                *counter = old + step;
        }
        return 0;
    }
    if (*past)
        return 2;
    *out = *counter;
    if (*counter > 63999u - step)
        *past = 1;
    else
        *counter += step;
    return 0;
}

static int emit_names(const unsigned char *s, unsigned int len, unsigned int *ip,
                      char *line, int *n, struct LabelEnt *map, int nmap, int list) {
    for (;;) {
        unsigned int at = *ip;
        unsigned int i;
        unsigned int nlen = 0;
        char tmp[12];
        int idx;
        unsigned int j;
        unsigned int k;

        while (*ip < len && s[*ip] == ' ')
            (*ip)++;
        if (*ip >= len || !is_letter(s[*ip])) {
            *ip = at;
            return 0;
        }
        if (put_span(line, n, s + at, *ip - at))
            return 4;
        i = *ip;
        while (i < len && name_char(s[i])) {
            if (nlen < 11)
                tmp[nlen] = (char)s[i];
            nlen++;
            i++;
        }
        if (nlen == 0 || nlen > 11)
            return 6;
        tmp[nlen] = '\0';
        idx = find_label(map, nmap, tmp);
        if (idx < 0)
            return 6;
        if (put_num(line, n, map[idx].num))
            return 4;
        *ip = i;
        if (!list)
            return 0;
        j = *ip;
        while (j < len && s[j] == ' ')
            j++;
        if (j >= len || s[j] != ',')
            return 0;
        k = j + 1;
        while (k < len && s[k] == ' ')
            k++;
        if (k >= len || !is_letter(s[k]))
            return 0;
        if (put_span(line, n, s + *ip, k - *ip))
            return 4;
        *ip = k;
    }
}

static int write_refs(const unsigned char *s, unsigned int from, unsigned int len,
                      char *line, int *n, struct LabelEnt *map, int nmap) {
    static const char *kws[] = { "GOSUB", "GOTO", "THEN", "ELSE", NULL };
    unsigned int i = from;
    int quote = 0;

    while (i < len) {
        int k;

        if (quote) {
            if (put_span(line, n, s + i, 1))
                return 4;
            if (s[i] == '"')
                quote = 0;
            i++;
            continue;
        }
        if (s[i] == '"') {
            if (put_span(line, n, s + i, 1))
                return 4;
            quote = 1;
            i++;
            continue;
        }
        if (word_at(s, len, i, "REM")) {
            if (put_span(line, n, s + i, len - i))
                return 4;
            break;
        }
        for (k = 0; kws[k] != NULL; k++) {
            int kn = word_at(s, len, i, kws[k]);
            int rc;

            if (!kn)
                continue;
            if (put_span(line, n, s + i, (unsigned int)kn))
                return 4;
            i += (unsigned int)kn;
            rc = emit_names(s, len, &i, line, n, map, nmap, kws[k][0] == 'G');
            if (rc)
                return rc;
            break;
        }
        if (kws[k] != NULL)
            continue;
        if (put_span(line, n, s + i, 1))
            return 4;
        i++;
    }
    return 0;
}

static int build_label_line(const unsigned char *s, unsigned int len, int mode,
                            unsigned int new_num, unsigned int after,
                            unsigned int stmt, struct LabelEnt *map, int nmap,
                            char *line, int *out_len) {
    int n = 0;
    int rc;

    if (mode == 1) {
        if (put_span(line, &n, s, after))
            return 4;
        rc = write_refs(s, after, len, line, &n, map, nmap);
    } else {
        if (put_num(line, &n, new_num))
            return 4;
        if (mode == 2) {
            if (stmt < len) {
                unsigned char sp = ' ';

                if (put_span(line, &n, &sp, 1))
                    return 4;
                rc = write_refs(s, stmt, len, line, &n, map, nmap);
            } else
                rc = 0;
        } else {
            unsigned char sp = ' ';

            if (put_span(line, &n, &sp, 1))
                return 4;
            rc = write_refs(s, 0, len, line, &n, map, nmap);
        }
    }
    if (rc)
        return rc;
    line[n] = '\0';
    *out_len = n;
    return 0;
}

int labels_to_numbered(unsigned char *text, unsigned int *used,
                       unsigned int text_cap,
                       struct LabelEnt *map, int map_cap,
                       unsigned int step, int *err) {
    unsigned int pos;
    unsigned int counter;
    int past;
    int nmap;
    int rc;

    rc = 0;
    if (step == 0 || step > 63999u) {
        rc = 1;
        goto done;
    }
    if (*used == 0)
        goto done;

    nmap = 0;
    counter = step;
    past = 0;
    pos = 0;
    while (pos < *used) {
        unsigned int end = line_end(text, *used, pos);
        unsigned int len = end - pos;
        unsigned int old = 0;
        unsigned int digits = 0;
        unsigned int after = 0;
        unsigned int start = 0;
        unsigned int nlen = 0;
        unsigned int stmt = 0;
        unsigned int assigned = 0;
        int kind;
        int lab = 0;

        if (len == 0) {
            pos = end < *used ? end + 1 : end;
            continue;
        }
        kind = line_num_at(text + pos, len, &old, &digits, &after);
        if (kind < 0) {
            rc = 2;
            goto done;
        }
        if (kind == 0) {
            lab = take_label(text + pos, len, &start, &nlen, &stmt);
            if (lab < 0) {
                rc = 7;
                goto done;
            }
            if (lab == 1) {
                char tmp[12];

                if (nmap >= map_cap) {
                    rc = 3;
                    goto done;
                }
                memcpy(tmp, text + pos + start, nlen);
                tmp[nlen] = '\0';
                if (find_label(map, nmap, tmp) >= 0) {
                    rc = 8;
                    goto done;
                }
                memcpy(map[nmap].name, tmp, (unsigned int)nlen + 1);
            }
        }
        rc = take_number(kind > 0, old, &counter, &past, step, &assigned);
        if (rc)
            goto done;
        if (lab == 1) {
            map[nmap].num = assigned;
            nmap++;
        }
        pos = end;
        if (pos < *used)
            pos++;
    }

    counter = step;
    past = 0;
    pos = 0;
    while (pos < *used) {
        unsigned int end = line_end(text, *used, pos);
        unsigned int len = end - pos;
        unsigned int old = 0;
        unsigned int digits = 0;
        unsigned int after = 0;
        unsigned int start = 0;
        unsigned int nlen = 0;
        unsigned int stmt = 0;
        unsigned int assigned = 0;
        int has_cr = end < *used;
        int new_len = 0;
        int kind;
        int lab = 0;
        int mode;
        char line[80];

        if (len == 0) {
            pos = has_cr ? end + 1 : end;
            continue;
        }
        kind = line_num_at(text + pos, len, &old, &digits, &after);
        if (kind == 0)
            lab = take_label(text + pos, len, &start, &nlen, &stmt);
        rc = take_number(kind > 0, old, &counter, &past, step, &assigned);
        if (rc)
            goto done;
        if (kind > 0)
            mode = 1;
        else if (lab == 1)
            mode = 2;
        else
            mode = 0;
        rc = build_label_line(text + pos, len, mode, assigned, after, stmt,
                              map, nmap, line, &new_len);
        if (rc)
            goto done;
        rc = fit_line(text, used, text_cap, pos, end, line, new_len);
        if (rc)
            goto done;
        pos += (unsigned int)new_len;
        if (has_cr)
            pos++;
    }
    rc = 0;

done:
    if (err)
        *err = rc;
    return rc;
}
