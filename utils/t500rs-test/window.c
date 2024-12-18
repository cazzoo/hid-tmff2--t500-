#include "window.h"
#include "curve_view.h"
#include "device.h"
#include <linux/input.h>

struct _T500RSWindow {
    GtkApplicationWindow parent;
    
    /* Device */
    T500RSDevice *device;
    
    /* Widgets */
    GtkWidget *curve_view;
    GtkWidget *effect_combo;
    GtkWidget *curve_combo;
    GtkWidget *strength_scale;
    GtkWidget *play_button;
    GtkWidget *stop_button;
    GtkWidget *combine_button;
    GtkWidget *weight_scale[T500RS_MAX_EFFECTS];
    GtkWidget *curve_type_combo[T500RS_MAX_EFFECTS];
    GtkWidget *curve_params_box[T500RS_MAX_EFFECTS];
    
    /* Effect parameters */
    struct t500rs_effect_params current_effect;
    struct t500rs_combined_effect combined_effect;
    bool is_combined;
    int active_effects[T500RS_MAX_EFFECTS];
    int num_active_effects;
};

G_DEFINE_TYPE(T500RSWindow, t500rs_window, GTK_TYPE_APPLICATION_WINDOW)

static void update_curve_view(T500RSWindow *win) 
{
    if (win->is_combined) {
        /* Update combined effect view */
        t500rs_curve_view_set_combined(T500RS_CURVE_VIEW(win->curve_view),
                                     win->combined_effect.weights,
                                     win->combined_effect.weight_params,
                                     win->combined_effect.num_effects);
    } else {
        /* Update single effect view */
        t500rs_curve_view_set_curve(T500RS_CURVE_VIEW(win->curve_view),
                                  gtk_combo_box_get_active(GTK_COMBO_BOX(win->curve_combo)),
                                  gtk_range_get_value(GTK_RANGE(win->strength_scale)));
    }
}

static void on_curve_changed(GtkComboBox *combo, T500RSWindow *win) 
{
    update_curve_view(win);
}

static void on_strength_changed(GtkRange *range, T500RSWindow *win)
{
    update_curve_view(win);
}

static void on_combine_clicked(GtkButton *button, T500RSWindow *win)
{
    win->is_combined = !win->is_combined;
    
    /* Toggle visibility of effect-specific widgets */
    gtk_widget_set_visible(win->curve_combo, !win->is_combined);
    gtk_widget_set_visible(win->strength_scale, !win->is_combined);
    
    for (int i = 0; i < win->num_active_effects; i++) {
        gtk_widget_set_visible(win->weight_scale[i], win->is_combined);
        gtk_widget_set_visible(win->curve_type_combo[i], win->is_combined);
        gtk_widget_set_visible(win->curve_params_box[i], win->is_combined);
    }
    
    update_curve_view(win);
}

static void on_weight_changed(GtkRange *range, T500RSWindow *win)
{
    int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "index"));
    win->combined_effect.weights[index] = gtk_range_get_value(range);
    
    if (win->device && win->is_combined) {
        t500rs_device_update_weights(win->device,
                                   win->combined_effect.effect_ids[index],
                                   &win->combined_effect.weights[index],
                                   1);
    }
    
    update_curve_view(win);
}

static void on_curve_type_changed(GtkComboBox *combo, T500RSWindow *win)
{
    int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(combo), "index"));
    struct t500rs_weight_params *params = &win->combined_effect.weight_params[index];
    
    params->curve_type = gtk_combo_box_get_active(combo);
    
    /* Show/hide curve-specific parameter widgets */
    GtkWidget *box = win->curve_params_box[index];
    gtk_container_foreach(GTK_CONTAINER(box), (GtkCallback)gtk_widget_destroy, NULL);
    
    switch (params->curve_type) {
    case T500RS_CURVE_CUSTOM:
        for (int i = 0; i < 8; i++) {
            GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                      0, 255, 1);
            gtk_range_set_value(GTK_RANGE(scale), params->curve_points[i]);
            g_signal_connect(scale, "value-changed",
                           G_CALLBACK(on_curve_point_changed), win);
            g_object_set_data(G_OBJECT(scale), "index",
                            GINT_TO_POINTER((index << 4) | i));
            gtk_container_add(GTK_CONTAINER(box), scale);
        }
        break;
        
    case T500RS_CURVE_SINE:
    case T500RS_CURVE_COSINE:
    case T500RS_CURVE_TRIANGLE:
    case T500RS_CURVE_SAWTOOTH:
        {
            GtkWidget *freq_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                           0, 255, 1);
            GtkWidget *phase_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                            0, 255, 1);
            gtk_range_set_value(GTK_RANGE(freq_scale), params->wave.frequency);
            gtk_range_set_value(GTK_RANGE(phase_scale), params->wave.phase);
            g_signal_connect(freq_scale, "value-changed",
                           G_CALLBACK(on_wave_param_changed), win);
            g_signal_connect(phase_scale, "value-changed",
                           G_CALLBACK(on_wave_param_changed), win);
            g_object_set_data(G_OBJECT(freq_scale), "param", "frequency");
            g_object_set_data(G_OBJECT(phase_scale), "param", "phase");
            g_object_set_data(G_OBJECT(freq_scale), "index", GINT_TO_POINTER(index));
            g_object_set_data(G_OBJECT(phase_scale), "index", GINT_TO_POINTER(index));
            gtk_container_add(GTK_CONTAINER(box), freq_scale);
            gtk_container_add(GTK_CONTAINER(box), phase_scale);
        }
        break;
        
    case T500RS_CURVE_BOUNCE:
        {
            GtkWidget *count_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                            1, 10, 1);
            GtkWidget *decay_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                            0, 255, 1);
            gtk_range_set_value(GTK_RANGE(count_scale), params->bounce.bounce_count);
            gtk_range_set_value(GTK_RANGE(decay_scale), params->bounce.decay);
            g_signal_connect(count_scale, "value-changed",
                           G_CALLBACK(on_bounce_param_changed), win);
            g_signal_connect(decay_scale, "value-changed",
                           G_CALLBACK(on_bounce_param_changed), win);
            g_object_set_data(G_OBJECT(count_scale), "param", "count");
            g_object_set_data(G_OBJECT(decay_scale), "param", "decay");
            g_object_set_data(G_OBJECT(count_scale), "index", GINT_TO_POINTER(index));
            g_object_set_data(G_OBJECT(decay_scale), "index", GINT_TO_POINTER(index));
            gtk_container_add(GTK_CONTAINER(box), count_scale);
            gtk_container_add(GTK_CONTAINER(box), decay_scale);
        }
        break;
    }
    
    gtk_widget_show_all(box);
    update_curve_view(win);
}

static void on_curve_point_changed(GtkRange *range, T500RSWindow *win)
{
    int data = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "index"));
    int index = data >> 4;
    int point = data & 0xf;
    
    win->combined_effect.weight_params[index].curve_points[point] = 
        gtk_range_get_value(range);
    
    update_curve_view(win);
}

static void on_wave_param_changed(GtkRange *range, T500RSWindow *win)
{
    int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "index"));
    const char *param = g_object_get_data(G_OBJECT(range), "param");
    struct t500rs_weight_params *params = &win->combined_effect.weight_params[index];
    
    if (strcmp(param, "frequency") == 0)
        params->wave.frequency = gtk_range_get_value(range);
    else if (strcmp(param, "phase") == 0)
        params->wave.phase = gtk_range_get_value(range);
    
    update_curve_view(win);
}

static void on_bounce_param_changed(GtkRange *range, T500RSWindow *win)
{
    int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "index"));
    const char *param = g_object_get_data(G_OBJECT(range), "param");
    struct t500rs_weight_params *params = &win->combined_effect.weight_params[index];
    
    if (strcmp(param, "count") == 0)
        params->bounce.bounce_count = gtk_range_get_value(range);
    else if (strcmp(param, "decay") == 0)
        params->bounce.decay = gtk_range_get_value(range);
    
    update_curve_view(win);
}

static void on_play_clicked(GtkButton *button, T500RSWindow *win)
{
    if (!win->device) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win),
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 "No device connected!");
        gtk_window_present(GTK_WINDOW(dialog));
        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        return;
    }
    
    if (win->is_combined) {
        /* Upload combined effect */
        win->combined_effect.num_effects = win->num_active_effects;
        win->combined_effect.dynamic_weights = true;
        
        int effect_id = t500rs_device_upload_combined(win->device, &win->combined_effect);
        if (effect_id >= 0)
            t500rs_device_start_effect(win->device, effect_id);
    } else {
        /* Upload single effect */
        win->current_effect.type = FF_CONSTANT;
        win->current_effect.id = -1;
        win->current_effect.u.constant.level = 0x7fff;
        win->current_effect.replay.length = 1000;
        win->current_effect.replay.delay = 0;
        
        t500rs_device_upload_effect(win->device, &win->current_effect,
                                  gtk_combo_box_get_active(GTK_COMBO_BOX(win->curve_combo)),
                                  gtk_range_get_value(GTK_RANGE(win->strength_scale)));
    }
}

static void on_stop_clicked(GtkButton *button, T500RSWindow *win)
{
    if (win->device)
        t500rs_device_stop_effect(win->device, win->current_effect.id);
}

static void t500rs_window_init(T500RSWindow *win)
{
    gtk_widget_init_template(GTK_WIDGET(win));
    
    /* Try to open device */
    win->device = t500rs_device_new("/dev/input/by-id/usb-Thrustmaster_T500RS_Racing_Wheel-event-joystick");
    
    /* Initialize effect parameters */
    memset(&win->current_effect, 0, sizeof(win->current_effect));
    memset(&win->combined_effect, 0, sizeof(win->combined_effect));
    win->is_combined = false;
    win->num_active_effects = 0;
    
    /* Create weight scale widgets */
    GtkWidget *weights_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        
        /* Weight scale */
        win->weight_scale[i] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                       0, 255, 1);
        gtk_range_set_value(GTK_RANGE(win->weight_scale[i]), 128);
        g_signal_connect(win->weight_scale[i], "value-changed",
                        G_CALLBACK(on_weight_changed), win);
        g_object_set_data(G_OBJECT(win->weight_scale[i]), "index",
                         GINT_TO_POINTER(i));
        
        /* Curve type combo */
        win->curve_type_combo[i] = gtk_combo_box_text_new();
        for (int j = 0; j < T500RS_CURVE_CUSTOM; j++) {
            char name[32];
            snprintf(name, sizeof(name), "Curve %d", j);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(win->curve_type_combo[i]),
                                         name);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(win->curve_type_combo[i]), 0);
        g_signal_connect(win->curve_type_combo[i], "changed",
                        G_CALLBACK(on_curve_type_changed), win);
        g_object_set_data(G_OBJECT(win->curve_type_combo[i]), "index",
                         GINT_TO_POINTER(i));
        
        /* Curve parameters box */
        win->curve_params_box[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        
        gtk_box_pack_start(GTK_BOX(row), win->weight_scale[i], TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), win->curve_type_combo[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), win->curve_params_box[i], TRUE, TRUE, 0);
        
        gtk_box_pack_start(GTK_BOX(weights_box), row, FALSE, FALSE, 0);
        
        /* Initially hide combined effect widgets */
        gtk_widget_set_visible(win->weight_scale[i], false);
        gtk_widget_set_visible(win->curve_type_combo[i], false);
        gtk_widget_set_visible(win->curve_params_box[i], false);
    }
    
    /* Add combine button */
    win->combine_button = gtk_toggle_button_new_with_label("Combine Effects");
    g_signal_connect(win->combine_button, "clicked",
                    G_CALLBACK(on_combine_clicked), win);
    
    /* Pack widgets */
    GtkWidget *main_box = gtk_bin_get_child(GTK_BIN(win));
    gtk_box_pack_start(GTK_BOX(main_box), weights_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), win->combine_button, FALSE, FALSE, 0);
    
    /* Connect other signals */
    g_signal_connect(win->curve_combo, "changed",
                    G_CALLBACK(on_curve_changed), win);
    g_signal_connect(win->strength_scale, "value-changed",
                    G_CALLBACK(on_strength_changed), win);
    g_signal_connect(win->play_button, "clicked",
                    G_CALLBACK(on_play_clicked), win);
    g_signal_connect(win->stop_button, "clicked",
                    G_CALLBACK(on_stop_clicked), win);
    
    update_curve_view(win);
}

static void t500rs_window_class_init(T500RSWindowClass *class)
{
    gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS(class),
                                              "/org/t500rs/test/window.ui");
    
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), T500RSWindow, curve_view);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), T500RSWindow, effect_combo);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), T500RSWindow, curve_combo);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), T500RSWindow, strength_scale);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), T500RSWindow, play_button);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), T500RSWindow, stop_button);
}

T500RSWindow *t500rs_window_new(GtkApplication *app)
{
    return g_object_new(T500RS_TYPE_WINDOW,
                       "application", app,
                       NULL);
}
