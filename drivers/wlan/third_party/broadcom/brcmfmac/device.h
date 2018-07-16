/*
 * Copyright (c) 2018 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef BRCMF_DEVICE_H
#define BRCMF_DEVICE_H

#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/usb.h>
#include <lib/async-loop/loop.h> // to start the worker thread
#include <lib/async/default.h>  // for async_get_default_dispatcher()
#include <lib/async/task.h>     // for async_post_task()
#include <lib/async/time.h>     // for async_now()
#include <pthread.h>
#include <string.h>
#include <sync/completion.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include "debug.h"
#include "usb.h"

#define BACKPLANE_ID_HIGH_REVCODE_HIGH 0x7000
#define BACKPLANE_ID_HIGH_REVCODE_HIGH_SHIFT 8
#define BACKPLANE_ID_HIGH_REVCODE_LOW 0xf
#define BACKPLANE_ID_LOW_INITIATOR 0x80

#define BACKPLANE_TARGET_STATE_LOW_RESET        0x00001
#define BACKPLANE_TARGET_STATE_LOW_REJECT       0x00002
#define BACKPLANE_TARGET_STATE_LOW_CLOCK        0x10000
#define BACKPLANE_TARGET_STATE_LOW_GATED_CLOCKS 0x20000
#define BACKPLANE_TARGET_STATE_HIGH_S_ERROR     0x00001
#define BACKPLANE_TARGET_STATE_HIGH_BUSY        0x00004

#define BACKPLANE_INITIATOR_STATE_REJECT        0x2000000
#define BACKPLANE_INITIATOR_STATE_BUSY          0x1800000
#define BACKPLANE_INITIATOR_STATE_IN_BAND_ERROR 0x0020000
#define BACKPLANE_INITIATOR_STATE_TIMEOUT       0x0040000

#define BC_CORE_CONTROL 0x0408
#define BC_CORE_CONTROL_FGC 0x2
#define BC_CORE_CONTROL_CLOCK 0x1
#define BC_CORE_RESET_CONTROL 0x800
#define BC_CORE_RESET_CONTROL_RESET 0x1
#define BC_CORE_ASYNC_BACKOFF_CAPABILITY_PRESENT 0x40
#define BC_CORE_POWER_CONTROL_RELOAD 0x2
#define BC_CORE_POWER_CONTROL_SHIFT 13

#define BRCMF_ERR_FIRMWARE_UNSUPPORTED (-23)

#define max(a, b) ((a)>(b)?(a):(b))

extern async_dispatcher_t* default_dispatcher;

// This is the function that timer users write to receive callbacks.
typedef void (brcmf_timer_callback_t)(void* data);

typedef struct brcmf_timer_info {
    async_task_t task;
    void* data;
    brcmf_timer_callback_t* callback_function;
    bool scheduled;
    completion_t finished;
    mtx_t lock;
} brcmf_timer_info_t;

void brcmf_timer_init(brcmf_timer_info_t* timer, brcmf_timer_callback_t* callback);

void brcmf_timer_set(brcmf_timer_info_t* timer, zx_duration_t delay);

void brcmf_timer_stop(brcmf_timer_info_t* timer);

static inline bool address_is_multicast(const uint8_t* address) {
    return 1 & *address;
}

static inline bool address_is_broadcast(const uint8_t* address) {
    static uint8_t all_ones[] = {255, 255, 255, 255, 255, 255};
    static_assert(ETH_ALEN == 6, "Oops");
    return !memcmp(address, all_ones, ETH_ALEN);
}

static inline bool address_is_zero(const uint8_t* address) {
    static uint8_t all_zeros[] = {0, 0, 0, 0, 0, 0};
    static_assert(ETH_ALEN == 6, "Oops");
    return !memcmp(address, all_zeros, ETH_ALEN);
}

static inline void fill_with_broadcast_addr(uint8_t* address) {
    memset(address, 0xff, ETH_ALEN);
}

static inline void fill_with_zero_addr(uint8_t* address) {
    memset(address, 0, ETH_ALEN);
}

enum {ADDRESSED_TO_MULTICAST = 1, ADDRESSED_TO_BROADCAST, ADDRESSED_TO_OTHER_HOST};

struct brcmf_bus;

struct brcmf_device {
    void* of_node;
    void* parent;
    struct brcmf_bus* bus;
    zx_device_t* zxdev;
    zx_device_t* child_zxdev;
};

static inline struct brcmf_bus* dev_to_bus(struct brcmf_device* dev) {
    return dev->bus;
}

struct brcmf_usb_interface_descriptor {
    int bInterfaceClass;
    int bInterfaceSubClass;
    int bInterfaceProtocol;
    int bInterfaceNumber;
    int bNumEndpoints;
};

struct brcmf_usb_device {
    usb_speed_t speed;
    struct brcmf_device dev;
    struct {
        int bNumConfigurations;
        int bDeviceClass;
    } descriptor;
};

struct brcmf_endpoint_container {
    usb_endpoint_descriptor_t desc;
};

struct brcmf_usb_altsetting {
    struct brcmf_usb_interface_descriptor desc;
    struct brcmf_endpoint_container* endpoint;
};

struct brcmf_usb_interface {
    struct brcmf_usb_altsetting* altsetting;
    struct brcmf_usb_device* usb_device;
    void* intfdata;
};

struct brcmf_usb_device_id {
    int idVendor;
    int idProduct;
};

struct brcmf_firmware {
    size_t size;
    void* data;
};

struct net_device {
    struct wireless_dev* ieee80211_ptr;
    const struct net_device_ops* netdev_ops;
    const struct ethtool_ops* ethtool_ops;
    uint8_t dev_addr[ETH_ALEN];
    char name[123];
    void* priv;
    uint32_t flags;
    struct {
        int tx_dropped;
        int tx_packets;
        int tx_bytes;
        int rx_packets;
        int rx_bytes;
        int multicast;
        int rx_errors;
        int tx_errors;
    } stats;
    uint32_t features;
    uint32_t needed_headroom;
    void* priv_destructor;
    int reg_state;
    int needs_free_net_device;
};

struct brcmf_bus* dev_to_bus(struct brcmf_device* dev);

// TODO(cphoenix): Wrap around whatever completion functions exist in PCIE and SDIO.
// TODO(cphoenix): To improve efficiency, analyze which spinlocks only need to protect small
// critical subsections of the completion functions. For those, bring back the individual spinlock.
// Note: This is a pthread_mutex_t instead of mtx_t because mtx_t doesn't implement recursive.
extern pthread_mutex_t irq_callback_lock;

struct net_device* brcmf_allocate_net_device(size_t priv_size, const char* name);

void brcmf_free_net_device(struct net_device* dev);

void brcmf_enable_tx(struct net_device* dev);

static inline struct brcmf_usb_device* intf_to_usbdev(const struct brcmf_usb_interface* intf) {
    return intf->usb_device;
}

// TODO(cphoenix): Fix this hack
#define ieee80211_frequency_to_channel(freq) (freq)

bool brcmf_test_and_set_bit_in_array(size_t bit_number, atomic_ulong* addr);

bool brcmf_test_and_clear_bit_in_array(size_t bit_number, atomic_ulong* addr);

bool brcmf_test_bit_in_array(size_t bit_number, atomic_ulong* addr);

void brcmf_clear_bit_in_array(size_t bit_number, atomic_ulong* addr);

void brcmf_set_bit_in_array(size_t bit_number, atomic_ulong* addr);

#endif /* BRCMF_DEVICE_H */
