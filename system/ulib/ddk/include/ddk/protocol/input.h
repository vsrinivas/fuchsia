// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

enum {
    INPUT_IOCTL_GET_PROTOCOL = 0,
    INPUT_IOCTL_GET_REPORT_DESC_SIZE = 1,
    INPUT_IOCTL_GET_REPORT_DESC = 2,
    INPUT_IOCTL_GET_NUM_REPORTS = 3,
    INPUT_IOCTL_GET_REPORT_IDS = 4,
    INPUT_IOCTL_GET_REPORT_SIZE = 5,
    INPUT_IOCTL_GET_MAX_REPORTSIZE = 6,

    INPUT_IOCTL_GET_REPORT = 7,
    INPUT_IOCTL_SET_REPORT = 8,
};

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
