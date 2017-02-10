// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/compiler.h>
#include <stddef.h>

__BEGIN_CDECLS;

// Broadcomm vendor id
#define SOC_VID_BROADCOMM 0x00BC


// Broadcomm specific PIDs
#define SOC_DID_BROADCOMM_VIDEOCORE_BUS 0x0000  // Videocore device (used as root bus)
#define SOC_DID_BROADCOMM_MAILBOX       0x0001  // Videocore mailbox, used for comms between cpu/gpu
#define SOC_DID_BROADCOMM_EMMC          0x0002  // Bcm28xx eMMC device.

typedef struct {
    uint32_t phys_width;    //request
    uint32_t phys_height;   //request
    uint32_t virt_width;    //request
    uint32_t virt_height;   //request
    uint32_t pitch;         //response
    uint32_t depth;         //request
    uint32_t virt_x_offs;   //request
    uint32_t virt_y_offs;   //request
    uint32_t fb_p;          //response
    uint32_t fb_size;       //response
} bcm_fb_desc_t;


#define IOCTL_BCM_POWER_ON_USB \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BCM, 0)

#define IOCTL_BCM_GET_FRAMEBUFFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BCM, 1)

#define IOCTL_BCM_FILL_FRAMEBUFFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BCM, 2)

#define IOCTL_BCM_GET_MACID \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BCM, 3)

#define IOCTL_BCM_GET_CLOCKRATE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BCM, 4)

// ssize_t ioctl_bcm_power_on_usb(int fd);
IOCTL_WRAPPER(ioctl_bcm_power_on_usb, IOCTL_BCM_POWER_ON_USB);

// ssize_t ioctl_bcm_get_framebuffer(int fd, bcm_fb_desc_t*, bcm_fb_desc_t*);
IOCTL_WRAPPER_INOUT(ioctl_bcm_get_framebuffer, IOCTL_BCM_GET_FRAMEBUFFER, bcm_fb_desc_t, bcm_fb_desc_t);

IOCTL_WRAPPER_IN(ioctl_bcm_fill_framebuffer, IOCTL_BCM_FILL_FRAMEBUFFER, uint8_t);

IOCTL_WRAPPER_VAROUT(ioctl_bcm_get_macid, IOCTL_BCM_GET_MACID, uint8_t);

IOCTL_WRAPPER_INOUT(ioctl_bcm_get_clock_rate, IOCTL_BCM_GET_CLOCKRATE, uint32_t, uint32_t);

__END_CDECLS
