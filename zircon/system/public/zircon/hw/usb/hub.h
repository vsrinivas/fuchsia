// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_HW_USB_HUB_H_
#define SYSROOT_ZIRCON_HW_USB_HUB_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

// clang-format off

__BEGIN_CDECLS

// Hub request types
#define USB_RECIP_HUB   (USB_TYPE_CLASS | USB_RECIP_DEVICE)
#define USB_RECIP_PORT  (USB_TYPE_CLASS | USB_RECIP_OTHER)

// Hub requests
#define USB_HUB_SET_DEPTH       12

// Hub descriptor types
#define USB_HUB_DESC_TYPE       0x29
#define USB_HUB_DESC_TYPE_SS    0x2A    // for superspeed hubs

// Hub Class Feature Selectors (USB 2.0 spec Table 11.17)
#define USB_FEATURE_C_HUB_LOCAL_POWER   0
#define USB_FEATURE_C_HUB_OVER_CURRENT  1
#define USB_FEATURE_PORT_CONNECTION     0
#define USB_FEATURE_PORT_ENABLE         1
#define USB_FEATURE_PORT_SUSPEND        2
#define USB_FEATURE_PORT_OVER_CURRENT   3
#define USB_FEATURE_PORT_RESET          4
#define USB_FEATURE_PORT_LINK_STATE     5
#define USB_FEATURE_PORT_POWER          8
#define USB_FEATURE_PORT_LOW_SPEED      9
#define USB_FEATURE_C_PORT_CONNECTION   16
#define USB_FEATURE_C_PORT_ENABLE       17
#define USB_FEATURE_C_PORT_SUSPEND      18
#define USB_FEATURE_C_PORT_OVER_CURRENT 19
#define USB_FEATURE_C_PORT_RESET        20
#define USB_FEATURE_PORT_TEST           21
#define USB_FEATURE_PORT_INDICATOR      22
#define USB_FEATURE_PORT_INDICATOR      22
#define USB_FEATURE_PORT_U1_TIMEOUT     23
#define USB_FEATURE_PORT_U2_TIMEOUT     24
#define USB_FEATURE_C_PORT_LINK_STATE   25
#define USB_FEATURE_C_PORT_CONFIG_ERROR 26
#define USB_FEATURE_PORT_REMOTE_WAKE_MASK 27
#define USB_FEATURE_BH_PORT_RESET       28
#define USB_FEATURE_C_BH_PORT_RESET     29
#define USB_FEATURE_FORCE_LINKPM_ACCEPT 30

typedef struct {
    uint8_t bDescLength;
    uint8_t bDescriptorType;
    uint8_t bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t bPowerOn2PwrGood;
    uint8_t bHubContrCurrent;
    union {
        // USB 2.0
        struct {
            // variable length depending on number of ports
            uint8_t  DeviceRemovable[4];
            uint8_t  PortPwrCtrlMask[4];
        }  __attribute__ ((packed)) hs;
        // USB 3.0
        struct {
            uint8_t bHubHdrDecLat;
            uint16_t wHubDelay;
            uint16_t DeviceRemovable;
        } __attribute__ ((packed)) ss;
    } __attribute__ ((packed));
} __attribute__ ((packed)) usb_hub_descriptor_t;

typedef struct {
    uint16_t wHubStatus;
    uint16_t wHubChange;
} __attribute__ ((packed)) usb_hub_status_t;

// wHubStatus bits
#define USB_HUB_LOCAL_POWER         (1 << 0)
#define USB_HUB_OVER_CURRENT        (1 << 1)

typedef struct {
    uint16_t wPortStatus;
    uint16_t wPortChange;
} __attribute__ ((packed)) usb_port_status_t;

// Port Status bits
#define USB_PORT_CONNECTION         (1 << 0)
#define USB_PORT_ENABLE             (1 << 1)
#define USB_PORT_SUSPEND            (1 << 2)    // USB 2.0 only
#define USB_PORT_OVER_CURRENT       (1 << 3)
#define USB_PORT_RESET              (1 << 4)
#define USB_PORT_POWER              (1 << 8)    // USB 2.0 only
#define USB_PORT_LOW_SPEED          (1 << 9)    // USB 2.0 only
#define USB_PORT_HIGH_SPEED         (1 << 10)   // USB 2.0 only
#define USB_PORT_TEST_MODE          (1 << 11)   // USB 2.0 only
#define USB_PORT_INDICATOR_CONTROL  (1 << 12)   // USB 2.0 only

// Port Status Changed bits
#define USB_C_PORT_CONNECTION       (1 << 0)
#define USB_C_PORT_ENABLE           (1 << 1)    // USB 2.0 only
#define USB_C_PORT_SUSPEND          (1 << 2)    // USB 2.0 only
#define USB_C_PORT_OVER_CURRENT     (1 << 3)
#define USB_C_PORT_RESET            (1 << 4)
#define USB_C_BH_PORT_RESET         (1 << 5)    // USB 3.0 only
#define USB_C_PORT_LINK_STATE       (1 << 6)    // USB 3.0 only
#define USB_C_PORT_CONFIG_ERROR     (1 << 7)    // USB 3.0 only
#define USB_C_PORT_POWER            (1 << 8)    // USB 2.0 only
#define USB_C_PORT_LOW_SPEED        (1 << 9)    // USB 2.0 only
#define USB_C_PORT_HIGH_SPEED       (1 << 10)   // USB 2.0 only
#define USB_C_PORT_TEST_MODE        (1 << 11)   // USB 2.0 only
#define USB_C_PORT_INDICATOR_CONTROL (1 << 12)   // USB 2.0 only

__END_CDECLS

#endif  // SYSROOT_ZIRCON_HW_USB_HUB_H_
