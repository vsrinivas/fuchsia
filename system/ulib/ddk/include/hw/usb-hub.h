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

// clang-format off

__BEGIN_CDECLS;

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
#define USB_FEATURE_PORT_POWER          8
#define USB_FEATURE_PORT_LOW_SPEED      9
#define USB_FEATURE_C_PORT_CONNECTION   16
#define USB_FEATURE_C_PORT_ENABLE       17
#define USB_FEATURE_C_PORT_SUSPEND      18
#define USB_FEATURE_C_PORT_OVER_CURRENT 19
#define USB_FEATURE_C_PORT_RESET        20
#define USB_FEATURE_PORT_TEST           21
#define USB_FEATURE_PORT_INDICATOR      22

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

// wPortStatus bits
#define USB_PORT_CONNECTION         (1 << 0)
#define USB_PORT_ENABLE             (1 << 1)
#define USB_PORT_SUSPEND            (1 << 2)
#define USB_PORT_OVER_CURRENT       (1 << 3)
#define USB_PORT_RESET              (1 << 4)
#define USB_PORT_POWER              (1 << 8)
#define USB_PORT_LOW_SPEED          (1 << 9)
#define USB_PORT_HIGH_SPEED         (1 << 10)
#define USB_PORT_TEST_MODE          (1 << 11)
#define USB_PORT_INDICATOR_CONTROL  (1 << 12)

__END_CDECLS;
