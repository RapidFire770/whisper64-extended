#include "w64tools.h"
#include "w64reu.h"
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

static int store_mode;
static int map_sorted;
static unsigned long store_size;
static unsigned long scan_lim;
static unsigned char line_cache[128];
static unsigned long cache_at;
static unsigned int cache_n;
static int cache_ok;
static unsigned char store_line[80];
static unsigned char tok_bytes[80];
static unsigned char prg_chunk[256];
static unsigned int prg_chunk_n;
static unsigned long prg_pos;
static unsigned long prg_ceiling;

static int prg_emit_byte(unsigned char b) {
    if (prg_pos + prg_chunk_n >= prg_ceiling)
        return 3;
    prg_chunk[prg_chunk_n++] = b;
    if (prg_chunk_n >= 256) {
        w64_store_put(prg_pos, prg_chunk, prg_chunk_n);
        prg_pos += prg_chunk_n;
        prg_chunk_n = 0;
    }
    return 0;
}

static int prg_emit_word(unsigned int w) {
    int rc;

    rc = prg_emit_byte((unsigned char)w);
    if (rc)
        return rc;
    return prg_emit_byte((unsigned char)(w >> 8));
}

static void prg_flush(void) {
    if (prg_chunk_n) {
        w64_store_put(prg_pos, prg_chunk, prg_chunk_n);
        prg_pos += prg_chunk_n;
        prg_chunk_n = 0;
    }
}

static void store_err_row(int *err_row, unsigned long row) {
    unsigned int r;

    if (!err_row)
        return;
    if (row > 65535ul)
        r = 65535u;
    else
        r = (unsigned int)row;
    *err_row = (int)r;
}

static unsigned char src_byte(unsigned long pos) {
    if (!cache_ok || pos < cache_at || pos >= cache_at + cache_n) {
        unsigned int n = 128;
        unsigned long room = scan_lim - pos;

        if (room < n)
            n = (unsigned int)room;
        if (n == 0)
            return 0;
        w64_store_get(pos, line_cache, n);
        cache_at = pos;
        cache_n = n;
        cache_ok = 1;
    }
    return line_cache[(unsigned int)(pos - cache_at)];
}

static unsigned long slot_addr(unsigned long i, int width) {
    return store_size - (i + 1UL) * (unsigned long)width;
}

static int slot_fits(unsigned long i, int width, unsigned long floor) {
    unsigned long need = (i + 1UL) * (unsigned long)width;

    /* A wrapped store_size - need compares as a high address and looks free. */
    if (need > store_size)
        return 0;
    return store_size - need >= floor;
}

static void rec_renum_get(unsigned long i, unsigned int *old, unsigned int *neu,
                          unsigned int *idx) {
    unsigned char b[6];

    w64_store_get(slot_addr(i, 6), b, 6);
    *old = (unsigned int)b[0] | ((unsigned int)b[1] << 8);
    *neu = (unsigned int)b[2] | ((unsigned int)b[3] << 8);
    *idx = (unsigned int)b[4] | ((unsigned int)b[5] << 8);
}

static void rec_renum_put(unsigned long i, unsigned int old, unsigned int neu,
                          unsigned int idx) {
    unsigned char b[6];

    b[0] = (unsigned char)old;
    b[1] = (unsigned char)(old >> 8);
    b[2] = (unsigned char)neu;
    b[3] = (unsigned char)(neu >> 8);
    b[4] = (unsigned char)idx;
    b[5] = (unsigned char)(idx >> 8);
    w64_store_put(slot_addr(i, 6), b, 6);
}

static unsigned int store_new(unsigned long idx) {
    unsigned int old, neu, id;

    rec_renum_get(idx, &old, &neu, &id);
    return neu;
}

static unsigned int store_line_num(unsigned long file_i, unsigned int step) {
    return step + (unsigned int)(file_i * (unsigned long)step);
}

static int renum_before(unsigned long i, unsigned long j) {
    unsigned int oi, ni, ii, oj, nj, ij;

    rec_renum_get(i, &oi, &ni, &ii);
    rec_renum_get(j, &oj, &nj, &ij);
    if (oi != oj)
        return oi < oj;
    return ii < ij;
}

static void swap_renum(unsigned long i, unsigned long j) {
    unsigned char a[6], b[6];

    if (i == j)
        return;
    w64_store_get(slot_addr(i, 6), a, 6);
    w64_store_get(slot_addr(j, 6), b, 6);
    w64_store_put(slot_addr(i, 6), b, 6);
    w64_store_put(slot_addr(j, 6), a, 6);
}

static void sift_renum(unsigned long start, unsigned long count) {
    for (;;) {
        unsigned long root = start;
        unsigned long child = root * 2UL + 1UL;

        if (child >= count)
            return;
        if (child + 1UL < count && renum_before(child, child + 1UL))
            child++;
        if (renum_before(root, child)) {
            swap_renum(root, child);
            start = child;
        } else
            return;
    }
}

static void sort_renum(unsigned long count) {
    long start;
    unsigned long end;

    if (count < 2)
        return;
    for (start = (long)(count / 2UL) - 1L; start >= 0L; start--)
        sift_renum((unsigned long)start, count);
    for (end = count - 1UL; end > 0; end--) {
        swap_renum(end, 0);
        sift_renum(0, end);
    }
}

static void label_put(unsigned long i, const char *name) {
    unsigned char b[14];
    unsigned int k;

    for (k = 0; k < 14; k++)
        b[k] = 0;
    for (k = 0; k < 11 && name[k]; k++)
        b[k] = (unsigned char)name[k];
    w64_store_put(slot_addr(i, 14), b, 14);
}

static void label_set_num(unsigned long i, unsigned int num) {
    unsigned char b[2];

    b[0] = (unsigned char)num;
    b[1] = (unsigned char)(num >> 8);
    w64_store_put(slot_addr(i, 14) + 12, b, 2);
}

static void label_name(unsigned long i, char *dst) {
    w64_store_get(slot_addr(i, 14), dst, 12);
    dst[11] = '\0';
}

static unsigned int label_num(unsigned long i) {
    unsigned char b[2];

    w64_store_get(slot_addr(i, 14) + 12, b, 2);
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8);
}

static long find_old(struct RenumEnt *map, long nmap, unsigned int old) {
    long i;

    if (store_mode) {
        long lo = 0;
        long hi = nmap;

        while (lo < hi) {
            long mid = lo + (hi - lo) / 2;
            unsigned int oi, ni, ii;

            rec_renum_get((unsigned long)mid, &oi, &ni, &ii);
            if (oi < old)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo < nmap) {
            unsigned int oi, ni, ii;

            rec_renum_get((unsigned long)lo, &oi, &ni, &ii);
            if (oi == old)
                return lo;
        }
        return -1L;
    }
    for (i = 0; i < nmap; i++) {
        if (map[i].old_num == old)
            return i;
    }
    return -1L;
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
                     char *line, int *n, struct RenumEnt *map, long nmap) {
    for (;;) {
        unsigned int at = *ip;
        unsigned int num = 0;
        unsigned int end = 0;
        unsigned int j;
        unsigned int k;
        int kind;
        long idx;

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
        if (put_num(line, n,
                    store_mode ? store_new((unsigned long)idx) : map[idx].new_num))
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
                      unsigned int after, struct RenumEnt *map, long nmap,
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
                 unsigned int step, int *err, int *err_row) {
    unsigned int pos;
    unsigned int next;
    int nmap;
    int rc;
    int past;
    int map_i;
    int pass;
    int row;

    *err_row = 0;
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

    for (pass = 0; pass < 2; pass++) {
        unsigned int sim = *used;

        pos = 0;
        map_i = 0;
        row = 0;
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

            row++;
            kind = line_num_at(text + pos, len, &old, &digits, &after);
            if (kind < 0) {
                rc = 2;
                goto done;
            }
            rc = build_line(text + pos, len, kind > 0,
                            kind > 0 ? map[map_i].new_num : 0,
                            digits, after, map, nmap, line, &new_len);
            if (rc) {
                if (rc == 4)
                    *err_row = row;
                goto done;
            }
            if (kind > 0)
                map_i++;
            if (pass == 0) {
                if ((unsigned int)new_len > len) {
                    unsigned int growth = (unsigned int)new_len - len;

                    if (sim > text_cap || growth > text_cap - sim) {
                        *err_row = row;
                        rc = 4;
                        goto done;
                    }
                    sim += growth;
                } else if ((unsigned int)new_len < len)
                    sim -= len - (unsigned int)new_len;
                pos = has_cr ? end + 1 : end;
                continue;
            }
            rc = fit_line(text, used, text_cap, pos, end, line, new_len);
            if (rc) {
                if (rc == 4)
                    *err_row = row;
                goto done;
            }
            pos += (unsigned int)new_len;
            if (has_cr)
                pos++;
        }
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

/* Same walk as same_name. Negative when a sorts first. */
static int name_order(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    if ((unsigned char)*a < (unsigned char)*b)
        return -1;
    if ((unsigned char)*a > (unsigned char)*b)
        return 1;
    return 0;
}

static long find_label(struct LabelEnt *map, long nmap, const char *name) {
    long i;

    if (store_mode) {
        if (map_sorted) {
            long lo = 0;
            long hi = nmap;

            while (lo < hi) {
                long mid = lo + (hi - lo) / 2;
                char got[12];

                label_name((unsigned long)mid, got);
                if (name_order(got, name) < 0)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo < nmap) {
                char got[12];

                label_name((unsigned long)lo, got);
                if (same_name(got, name))
                    return lo;
            }
            return -1L;
        }
        for (i = 0; i < nmap; i++) {
            char got[12];

            label_name((unsigned long)i, got);
            if (same_name(got, name))
                return i;
        }
        return -1L;
    }
    for (i = 0; i < nmap; i++) {
        if (same_name(map[i].name, name))
            return i;
    }
    return -1L;
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
                      char *line, int *n, struct LabelEnt *map, long nmap, int list) {
    for (;;) {
        unsigned int at = *ip;
        unsigned int i;
        unsigned int nlen = 0;
        char tmp[12];
        long idx;
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
        if (put_num(line, n, store_mode ? label_num((unsigned long)idx) : map[idx].num))
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

/* THEN/ELSE: a keyword is a statement. A mapped name is a jump.
   GOTO and GOSUB keep their targets. Anything else stays text. */
static int emit_then(const unsigned char *s, unsigned int len, unsigned int *ip,
                     char *line, int *n, struct LabelEnt *map, long nmap) {
    unsigned int at = *ip;
    unsigned int i;
    unsigned int nlen = 0;
    char tmp[12];
    int kn;
    long idx;

    while (*ip < len && s[*ip] == ' ')
        (*ip)++;
    if (*ip >= len || !is_letter(s[*ip])) {
        *ip = at;
        return 0;
    }
    kn = word_at(s, len, *ip, "GOTO");
    if (!kn)
        kn = word_at(s, len, *ip, "GOSUB");
    if (kn) {
        if (put_span(line, n, s + at, *ip - at + (unsigned int)kn))
            return 4;
        *ip += (unsigned int)kn;
        return emit_names(s, len, ip, line, n, map, nmap, 1);
    }
    kn = conv_keyword_len(s, len, *ip);
    if (kn) {
        if (put_span(line, n, s + at, *ip - at + (unsigned int)kn))
            return 4;
        *ip += (unsigned int)kn;
        return 0;
    }
    i = *ip;
    while (i < len && name_char(s[i])) {
        if (nlen < 11)
            tmp[nlen] = (char)s[i];
        nlen++;
        i++;
    }
    if (nlen == 0 || nlen > 11) {
        *ip = at;
        return 0;
    }
    tmp[nlen] = '\0';
    idx = find_label(map, nmap, tmp);
    if (idx < 0) {
        *ip = at;
        return 0;
    }
    if (put_span(line, n, s + at, *ip - at))
        return 4;
    if (put_num(line, n, store_mode ? label_num((unsigned long)idx) : map[idx].num))
        return 4;
    *ip = i;
    return 0;
}

static int write_refs(const unsigned char *s, unsigned int from, unsigned int len,
                      char *line, int *n, struct LabelEnt *map, long nmap) {
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
            if (kws[k][0] == 'G')
                rc = emit_names(s, len, &i, line, n, map, nmap, 1);
            else
                rc = emit_then(s, len, &i, line, n, map, nmap);
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
                            unsigned int stmt, struct LabelEnt *map, long nmap,
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

static int flush_hold(struct LabelEnt *map, int nmap, int *hold,
                      unsigned int *counter, int *past, unsigned int step) {
    while (*hold) {
        unsigned int assigned = 0;
        int rc = take_number(0, 0, counter, past, step, &assigned);

        if (rc)
            return rc;
        map[nmap - *hold].num = assigned;
        (*hold)--;
    }
    return 0;
}

/* 1 when a later code line should wear this label's number. */
static int next_is_code(const unsigned char *text, unsigned int used,
                        unsigned int pos) {
    while (pos < used) {
        unsigned int end = line_end(text, used, pos);
        unsigned int len = end - pos;
        unsigned int start = 0;
        unsigned int nlen = 0;
        unsigned int stmt = 0;
        int lab;

        if (len == 0)
            return 0;
        lab = take_label(text + pos, len, &start, &nlen, &stmt);
        if (lab == 1 && stmt == len) {
            pos = end < used ? end + 1 : end;
            continue;
        }
        return 1;
    }
    return 0;
}

static void drop_span(unsigned char *text, unsigned int *used,
                      unsigned int pos, unsigned int end) {
    unsigned int cut = end;

    if (cut < *used)
        cut++;
    if (*used > cut)
        memmove(text + pos, text + cut, *used - cut);
    *used -= cut - pos;
}

static int label_emit(unsigned char *text, unsigned int *used,
                      unsigned int text_cap, struct LabelEnt *map, int nmap,
                      unsigned int step, int apply, int *err_row) {
    unsigned int pos = 0;
    unsigned int counter = step;
    unsigned int sim = *used;
    int past = 0;
    int row = 0;

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
        int rc;
        char line[80];

        row++;
        if (len == 0) {
            pos = has_cr ? end + 1 : end;
            continue;
        }
        kind = line_num_at(text + pos, len, &old, &digits, &after);
        if (kind < 0)
            return 2;
        if (kind == 0)
            lab = take_label(text + pos, len, &start, &nlen, &stmt);
        if (lab == 1 && stmt == len &&
            next_is_code(text, *used, has_cr ? end + 1 : end)) {
            if (apply)
                drop_span(text, used, pos, end);
            else {
                sim -= len + (has_cr ? 1u : 0u);
                pos = has_cr ? end + 1 : end;
            }
            continue;
        }
        rc = take_number(kind > 0, old, &counter, &past, step, &assigned);
        if (rc)
            return rc;
        if (kind > 0)
            mode = 1;
        else if (lab == 1)
            mode = 2;
        else
            mode = 0;
        rc = build_label_line(text + pos, len, mode, assigned, after, stmt,
                              map, nmap, line, &new_len);
        if (rc) {
            if (rc == 4 || rc == 6)
                *err_row = row;
            return rc;
        }
        if (!apply) {
            if ((unsigned int)new_len > len) {
                unsigned int growth = (unsigned int)new_len - len;

                if (sim > text_cap || growth > text_cap - sim) {
                    *err_row = row;
                    return 4;
                }
                sim += growth;
            } else if ((unsigned int)new_len < len)
                sim -= len - (unsigned int)new_len;
            pos = has_cr ? end + 1 : end;
            continue;
        }
        rc = fit_line(text, used, text_cap, pos, end, line, new_len);
        if (rc) {
            if (rc == 4)
                *err_row = row;
            return rc;
        }
        pos += (unsigned int)new_len;
        if (has_cr)
            pos++;
    }
    return 0;
}

int labels_to_numbered(unsigned char *text, unsigned int *used,
                       unsigned int text_cap,
                       struct LabelEnt *map, int map_cap,
                       unsigned int step, int *err, int *err_row) {
    unsigned int pos;
    unsigned int counter;
    int past;
    int nmap;
    int hold;
    int rc;

    *err_row = 0;
    rc = 0;
    if (step == 0 || step > 63999u) {
        rc = 1;
        goto done;
    }
    if (*used == 0)
        goto done;

    nmap = 0;
    hold = 0;
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
            rc = flush_hold(map, nmap, &hold, &counter, &past, step);
            if (rc)
                goto done;
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
                if (stmt == len) {
                    nmap++;
                    hold++;
                    pos = end < *used ? end + 1 : end;
                    continue;
                }
            }
        }
        rc = take_number(kind > 0, old, &counter, &past, step, &assigned);
        if (rc)
            goto done;
        if (hold) {
            int i;

            for (i = nmap - hold; i < nmap; i++)
                map[i].num = assigned;
            hold = 0;
        }
        if (lab == 1) {
            map[nmap].num = assigned;
            nmap++;
        }
        pos = end;
        if (pos < *used)
            pos++;
    }
    rc = flush_hold(map, nmap, &hold, &counter, &past, step);
    if (rc)
        goto done;

    rc = label_emit(text, used, text_cap, map, nmap, step, 0, err_row);
    if (rc)
        goto done;
    rc = label_emit(text, used, text_cap, map, nmap, step, 1, err_row);

done:
    if (err)
        *err = rc;
    return rc;
}

static void store_line_prefix(unsigned long pos, unsigned long end,
                              unsigned int *prefix_len, int *overflow80) {
    unsigned long p = pos;
    unsigned int n = 0;

    *overflow80 = 0;
    while (p < end) {
        unsigned char c = src_byte(p);

        if (n < 79)
            store_line[n++] = c;
        else
            *overflow80 = 1;
        p++;
    }
    *prefix_len = n;
}

int renumber_seq_store(unsigned long text_len, unsigned long reu_size,
                       unsigned int step, unsigned long *out_at,
                       unsigned long *out_len, int *err, int *err_row) {
    unsigned long pos;
    unsigned int next;
    long nmap;
    int rc;
    int past;
    int pass;
    int row;
    long map_i;
    unsigned long map_floor;
    unsigned long appended;

    store_mode = 1;
    map_sorted = 0;
    cache_ok = 0;
    scan_lim = text_len;
    store_size = reu_size;
    *err_row = 0;
    rc = 0;
    if (step == 0 || step > 63999u) {
        rc = 1;
        goto done;
    }
    if (scan_lim == 0) {
        if (out_len)
            *out_len = 0;
        goto done;
    }

    nmap = 0;
    next = step;
    past = 0;
    pos = 0;
    while (pos < scan_lim) {
        unsigned long end = pos;
        unsigned int prefix_len;
        int overflow80;
        unsigned int old = 0;
        unsigned int digits = 0;
        unsigned int after = 0;
        int kind;

        while (end < scan_lim && src_byte(end) != '\r')
            end++;
        store_line_prefix(pos, end, &prefix_len, &overflow80);
        kind = line_num_at(store_line, prefix_len, &old, &digits, &after);
        if (kind < 0) {
            rc = 2;
            goto done;
        }
        if (kind > 0) {
            if (!slot_fits((unsigned long)nmap, 6, text_len)) {
                rc = 3;
                goto done;
            }
            if (past) {
                rc = 2;
                goto done;
            }
            rec_renum_put((unsigned long)nmap, old, next, (unsigned int)nmap);
            nmap++;
            if (next > 63999u - step)
                past = 1;
            else
                next += step;
        }
        pos = end;
        if (pos < scan_lim)
            pos++;
    }

    sort_renum((unsigned long)nmap);
    map_sorted = 1;
    map_floor = nmap > 0L ? slot_addr((unsigned long)nmap - 1UL, 6) : store_size;

    for (pass = 0; pass < 2; pass++) {
        pos = 0;
        map_i = 0;
        row = 0;
        appended = 0;
        while (pos < scan_lim) {
            unsigned long end = pos;
            unsigned int prefix_len;
            int overflow80;
            int has_cr;
            unsigned int old = 0;
            unsigned int digits = 0;
            unsigned int after = 0;
            int kind;
            int new_len = 0;
            unsigned int neu = 0;
            char line[80];

            while (end < scan_lim && src_byte(end) != '\r')
                end++;
            has_cr = end < scan_lim;
            store_line_prefix(pos, end, &prefix_len, &overflow80);
            row++;
            if (overflow80) {
                *err_row = row;
                rc = 4;
                goto done;
            }
            kind = line_num_at(store_line, prefix_len, &old, &digits, &after);
            if (kind < 0) {
                rc = 2;
                goto done;
            }
            if (kind > 0)
                neu = store_line_num((unsigned long)map_i, step);
            rc = build_line(store_line, prefix_len, kind > 0,
                            kind > 0 ? neu : 0, digits, after,
                            NULL, nmap, line, &new_len);
            if (rc) {
                if (rc == 4)
                    *err_row = row;
                goto done;
            }
            if (kind > 0)
                map_i++;
            if (pass == 0) {
                appended += (unsigned long)new_len;
                if (has_cr)
                    appended++;
                if (text_len + appended > map_floor) {
                    rc = 3;
                    goto done;
                }
                pos = has_cr ? end + 1 : end;
                continue;
            }
            w64_store_put(text_len + appended, line, (unsigned int)new_len);
            appended += (unsigned long)new_len;
            if (has_cr) {
                unsigned char cr = '\r';

                w64_store_put(text_len + appended, &cr, 1);
                appended++;
            }
            pos = has_cr ? end + 1 : end;
        }
        if (pass == 1) {
            if (out_at)
                *out_at = text_len;
            if (out_len)
                *out_len = appended;
        }
    }
    rc = 0;

done:
    store_mode = 0;
    cache_ok = 0;
    if (err)
        *err = rc;
    return rc;
}

static int label_before(unsigned long i, unsigned long j) {
    char a[12];
    char b[12];

    label_name(i, a);
    label_name(j, b);
    return name_order(a, b) < 0;
}

static void swap_label(unsigned long i, unsigned long j) {
    unsigned char a[14];
    unsigned char b[14];

    if (i == j)
        return;
    w64_store_get(slot_addr(i, 14), a, 14);
    w64_store_get(slot_addr(j, 14), b, 14);
    w64_store_put(slot_addr(i, 14), b, 14);
    w64_store_put(slot_addr(j, 14), a, 14);
}

static void sift_label(unsigned long start, unsigned long count) {
    for (;;) {
        unsigned long root = start;
        unsigned long child = root * 2UL + 1UL;

        if (child >= count)
            return;
        if (child + 1UL < count && label_before(child, child + 1UL))
            child++;
        if (label_before(root, child)) {
            swap_label(root, child);
            start = child;
        } else
            return;
    }
}

static void sort_label(unsigned long count) {
    long start;
    unsigned long end;

    if (count < 2)
        return;
    for (start = (long)(count / 2UL) - 1L; start >= 0L; start--)
        sift_label((unsigned long)start, count);
    for (end = count - 1UL; end > 0; end--) {
        swap_label(end, 0);
        sift_label(0, end);
    }
}

static int flush_label_hold(long nmap, long *hold, unsigned int *counter,
                            int *past, unsigned int step) {
    while (*hold) {
        unsigned int assigned = 0;
        int rc = take_number(0, 0, counter, past, step, &assigned);

        if (rc)
            return rc;
        label_set_num((unsigned long)(nmap - *hold), assigned);
        (*hold)--;
    }
    return 0;
}

/* Skip further label-only lines. 0 on a blank line or end of file. */
static int next_is_code_store(unsigned long pos) {
    while (pos < scan_lim) {
        unsigned char buf[80];
        unsigned int n = 0;
        int overflow = 0;
        unsigned int start = 0;
        unsigned int nlen = 0;
        unsigned int stmt = 0;
        int lab;
        int has_cr = 0;

        while (pos < scan_lim) {
            unsigned char c = src_byte(pos);

            if (c == '\r') {
                has_cr = 1;
                break;
            }
            if (n < 79)
                buf[n++] = c;
            else
                overflow = 1;
            pos++;
        }
        if (n == 0 && !overflow)
            return 0;
        lab = take_label(buf, n, &start, &nlen, &stmt);
        if (lab == 1 && stmt == n && !overflow) {
            if (has_cr)
                pos++;
            continue;
        }
        return 1;
    }
    return 0;
}

int labels_to_numbered_store(unsigned long text_len, unsigned long reu_size,
                             unsigned int step, unsigned long *out_at,
                             unsigned long *out_len, int *err, int *err_row,
                             unsigned long *ceiling) {
    unsigned long pos;
    unsigned int counter;
    long nmap;
    long hold;
    int rc;
    int past;
    int pass;
    int row;
    unsigned long map_floor;
    unsigned long appended;

    store_mode = 1;
    map_sorted = 0;
    cache_ok = 0;
    scan_lim = text_len;
    store_size = reu_size;
    *err_row = 0;
    rc = 0;
    if (step == 0 || step > 63999u) {
        rc = 1;
        goto done;
    }
    if (scan_lim == 0) {
        if (out_len)
            *out_len = 0;
        if (ceiling)
            *ceiling = store_size;
        goto done;
    }

    nmap = 0;
    hold = 0;
    counter = step;
    past = 0;
    pos = 0;
    while (pos < scan_lim) {
        unsigned long end = pos;
        unsigned int prefix_len;
        int overflow80;
        unsigned int old = 0;
        unsigned int digits = 0;
        unsigned int after = 0;
        unsigned int start = 0;
        unsigned int nlen = 0;
        unsigned int stmt = 0;
        unsigned int assigned = 0;
        int kind;
        int lab = 0;

        while (end < scan_lim && src_byte(end) != '\r')
            end++;
        if (end == pos) {
            rc = flush_label_hold(nmap, &hold, &counter, &past, step);
            if (rc)
                goto done;
            pos = end < scan_lim ? end + 1 : end;
            continue;
        }
        store_line_prefix(pos, end, &prefix_len, &overflow80);
        kind = line_num_at(store_line, prefix_len, &old, &digits, &after);
        if (kind < 0) {
            rc = 2;
            goto done;
        }
        if (kind == 0) {
            lab = take_label(store_line, prefix_len, &start, &nlen, &stmt);
            if (lab < 0) {
                rc = 7;
                goto done;
            }
            if (lab == 1) {
                char tmp[12];

                if (!slot_fits((unsigned long)nmap, 14, text_len)) {
                    rc = 3;
                    goto done;
                }
                memcpy(tmp, store_line + start, nlen);
                tmp[nlen] = '\0';
                if (find_label(NULL, nmap, tmp) >= 0) {
                    rc = 8;
                    goto done;
                }
                label_put((unsigned long)nmap, tmp);
                if (stmt == prefix_len && !overflow80) {
                    nmap++;
                    hold++;
                    pos = end < scan_lim ? end + 1 : end;
                    continue;
                }
            }
        }
        rc = take_number(kind > 0, old, &counter, &past, step, &assigned);
        if (rc)
            goto done;
        if (hold) {
            long slot;

            for (slot = nmap - hold; slot < nmap; slot++)
                label_set_num((unsigned long)slot, assigned);
            hold = 0;
        }
        if (lab == 1) {
            label_set_num((unsigned long)nmap, assigned);
            nmap++;
        }
        pos = end;
        if (pos < scan_lim)
            pos++;
    }
    rc = flush_label_hold(nmap, &hold, &counter, &past, step);
    if (rc)
        goto done;

    sort_label((unsigned long)nmap);
    map_sorted = 1;
    map_floor = nmap > 0L ? slot_addr((unsigned long)nmap - 1UL, 14) : store_size;
    if (ceiling)
        *ceiling = map_floor;

    for (pass = 0; pass < 2; pass++) {
        pos = 0;
        row = 0;
        appended = 0;
        counter = step;
        past = 0;
        while (pos < scan_lim) {
            unsigned long end = pos;
            unsigned int prefix_len;
            int overflow80;
            int has_cr;
            unsigned int old = 0;
            unsigned int digits = 0;
            unsigned int after = 0;
            unsigned int start = 0;
            unsigned int nlen = 0;
            unsigned int stmt = 0;
            unsigned int assigned = 0;
            int kind;
            int lab = 0;
            int mode;
            int new_len = 0;
            char line[80];

            while (end < scan_lim && src_byte(end) != '\r')
                end++;
            has_cr = end < scan_lim;
            row++;
            if (end == pos) {
                if (pass == 0) {
                    if (has_cr)
                        appended++;
                    if (text_len + appended > map_floor) {
                        rc = 3;
                        goto done;
                    }
                } else if (has_cr) {
                    unsigned char cr = '\r';

                    w64_store_put(text_len + appended, &cr, 1);
                    appended++;
                }
                pos = has_cr ? end + 1 : end;
                continue;
            }
            store_line_prefix(pos, end, &prefix_len, &overflow80);
            if (overflow80) {
                *err_row = row;
                rc = 4;
                goto done;
            }
            kind = line_num_at(store_line, prefix_len, &old, &digits, &after);
            if (kind < 0) {
                rc = 2;
                goto done;
            }
            if (kind == 0)
                lab = take_label(store_line, prefix_len, &start, &nlen, &stmt);
            if (lab == 1 && stmt == prefix_len &&
                next_is_code_store(has_cr ? end + 1 : end)) {
                pos = has_cr ? end + 1 : end;
                continue;
            }
            rc = take_number(kind > 0, old, &counter, &past, step, &assigned);
            if (rc)
                goto done;
            if (kind > 0)
                mode = 1;
            else if (lab == 1)
                mode = 2;
            else
                mode = 0;
            rc = build_label_line(store_line, prefix_len, mode, assigned, after,
                                  stmt, NULL, nmap, line, &new_len);
            if (rc) {
                if (rc == 4 || rc == 6)
                    *err_row = row;
                goto done;
            }
            if (pass == 0) {
                appended += (unsigned long)new_len;
                if (has_cr)
                    appended++;
                if (text_len + appended > map_floor) {
                    rc = 3;
                    goto done;
                }
                pos = has_cr ? end + 1 : end;
                continue;
            }
            w64_store_put(text_len + appended, line, (unsigned int)new_len);
            appended += (unsigned long)new_len;
            if (has_cr) {
                unsigned char cr = '\r';

                w64_store_put(text_len + appended, &cr, 1);
                appended++;
            }
            pos = has_cr ? end + 1 : end;
        }
        if (pass == 1) {
            if (out_at)
                *out_at = text_len;
            if (out_len)
                *out_len = appended;
        }
    }
    rc = 0;

done:
    store_mode = 0;
    map_sorted = 0;
    cache_ok = 0;
    if (err)
        *err = rc;
    return rc;
}

int tokenize_store(unsigned long seq_at, unsigned long seq_len,
                   unsigned long reu_size, unsigned long *out_at,
                   unsigned long *out_len, int *wrote, int *err, int *err_row) {
    unsigned long pos;
    unsigned long out_base;
    unsigned int addr = 0x0801;
    int lines = 0;
    unsigned long row = 0;
    int rc = 0;

    cache_ok = 0;
    scan_lim = seq_at + seq_len;
    out_base = seq_at + seq_len;
    prg_pos = out_base;
    prg_chunk_n = 0;
    prg_ceiling = reu_size;
    if (err_row)
        *err_row = 0;

    rc = prg_emit_word(0x0801);
    if (rc)
        goto done;

    pos = seq_at;
    while (pos < scan_lim) {
        unsigned long end = pos;
        unsigned int n = 0;
        int past = 0;
        int slen;
        unsigned int line_no;
        unsigned int next;
        int j;

        row++;
        while (end < scan_lim && src_byte(end) != '\r') {
            if (n < 79)
                store_line[n++] = src_byte(end);
            else
                past = 1;
            end++;
        }
        if (end < scan_lim)
            end++;
        if (past) {
            store_err_row(err_row, row);
            rc = 4;
            goto done;
        }
        store_line[n] = '\0';
        slen = conv_tokenize((char *)store_line, &line_no, tok_bytes, 79);
        if (slen == -1) {
            pos = end;
            continue;
        }
        if (slen == -2) {
            rc = 5;
            goto done;
        }
        if (slen == -3) {
            rc = 2;
            goto done;
        }
        if (slen < 0) {
            store_err_row(err_row, row);
            rc = 4;
            goto done;
        }
        next = addr + 4u + (unsigned int)slen + 1u;
        if (next < addr) {
            rc = 3;
            goto done;
        }
        rc = prg_emit_word(next);
        if (rc)
            goto done;
        rc = prg_emit_word(line_no);
        if (rc)
            goto done;
        for (j = 0; j < slen; j++) {
            rc = prg_emit_byte(tok_bytes[j]);
            if (rc)
                goto done;
        }
        rc = prg_emit_byte(0);
        if (rc)
            goto done;
        addr = next;
        lines++;
        pos = end;
    }
    rc = prg_emit_word(0);
    if (rc)
        goto done;
    prg_flush();
    if (out_at)
        *out_at = out_base;
    if (out_len)
        *out_len = prg_pos - out_base;
    if (wrote)
        *wrote = lines;
    rc = 0;

done:
    if (rc == 3) {
        prg_flush();
        if (out_len)
            *out_len = prg_pos - out_base;
    }
    cache_ok = 0;
    if (err)
        *err = rc;
    return rc;
}
