#pragma once

#include <gtk/gtk.h>

#define T500RS_TYPE_WINDOW (t500rs_window_get_type())
G_DECLARE_FINAL_TYPE(T500RSWindow, t500rs_window, T500RS, WINDOW, GtkApplicationWindow)

T500RSWindow *t500rs_window_new(GtkApplication *app);
void t500rs_window_open(T500RSWindow *win, const char *file);
