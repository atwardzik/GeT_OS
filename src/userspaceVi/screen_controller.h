//
// Created by Artur Twardzik on 08/07/2026.
//

#ifndef OS_SCREEN_CONTROLLER_H
#define OS_SCREEN_CONTROLLER_H

#include "editor.h"

void screen_move_home(void);

void screen_move_absolute(unsigned int x, unsigned int y);

void screen_move_to_col(unsigned int x);

void screen_clear(void);

void screen_scroll_dir_up(int count);

void screen_scroll_dir_dn(void);

void highlight(const char *pattern);

#endif //OS_SCREEN_CONTROLLER_H
