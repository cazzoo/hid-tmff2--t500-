#pragma once

#include <gtk/gtk.h>

#define T500RS_TYPE_CURVE_VIEW (t500rs_curve_view_get_type())
G_DECLARE_FINAL_TYPE(T500RSCurveView, t500rs_curve_view, T500RS, CURVE_VIEW, GtkDrawingArea)

GtkWidget *t500rs_curve_view_new(void);
void t500rs_curve_view_set_curve(T500RSCurveView *view, int curve_type, double strength);
