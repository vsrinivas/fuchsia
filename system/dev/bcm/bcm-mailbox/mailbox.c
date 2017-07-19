// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/bcm-bus.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/platform-device.h>

#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/assert.h>

#include <bcm/bcm28xx.h>
#include <bcm/ioctl.h>

#define BCM_PROPERTY_TAG_GET_MACADDR        (0x00010003)

#define BCM_MAILBOX_REQUEST                 (0x00000000)

#define MAILBOX_MMIO    0

// Preserve columns
// clang-format off
enum mailbox_channel {
    ch_power               = 0,
    ch_framebuffer         = 1,
    ch_vuart               = 2,
    ch_vchic               = 3,
    ch_leds                = 4,
    ch_buttons             = 5,
    ch_touchscreen         = 6,
    ch_unused              = 7,
    ch_propertytags_tovc   = 8,
    ch_propertytags_fromvc = 9,
};

enum bcm_device {
    bcm_dev_sd     = 0,
    bcm_dev_uart0  = 1,
    bcm_dev_uart1  = 2,
    bcm_dev_usb    = 3,
    bcm_dev_i2c0   = 4,
    bcm_dev_i2c1   = 5,
    bcm_dev_i2c2   = 6,
    bcm_dev_spi    = 7,
    bcm_dev_ccp2tx = 8,
};

typedef struct {
    uint32_t buff_size;
    uint32_t code;
} property_tag_header_t;


typedef struct {
    uint32_t tag;
    uint32_t size;
    uint32_t req;
    uint8_t  macid[8];  //note: this is a 6 byte request, but value buffers need to be 32-bit aligned
} property_tag_get_macid_t;
#define BCM_MAILBOX_TAG_GET_MACID   {0x00010003,8,6,{0,0,0,0,0,0,0,0}}

typedef struct {
    uint32_t tag;
    uint32_t size;
    uint32_t valsize;
    uint32_t clockid;
    uint32_t resp;
} property_tag_get_clock_rate_t;
#define BCM_MAILBOX_TAG_GET_CLOCKRATE   {0x00030002,8,4,0,0}

typedef struct {
    uint32_t    tag;
} property_tag_endtag_t;
#define BCM_MAILBOX_TAG_ENDTAG              {0x00000000}

// Must mmap memory on 4k page boundaries. The device doesn't exactly fall on
// a page boundary, so we align it to one.
#define PAGE_MASK_4K (~0xFFF)
#define MAILBOX_PAGE_ADDRESS ((ARMCTRL_0_SBM_BASE + 0x80) & PAGE_MASK_4K)

#define MAILBOX_PHYSICAL_ADDRESS (ARMCTRL_0_SBM_BASE + 0x80)

// The delta between the base of the page and the start of the device.
#define PAGE_REG_DELTA (MAILBOX_PHYSICAL_ADDRESS - MAILBOX_PAGE_ADDRESS)

// Offsets into the mailbox register for various operations.
#define MAILBOX_READ               0
#define MAILBOX_PEEK               2
#define MAILBOX_CONDIG             4
#define MAILBOX_STATUS             6
#define MAILBOX_WRITE              8

// Flags in the mailbox status register to signify state.
#define MAILBOX_FULL               0x80000000
#define MAILBOX_EMPTY              0x40000000

// Carve out 4k of device memory.
#define MAILBOX_REGS_LENGTH        0x1000

#define MAX_MAILBOX_READ_ATTEMPTS  8
#define MAILBOX_IO_DEADLINE_MS     1000

// clang-format on

static volatile uint32_t* mailbox_regs;

// All devices are initially turned off.
static uint32_t power_state = 0x0;

static mx_status_t mailbox_write(const enum mailbox_channel ch, uint32_t value) {
    value = value | ch;

    // Wait for there to be space in the FIFO.
    mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_MSEC(MAILBOX_IO_DEADLINE_MS);
    while (mailbox_regs[MAILBOX_STATUS] & MAILBOX_FULL) {
        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline)
            return MX_ERR_TIMED_OUT;
    }

    // Write the value to the mailbox.
    mailbox_regs[MAILBOX_WRITE] = value;

    return MX_OK;
}

static mx_status_t mailbox_read(enum mailbox_channel ch, uint32_t* result) {
    assert(result);
    uint32_t local_result = 0;
    uint32_t attempts = 0;

    do {
        mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_MSEC(MAILBOX_IO_DEADLINE_MS);
        while (mailbox_regs[MAILBOX_STATUS] & MAILBOX_EMPTY) {
            if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline)
                return MX_ERR_TIMED_OUT;
        }

        local_result = mailbox_regs[MAILBOX_READ];

        attempts++;

    } while ((((local_result)&0xF) != ch) && (attempts < MAX_MAILBOX_READ_ATTEMPTS));

    // The bottom 4 bits represent the channel, shift those away and write the
    // result into the ret parameter.
    *result = (local_result >> 4);

    return attempts < MAX_MAILBOX_READ_ATTEMPTS ? MX_OK : MX_ERR_IO;
}

// Use the Videocore to power on/off devices.
static mx_status_t bcm_vc_poweron(enum bcm_device dev) {
    const uint32_t bit = 1 << dev;
    mx_status_t ret = MX_OK;
    uint32_t new_power_state = power_state | bit;

    if (new_power_state == power_state) {
        // The VideoCore won't return an ACK if we try to enable a device that's
        // already enabled, so we should terminate the control flow here.
        return MX_OK;
    }

    ret = mailbox_write(ch_power, new_power_state << 4);
    if (ret != MX_OK)
        return ret;

    // The Videocore must acknowledge a successful power on.
    uint32_t ack = 0x0;
    ret = mailbox_read(ch_power, &ack);
    if (ret != MX_OK)
        return ret;

    // Preserve the power state of the peripherals.
    power_state = ack;

    if (ack != new_power_state)
        return MX_ERR_IO;

    return MX_OK;
}

static mx_status_t bcm_get_property_tag(uint8_t* buf, const size_t len) {
    mx_status_t ret = MX_OK;
    iotxn_t* txn;

    property_tag_header_t header;
    property_tag_endtag_t endtag = BCM_MAILBOX_TAG_ENDTAG;

    header.buff_size = sizeof(header) + len + sizeof(endtag);
    header.code = BCM_MAILBOX_REQUEST;

    ret = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS | IOTXN_ALLOC_POOL, header.buff_size);
    if (ret != 0)
        return ret;

    iotxn_physmap(txn);
    MX_DEBUG_ASSERT(txn->phys_count == 1);
    mx_paddr_t phys = iotxn_phys(txn);

    uint32_t offset = 0;

    iotxn_copyto(txn, &header, sizeof(header), offset);
    offset += sizeof(header);

    iotxn_copyto(txn, buf, len, offset);
    offset += len;

    iotxn_copyto(txn, &endtag, sizeof(endtag), offset);
    iotxn_cacheop(txn, IOTXN_CACHE_CLEAN, 0, header.buff_size);

    ret = mailbox_write(ch_propertytags_tovc, (phys + BCM_SDRAM_BUS_ADDR_BASE));
    if (ret != MX_OK) {
        goto cleanup_and_exit;
    }

    uint32_t ack = 0x0;
    ret = mailbox_read(ch_propertytags_tovc, &ack);
    if (ret != MX_OK) {
        goto cleanup_and_exit;
    }

    iotxn_cacheop(txn, IOTXN_CACHE_INVALIDATE, 0, header.buff_size);
    iotxn_copyfrom(txn, buf, len, sizeof(header));

cleanup_and_exit:
    iotxn_release(txn);
    return ret;
}

static mx_status_t bcm_get_macid(void* ctx, uint8_t* mac) {
    if (!mac) return MX_ERR_INVALID_ARGS;
    property_tag_get_macid_t tag = BCM_MAILBOX_TAG_GET_MACID;

    mx_status_t ret = bcm_get_property_tag((uint8_t*)&tag, sizeof(tag));

    memcpy(mac, tag.macid, 6);

    return ret;
}

static mx_status_t bcm_get_clock_rate(void* ctx, const uint32_t clockid, uint32_t* res) {
    if (!res) return MX_ERR_INVALID_ARGS;
    property_tag_get_clock_rate_t tag = BCM_MAILBOX_TAG_GET_CLOCKRATE;

    tag.clockid = clockid;

    mx_status_t ret = bcm_get_property_tag((uint8_t*)&tag, sizeof(tag));

    // Make sure that we're getting data back for the clock that we requested.
    if (tag.clockid != clockid) {
        return MX_ERR_IO;
    }

    // Fill in the return parameter;
    *res = tag.resp;

    return ret;
}

static mx_status_t mailbox_device_ioctl(void* ctx, uint32_t op,
                                        const void* in_buf, size_t in_len,
                                        void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_BCM_POWER_ON_USB:
        return bcm_vc_poweron(bcm_dev_usb);
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

static mx_protocol_device_t mailbox_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = mailbox_device_ioctl,
};

static mx_status_t bcm_set_framebuffer(void* ctx, mx_paddr_t addr) {
    mx_status_t ret = mailbox_write(ch_framebuffer, addr + BCM_SDRAM_BUS_ADDR_BASE);
    if (ret != MX_OK)
        return ret;

    uint32_t ack = 0x0;
    return mailbox_read(ch_framebuffer, &ack);
}

static bcm_bus_protocol_ops_t bus_protocol_ops = {
    .get_macid = bcm_get_macid,
    .get_clock_rate = bcm_get_clock_rate,
    .set_framebuffer = bcm_set_framebuffer,
};

static mx_status_t mailbox_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    if (proto_id == MX_PROTOCOL_BCM_BUS) {
        bcm_bus_protocol_t* proto = out;
        proto->ops = &bus_protocol_ops;
        proto->ctx = ctx;
        return MX_OK;
    } else {
        return MX_ERR_NOT_SUPPORTED;
    }
}

static mx_status_t mailbox_add_gpios(void* ctx, uint32_t start, uint32_t count, uint32_t mmio_index,
                                     const uint32_t* irqs, uint32_t irq_count) {
        return MX_ERR_NOT_SUPPORTED;
}

static pbus_interface_ops_t mailbox_bus_ops = {
    .get_protocol = mailbox_get_protocol,
    .add_gpios = mailbox_add_gpios,
};

static mx_status_t mailbox_bind(void* ctx, mx_device_t* parent, void** cookie) {
    platform_device_protocol_t pdev;
    if (device_get_protocol(parent, MX_PROTOCOL_PLATFORM_DEV, &pdev) != MX_OK) {
        return MX_ERR_NOT_SUPPORTED;
    }

    // Carve out some address space for the device -- it's memory mapped.
    uintptr_t mmio_base;
    size_t mmio_size;
    mx_handle_t mmio_handle;
    mx_status_t status = pdev_map_mmio(&pdev, MAILBOX_MMIO, MX_CACHE_POLICY_UNCACHED_DEVICE,
                                       (void **)&mmio_base, &mmio_size, &mmio_handle);
    if (status != MX_OK) {
        printf("mailbox_bind pdev_map_mmio failed %d\n", status);
        return status;
    }

    // The device is actually mapped at some offset into the page.
    mailbox_regs = (uint32_t*)(mmio_base + PAGE_REG_DELTA);

    device_add_args_t vc_rpc_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "bcm-vc-rpc",
        .ops = &mailbox_device_protocol,
        // nothing should bind to this device
        // all interaction will be done via the pbus_interface_t
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &vc_rpc_args, NULL);
    if (status != MX_OK) {
        mx_vmar_unmap(mx_vmar_root_self(), mmio_base, mmio_size);
        mx_handle_close(mmio_handle);
        return status;
    }

    bcm_vc_poweron(bcm_dev_sd);
    bcm_vc_poweron(bcm_dev_usb);
    bcm_vc_poweron(bcm_dev_i2c1);

    pbus_interface_t intf;
    intf.ops = &mailbox_bus_ops;
    intf.ctx = NULL;    // TODO(voydanoff) - add mailbox ctx struct
    pdev_set_interface(&pdev, &intf);

    return MX_OK;
}

static mx_driver_ops_t bcm_mailbox_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = mailbox_bind,
};

MAGENTA_DRIVER_BEGIN(bcm_mailbox, bcm_mailbox_driver_ops, "magenta", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOMM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_BROADCOMM_RPI3),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_BUS_IMPLEMENTOR_DID),
MAGENTA_DRIVER_END(bcm_mailbox)
