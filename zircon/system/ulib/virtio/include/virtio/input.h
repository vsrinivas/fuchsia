// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

enum virtio_input_config_select {
    VIRTIO_INPUT_CFG_UNSET = 0x00,
    VIRTIO_INPUT_CFG_ID_NAME = 0x01,
    VIRTIO_INPUT_CFG_ID_SERIAL = 0x02,
    VIRTIO_INPUT_CFG_ID_DEVIDS  = 0x03,
    VIRTIO_INPUT_CFG_PROP_BITS = 0x10,
    VIRTIO_INPUT_CFG_EV_BITS  = 0x11,
    VIRTIO_INPUT_CFG_ABS_INFO = 0x12,
};

/* These are evdev event types as defined by linux.
 *
 * See linux/include/uapi/linux/input-event-codes.h
 */
enum virtio_input_event_type {
    VIRTIO_INPUT_EV_SYN = 0x00,
    VIRTIO_INPUT_EV_KEY = 0x01,
    VIRTIO_INPUT_EV_REL = 0x02,
    VIRTIO_INPUT_EV_ABS = 0x03,
    VIRTIO_INPUT_EV_MSC = 0x04,
    VIRTIO_INPUT_EV_SW = 0x05,
    VIRTIO_INPUT_EV_LED = 0x11,
    VIRTIO_INPUT_EV_SND = 0x12,
    VIRTIO_INPUT_EV_REP = 0x14,
    VIRTIO_INPUT_EV_FF = 0x15,
    VIRTIO_INPUT_EV_PWR = 0x16,
    VIRTIO_INPUT_EV_FF_STATUS = 0x17,
};

/* To populate 'value' in an EV_KEY event. */
enum virtio_input_key_event_value {
    VIRTIO_INPUT_EV_KEY_RELEASED = 0,
    VIRTIO_INPUT_EV_KEY_PRESSED = 1,
};

/* To populate 'code' in an EV_REL event. */
enum virtio_input_rel_event_code {
    VIRTIO_INPUT_EV_REL_X = 0,
    VIRTIO_INPUT_EV_REL_Y = 1,
    VIRTIO_INPUT_EV_REL_Z = 2,
    VIRTIO_INPUT_EV_REL_RX = 3,
    VIRTIO_INPUT_EV_REL_RY = 4,
    VIRTIO_INPUT_EV_REL_RZ = 5,
    VIRTIO_INPUT_EV_REL_HWHEEL = 6,
    VIRTIO_INPUT_EV_REL_DIAL = 7,
    VIRTIO_INPUT_EV_REL_WHEEL = 8,
    VIRTIO_INPUT_EV_REL_MISC = 9,
};

/* To populate 'code' in an EV_ABS event. */
enum virtio_input_abs_event_code {
    VIRTIO_INPUT_EV_ABS_X = 0,
    VIRTIO_INPUT_EV_ABS_Y = 1,
    VIRTIO_INPUT_EV_ABS_Z = 2,
    VIRTIO_INPUT_EV_ABS_RX = 3,
    VIRTIO_INPUT_EV_ABS_RY = 4,
    VIRTIO_INPUT_EV_ABS_RZ = 5,
};

typedef struct virtio_input_absinfo {
    uint32_t min;
    uint32_t max;
    uint32_t fuzz;
    uint32_t flat;
    uint32_t res;
} __PACKED virtio_input_absinfo_t;

typedef struct virtio_input_devids {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
} __PACKED virtio_input_devids_t;

typedef struct virtio_input_config {
    uint8_t select;
    uint8_t subsel;
    uint8_t size;
    uint8_t reserved[5];
    union {
        char string[128];
        uint8_t bitmap[128];
        virtio_input_absinfo_t abs;
        virtio_input_devids_t ids;
    } u;

} __PACKED virtio_input_config_t;

typedef struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __PACKED virtio_input_event_t;

__END_CDECLS
