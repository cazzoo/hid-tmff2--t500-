#pragma once

#include <gtk/gtk.h>
#include <linux/input.h>

/* T500RS Effect Types */
enum t500rs_effect_type {
    /* Basic Effects */
    T500RS_EFFECT_CONSTANT = 0x00,
    T500RS_EFFECT_SPRING = 0x40,
    T500RS_EFFECT_FRICTION = 0x41,
    T500RS_EFFECT_DAMPER = 0x41,
    T500RS_EFFECT_INERTIA = 0x41,
    
    /* Periodic Effects */
    T500RS_EFFECT_SQUARE = 0x20,
    T500RS_EFFECT_SINE = 0x22,
    T500RS_EFFECT_TRIANGLE = 0x21,
    T500RS_EFFECT_SAWTOOTH_UP = 0x23,
    T500RS_EFFECT_SAWTOOTH_DOWN = 0x24,
    T500RS_EFFECT_RAMP = 0x24,
    
    /* Extended Effects */
    T500RS_EFFECT_FRICTION_2 = 0x0c,
    T500RS_EFFECT_DAMPER_2 = 0x0d,
    T500RS_EFFECT_INERTIA_2 = 0x07,
    T500RS_EFFECT_AUTOCENTER = 0x06,
    T500RS_EFFECT_COMBINE = 0x0f
};

/* Weight curve types for combined effects */
enum t500rs_weight_curve {
    T500RS_CURVE_LINEAR = 0,
    T500RS_CURVE_EXPONENTIAL = 1,
    T500RS_CURVE_LOGARITHMIC = 2,
    T500RS_CURVE_SIGMOID = 3,
    T500RS_CURVE_SINE = 4,
    T500RS_CURVE_COSINE = 5,
    T500RS_CURVE_SMOOTH = 6,
    T500RS_CURVE_SMOOTHERSTEP = 7,
    T500RS_CURVE_BOUNCE = 8,
    T500RS_CURVE_ELASTIC = 9,
    T500RS_CURVE_QUADRATIC = 10,
    T500RS_CURVE_CUBIC = 11,
    T500RS_CURVE_PULSE = 12,
    T500RS_CURVE_RAMPHOLD = 13,
    T500RS_CURVE_TRIANGLE = 14,
    T500RS_CURVE_SAWTOOTH = 15,
    T500RS_CURVE_NOISE = 16,
    T500RS_CURVE_SPRING = 17,
    T500RS_CURVE_CUSTOM = 18
};

/* Effect Parameters */
struct t500rs_envelope {
    uint16_t attack_length;
    uint8_t attack_level;
    uint16_t fade_length;
    uint8_t fade_level;
};

struct t500rs_periodic {
    uint8_t waveform;
    uint8_t magnitude;
    uint8_t offset;
    uint16_t period;
    uint8_t phase;
};

struct t500rs_condition {
    uint8_t center;
    uint8_t deadband;
    uint8_t right_coeff;
    uint8_t left_coeff;
    uint8_t right_sat;
    uint8_t left_sat;
};

struct t500rs_weight_params {
    enum t500rs_weight_curve curve_type;
    uint8_t curve_points[8];  /* For custom curves */
    uint8_t curve_strength;
    bool invert;
    union {
        struct {
            uint8_t frequency;
            uint8_t phase;
        } wave;
        struct {
            uint8_t bounce_count;
            uint8_t decay;
        } bounce;
        struct {
            uint8_t elasticity;
            uint8_t damping;
        } elastic;
        struct {
            uint8_t pulse_width;
            uint8_t duty_cycle;
            uint8_t rise_time;
            uint8_t fall_time;
        } pulse;
    };
};

struct t500rs_combined_effect {
    uint8_t num_effects;
    uint8_t effect_ids[16];  /* Max 16 effects */
    uint8_t weights[16];
    bool dynamic_weights;
    struct t500rs_weight_params weight_params[16];
};

struct t500rs_effect_params {
    uint8_t level;          /* Effect strength */
    uint16_t duration;      /* Effect duration in ms */
    struct t500rs_envelope envelope;
    union {
        struct t500rs_periodic periodic;
        struct {
            uint8_t start_level;
            uint8_t end_level;
        } ramp;
        struct t500rs_condition condition;
        struct t500rs_combined_effect combined;
    };
};

typedef struct _T500RSDevice T500RSDevice;

/* Device Management */
T500RSDevice *t500rs_device_new(const char *path);
void t500rs_device_free(T500RSDevice *device);

/* Effect Management */
int t500rs_device_upload_effect(T500RSDevice *device, 
                               enum t500rs_effect_type type,
                               struct t500rs_effect_params *params);
void t500rs_device_start_effect(T500RSDevice *device, int effect_id);
void t500rs_device_stop_effect(T500RSDevice *device, int effect_id);
void t500rs_device_modify_effect(T500RSDevice *device, 
                                int effect_id,
                                struct t500rs_effect_params *params);

/* Combined Effects */
int t500rs_device_upload_combined(T500RSDevice *device,
                                struct t500rs_combined_effect *combined);
int t500rs_device_update_weights(T500RSDevice *device,
                               int effect_id,
                               const uint8_t *weights,
                               uint8_t num_weights);

/* Extended Effects */
int t500rs_device_set_autocenter(T500RSDevice *device, uint8_t strength);
int t500rs_device_set_gain(T500RSDevice *device, uint8_t gain);
