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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <runtime/mutex.h>
#include <sys/types.h>

#ifndef HID_FIFO_SIZE
#define HID_FIFO_SIZE 4096
#endif
#define HID_FIFO_MASK (HID_FIFO_SIZE-1)

typedef struct {
    uint8_t buf[HID_FIFO_SIZE];
    uint32_t head;
    uint32_t tail;
    bool empty;
    mxr_mutex_t lock;
} mx_hid_fifo_t;

mx_status_t mx_hid_fifo_create(mx_hid_fifo_t** fifo);
void mx_hid_fifo_init(mx_hid_fifo_t* fifo);
size_t mx_hid_fifo_size(mx_hid_fifo_t* fifo);
ssize_t mx_hid_fifo_peek(mx_hid_fifo_t* fifo, uint8_t* out);
ssize_t mx_hid_fifo_read(mx_hid_fifo_t* fifo, uint8_t* buf, size_t len);
ssize_t mx_hid_fifo_write(mx_hid_fifo_t* fifo, const uint8_t* buf, size_t len);

void mx_hid_fifo_dump(mx_hid_fifo_t* fifo);
