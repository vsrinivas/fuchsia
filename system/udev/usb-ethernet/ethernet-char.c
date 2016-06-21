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
#include <ddk/binding.h>
#include <ddk/protocol/char.h>
#include <ddk/protocol/ethernet.h>

#include <runtime/thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    mx_device_t* eth_device;
    mx_device_t char_device;
    ethernet_protocol_t* eth_protocol;
    size_t mtu;
    uint8_t mac_addr[6];
} ethernet_char_t;
#define get_eth_device(dev) containerof(dev, ethernet_char_t, char_device)

static mx_status_t ethernet_char_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t ethernet_char_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t ethernet_char_release(mx_device_t* device) {
    ethernet_char_t* eth = get_eth_device(device);
    free(eth);
    return NO_ERROR;
}

static mx_protocol_device_t ethernet_char_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = ethernet_char_open,
    .close = ethernet_char_close,
    .release = ethernet_char_release,
};

static ssize_t ethernet_char_read(mx_device_t* device, void* buf, size_t count) {
    ethernet_char_t* eth = get_eth_device(device);

    // special case reading MAC address
    if (count == sizeof(eth->mac_addr)) {
        memcpy(buf, eth->mac_addr, count);
        return count;
    }
    if (count < eth->mtu) {
        return ERR_NOT_ENOUGH_BUFFER;
    }
    return eth->eth_protocol->recv(eth->eth_device, buf, count);
}

static ssize_t ethernet_char_write(mx_device_t* device, const void* buf, size_t count) {
    ethernet_char_t* eth = get_eth_device(device);
    return eth->eth_protocol->send(eth->eth_device, buf, count);
}

static mx_protocol_char_t ethernet_char_proto = {
    .read = ethernet_char_read,
    .write = ethernet_char_write,
};

static mx_status_t ethernet_char_bind(mx_driver_t* driver, mx_device_t* device) {
    ethernet_protocol_t* eth_protocol;
    if (device_get_protocol(device, MX_PROTOCOL_ETHERNET, (void**)&eth_protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    ethernet_char_t* eth = calloc(1, sizeof(ethernet_char_t));
    if (!eth)
        return ERR_NO_MEMORY;
    eth->eth_device = device;
    eth->eth_protocol = eth_protocol;

    eth->mtu = eth_protocol->get_mtu(device);
    eth_protocol->get_mac_addr(device, eth->mac_addr);

    mx_status_t status = device_init(&eth->char_device, driver, "ethernet_char",
                                     &ethernet_char_device_proto);
    if (status != NO_ERROR) {
        free(eth);
        return status;
    }
    eth->char_device.protocol_id = MX_PROTOCOL_CHAR;
    eth->char_device.protocol_ops = &ethernet_char_proto;

    // duplicate ethernet device status to retweet readable/writable events
    mx_handle_t event_handle = _magenta_handle_duplicate(device->event);
    if (event_handle < 0) {
        free(eth);
        return event_handle;
    }
    eth->char_device.event = event_handle;
    device_add(&eth->char_device, device);

    return NO_ERROR;
}

static mx_status_t ethernet_char_unbind(mx_driver_t* drv, mx_device_t* dev) {
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ETHERNET),
};

mx_driver_t _driver_ethernet_char BUILTIN_DRIVER = {
    .name = "ethernet_char",
    .ops = {
        .bind = ethernet_char_bind,
        .unbind = ethernet_char_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
