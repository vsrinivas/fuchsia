// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_HW_USB_H_
#define SYSROOT_ZIRCON_HW_USB_H_

// clang-format off

#include <endian.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// maximum number of endpoints per device
#define USB_MAX_EPS                     32

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
#define USB_CLASS_APPLICATION_SPECIFIC      0xfe
#define USB_CLASS_VENDOR                    0xFf

#define USB_SUBCLASS_COMM_ACM               0x02

#define USB_SUBCLASS_WIRELESS_MISC          0x01
#define USB_PROTOCOL_WIRELESS_MISC_RNDIS    0x03

#define USB_SUBCLASS_MSC_RNDIS              0x04
#define USB_PROTOCOL_MSC_RNDIS_ETHERNET     0x01

#define USB_SUBCLASS_MSC_SCSI               0x06
#define USB_PROTOCOL_MSC_BULK_ONLY          0x50

#define USB_SUBCLASS_DFU                    0x01
#define USB_PROTOCOL_DFU                    0x02

#define USB_SUBCLASS_VENDOR                 0xFF
#define USB_PROTOCOL_TEST_FTDI              0x01
#define USB_PROTOCOL_TEST_HID_ONE_ENDPOINT  0x02
#define USB_PROTOCOL_TEST_HID_TWO_ENDPOINT  0x03

/* Descriptor Types */
#define USB_DT_DEVICE                      0x01
#define USB_DT_CONFIG                      0x02
#define USB_DT_STRING                      0x03
#define USB_DT_INTERFACE                   0x04
#define USB_DT_ENDPOINT                    0x05
#define USB_DT_DEVICE_QUALIFIER            0x06
#define USB_DT_OTHER_SPEED_CONFIG          0x07
#define USB_DT_INTERFACE_POWER             0x08
#define USB_DT_INTERFACE_ASSOCIATION       0x0b
#define USB_DT_HID                         0x21
#define USB_DT_HIDREPORT                   0x22
#define USB_DT_HIDPHYSICAL                 0x23
#define USB_DT_CS_INTERFACE                0x24
#define USB_DT_CS_ENDPOINT                 0x25
#define USB_DT_SS_EP_COMPANION             0x30
#define USB_DT_SS_ISOCH_EP_COMPANION       0x31

/* USB device feature selectors */
#define USB_DEVICE_SELF_POWERED            0x00
#define USB_DEVICE_REMOTE_WAKEUP           0x01
#define USB_DEVICE_TEST_MODE               0x02

/* Configuration attributes (bm_attributes) */
#define USB_CONFIGURATION_REMOTE_WAKEUP    0x20
#define USB_CONFIGURATION_SELF_POWERED     0x40
#define USB_CONFIGURATION_RESERVED_7       0x80 // This bit must be set

/* Endpoint direction (bEndpointAddress) */
#define USB_ENDPOINT_IN                    0x80
#define USB_ENDPOINT_OUT                   0x00
#define USB_ENDPOINT_DIR_MASK              0x80
#define USB_ENDPOINT_NUM_MASK              0x1F

/* Endpoint types (bm_attributes) */
#define USB_ENDPOINT_CONTROL               0x00
#define USB_ENDPOINT_ISOCHRONOUS           0x01
#define USB_ENDPOINT_BULK                  0x02
#define USB_ENDPOINT_INTERRUPT             0x03
#define USB_ENDPOINT_TYPE_MASK             0x03

/* Endpoint synchronization type (bm_attributes) */
#define USB_ENDPOINT_NO_SYNCHRONIZATION    0x00
#define USB_ENDPOINT_ASYNCHRONOUS          0x04
#define USB_ENDPOINT_ADAPTIVE              0x08
#define USB_ENDPOINT_SYNCHRONOUS           0x0C
#define USB_ENDPOINT_SYNCHRONIZATION_MASK  0x0C

/* Endpoint usage type (bm_attributes) */
#define USB_ENDPOINT_DATA                  0x00
#define USB_ENDPOINT_FEEDBACK              0x10
#define USB_ENDPOINT_IMPLICIT_FEEDBACK     0x20
#define USB_ENDPOINT_USAGE_MASK            0x30

#define USB_ENDPOINT_HALT                  0x00

/* general USB defines */
typedef struct {
    uint8_t bm_request_type;
    uint8_t b_request;
    uint16_t w_value;
    uint16_t w_index;
    uint16_t w_length;
} __attribute__ ((packed)) usb_setup_info_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;
} __attribute__ ((packed)) usb_descriptor_header_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_DEVICE
    uint16_t bcd_usb;
    uint8_t b_device_class;
    uint8_t b_device_sub_class;
    uint8_t b_device_protocol;
    uint8_t b_max_packet_size0;
    uint16_t id_vendor;
    uint16_t id_product;
    uint16_t bcd_device;
    uint8_t i_manufacturer;
    uint8_t i_product;
    uint8_t i_serial_number;
    uint8_t b_num_configurations;
} __attribute__ ((packed)) usb_device_descriptor_info_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_CONFIG
    uint16_t w_total_length;
    uint8_t b_num_interfaces;
    uint8_t b_configuration_value;
    uint8_t i_configuration;
    uint8_t bm_attributes;
    uint8_t b_max_power;
} __attribute__ ((packed)) usb_configuration_descriptor_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_STRING
    uint8_t b_string[];
} __attribute__ ((packed)) usb_string_descriptor_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_INTERFACE
    uint8_t b_interface_number;
    uint8_t b_alternate_setting;
    uint8_t b_num_endpoints;
    uint8_t b_interface_class;
    uint8_t b_interface_sub_class;
    uint8_t b_interface_protocol;
    uint8_t i_interface;
} __attribute__ ((packed)) usb_interface_info_descriptor_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_ENDPOINT
    uint8_t b_endpoint_address;
    uint8_t bm_attributes;
    uint16_t w_max_packet_size;
    uint8_t b_interval;
} __attribute__ ((packed)) usb_endpoint_info_descriptor_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_DEVICE_QUALIFIER
    uint16_t bcd_usb;
    uint8_t b_device_class;
    uint8_t b_device_sub_class;
    uint8_t b_device_protocol;
    uint8_t b_max_packet_size0;
    uint8_t b_num_configurations;
    uint8_t b_reserved;
} __attribute__ ((packed)) usb_device_qualifier_descriptor_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_SS_EP_COMPANION
    uint8_t b_max_burst;
    uint8_t bm_attributes;
    uint16_t w_bytes_per_interval;
} __attribute__ ((packed)) usb_ss_ep_comp_descriptor_info_t;
#define usb_ss_ep_comp_isoc_mult(ep) ((ep)->bm_attributes & 0x3)
#define usb_ss_ep_comp_isoc_comp(ep) (!!((ep)->bm_attributes & 0x80))

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_SS_ISOCH_EP_COMPANION
    uint16_t w_reserved;
    uint32_t dw_bytes_per_interval;
} __attribute__ ((packed)) usb_ss_isoch_ep_comp_descriptor_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_INTERFACE_ASSOCIATION
    uint8_t b_first_interface;
    uint8_t b_interface_count;
    uint8_t b_function_class;
    uint8_t b_function_sub_class;
    uint8_t b_function_protocol;
    uint8_t i_function;
} __attribute__ ((packed)) usb_interface_assoc_descriptor_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_CS_INTERFACE
    uint8_t b_descriptor_sub_type;
} __attribute__ ((packed)) usb_cs_interface_descriptor_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_STRING
    uint16_t w_lang_ids[127];
} __attribute__ ((packed)) usb_langid_desc_t;

typedef struct {
    uint8_t b_length;
    uint8_t b_descriptor_type;    // USB_DT_STRING
    uint16_t code_points[127];
} __attribute__ ((packed)) usb_string_desc_t;

__END_CDECLS

#endif  // SYSROOT_ZIRCON_HW_USB_H_
