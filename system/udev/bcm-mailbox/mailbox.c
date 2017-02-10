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
#include <ddk/protocol/bcm.h>
#include <ddk/protocol/display.h>

#include <magenta/syscalls.h>

#include "../bcm-common/bcm28xx.h"

#define BCM_PROPERTY_TAG_GET_MACADDR        (0x00010003)

#define BCM_MAILBOX_REQUEST                 (0x00000000)

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
    bcm_dev_sd    = 0,
    bcm_dev_uart0 = 1,
    bcm_dev_uart1 = 2,
    bcm_dev_usb   = 3,
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

static bcm_fb_desc_t bcm_vc_framebuffer;
static uint8_t* vc_framebuffer = (uint8_t*)NULL;

static mx_device_t disp_device;
static mx_display_info_t disp_info;

static mx_status_t mailbox_write(const enum mailbox_channel ch, uint32_t value) {
    value = value | ch;

    // Wait for there to be space in the FIFO.
    mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_MSEC(MAILBOX_IO_DEADLINE_MS);
    while (mailbox_regs[MAILBOX_STATUS] & MAILBOX_FULL) {
        if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline)
            return ERR_TIMED_OUT;
    }

    // Write the value to the mailbox.
    mailbox_regs[MAILBOX_WRITE] = value;

    return NO_ERROR;
}

static mx_status_t mailbox_read(enum mailbox_channel ch, uint32_t* result) {
    assert(result);
    uint32_t local_result = 0;
    uint32_t attempts = 0;

    do {
        mx_time_t deadline = mx_time_get(MX_CLOCK_MONOTONIC) + MX_MSEC(MAILBOX_IO_DEADLINE_MS);
        while (mailbox_regs[MAILBOX_STATUS] & MAILBOX_EMPTY) {
            if (mx_time_get(MX_CLOCK_MONOTONIC) > deadline)
                return ERR_TIMED_OUT;
        }

        local_result = mailbox_regs[MAILBOX_READ];

        attempts++;

    } while ((((local_result)&0xF) != ch) && (attempts < MAX_MAILBOX_READ_ATTEMPTS));

    // The bottom 4 bits represent the channel, shift those away and write the
    // result into the ret parameter.
    *result = (local_result >> 4);

    return attempts < MAX_MAILBOX_READ_ATTEMPTS ? NO_ERROR : ERR_IO;
}

static mx_status_t bcm_vc_get_framebuffer(bcm_fb_desc_t* fb_desc) {
    mx_status_t ret = NO_ERROR;
    iotxn_t* txn;

    if (!vc_framebuffer) {

        // buffer needs to be aligned on 16 byte boundary, pad the alloc to make sure we have room to adjust
        const size_t txnsize = sizeof(bcm_fb_desc_t) + 16;
        ret = iotxn_alloc(&txn, 0, txnsize, 0);
        if (ret < 0)
            return ret;

        mx_paddr_t pa;

        txn->ops->physmap(txn, &pa);

        // calculate offset in buffer that will provide 16 byte alignment (physical)
        uint32_t offset = (16 - (pa % 16)) % 16;

        txn->ops->copyto(txn, fb_desc, sizeof(bcm_fb_desc_t), offset);
        txn->ops->cacheop(txn, IOTXN_CACHE_CLEAN, 0, txnsize);

        ret = mailbox_write(ch_framebuffer, (pa + offset + BCM_SDRAM_BUS_ADDR_BASE));
        if (ret != NO_ERROR)
            return ret;

        uint32_t ack = 0x0;
        ret = mailbox_read(ch_framebuffer, &ack);
        if (ret != NO_ERROR)
            return ret;

        txn->ops->cacheop(txn, IOTXN_CACHE_INVALIDATE, 0, txnsize);
        txn->ops->copyfrom(txn, &bcm_vc_framebuffer, sizeof(bcm_fb_desc_t), offset);

        uintptr_t page_base;

        // map framebuffer into userspace
        mx_mmap_device_memory(
            get_root_resource(),
            bcm_vc_framebuffer.fb_p & 0x3fffffff, bcm_vc_framebuffer.fb_size,
            MX_CACHE_POLICY_CACHED, &page_base);
        vc_framebuffer = (uint8_t*)page_base;
        memset(vc_framebuffer, 0x00, bcm_vc_framebuffer.fb_size);

        txn->ops->release(txn);
    }
    memcpy(fb_desc, &bcm_vc_framebuffer, sizeof(bcm_fb_desc_t));
    return sizeof(bcm_fb_desc_t);
}

void vc_flush_framebuffer(mx_device_t* dev) {
    mx_cache_flush(vc_framebuffer, bcm_vc_framebuffer.fb_size,
                   MX_CACHE_FLUSH_DATA);
}

// Use the Videocore to power on/off devices.
static mx_status_t bcm_vc_poweron(enum bcm_device dev) {
    const uint32_t bit = 1 << dev;
    mx_status_t ret = NO_ERROR;
    uint32_t new_power_state = power_state | bit;

    if (new_power_state == power_state) {
        // The VideoCore won't return an ACK if we try to enable a device that's
        // already enabled, so we should terminate the control flow here.
        return NO_ERROR;
    }

    ret = mailbox_write(ch_power, new_power_state << 4);
    if (ret != NO_ERROR)
        return ret;

    // The Videocore must acknowledge a successful power on.
    uint32_t ack = 0x0;
    ret = mailbox_read(ch_power, &ack);
    if (ret != NO_ERROR)
        return ret;

    // Preserve the power state of the peripherals.
    power_state = ack;

    if (ack != new_power_state)
        return ERR_IO;

    return NO_ERROR;
}

static mx_status_t bcm_get_property_tag(uint8_t* buf, const size_t len) {
    mx_status_t ret = NO_ERROR;
    iotxn_t* txn;

    property_tag_header_t header;
    property_tag_endtag_t endtag = BCM_MAILBOX_TAG_ENDTAG;

    header.buff_size = sizeof(header) + len + sizeof(endtag);
    header.code = BCM_MAILBOX_REQUEST;

    ret = iotxn_alloc(&txn, 0, header.buff_size, 0);
    if (ret != 0)
        return ret;

    mx_paddr_t pa;

    txn->ops->physmap(txn, &pa);

    uint32_t offset = 0;

    txn->ops->copyto(txn, &header, sizeof(header), offset);
    offset += sizeof(header);

    txn->ops->copyto(txn, buf, len, offset);
    offset += len;

    txn->ops->copyto(txn, &endtag, sizeof(endtag), offset);
    txn->ops->cacheop(txn, IOTXN_CACHE_CLEAN, 0, header.buff_size);

    ret = mailbox_write(ch_propertytags_tovc, (pa + BCM_SDRAM_BUS_ADDR_BASE));
    if (ret != NO_ERROR) {
        goto cleanup_and_exit;
    }

    uint32_t ack = 0x0;
    ret = mailbox_read(ch_propertytags_tovc, &ack);
    if (ret != NO_ERROR) {
        goto cleanup_and_exit;
    }

    txn->ops->cacheop(txn, IOTXN_CACHE_INVALIDATE, 0, header.buff_size);
    txn->ops->copyfrom(txn, buf, len, sizeof(header));

cleanup_and_exit:
    txn->ops->release(txn);
    return ret;
}

static mx_status_t bcm_get_macid(uint8_t* mac) {
    mx_status_t ret = NO_ERROR;
    property_tag_get_macid_t tag = BCM_MAILBOX_TAG_GET_MACID;

    ret = bcm_get_property_tag((uint8_t*)&tag, sizeof(tag));

    memcpy(mac, tag.macid, 6);

    return ret;
}

static mx_status_t bcm_get_clock_rate(const uint32_t clockid, uint32_t* res) {
    mx_status_t ret = NO_ERROR;
    property_tag_get_clock_rate_t tag = BCM_MAILBOX_TAG_GET_CLOCKRATE;

    tag.clockid = clockid;

    ret = bcm_get_property_tag((uint8_t*)&tag, sizeof(tag));

    // Make sure that we're getting data back for the clock that we requested.
    if (tag.clockid != clockid) {
        return ERR_IO;
    }

    // Fill in the return parameter;
    *res = tag.resp;

    return ret;
}

static ssize_t mailbox_device_ioctl(mx_device_t* dev, uint32_t op,
                                    const void* in_buf, size_t in_len,
                                    void* out_buf, size_t out_len) {
    bcm_fb_desc_t fbdesc;
    uint8_t macid[6];

    switch (op) {
    case IOCTL_BCM_POWER_ON_USB:
        return bcm_vc_poweron(bcm_dev_usb);

    case IOCTL_BCM_GET_FRAMEBUFFER:
        memcpy(&fbdesc, in_buf, in_len);
        bcm_vc_get_framebuffer(&fbdesc);
        memcpy(out_buf, &fbdesc, out_len);
        return out_len;

    case IOCTL_BCM_GET_MACID:

        bcm_get_macid(macid);
        memcpy(out_buf, macid, out_len);
        return out_len;
    case IOCTL_BCM_GET_CLOCKRATE:
        // Input buffer should be exactly 4 bytes long and should contain the
        // ID of the target clock. Output buffer should also be exactly 4 bytes
        // and will contain the clock rate of the target clock.
        if (in_len != 4 || out_len != 4)
            return ERR_INVALID_ARGS;

        const uint32_t clockid = *((uint32_t*)(in_buf));

        mx_status_t rc = bcm_get_clock_rate(clockid, (uint32_t*)out_buf);
        if (rc != NO_ERROR)
            return rc;

        return out_len;
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t vc_set_mode(mx_device_t* dev, mx_display_info_t* info) {

    return NO_ERROR;
}

static mx_status_t vc_get_mode(mx_device_t* dev, mx_display_info_t* info) {
    assert(info);
    memcpy(info, &disp_info, sizeof(mx_display_info_t));
    return NO_ERROR;
}

static mx_status_t vc_get_framebuffer(mx_device_t* dev, void** framebuffer) {
    assert(framebuffer);
    (*framebuffer) = vc_framebuffer;
    return NO_ERROR;
}

static mx_display_protocol_t vc_display_proto = {
    .set_mode = vc_set_mode,
    .get_mode = vc_get_mode,
    .get_framebuffer = vc_get_framebuffer,
    .flush = vc_flush_framebuffer
};

static mx_protocol_device_t mailbox_device_proto = {
    .ioctl = mailbox_device_ioctl,
};

static mx_protocol_device_t empty_device_proto = {};

mx_status_t mailbox_bind(mx_driver_t* driver, mx_device_t* parent, void** cookie) {
    uintptr_t page_base;

    // Carve out some address space for the device -- it's memory mapped.
    mx_status_t status = mx_mmap_device_memory(
        get_root_resource(),
        MAILBOX_PAGE_ADDRESS, MAILBOX_REGS_LENGTH,
        MX_CACHE_POLICY_UNCACHED_DEVICE, &page_base);

    if (status != NO_ERROR)
        return status;

    // The device is actually mapped at some offset into the page.
    mailbox_regs = (uint32_t*)(page_base + PAGE_REG_DELTA);

    mx_device_t* dev;
    status = device_create(&dev, driver, "bcm-vc-rpc", &mailbox_device_proto);
    if (status != NO_ERROR)
        return status;

    dev->props = calloc(2, sizeof(mx_device_prop_t));
    dev->props[0] = (mx_device_prop_t){BIND_SOC_VID, 0, SOC_VID_BROADCOMM};
    dev->props[1] = (mx_device_prop_t){BIND_SOC_DID, 0, SOC_DID_BROADCOMM_MAILBOX};
    dev->prop_count = 2;

    status = device_add(dev, parent);
    if (status != NO_ERROR) {
        free(dev);
        return status;
    }

    bcm_fb_desc_t framebuff_descriptor;

    // For now these are set to work with the rpi 5" lcd didsplay
    // TODO: add a mechanisms to specify and change settings outside the driver

    framebuff_descriptor.phys_width = 800;
    framebuff_descriptor.phys_height = 480;
    framebuff_descriptor.virt_width = 800;
    framebuff_descriptor.virt_height = 480;
    framebuff_descriptor.pitch = 0;
    framebuff_descriptor.depth = 32;
    framebuff_descriptor.virt_x_offs = 0;
    framebuff_descriptor.virt_y_offs = 0;
    framebuff_descriptor.fb_p = 0;
    framebuff_descriptor.fb_size = 0;

    bcm_vc_get_framebuffer(&framebuff_descriptor);

    device_init(&disp_device, driver, "bcm-vc-fbuff", &empty_device_proto);

    disp_device.protocol_id = MX_PROTOCOL_DISPLAY;
    disp_device.protocol_ops = &vc_display_proto;

    disp_info.format = MX_PIXEL_FORMAT_ARGB_8888;
    disp_info.width = 800;
    disp_info.height = 480;
    disp_info.stride = 800;

    mx_set_framebuffer(get_root_resource(), vc_framebuffer,
                       bcm_vc_framebuffer.fb_size, disp_info.format,
                       disp_info.width, disp_info.height, disp_info.stride);

    status = device_add(&disp_device, parent);
    if (status != NO_ERROR) {
        return status;
    }

    bcm_vc_poweron(bcm_dev_sd);

    // Publish this mock device to allow the eMMC device to bind to.
    mx_device_t* sdmmc_device = malloc(sizeof(*sdmmc_device));
    device_init(sdmmc_device, driver, "bcm-sdmmc", &empty_device_proto);
    sdmmc_device->props = calloc(2, sizeof(mx_device_prop_t));
    sdmmc_device->props[0] = (mx_device_prop_t){BIND_SOC_VID, 0, SOC_VID_BROADCOMM};
    sdmmc_device->props[1] = (mx_device_prop_t){BIND_SOC_DID, 0, SOC_DID_BROADCOMM_EMMC};
    sdmmc_device->prop_count = 2;
    sdmmc_device->protocol_id = MX_PROTOCOL_SOC;
    status = device_add(sdmmc_device, parent);

    bcm_vc_poweron(bcm_dev_usb);

    return NO_ERROR;
}

mx_driver_t _driver_bcm_mailbox = {
    .name = "bcm-vc-rpc",
    .ops = {
        .bind = mailbox_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_bcm_mailbox, "bcm-vc-rpc", "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_SOC),
    BI_ABORT_IF(NE, BIND_SOC_VID, SOC_VID_BROADCOMM),
    BI_MATCH_IF(EQ, BIND_SOC_DID, SOC_DID_BROADCOMM_VIDEOCORE_BUS),
MAGENTA_DRIVER_END(_driver_bcm_mailbox)
