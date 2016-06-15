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

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/char.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <runtime/thread.h>
#include <runtime/mutex.h>

#define FIFOSIZE 256
#define FIFOMASK (FIFOSIZE - 1)

static struct {
    uint8_t data[FIFOSIZE];
    uint32_t head;
    uint32_t tail;
    mxr_mutex_t lock;
} fifo = {
    .lock = MXR_MUTEX_INIT,
};

static mx_status_t fifo_read(uint8_t* out) {
    if (fifo.head == fifo.tail) {
        return -1;
    }
    *out = fifo.data[fifo.tail];
    fifo.tail = (fifo.tail + 1) & FIFOMASK;
    return NO_ERROR;
}

static void fifo_write(uint8_t x) {
    uint32_t next = (fifo.head + 1) & FIFOMASK;
    if (next != fifo.tail) {
        fifo.data[fifo.head] = x;
        fifo.head = next;
    }
}

static int debug_reader(void* arg) {
    mx_device_t* dev = arg;
    uint8_t ch;
    printf("debug_reader()\n");
    for (;;) {
        if (_magenta_debug_read((void*)&ch, 1) == 1) {
            mxr_mutex_lock(&fifo.lock);
            if (fifo.head == fifo.tail) {
                device_state_set(dev, DEV_STATE_READABLE);
            }
            fifo_write(ch);
            mxr_mutex_unlock(&fifo.lock);
        }
    }
    return 0;
}

static ssize_t console_read(mx_device_t* dev, void* buf, size_t count) {
    uint8_t* data = buf;
    mxr_mutex_lock(&fifo.lock);
    while (count-- > 0) {
        if (fifo_read(data)) break;
        data++;
    }
    if (fifo.head == fifo.tail) {
        device_state_clr(dev, DEV_STATE_READABLE);
    }
    mxr_mutex_unlock(&fifo.lock);
    return data - (uint8_t*)buf;
}

static ssize_t console_write(mx_device_t* dev, const void* buf, size_t count) {
    return _magenta_debug_write(buf, count);
}

static mx_protocol_char_t console_char_proto = {
    .read = console_read,
    .write = console_write,
};

static mx_status_t console_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t console_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t console_release(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_protocol_device_t console_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = console_open,
    .close = console_close,
    .release = console_release,
};

mx_status_t console_init(mx_driver_t* driver) {
    mx_device_t* dev;
    printf("console_init()\n");
    if (device_create(&dev, driver, "console", &console_device_proto) == NO_ERROR) {
        dev->protocol_id = MX_PROTOCOL_CHAR;
        dev->protocol_ops = &console_char_proto;
        if (device_add(dev, NULL) < 0) {
            free(dev);
        } else {
            mxr_thread_t* t;
            mxr_thread_create(debug_reader, dev, "debug-reader", &t);
        }
    }
    return NO_ERROR;
}

mx_driver_t _driver_console BUILTIN_DRIVER = {
    .name = "console",
    .ops = {
        .init = console_init,
    },
};
