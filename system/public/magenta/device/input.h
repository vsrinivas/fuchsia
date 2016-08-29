// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/device/ioctl.h>

#define IOCTL_INPUT_GET_PROTOCOL \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 0)
#define IOCTL_INPUT_GET_REPORT_DESC_SIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 1)
#define IOCTL_INPUT_GET_REPORT_DESC \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 2)
#define IOCTL_INPUT_GET_NUM_REPORTS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 3)
#define IOCTL_INPUT_GET_REPORT_IDS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 4)
#define IOCTL_INPUT_GET_REPORT_SIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 5)
#define IOCTL_INPUT_GET_MAX_REPORTSIZE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 6)
#define IOCTL_INPUT_GET_REPORT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 7)
#define IOCTL_INPUT_SET_REPORT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INPUT, 8)

enum {
    INPUT_PROTO_NONE = 0,
    INPUT_PROTO_KBD = 1,
    INPUT_PROTO_MOUSE = 2,
};

enum {
    INPUT_REPORT_INPUT = 1,
    INPUT_REPORT_OUTPUT = 2,
    INPUT_REPORT_FEATURE = 3,
};

typedef uint8_t input_report_id_t;
typedef uint8_t input_report_type_t;
typedef uint16_t input_report_size_t;

typedef struct input_get_report_size {
    input_report_id_t id;
    input_report_type_t type;
} input_get_report_size_t;

typedef struct input_get_report {
    input_report_id_t id;
    input_report_type_t type;
} input_get_report_t;

typedef struct input_set_report {
    input_report_id_t id;
    input_report_type_t type;
    uint8_t data[];
} input_set_report_t;

typedef struct boot_kbd_report {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t usage[6];
} __attribute__((packed)) boot_kbd_report_t;

typedef struct boot_mouse_report {
    uint8_t buttons;
    int8_t rel_x;
    int8_t rel_y;
} __attribute__((packed)) boot_mouse_report_t;

extern const boot_kbd_report_t report_err_rollover;
