#include "curve_view.h"
#include <math.h>

struct _T500RSCurveView {
    GtkDrawingArea parent;
    int curve_type;
    double strength;
};

G_DEFINE_TYPE(T500RSCurveView, t500rs_curve_view, GTK_TYPE_DRAWING_AREA)

static double calculate_curve_value(T500RSCurveView *view, double x)
{
    double strength = view->strength;
    
    switch (view->curve_type) {
    case 0: /* Linear */
        return x;
    case 1: /* Exponential */
        return pow(x, 1.0 + strength);
    case 2: /* Logarithmic */
        return log1p(x * strength) / log1p(strength);
    case 3: /* Sigmoid */
        return 1.0 / (1.0 + exp(-strength * (x - 0.5)));
    case 4: /* Sine */
        return 0.5 + 0.5 * sin(x * 2 * M_PI * strength);
    /* Add more curves as needed */
    default:
        return x;
    }
}

static void draw_function(GtkDrawingArea *da, cairo_t *cr, int width, int height, gpointer data)
{
    T500RSCurveView *view = T500RS_CURVE_VIEW(da);
    double x, y;
    
    /* Clear background */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    
    /* Draw grid */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
    cairo_set_line_width(cr, 0.5);
    
    for (int i = 0; i <= 10; i++) {
        double pos = i * (width / 10.0);
        cairo_move_to(cr, pos, 0);
        cairo_line_to(cr, pos, height);
        cairo_move_to(cr, 0, pos * height/width);
        cairo_line_to(cr, width, pos * height/width);
    }
    cairo_stroke(cr);
    
    /* Draw curve */
    cairo_set_source_rgb(cr, 0, 0, 1);
    cairo_set_line_width(cr, 2);
    
    cairo_move_to(cr, 0, height);
    for (x = 0; x <= width; x++) {
        y = calculate_curve_value(view, x / width);
        cairo_line_to(cr, x, height * (1 - y));
    }
    cairo_stroke(cr);
}

static void t500rs_curve_view_init(T500RSCurveView *view)
{
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(view),
                                 draw_function,
                                 NULL, NULL);
    
    gtk_widget_set_size_request(GTK_WIDGET(view), 200, 200);
}

static void t500rs_curve_view_class_init(T500RSCurveViewClass *class)
{
}

GtkWidget *t500rs_curve_view_new(void)
{
    return GTK_WIDGET(g_object_new(T500RS_TYPE_CURVE_VIEW, NULL));
}

void t500rs_curve_view_set_curve(T500RSCurveView *view, int curve_type, double strength)
{
    view->curve_type = curve_type;
    view->strength = strength;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}
