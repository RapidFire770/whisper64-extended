#include "editfmt.h"

void edit_set(char *dst, const char *src) {
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

void edit_add(char *dst, const char *src) {
    while (*dst)
        dst++;
    edit_set(dst, src);
}

void edit_add_n(char *dst, const char *src, int n) {
    while (*dst)
        dst++;
    while (n > 0 && *src) {
        *dst++ = *src++;
        n--;
    }
    *dst = '\0';
}

void edit_add_dec(char *dst, unsigned int value, int width) {
    char rev[5];
    unsigned char n = 0;
    unsigned int v = value;
    int digits;

    while (*dst)
        dst++;
    if (v == 0)
        rev[n++] = '0';
    while (v) {
        rev[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    digits = (int)n;
    while (width > digits) {
        *dst++ = ' ';
        width--;
    }
    while (n)
        *dst++ = rev[--n];
    *dst = '\0';
}

void edit_add_field(char *dst, const char *src, int width) {
    int n = 0;

    while (*dst)
        dst++;
    while (*src) {
        *dst++ = *src++;
        n++;
    }
    while (width > n) {
        *dst++ = ' ';
        n++;
    }
    *dst = '\0';
}