// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

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

// ssize_t ioctl_input_get_protocol(int fd, int* out);
IOCTL_WRAPPER_OUT(ioctl_input_get_protocol, IOCTL_INPUT_GET_PROTOCOL, int);

// ssize_t ioctl_input_get_report_desc_size(int fd, size_t* out);
IOCTL_WRAPPER_OUT(ioctl_input_get_report_desc_size, IOCTL_INPUT_GET_REPORT_DESC_SIZE, size_t);

// ssize_t ioctl_input_get_report_desc(int fd, void* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_input_get_report_desc, IOCTL_INPUT_GET_REPORT_DESC, void);

// ssize_t ioctl_input_get_num_reports(int fd, size_t* out);
IOCTL_WRAPPER_OUT(ioctl_input_get_num_reports, IOCTL_INPUT_GET_NUM_REPORTS, size_t);

// ssize_t ioctl_input_get_report_ids(int fd, input_report_id_t* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_input_get_report_ids, IOCTL_INPUT_GET_REPORT_IDS, input_report_id_t);

// ssize_t ioctl_input_get_report_size(int fd, const input_get_report_size_t* in,
//                                     input_report_size_t* out);
IOCTL_WRAPPER_INOUT(ioctl_input_get_report_size, IOCTL_INPUT_GET_REPORT_SIZE,
        input_get_report_size_t, input_report_size_t);

// ssize_t ioctl_input_get_max_reportsize(int fd, input_report_size_t* out);
IOCTL_WRAPPER_OUT(ioctl_input_get_max_reportsize, IOCTL_INPUT_GET_MAX_REPORTSIZE, input_report_size_t);

// ssize_t ioctl_input_get_report(int fd, const input_get_report_t* in,
//                                void* out, size_t out_len);
IOCTL_WRAPPER_IN_VAROUT(ioctl_input_get_report, IOCTL_INPUT_GET_REPORT,
        input_get_report_t, void);

// ssize_t ioctl_input_set_report(int fd, const input_set_report_t* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_input_set_report, IOCTL_INPUT_SET_REPORT, input_set_report_t);
