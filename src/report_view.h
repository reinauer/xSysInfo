// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 xSysInfo contributors
#ifndef REPORT_VIEW_H
#define REPORT_VIEW_H

#include "gui.h"

void open_report_view(void);
void close_report_view(void);
void draw_report_view(void);
void report_view_update_buttons(void);
void report_view_handle_button(ButtonID id);
void report_view_key(UWORD code, UWORD qualifier);
void report_view_scrollbar(WORD y);

#endif
