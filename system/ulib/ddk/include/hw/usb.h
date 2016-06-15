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

#include <sys/types.h>
#include <runtime/compiler.h>

__BEGIN_CDECLS;

/* Request Types */
#define USB_DIR_OUT                     (0 << 7)
#define USB_DIR_IN                      (1 << 7)
#define USB_DIR_MASK                    (1 << 7)
#define USB_TYPE_STANDARD               (0 << 5)
#define USB_TYPE_CLASS                  (1 << 5)
#define USB_TYPE_VENDOR                 (2 << 5)
#define USB_TYPE_MASK                   (3 << 5)
#define USB_RECIP_DEVICE                (0 << 0)
#define USB_RECIP_INTERFACE             (1 << 0)
#define USB_RECIP_ENDPOINT              (2 << 0)
#define USB_RECIP_OTHER                 (3 << 0)
#define USB_RECIP_MASK                  (0x1f << 0)

/* 1.0 Request Values */
#define USB_REQ_GET_STATUS                  0x00
#define USB_REQ_CLEAR_FEATURE               0x01
#define USB_REQ_SET_FEATURE                 0x03
#define USB_REQ_SET_ADDRESS                 0x05
#define USB_REQ_GET_DESCRIPTOR              0x06
#define USB_REQ_SET_DESCRIPTOR              0x07
#define USB_REQ_GET_CONFIGURATION           0x08
#define USB_REQ_SET_CONFIGURATION           0x09
#define USB_REQ_GET_INTERFACE               0x0A
#define USB_REQ_SET_INTERFACE               0x0B
#define USB_REQ_SYNCH_FRAME                 0x0C

/* USB device/interface classes */
#define USB_CLASS_AUDIO                     0x01
#define USB_CLASS_COMM                      0x02
#define USB_CLASS_HID                       0x03
#define USB_CLASS_PHYSICAL                  0x05
#define USB_CLASS_IMAGING                   0x06
#define USB_CLASS_PRINTER                   0x07
#define USB_CLASS_MSC                       0x08
#define USB_CLASS_HUB                       0x09
#define USB_CLASS_CDC                       0x0a
#define USB_CLASS_CCID                      0x0b
#define USB_CLASS_SECURITY                  0x0d
#define USB_CLASS_VIDEO                     0x0e
#define USB_CLASS_HEALTHCARE                0x0f
#define USB_CLASS_DIAGNOSTIC                0xdc
#define USB_CLASS_WIRELESS                  0xe0
#define USB_CLASS_MISC                      0xef

/* Mass storage requests */
#define USB_MASS_STORAGE_GET_MAX_LUN    0xfe
#define USB_MASS_STORAGE_RESET          0xff

/* HID Request Values */
#define USB_HID_GET_REPORT                  0x01
#define USB_HID_GET_IDLE                    0x02
#define USB_HID_GET_PROTOCOL                0x03
#define USB_HID_SET_REPORT                  0x09
#define USB_HID_SET_IDLE                    0x0A
#define USB_HID_SET_PROTOCOL                0x0B

/* Descriptor Types */
#define USB_DT_DEVICE                      0x01
#define USB_DT_CONFIG                      0x02
#define USB_DT_STRING                      0x03
#define USB_DT_INTERFACE                   0x04
#define USB_DT_ENDPOINT                    0x05
#define USB_DT_DEVICE_QUALIFIER            0x06
#define USB_DT_OTHER_SPEED_CONFIG          0x07
#define USB_DT_INTERFACE_POWER             0x08
#define USB_DT_HID                         0x21
#define USB_DT_HIDREPORT                   0x22
#define USB_DT_HIDPHYSICAL                 0x23

/* USB device feature selectors */
#define USB_DEVICE_SELF_POWERED            0x00
#define USB_DEVICE_REMOTE_WAKEUP           0x01
#define USB_DEVICE_TEST_MODE               0x02

/* Endpoint direction (bEndpointAddress) */
#define USB_ENDPOINT_IN                    0x80
#define USB_ENDPOINT_OUT                   0x00
#define USB_ENDPOINT_DIR_MASK              0x80

/* Endpoint types (bmAttributes) */
#define USB_ENDPOINT_CONTROL               0x00
#define USB_ENDPOINT_ISOCHRONOUS           0x01
#define USB_ENDPOINT_BULK                  0x02
#define USB_ENDPOINT_INTERRUPT             0x03
#define USB_ENDPOINT_TYPE_MASK             0x03

#define USB_ENDPOINT_HALT                  0x00

/* general USB defines */
typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__ ((packed)) usb_setup_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
} __attribute__ ((packed)) descriptor_header_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__ ((packed)) usb_device_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__ ((packed)) usb_configuration_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__ ((packed)) usb_interface_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__ ((packed)) usb_endpoint_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    uint8_t bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} __attribute__ ((packed)) usb_hid_descriptor_t;

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

__END_CDECLS;
