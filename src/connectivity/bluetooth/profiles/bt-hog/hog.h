// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_PROFILES_BT_HOG_HOG_H_
#define SRC_CONNECTIVITY_BLUETOOTH_PROFILES_BT_HOG_HOG_H_

#include <lib/device-protocol/bt-gatt-svc.h>
#include <stdbool.h>
#include <threads.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/hidbus.h>

// org.bluetooth.characteristic.protocol_mode
#define BT_HOG_PROTOCOL_MODE 0x2A4E
#define BT_HOG_PROTOCOL_MODE_BOOT_MODE 0
#define BT_HOG_PROTOCOL_MODE_REPORT_MODE 1

// org.bluetooth.characteristic.report_map
#define BT_HOG_REPORT_MAP 0x2A4B

// org.bluetooth.characteristic.report
#define BT_HOG_REPORT 0x2A4D

// org.bluetooth.characteristic.boot_keyboard_input_report
#define BT_HOG_BOOT_KEYBOARD_INPUT_REPORT 0x2A22

// org.bluetooth.characteristic.boot_keyboard_output_report
#define BT_HOG_BOOT_KEYBOARD_OUTPUT_REPORT 0x2A32

// org.bluetooth.characteristic.boot_mouse_input_report
#define BT_HOG_BOOT_MOUSE_INPUT_REPORT 0x2A33

typedef enum {
  HOGD_DEVICE_BOOT_KEYBOARD,
  HOGD_DEVICE_BOOT_MOUSE,
  HOGD_DEVICE_REPORT,
} hogd_device_type_t;

typedef enum {
  HOGD_STATE_UNINITIALIZED = 0,
  HOGD_STATE_INITIALIZED,
  HOGD_STATE_TERMINATED,
} hogd_state_t;

typedef struct hogd_t hogd_t;
typedef struct hogd_device_t hogd_device_t;
struct hogd_device_t {
  hogd_device_type_t device_type;

  bt_gatt_id_t input_report_id;
  bool has_input_report_id;
  bt_gatt_id_t output_report_id;
  bool has_output_report_id;

  hogd_state_t state;
  zx_device_t* dev;
  mtx_t lock;
  hidbus_ifc_protocol_t ifc;

  // Reference to owner.
  hogd_t* parent;

  // Report devices are stored as a singly linked list. Currently unused.
  hogd_device_t* next;
};

struct hogd_t {
  bt_gatt_svc_protocol_t gatt_svc;

  bool has_report_map;
  bt_gatt_id_t report_map_id;

  bool has_protocol_mode;
  bt_gatt_id_t protocol_mode_id;

  uint8_t protocol_mode;

  void* hid_descriptor;
  size_t hid_descriptor_len;

  zx_device_t* bus_dev;

  hogd_device_t boot_keyboard_device;
  hogd_device_t boot_mouse_device;

  // Report devices are stored as a singly linked list.
  hogd_device_t* report_device;
};

#endif  // SRC_CONNECTIVITY_BLUETOOTH_PROFILES_BT_HOG_HOG_H_
