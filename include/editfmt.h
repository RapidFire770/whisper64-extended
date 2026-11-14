#ifndef EDITFMT_H
#define EDITFMT_H

void edit_set(char *dst, const char *src);
void edit_add(char *dst, const char *src);
void edit_add_n(char *dst, const char *src, int n);
void edit_add_dec(char *dst, unsigned int value, int width);
void edit_add_field(char *dst, const char *src, int width);

#endif