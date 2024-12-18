// SPDX-License-Identifier: GPL-2.0
/*
 * Thrustmaster T500RS Racing Wheel Driver
 *
 * Copyright (c) 2024 Thrustmaster
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "hid-tmt500rs.h"

/* Static function declarations */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state);
static int t500rs_upload_ramp(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state);
static int t500rs_upload_periodic(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state);
static int t500rs_upload_condition(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state, u8 effect_type);
static int t500rs_upload_condition_extended(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state, u8 effect_type);
static int t500rs_upload_combined(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state);
static int t500rs_upload_inertia(struct t500rs_device_entry *t500rs,
        struct t500rs_inertia *params);
static int t500rs_upload_autocenter(struct t500rs_device_entry *t500rs,
        struct t500rs_autocenter *params);
static int t500rs_upload_envelope(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state);
static int t500rs_send_effect(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state, u8 command_id,
        const u8 *params, size_t params_size);
static int t500rs_play_effect(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state);
static int t500rs_stop_effect(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state);
static int t500rs_upload_weight(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state,
        struct t500rs_weight_update *update);
static int t500rs_send_int(struct input_dev *dev, u8 *send_buffer, int *trans);
static int t500rs_upload_custom_int(struct input_dev *dev, u8 *send_buffer, int *trans);
static int t500rs_timer_helper(struct t500rs_device_entry *t500rs);
static enum hrtimer_restart t500rs_timer(struct hrtimer *t);
static int t500rs_upload(struct input_dev *dev,
		struct ff_effect *effect, struct ff_effect *old);
static int t500rs_play(struct input_dev *dev, int effect_id, int value);
static ssize_t spring_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t spring_level_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t damper_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t damper_level_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t friction_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t friction_level_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t range_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t range_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static void t500rs_set_autocenter(struct input_dev *dev, u16 value);
static void t500rs_set_gain(struct input_dev *dev, u16 gain);
static void t500rs_destroy(struct ff_device *ff);
static int t500rs_open(struct input_dev *dev);
static void t500rs_close(struct input_dev *dev);
static int t500rs_create_files(struct hid_device *hdev);
static int t500rs_init(struct hid_device *hdev, const signed short *ff_bits);
static int t500rs_probe(struct hid_device *hdev, const struct hid_device_id *id);
static void t500rs_remove(struct hid_device *hdev);
static __u8 *t500rs_report_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize);

/* Force feedback effect bits supported by the device */
static const signed short t500rs_ff_effects[] = {
    FF_CONSTANT,
    FF_RAMP,
    FF_SPRING,
    FF_DAMPER,
    FF_FRICTION,
    FF_INERTIA,
    FF_PERIODIC,
    FF_SINE,
    FF_TRIANGLE,
    FF_SQUARE,
    FF_SAW_UP,
    FF_SAW_DOWN,
    FF_AUTOCENTER,
    FF_GAIN,
    -1
};

/* Core force feedback effect handling */
static int t500rs_upload_effect(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state)
{
    u8 params[13];
    int ret;

    /* Check if this is a combined effect */
    if (state->combined)
        return t500rs_upload_combined(t500rs, state);

    /* First upload the basic effect */
    switch (state->effect.type) {
    case FF_CONSTANT:
        ret = t500rs_upload_constant(t500rs, state);
        if (ret)
            return ret;
        if (state->effect.u.constant.envelope.attack_length ||
            state->effect.u.constant.envelope.fade_length) {
            ret = t500rs_upload_envelope(t500rs, state);
            if (ret)
                return ret;
        }
        break;
    case FF_RAMP:
        ret = t500rs_upload_ramp(t500rs, state);
        if (ret)
            return ret;
        if (state->effect.u.ramp.envelope.attack_length ||
            state->effect.u.ramp.envelope.fade_length) {
            ret = t500rs_upload_envelope(t500rs, state);
            if (ret)
                return ret;
        }
        break;
    case FF_SPRING:
        ret = t500rs_upload_condition(t500rs, state, T500RS_EFFECT_SPRING);
        break;
    case FF_DAMPER:
        /* Use extended damper by default for better feel */
        ret = t500rs_upload_condition_extended(t500rs, state, T500RS_EFFECT_DAMPER_2);
        break;
    case FF_FRICTION:
        /* Use extended friction by default for better feel */
        ret = t500rs_upload_condition_extended(t500rs, state, T500RS_EFFECT_FRICTION_2);
        break;
    case FF_INERTIA:
        {
            struct t500rs_inertia params = {
                .strength = state->effect.u.condition[0].right_coeff >> 8,
                .damping = state->effect.u.condition[0].left_coeff >> 8,
                .resistance = state->effect.u.condition[0].center >> 8
            };
            ret = t500rs_upload_inertia(t500rs, &params);
        }
        break;
    case FF_PERIODIC:
        ret = t500rs_upload_periodic(t500rs, state);
        break;
    default:
        return -EINVAL;
    }

    /* Set duration if specified */
    if (state->effect.replay.length)
        ret = t500rs_modify_duration(t500rs, state);

    return ret;
}

/* Basic force feedback effects */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state)
{
    u8 params[13];
    int ret;

    /* Set envelope parameters */
    params[0] = T500RS_CMD_SET_ENVELOPE;
    params[1] = 0x1c;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = 0x00;
    params[5] = 0x00;
    params[6] = 0x00;
    params[7] = 0x00;
    params[8] = 0x00;

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_ENVELOPE, params, 9);
    if (ret < 0)
        return ret;

    /* Set constant force parameters */
    params[0] = T500RS_CMD_SET_CONSTANT;
    params[1] = 0x0e;
    params[2] = 0x00;
    params[3] = state->effect.u.constant.level;  /* Force level */

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_CONSTANT, params, 4);
    if (ret < 0)
        return ret;

    /* Upload effect */
    params[0] = T500RS_CMD_UPLOAD_EFFECT;
    params[1] = 0x00;
    params[2] = T500RS_EFFECT_CONSTANT;
    params[3] = 0x40;
    params[4] = 0x17;
    params[5] = 0x25;
    params[6] = 0x00;
    params[7] = 0xff;
    params[8] = 0xff;
    params[9] = 0x0e;
    params[10] = 0x00;
    params[11] = 0x1c;
    params[12] = 0x00;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_UPLOAD_EFFECT, params, 13);
}

static int t500rs_upload_ramp(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state)
{
    u8 params[13];
    int ret;

    /* Set envelope parameters */
    params[0] = T500RS_CMD_SET_ENVELOPE;
    params[1] = 0x1c;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = 0x00;
    params[5] = 0x00;
    params[6] = 0x00;
    params[7] = 0x00;
    params[8] = 0x00;

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_ENVELOPE, params, 9);
    if (ret < 0)
        return ret;

    /* Set ramp force parameters */
    params[0] = T500RS_CMD_SET_RAMP;
    params[1] = 0x0e;
    params[2] = 0x00;
    params[3] = state->effect.u.ramp.start_level;  /* Start level */
    params[4] = state->effect.u.ramp.end_level;  /* End level */

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_RAMP, params, 5);
    if (ret < 0)
        return ret;

    /* Upload effect */
    params[0] = T500RS_CMD_UPLOAD_EFFECT;
    params[1] = 0x00;
    params[2] = T500RS_EFFECT_RAMP;
    params[3] = 0x40;
    params[4] = 0x17;
    params[5] = 0x25;
    params[6] = 0x00;
    params[7] = 0xff;
    params[8] = 0xff;
    params[9] = 0x0e;
    params[10] = 0x00;
    params[11] = 0x1c;
    params[12] = 0x00;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_UPLOAD_EFFECT, params, 13);
}

static int t500rs_upload_periodic(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state)
{
    u8 params[13];
    int ret;
    u8 waveform;

    /* Map effect type to T500RS waveform */
    switch (state->effect.type) {
    case FF_SINE:
        waveform = T500RS_EFFECT_SINE;
        break;
    case FF_SQUARE:
        waveform = T500RS_EFFECT_SQUARE;
        break;
    case FF_TRIANGLE:
        waveform = T500RS_EFFECT_TRIANGLE;
        break;
    case FF_SAW_UP:
        waveform = T500RS_EFFECT_SAWTOOTH_UP;
        break;
    case FF_SAW_DOWN:
        waveform = T500RS_EFFECT_SAWTOOTH_DOWN;
        break;
    default:
        return -EINVAL;
    }

    /* Set envelope parameters */
    params[0] = T500RS_CMD_SET_ENVELOPE;
    params[1] = 0x1c;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = 0x00;
    params[5] = 0x00;
    params[6] = 0x00;
    params[7] = 0x00;
    params[8] = 0x00;

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_ENVELOPE, params, 9);
    if (ret < 0)
        return ret;

    /* Set periodic parameters */
    params[0] = T500RS_CMD_SET_PERIODIC;
    params[1] = 0x0e;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = 0x00;
    params[5] = 0x00;
    params[6] = 0xe8;
    params[7] = 0x03;

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_PERIODIC, params, 8);
    if (ret < 0)
        return ret;

    /* Upload effect */
    params[0] = T500RS_CMD_UPLOAD_EFFECT;
    params[1] = 0x00;
    params[2] = waveform;
    params[3] = 0x40;
    params[4] = 0x17;
    params[5] = 0x25;
    params[6] = 0x00;
    params[7] = 0xff;
    params[8] = 0xff;
    params[9] = 0x0e;
    params[10] = 0x00;
    params[11] = 0x1c;
    params[12] = 0x00;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_UPLOAD_EFFECT, params, 13);
}

/* Condition-based effects */
static int t500rs_upload_condition(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state,
        u8 effect_type)
{
    u8 params[13];
    int ret;

    /* Set condition parameters */
    params[0] = T500RS_CMD_SET_CONDITION;
    params[1] = 0x0e;
    params[2] = 0x00;
    params[3] = 0x64;  /* Center */
    params[4] = 0x64;  /* Deadband */
    params[5] = 0x00;
    params[6] = 0x00;
    params[7] = 0x00;
    params[8] = 0x00;
    params[9] = 0x64;  /* Coefficient */
    params[10] = 0x64;

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_CONDITION, params, 11);
    if (ret < 0)
        return ret;

    /* Set envelope parameters */
    params[0] = T500RS_CMD_SET_ENVELOPE;
    params[1] = 0x1c;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = 0x00;
    params[5] = 0x00;
    params[6] = 0x00;
    params[7] = 0x00;
    params[8] = 0x00;

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_ENVELOPE, params, 9);
    if (ret < 0)
        return ret;

    /* Upload effect */
    params[0] = T500RS_CMD_UPLOAD_EFFECT;
    params[1] = 0x00;
    params[2] = effect_type;
    params[3] = 0x40;
    params[4] = 0x17;
    params[5] = 0x25;
    params[6] = 0x00;
    params[7] = 0xff;
    params[8] = 0xff;
    params[9] = 0x0e;
    params[10] = 0x00;
    params[11] = 0x1c;
    params[12] = 0x00;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_UPLOAD_EFFECT, params, 13);
}

static int t500rs_upload_condition_extended(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state,
        u8 effect_type)
{
    u8 params[15];
    struct ff_effect *effect = &state->effect;
    struct t500rs_condition_extended ext = {0};
    
    /* Convert condition effect parameters */
    ext.basic.right_coeff = effect->u.condition[0].right_coeff >> 8;
    ext.basic.left_coeff = effect->u.condition[0].left_coeff >> 8;
    ext.basic.right_sat = effect->u.condition[0].right_saturation >> 9;
    ext.basic.left_sat = effect->u.condition[0].left_saturation >> 9;
    ext.basic.dead_band = effect->u.condition[0].deadband >> 9;
    ext.basic.center = effect->u.condition[0].center >> 9;

    /* Add extended parameters based on effect type */
    if (effect_type == T500RS_EFFECT_DAMPER_2) {
        ext.velocity_factor = 0x64;     /* Default from captures */
        ext.acceleration_factor = 0x32;  /* Default from captures */
    } else if (effect_type == T500RS_EFFECT_FRICTION_2) {
        ext.position_factor = 0x64;     /* Default from captures */
        ext.velocity_factor = 0x32;     /* Default from captures */
    }

    params[0] = effect_type;
    params[1] = 0x00;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = 0x05;
    params[5] = 0x0e;
    /* Basic parameters */
    params[6] = ext.basic.right_coeff;
    params[7] = ext.basic.left_coeff;
    params[8] = ext.basic.right_sat;
    params[9] = ext.basic.left_sat;
    params[10] = ext.basic.dead_band;
    params[11] = ext.basic.center;
    /* Extended parameters */
    params[12] = ext.velocity_factor;
    params[13] = ext.acceleration_factor;
    params[14] = ext.position_factor;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_UPDATE, params, 15);
}

/* Advanced effects */
static int t500rs_upload_combined(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state)
{
    u8 params[8 + T500RS_MAX_COMBINED_EFFECTS * 3] = {0};  /* Extra byte per effect for flags */
    struct t500rs_combined_effect *combined = state->combined;
    int i;

    if (!combined || combined->num_effects == 0 ||
        combined->num_effects > T500RS_MAX_COMBINED_EFFECTS)
        return -EINVAL;

    params[0] = T500RS_EFFECT_COMBINE;
    params[1] = 0x00;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = 0x05;
    params[5] = 0x0e;
    params[6] = combined->num_effects;
    params[7] = combined->dynamic_weights ? 0x01 : 0x00;

    /* Pack effect IDs, weights, and ranges */
    for (i = 0; i < combined->num_effects; i++) {
        params[8 + i * 3] = combined->effect_ids[i];
        params[9 + i * 3] = combined->weights[i];
        /* Pack min/max into a single byte if dynamic weights enabled */
        if (combined->dynamic_weights) {
            params[10 + i * 3] = (combined->min_weights[i] & 0xF0) |
                                ((combined->max_weights[i] >> 4) & 0x0F);
        }
    }

    return t500rs_send_effect(t500rs, state, T500RS_CMD_UPDATE, 
                            params, 8 + combined->num_effects * 
                            (combined->dynamic_weights ? 3 : 2));
}

static int t500rs_upload_inertia(struct t500rs_device_entry *t500rs,
        struct t500rs_inertia *params)
{
    u8 cmd[8] = {0};
    
    cmd[0] = T500RS_EFFECT_INERTIA;
    cmd[1] = 0x00;
    cmd[2] = 0x00;
    cmd[3] = 0x00;
    cmd[4] = 0x03;
    cmd[5] = 0x0e;
    cmd[6] = params->strength;
    cmd[7] = params->damping;

    return t500rs_send_effect(t500rs, NULL, T500RS_CMD_UPDATE, cmd, 8);
}

static int t500rs_upload_autocenter(struct t500rs_device_entry *t500rs,
        struct t500rs_autocenter *params)
{
    u8 cmd[8] = {0};
    
    cmd[0] = T500RS_EFFECT_AUTOCENTER;
    cmd[1] = 0x00;
    cmd[2] = 0x00;
    cmd[3] = 0x00;
    cmd[4] = 0x03;
    cmd[5] = 0x0e;
    cmd[6] = params->strength;
    cmd[7] = params->coefficient;

    return t500rs_send_effect(t500rs, NULL, T500RS_CMD_UPDATE, cmd, 8);
}

/* Effect modifiers and parameters */
static int t500rs_upload_envelope(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state)
{
    u8 params[13];
    int ret;

    /* Set envelope parameters */
    params[0] = T500RS_CMD_SET_ENVELOPE;
    params[1] = 0x1c;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = 0x00;
    params[5] = 0x00;
    params[6] = 0x00;
    params[7] = 0x00;
    params[8] = 0x00;

    ret = t500rs_send_effect(t500rs, state, T500RS_CMD_SET_ENVELOPE, params, 9);
    if (ret < 0)
        return ret;

    /* Upload effect */
    params[0] = T500RS_CMD_UPLOAD_EFFECT;
    params[1] = 0x00;
    params[2] = state->effect.type;
    params[3] = 0x40;
    params[4] = 0x17;
    params[5] = 0x25;
    params[6] = 0x00;
    params[7] = 0xff;
    params[8] = 0xff;
    params[9] = 0x0e;
    params[10] = 0x00;
    params[11] = 0x1c;
    params[12] = 0x00;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_UPLOAD_EFFECT, params, 13);
}

static int t500rs_upload_weight(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state,
        struct t500rs_weight_update *update)
{
    u8 params[8] = {0};
    struct t500rs_combined_effect *combined = state->combined;
    int i, found = -1;

    if (!combined || !combined->dynamic_weights)
        return -EINVAL;

    /* Find the effect in the combined effect */
    for (i = 0; i < combined->num_effects; i++) {
        if (combined->effect_ids[i] == update->effect_id) {
            found = i;
            break;
        }
    }

    if (found < 0)
        return -EINVAL;

    /* Validate weight against min/max */
    if (update->new_weight < combined->min_weights[found] ||
        update->new_weight > combined->max_weights[found])
        return -EINVAL;

    params[0] = T500RS_WEIGHT_UPDATE;
    params[1] = 0x00;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = update->effect_id;
    params[5] = update->new_weight;
    params[6] = update->smooth_transition ? update->transition_steps : 0;
    params[7] = 0x00;  /* Reserved */

    /* Update the stored weight */
    combined->weights[found] = update->new_weight;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_UPDATE, params, 8);
}

/* Low-level communication */
static int t500rs_send_effect(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state,
        u8 command_id,
        const u8 *params,
        size_t params_size)
{
    u8 *cmd;
    int ret;

    cmd = kzalloc(T500RS_CMD_HEADER_SIZE + params_size, GFP_KERNEL);
    if (!cmd)
        return -ENOMEM;

    /* Copy header */
    memcpy(cmd, t500rs_cmd_header, T500RS_CMD_HEADER_SIZE);
    
    /* Copy parameters if any */
    if (params && params_size)
        memcpy(cmd + T500RS_CMD_HEADER_SIZE, params, params_size);

    ret = t500rs_send_int(t500rs->input_dev, cmd, NULL);
    kfree(cmd);
    return ret;
}

static int t500rs_send_int(struct input_dev *dev, u8 *send_buffer, int *trans)
{
    struct hid_device *hdev = input_get_drvdata(dev);
    struct t500rs_device_entry *t500rs;
    int i;

    t500rs = t500rs_get_device(hdev);
    if (!t500rs) {
        hid_err(hdev, "could not get device\n");
        return -1;
    }

    for (i = 0; i < T500RS_BUFFER_LENGTH; ++i)
        t500rs->ff_field->value[i] = send_buffer[i];

    hid_hw_request(t500rs->hdev, t500rs->report, HID_REQ_SET_REPORT);

    memset(send_buffer, 0, T500RS_BUFFER_LENGTH);

    return 0;
}

static int t500rs_upload_custom_int(struct input_dev *dev, u8 *send_buffer, int *trans){
    struct hid_device *hdev = input_get_drvdata(dev);
    struct t500rs_device_entry *t500rs;
    struct usb_device *usbdev;
    struct usb_interface *usbif;
    struct usb_host_endpoint *ep;
    struct urb *urb = usb_alloc_urb(0, GFP_ATOMIC);
    
    t500rs = t500rs_get_device(hdev);
    if(!t500rs){
        hid_err(hdev, "could not get device\n");
    }

    usbdev = t500rs->usbdev;
    usbif = t500rs->usbif;
    ep = &usbif->cur_altsetting->endpoint[1];

    usb_fill_int_urb(
            urb,
            usbdev,
            usb_sndintpipe(usbdev, 1),
            send_buffer,
            T500RS_BUFFER_LENGTH,
            t500rs_int_callback,
            hdev,
            ep->desc.bInterval
            );

    return usb_submit_urb(urb, GFP_ATOMIC);
}

/* Effect playback control */
static int t500rs_play_effect(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state)
{
    u8 params[8] = {0};
    
    params[0] = T500RS_EFFECT_CONSTANT;
    params[1] = 0x00;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = T500RS_CMD_PLAY;
    params[5] = 0x00;
    params[6] = 0x41;
    params[7] = 0x01;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_PLAY, params, 8);
}

static int t500rs_stop_effect(struct t500rs_device_entry *t500rs,
        struct t500rs_effect_state *state)
{
    u8 params[8] = {0};
    
    params[0] = T500RS_EFFECT_CONSTANT;
    params[1] = 0x00;
    params[2] = 0x00;
    params[3] = 0x00;
    params[4] = T500RS_CMD_PLAY;
    params[5] = 0x00;
    params[6] = 0x00;
    params[7] = 0x01;

    return t500rs_send_effect(t500rs, state, T500RS_CMD_STOP, params, 8);
}

/* Timer and scheduling */
static int t500rs_timer_helper(struct t500rs_device_entry *t500rs)
{
	struct t500rs_effect_state *state;
	unsigned long jiffies_now = JIFFIES2MS(jiffies);
	int max_count = 0, effect_id, ret;

	for (effect_id = 0; effect_id < T500RS_MAX_EFFECTS; ++effect_id) {

		state = &t500rs->states[effect_id];

		if (test_bit(FF_EFFECT_PLAYING, &state->flags) && state->effect.replay.length) {
			if ((jiffies_now - state->start_time) >= state->effect.replay.length) {
				__clear_bit(FF_EFFECT_PLAYING, &state->flags);

				/* lazy bum fix? */
				__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);

				if (state->count)
					state->count--;

				if (state->count)
					__set_bit(FF_EFFECT_QUEUE_START, &state->flags);
			}
		}

		if (test_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags)) {
			__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

			ret = t500rs_upload_effect(t500rs, state);
			if (ret) {
				hid_err(t500rs->hdev, "failed uploading effects");
				return ret;
			}
		}

		if (test_bit(FF_EFFECT_QUEUE_START, &state->flags)) {
			__clear_bit(FF_EFFECT_QUEUE_START, &state->flags);
			__set_bit(FF_EFFECT_PLAYING, &state->flags);

			ret = t500rs_play_effect(t500rs, state);
			if (ret) {
				hid_err(t500rs->hdev, "failed starting effects\n");
				return ret;
			}

		}

		if (test_bit(FF_EFFECT_QUEUE_STOP, &state->flags)) {
			__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
			__clear_bit(FF_EFFECT_PLAYING, &state->flags);

			ret = t500rs_stop_effect(t500rs, state);
			if (ret) {
				hid_err(t500rs->hdev, "failed stopping effect\n");
				return ret;
			}
		}

		if (state->count > max_count)
			max_count = state->count;
	}

	return max_count;
}

static enum hrtimer_restart t500rs_timer(struct hrtimer *t)
{
	struct t500rs_device_entry *t500rs = container_of(t, struct t500rs_device_entry, hrtimer);
	int max_count;

	max_count = t500rs_timer_helper(t500rs);

	if (max_count > 0) {
		hrtimer_forward_now(&t500rs->hrtimer, ms_to_ktime(timer_msecs));
		return HRTIMER_RESTART;
	} else {
		return HRTIMER_NORESTART;
	}
}

/* Device initialization and cleanup */
static int t500rs_upload(struct input_dev *dev,
		struct ff_effect *effect, struct ff_effect *old)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t500rs_device_entry *t500rs;
	struct t500rs_effect_state *state;

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	if (effect->type == FF_PERIODIC && effect->u.periodic.period == 0)
		return -EINVAL;

	state = &t500rs->states[effect->id];

	spin_lock_irqsave(&t500rs->lock, t500rs->lock_flags);

	state->effect = *effect;

	if (old) {
		state->old = *old;
		__set_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
	} else {
		__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
	}
	__set_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

	spin_unlock_irqrestore(&t500rs->lock, t500rs->lock_flags);

	return 0;
}

static int t500rs_play(struct input_dev *dev, int effect_id, int value)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t500rs_device_entry *t500rs;
	struct t500rs_effect_state *state;

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	state = &t500rs->states[effect_id];

	if (&state->effect == 0)
		return 0;

	spin_lock_irqsave(&t500rs->lock, t500rs->lock_flags);

	if (value > 0) {
		state->count = value;
		state->start_time = JIFFIES2MS(jiffies);
		__set_bit(FF_EFFECT_QUEUE_START, &state->flags);

		if (test_bit(FF_EFFECT_QUEUE_STOP, &state->flags))
			__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);

	} else {
		__set_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
	}

	if (!hrtimer_active(&t500rs->hrtimer))
		hrtimer_start(&t500rs->hrtimer, ms_to_ktime(timer_msecs), HRTIMER_MODE_REL);

	spin_unlock_irqrestore(&t500rs->lock, t500rs->lock_flags);
	return 0;
}

static ssize_t spring_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	unsigned int value;
    int ret;

	ret = kstrtouint(buf, 0, &value);
    if (ret) {
        hid_err(hdev, "kstrtouint failed at spring_level_store: %i", ret);
        return ret;
    }

	if (value > 100)
		value = 100;

	spring_level = value;

	return count;
}
static ssize_t spring_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count;

	count = scnprintf(buf, PAGE_SIZE, "%u\n", spring_level);

	return count;
}

static ssize_t damper_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	unsigned int value;
    int ret;

	ret = kstrtouint(buf, 0, &value);
    if (ret) {
        hid_err(hdev, "kstrtouint failed at damper_level_store: %i", ret);
        return ret;
    }

	if (value > 100)
		value = 100;

	damper_level = value;

	return count;
}

static ssize_t damper_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count;

	count = scnprintf(buf, PAGE_SIZE, "%u\n", damper_level);

	return count;
}

static ssize_t friction_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	unsigned int value;
    int ret;

	ret = kstrtouint(buf, 0, &value);
    if (ret) {
        hid_err(hdev, "kstrtouint failed at friction_level_store: %i", ret);
        return ret;
    }

	if (value > 100)
		value = 100;

	friction_level = value;

	return count;
}
static ssize_t friction_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count;

	count = scnprintf(buf, PAGE_SIZE, "%u\n", friction_level);

	return count;
}

static ssize_t range_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct t500rs_device_entry *t500rs;
	u8 *send_buffer;
	unsigned int range;
	int ret, trans;

	ret = kstrtouint(buf, 0, &range);
    if (ret) {
        hid_err(hdev, "kstrtouint failed at range_store: %i", ret);
        return ret;
    }

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	send_buffer = t500rs->send_buffer;

	if (range < 40)
		range = 40;

	if (range > 1080)
		range = 1080;

	range *= 0x3c;


	send_buffer[0] = 0x08;
	send_buffer[1] = 0x11;
	send_buffer[2] = range & 0xff;
	send_buffer[3] = range >> 8;

	ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
	if (ret) {
		hid_err(hdev, "failed sending interrupts\n");
		return -1;
	}

	t500rs->range = range / 0x3c;

    hid_info(hdev, "Current range is [%i] \n", t500rs->range);

	return count;
}

static ssize_t range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct t500rs_device_entry *t500rs;
	size_t count = 0;

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	count = scnprintf(buf, PAGE_SIZE, "%u\n", t500rs->range);
	return count;
}

/* Device attributes for sysfs */
static DEVICE_ATTR_RW(spring_level);
static DEVICE_ATTR_RW(damper_level);
static DEVICE_ATTR_RW(friction_level);
static DEVICE_ATTR_RW(range);

/* Device initialization and cleanup */
static int t500rs_init(struct hid_device *hdev, const signed short *ff_bits)
{
	struct t500rs_device_entry *t500rs;
	struct t500rs_data *drv_data;
	struct list_head *report_list;
	struct hid_input *hidinput = list_entry(hdev->inputs.next,
			struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;
	struct device *dev = &hdev->dev;
	struct usb_interface *usbif = to_usb_interface(dev->parent);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	struct ff_device *ff;
	char range[10] = "1024"; // max
	int i, ret;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "private driver data not allocated\n");
		ret = -ENOMEM;
		goto err;
	}

	t500rs = kzalloc(sizeof(struct t500rs_device_entry), GFP_KERNEL);
	if (!t500rs) {
		ret = -ENOMEM;
		goto t500rs_err;
	}

	t500rs->input_dev = input_dev;
	t500rs->hdev = hdev;
	t500rs->usbdev = usbdev;
	t500rs->usbif = usbif;

	t500rs->states = kzalloc(
			sizeof(struct t500rs_effect_state) * T500RS_MAX_EFFECTS, GFP_KERNEL);

	if (!t500rs->states) {
		ret = -ENOMEM;
		goto states_err;
	}

	t500rs->send_buffer = kzalloc(T500RS_BUFFER_LENGTH, GFP_KERNEL);
	if (!t500rs->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	t500rs->firmware_response = kzalloc(sizeof(struct t500rs_firmware_response), GFP_KERNEL);
	if (!t500rs->firmware_response) {
		ret = -ENOMEM;
		goto firmware_err;
	}

	// Check firmware version
	ret = usb_control_msg(t500rs->usbdev,
			usb_rcvctrlpipe(t500rs->usbdev, 0),
			t500rs_firmware_request.bRequest,
			t500rs_firmware_request.bRequestType,
			t500rs_firmware_request.wValue,
			t500rs_firmware_request.wIndex,
			t500rs->firmware_response,
			t500rs_firmware_request.wLength,
			USB_CTRL_SET_TIMEOUT
			   );

	// Educated guess
    hid_info(hdev, "Current firmware version: %i", t500rs->firmware_response->firmware_version);
	if (t500rs->firmware_response->firmware_version < 31 && ret >= 0) {
		hid_err(t500rs->hdev,
			"firmware version %i is too old, please update.",
			t500rs->firmware_response->firmware_version
			);

		hid_info(t500rs->hdev, "note: this has to be done through Windows.");

		ret = -EINVAL;
		goto out;
	}

	spin_lock_init(&t500rs->lock);

	drv_data->device_props = t500rs;

	report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;

	// because we set the rdesc, we know exactly which report and field to use
	t500rs->report = list_entry(report_list->next, struct hid_report, list);
	t500rs->ff_field = t500rs->report->field[0];

	// set ff capabilities
	for (i = 0; ff_bits[i] >= 0; ++i)
		__set_bit(ff_bits[i], input_dev->ffbit);

	ret = input_ff_create(input_dev, T500RS_MAX_EFFECTS);
	if (ret) {
		hid_err(hdev, "could not create input_ff\n");
		goto out;
	}

	ff = input_dev->ff;
	ff->upload = t500rs_upload;
	ff->playback = t500rs_play;
	ff->set_gain = t500rs_set_gain;
	ff->set_autocenter = t500rs_set_autocenter;
	ff->destroy = t500rs_destroy;

	t500rs->open = input_dev->open;
	t500rs->close = input_dev->close;

	input_dev->open = t500rs_open;
	input_dev->close = t500rs_close;

    hid_info(hdev, "Before create files\n");
	ret = t500rs_create_files(hdev);
	if (ret) {
		// this might not be a catastrophic issue, but it could affect
		// programs such as oversteer, best play it safe
		hid_err(hdev, "could not create sysfs files\n");
		goto out;
	}
    hid_info(hdev, "After create files\n");

    hid_info(hdev, "Before timer init\n");
	hrtimer_init(&t500rs->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	t500rs->hrtimer.function = t500rs_timer;

    hid_info(hdev, "Before range store\n");
	range_store(dev, &dev_attr_range, range, 10);
	t500rs_set_gain(input_dev, 0xffff);

	hid_info(hdev, "force feedback for t500rs\n");
	return 0;

out:
	kfree(t500rs->firmware_response);
firmware_err:
	kfree(t500rs->send_buffer);
send_err:
	kfree(t500rs->states);
states_err:
	kfree(t500rs);
t500rs_err:
	kfree(drv_data);
err:
	hid_err(hdev, "failed creating force feedback device\n");
	return ret;

}

static int t500rs_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct t500rs_data *drv_data;

	spin_lock_init(&lock);

	drv_data = kzalloc(sizeof(struct t500rs_data), GFP_KERNEL);
	if (!drv_data) {
		ret = -ENOMEM;
		goto err;
	}

	drv_data->quirks = id->driver_data;
	hid_set_drvdata(hdev, (void *)drv_data);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err;
	}

	ret = t500rs_init(hdev, (void *)id->driver_data);
	if (ret) {
		hid_err(hdev, "t500rs_init failed\n");
		goto err;
	}
	return 0;
err:
	return ret;
}

static void t500rs_remove(struct hid_device *hdev)
{
	struct t500rs_device_entry *t500rs;
	struct t500rs_data *drv_data;

	drv_data = hid_get_drvdata(hdev);
	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	hrtimer_cancel(&t500rs->hrtimer);

	device_remove_file(&hdev->dev, &dev_attr_range);
	device_remove_file(&hdev->dev, &dev_attr_spring_level);
	device_remove_file(&hdev->dev, &dev_attr_damper_level);
	device_remove_file(&hdev->dev, &dev_attr_friction_level);

	hid_hw_stop(hdev);
	kfree(t500rs->states);
	kfree(t500rs->send_buffer);
	kfree(t500rs->firmware_response);
	kfree(t500rs);
	kfree(drv_data);
}

static __u8 *t500rs_report_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize)
{
    /* Replace the original descriptor with our fixed version */
    return (u8 *)t500_report_descriptor;
}

static const struct hid_device_id t500rs_devices[] = {
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb65e),
		.driver_data = (unsigned long)t500rs_ff_effects},
	{}
};
MODULE_DEVICE_TABLE(hid, t500rs_devices);

static struct hid_driver t500rs_driver = {
	.name = "t500rs",
	.id_table = t500rs_devices,
	.probe = t500rs_probe,
	.remove = t500rs_remove,
	.report_fixup = t500rs_report_fixup,
};
module_hid_driver(t500rs_driver);

MODULE_LICENSE("GPL");

MODULE_AUTHOR("Thrustmaster");
MODULE_DESCRIPTION("Force feedback driver for Thrustmaster T500RS Racing Wheel");
