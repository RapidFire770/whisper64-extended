#ifndef W64TOOLUI_H
#define W64TOOLUI_H

#define COL_GREEN 5
#define COL_YELLOW 7
#define COL_RED 2
#define DIR_BYTES 18
#define DIR_MAX 144

extern unsigned char read_drive;
extern unsigned char write_drive;
extern char in_name[17];
extern char out_name[17];
extern unsigned char tok[80];
extern char text_line[80];
extern unsigned long reu_bytes;
extern unsigned char last_st;

int tool_reu_ok(unsigned long bytes);

void tool_set_str(char *dst, const char *src);
void tool_add_str(char *dst, const char *src);
void tool_add_dec(char *dst, unsigned int v);
void tool_add_hex2(char *dst, unsigned char v);
void tool_file_spec(char *dst, int replace, const char *name, const char *tail);
int tool_name_ok(const char *s);
void tool_put_row(unsigned char row, const char *msg);
void tool_put_err(unsigned char row, const char *msg);
void tool_center_row(unsigned char row, const char *msg);
void tool_paint_reu(void);
void tool_lowercase(void);
void tool_clrscr(void);
int tool_cgetc(void);
void tool_finish(unsigned char code, const char *msg);
int tool_ask_drive(unsigned char row, const char *label, unsigned char *dest);
int tool_read_field(unsigned char row, const char *label, char *buf, int cap);
int tool_open_ch(unsigned char lfn, unsigned char drive, const char *spec);
void tool_close_ch(unsigned char lfn);
int tool_read_byte(unsigned char lfn_ready, unsigned char *eof);
int tool_drive_status_ok(void);
int tool_confirm_overwrite(const char *spec);
int tool_read_seq_line(unsigned char *eof, char *line, int *err);
void tool_note_progress(unsigned char row, const char *label, unsigned int n);
int tool_flush_at(const char *spec, unsigned long start, unsigned long nbytes);
void tool_stash_reset(void);
int tool_stash_byte(unsigned char b);
void tool_stash_flush(void);
unsigned long tool_stored(void);
int tool_pick_file(char want);
int tool_another(void);
void tool_stay(void) __attribute__((noreturn));
void tool_exit_ready(unsigned char device) __attribute__((noreturn));
void tool_show_err(int rc, int row);
int tool_parse_step(const char *s, unsigned int *step);
void tool_remember_output(const char *spec);
int tool_prepare(char want);
int tool_trailing_ml(unsigned char *eof);

#endif
