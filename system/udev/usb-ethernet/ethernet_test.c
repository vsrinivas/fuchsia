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
#include <ddk/protocol/ethernet.h>

#include <runtime/thread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    mx_device_t* device;
    ethernet_protocol_t* protocol;
} ethernet_test_t;

// this is the beacon that the bootloader sends to bootserver
static const uint8_t beacon[] = {
    0x33, 0x33, 0x00, 0x00, 0x00, 0x01, 0x00, 0x50, 0xB6, 0x17, 0x1C, 0x71, 0x86, 0xDD, 0x60, 0x00,
    0x00, 0x00, 0x00, 0x43, 0x11, 0xFF, 0xFE, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50,
    0xB6, 0xFF, 0xFE, 0x17, 0x1C, 0x71, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x82, 0x32, 0x82, 0x33, 0x00, 0x43, 0x6E, 0x0E, 0x17, 0x42,
    0x77, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x77, 0x77, 0x77, 0x77, 0x00, 0x00, 0x00, 0x00, 0x76, 0x65,
    0x72, 0x73, 0x69, 0x6F, 0x6E, 0x00, 0x2E, 0x31, 0x00, 0x73, 0x65, 0x72, 0x69, 0x61, 0x6C, 0x6E,
    0x6F, 0x00, 0x75, 0x6E, 0x6B, 0x6E, 0x6F, 0x77, 0x6E, 0x00, 0x62, 0x6F, 0x61, 0x72, 0x64, 0x00,
    0x75, 0x6E, 0x6B, 0x6E, 0x6F, 0x77, 0x6E, 0x00, 0x00, 0x00};

static void wait_signal(ethernet_test_t* eth, mx_signals_t signal) {
    mx_signals_t satisfied_signals, satisfiable_signals;
    do {
        _magenta_handle_wait_one(eth->device->event, signal, MX_TIME_INFINITE,
                                 &satisfied_signals, &satisfiable_signals);
    } while ((satisfied_signals & signal) != signal);
}

static int ethernet_read_thread(void* arg) {
    ethernet_test_t* eth = (ethernet_test_t*)arg;

    while (1) {
        wait_signal(eth, DEV_STATE_READABLE);
        uint8_t buffer[2048];
        mx_status_t status = eth->protocol->recv(eth->device, buffer, sizeof(buffer));
        printf("ethernet_read_thread got %d\n", status);
    }
    return 0;
}

static int ethernet_write_thread(void* arg) {
    ethernet_test_t* eth = (ethernet_test_t*)arg;

    while (1) {
        wait_signal(eth, DEV_STATE_WRITABLE);
        mx_status_t status = eth->protocol->send(eth->device, beacon, sizeof(beacon));
        printf("ethernet_write_thread got %d\n", status);
        sleep(1);
    }
    return 0;
}

static mx_status_t ethernet_test_probe(mx_driver_t* driver, mx_device_t* device) {
    ethernet_protocol_t* protocol;
    if (device_get_protocol(device, MX_PROTOCOL_ETHERNET, (void**)&protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    return NO_ERROR;
}

static mx_status_t ethernet_test_bind(mx_driver_t* driver, mx_device_t* device) {
    ethernet_protocol_t* protocol;
    if (device_get_protocol(device, MX_PROTOCOL_ETHERNET, (void**)&protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    ethernet_test_t* eth = calloc(1, sizeof(ethernet_test_t));
    if (!eth)
        return ERR_NO_MEMORY;
    eth->device = device;
    eth->protocol = protocol;

    mxr_thread_t* thread;
    mxr_thread_create(ethernet_read_thread, eth, "ethernet_read_thread", &thread);
    mxr_thread_detach(thread);
    mxr_thread_create(ethernet_write_thread, eth, "ethernet_write_thread", &thread);
    mxr_thread_detach(thread);

    return NO_ERROR;
}

static mx_status_t ethernet_test_unbind(mx_driver_t* drv, mx_device_t* dev) {
    return NO_ERROR;
}

static mx_driver_binding_t binding = {
    .protocol_id = MX_PROTOCOL_ETHERNET,
};

// uncomment BUILTIN_DRIVER below to enable this test driver
mx_driver_t _driver_ethernet_test /* BUILTIN_DRIVER */ = {
    .name = "ethernet_test",
    .ops = {
        .probe = ethernet_test_probe,
        .bind = ethernet_test_bind,
        .unbind = ethernet_test_unbind,
    },
    .binding = &binding,
    .binding_count = 1,
};
