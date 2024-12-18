#include "device.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <linux/input.h>
#include <linux/uinput.h>

/* Protocol Commands */
#define T500RS_CMD_START_EFFECT    0x41
#define T500RS_CMD_STOP_EFFECT     0x41
#define T500RS_CMD_UPLOAD_EFFECT   0x01
#define T500RS_CMD_MODIFY_EFFECT   0x02
#define T500RS_CMD_SET_ENVELOPE    0x02
#define T500RS_CMD_SET_CONSTANT    0x03
#define T500RS_CMD_SET_PERIODIC    0x04
#define T500RS_CMD_SET_CONDITION   0x05
#define T500RS_CMD_UPDATE_WEIGHTS  0x06

/* Protocol Constants */
#define T500RS_MAX_EFFECTS    16
#define T500RS_PACKET_SIZE    64
#define T500RS_HEADER_SIZE    23

struct _T500RSDevice {
    int fd;
    uint8_t effect_ids[T500RS_MAX_EFFECTS];  /* Track active effects */
    uint8_t send_buffer[T500RS_PACKET_SIZE];
};

/* Helper Functions */
static int t500rs_write_raw(T500RSDevice *device, const uint8_t *data, size_t len)
{
    if (write(device->fd, data, len) != len) {
        fprintf(stderr, "Failed to write to device: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* Device Management */
T500RSDevice *t500rs_device_new(const char *path)
{
    T500RSDevice *device = g_new0(T500RSDevice, 1);
    device->fd = open(path, O_RDWR);
    if (device->fd < 0) {
        g_free(device);
        fprintf(stderr, "Failed to open device: %s\n", strerror(errno));
        return NULL;
    }
    return device;
}

void t500rs_device_free(T500RSDevice *device)
{
    if (device) {
        if (device->fd >= 0)
            close(device->fd);
        g_free(device);
    }
}

/* Effect Management */
static int t500rs_upload_envelope(T500RSDevice *device, 
                                int effect_id,
                                const struct t500rs_envelope *envelope)
{
    uint8_t *data = device->send_buffer;
    
    data[0] = T500RS_CMD_SET_ENVELOPE;
    data[1] = effect_id;
    data[2] = envelope->attack_length & 0xFF;
    data[3] = (envelope->attack_length >> 8) & 0xFF;
    data[4] = envelope->attack_level;
    data[5] = envelope->fade_length & 0xFF;
    data[6] = (envelope->fade_length >> 8) & 0xFF;
    data[7] = envelope->fade_level;
    
    return t500rs_write_raw(device, data, 8);
}

static int t500rs_upload_periodic(T500RSDevice *device,
                                int effect_id,
                                const struct t500rs_periodic *periodic)
{
    uint8_t *data = device->send_buffer;
    
    data[0] = T500RS_CMD_SET_PERIODIC;
    data[1] = effect_id;
    data[2] = periodic->waveform;
    data[3] = periodic->magnitude;
    data[4] = periodic->offset;
    data[5] = periodic->period & 0xFF;
    data[6] = (periodic->period >> 8) & 0xFF;
    data[7] = periodic->phase;
    
    return t500rs_write_raw(device, data, 8);
}

static int t500rs_upload_condition(T500RSDevice *device,
                                 int effect_id,
                                 const struct t500rs_condition *condition)
{
    uint8_t *data = device->send_buffer;
    
    data[0] = T500RS_CMD_SET_CONDITION;
    data[1] = effect_id;
    data[2] = condition->center;
    data[3] = condition->deadband;
    data[4] = condition->right_coeff;
    data[5] = condition->left_coeff;
    data[6] = condition->right_sat;
    data[7] = condition->left_sat;
    
    return t500rs_write_raw(device, data, 8);
}

int t500rs_device_upload_effect(T500RSDevice *device, 
                               enum t500rs_effect_type type,
                               struct t500rs_effect_params *params)
{
    uint8_t *data = device->send_buffer;
    int effect_id = -1;
    int ret;

    /* Find free effect slot */
    for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
        if (!device->effect_ids[i]) {
            effect_id = i;
            break;
        }
    }
    
    if (effect_id < 0)
        return -ENOMEM;

    /* Basic effect header */
    data[0] = T500RS_CMD_UPLOAD_EFFECT;
    data[1] = effect_id;
    data[2] = type;
    data[3] = params->level;
    data[4] = params->duration & 0xFF;
    data[5] = (params->duration >> 8) & 0xFF;

    /* Upload envelope if present */
    if (params->envelope.attack_length || params->envelope.fade_length) {
        ret = t500rs_upload_envelope(device, effect_id, &params->envelope);
        if (ret < 0)
            return ret;
    }

    /* Effect-specific parameters */
    switch (type) {
    case T500RS_EFFECT_CONSTANT:
        data[6] = params->level;
        ret = t500rs_write_raw(device, data, 7);
        break;
        
    case T500RS_EFFECT_PERIODIC:
        ret = t500rs_upload_periodic(device, effect_id, &params->periodic);
        break;
        
    case T500RS_EFFECT_SPRING:
    case T500RS_EFFECT_FRICTION:
    case T500RS_EFFECT_DAMPER:
    case T500RS_EFFECT_INERTIA:
    case T500RS_EFFECT_FRICTION_2:
    case T500RS_EFFECT_DAMPER_2:
    case T500RS_EFFECT_INERTIA_2:
        ret = t500rs_upload_condition(device, effect_id, &params->condition);
        break;
        
    case T500RS_EFFECT_COMBINE:
        ret = t500rs_device_upload_combined(device, &params->combined);
        break;
        
    default:
        return -EINVAL;
    }

    if (ret >= 0)
        device->effect_ids[effect_id] = 1;

    return ret < 0 ? ret : effect_id;
}

void t500rs_device_start_effect(T500RSDevice *device, int effect_id)
{
    uint8_t data[8] = {0};
    
    data[0] = T500RS_CMD_START_EFFECT;
    data[1] = effect_id;
    data[2] = 0x41;  /* Start command */
    
    t500rs_write_raw(device, data, 3);
}

void t500rs_device_stop_effect(T500RSDevice *device, int effect_id)
{
    uint8_t data[8] = {0};
    
    data[0] = T500RS_CMD_STOP_EFFECT;
    data[1] = effect_id;
    data[2] = 0x00;  /* Stop command */
    
    t500rs_write_raw(device, data, 3);
    device->effect_ids[effect_id] = 0;
}

void t500rs_device_modify_effect(T500RSDevice *device, 
                                int effect_id,
                                struct t500rs_effect_params *params)
{
    uint8_t data[8] = {0};
    
    data[0] = T500RS_CMD_MODIFY_EFFECT;
    data[1] = effect_id;
    data[2] = params->level;
    
    t500rs_write_raw(device, data, 3);
}

/* Combined Effects */
int t500rs_device_upload_combined(T500RSDevice *device,
                                struct t500rs_combined_effect *combined)
{
    uint8_t *data = device->send_buffer;
    int effect_id = -1;
    
    /* Find free effect slot */
    for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
        if (!device->effect_ids[i]) {
            effect_id = i;
            break;
        }
    }
    
    if (effect_id < 0)
        return -ENOMEM;

    /* Upload combined effect header */
    data[0] = T500RS_CMD_UPLOAD_EFFECT;
    data[1] = effect_id;
    data[2] = T500RS_EFFECT_COMBINE;
    data[3] = combined->num_effects;
    data[4] = combined->dynamic_weights ? 1 : 0;
    
    /* Copy effect IDs and weights */
    for (int i = 0; i < combined->num_effects; i++) {
        data[5 + i] = combined->effect_ids[i];
        data[5 + combined->num_effects + i] = combined->weights[i];
    }
    
    int ret = t500rs_write_raw(device, data, 5 + 2 * combined->num_effects);
    if (ret < 0)
        return ret;

    /* Upload weight curves if dynamic weights enabled */
    if (combined->dynamic_weights) {
        for (int i = 0; i < combined->num_effects; i++) {
            const struct t500rs_weight_params *params = &combined->weight_params[i];
            
            data[0] = T500RS_CMD_UPDATE_WEIGHTS;
            data[1] = effect_id;
            data[2] = i;  /* Weight index */
            data[3] = params->curve_type;
            data[4] = params->curve_strength;
            data[5] = params->invert ? 1 : 0;
            
            /* Copy curve-specific parameters */
            switch (params->curve_type) {
            case T500RS_CURVE_CUSTOM:
                memcpy(&data[6], params->curve_points, 8);
                ret = t500rs_write_raw(device, data, 14);
                break;
                
            case T500RS_CURVE_SINE:
            case T500RS_CURVE_COSINE:
            case T500RS_CURVE_TRIANGLE:
            case T500RS_CURVE_SAWTOOTH:
                data[6] = params->wave.frequency;
                data[7] = params->wave.phase;
                ret = t500rs_write_raw(device, data, 8);
                break;
                
            case T500RS_CURVE_BOUNCE:
                data[6] = params->bounce.bounce_count;
                data[7] = params->bounce.decay;
                ret = t500rs_write_raw(device, data, 8);
                break;
                
            case T500RS_CURVE_ELASTIC:
                data[6] = params->elastic.elasticity;
                data[7] = params->elastic.damping;
                ret = t500rs_write_raw(device, data, 8);
                break;
                
            case T500RS_CURVE_PULSE:
                data[6] = params->pulse.pulse_width;
                data[7] = params->pulse.duty_cycle;
                data[8] = params->pulse.rise_time;
                data[9] = params->pulse.fall_time;
                ret = t500rs_write_raw(device, data, 10);
                break;
                
            default:
                ret = t500rs_write_raw(device, data, 6);
                break;
            }
            
            if (ret < 0)
                return ret;
        }
    }

    device->effect_ids[effect_id] = 1;
    return effect_id;
}

int t500rs_device_update_weights(T500RSDevice *device,
                               int effect_id,
                               const uint8_t *weights,
                               uint8_t num_weights)
{
    uint8_t *data = device->send_buffer;
    
    if (num_weights > T500RS_MAX_EFFECTS)
        return -EINVAL;
        
    data[0] = T500RS_CMD_UPDATE_WEIGHTS;
    data[1] = effect_id;
    data[2] = num_weights;
    memcpy(&data[3], weights, num_weights);
    
    return t500rs_write_raw(device, data, 3 + num_weights);
}

/* Extended Effects */
int t500rs_device_set_autocenter(T500RSDevice *device, uint8_t strength)
{
    uint8_t data[8] = {0};
    
    data[0] = T500RS_EFFECT_AUTOCENTER;
    data[1] = strength;
    
    return t500rs_write_raw(device, data, 2);
}

int t500rs_device_set_gain(T500RSDevice *device, uint8_t gain)
{
    uint8_t data[8] = {0};
    
    data[0] = 0x02;  /* Set gain command */
    data[1] = gain;
    
    return t500rs_write_raw(device, data, 2);
}
