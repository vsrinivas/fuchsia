// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

enum {
    INPUT_IOCTL_GET_PROTOCOL = 0,
    INPUT_IOCTL_GET_REPORT_DESC_SIZE = 1,
    INPUT_IOCTL_GET_REPORT_DESC = 2,
    INPUT_IOCTL_GET_NUM_REPORTS = 3,
    INPUT_IOCTL_GET_REPORT_IDS = 4,
    INPUT_IOCTL_GET_REPORT_SIZE = 5,
    INPUT_IOCTL_GET_MAX_REPORTSIZE = 6,
};

enum {
    INPUT_PROTO_NONE = 0,
    INPUT_PROTO_KBD = 1,
    INPUT_PROTO_MOUSE = 2,
};

typedef uint8_t input_report_id_t;
typedef uint16_t input_report_size_t;

typedef struct input_report {
    uint8_t id;      // report id from the HID descriptor
    uint8_t data[];  // report data from the device; length can be determined by
                     // using the GET_REPORT_SIZE ioctl for the id
} input_report_t;
