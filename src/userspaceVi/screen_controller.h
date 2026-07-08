//
// Created by Artur Twardzik on 08/07/2026.
//

#ifndef OS_SCREEN_CONTROLLER_H
#define OS_SCREEN_CONTROLLER_H

#include "editor.h"

void move_home(void);

void move_end(void);

void move_cmd_field(void);

void move_absolute(int x, int y);

void move_to_col(int x);

void clear_screen(void);


void write_line(struct Line *line);

void highlight(const char *pattern);

#endif //OS_SCREEN_CONTROLLER_H
