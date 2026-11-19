#ifndef EDITOR_H
#define EDITOR_H

#include "whisper64.h"

// Initialization
void init_editor(void);

// Text editing operations
void insert_char(char c);
int delete_char(void);
int delete_marked_lines(void);
int new_line(void);

// Paging
void save_current_page_to_temp(void);
void load_page(int page_num);
void check_page_boundary(void);

#endif // EDITOR_H