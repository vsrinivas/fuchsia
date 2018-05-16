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
#include <pthread.h>
#include <threads.h>
#include <zircon/types.h>

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

struct brcmf_device {
    void* of_node;
    void* parent;
    void* drvdata;
    zx_device_t* zxdev;
};

struct brcmf_pci_device {
    struct brcmf_device dev;
    int vendor;
    int device;
    int irq;
    int bus_number;
    int domain;
    zx_handle_t bti;
    pci_protocol_t pci_proto;
};

struct brcmf_firmware {
    size_t size;
    void* data;
};

struct brcmf_bus* dev_get_drvdata(struct brcmf_device* dev);

void dev_set_drvdata(struct brcmf_device* dev, struct brcmf_bus* bus);

struct brcmfmac_platform_data* dev_get_platdata(struct brcmf_device* dev);

// TODO(cphoenix): Wrap around whatever completion functions exist in PCIE and SDIO.
// TODO(cphoenix): To improve efficiency, analyze which spinlocks only need to protect small
// critical subsections of the completion functions. For those, bring back the individual spinlock.
// Note: This is a pthread_mutex_t instead of mtx_t because mtx_t doesn't implement recursive.
extern pthread_mutex_t irq_callback_lock;

#endif /* BRCMF_DEVICE_H */
