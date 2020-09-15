// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt-hci-broadcom.c"

#include <ddk/device.h>
#include <zxtest/zxtest.h>

static bcm_hci_t* dev;
static zx_status_t load_firmware_result;

// Stub out the firmware loading from the devhost API.
zx_status_t load_firmware(zx_device_t* dev, const char* path, zx_handle_t* fw, size_t* size) {
  *fw = ZX_HANDLE_INVALID;
  *size = 0;
  return load_firmware_result;
}

// Test command with two additional params
typedef struct {
  hci_event_header_t header;
  uint8_t num_hci_command_packets;
  uint16_t command_opcode;
  uint8_t return_code;
  uint8_t test_param_1;
  uint8_t test_param_2;
} __PACKED hci_read_test_command_complete_t;

// zx_channel_read stub for use in bcm_hci_send_command which is the only place it is used in
// bt-hci-broadcom.c
zx_status_t zx_channel_read(zx_handle_t handle, uint32_t options, void* bytes, zx_handle_t* handles,
                            uint32_t num_bytes, uint32_t num_handles, uint32_t* actual_bytes,
                            uint32_t* actual_handles) {
  hci_read_test_command_complete_t read_result = {
      .header =
          {
              .event_code = HCI_EVT_COMMAND_COMPLETE,
              .parameter_total_size =
                  sizeof(hci_read_test_command_complete_t) - sizeof(hci_event_header_t),
          },
      .num_hci_command_packets = 0,
      .command_opcode = 0,
      .return_code = 0,
      .test_param_1 = 0xaa,
      .test_param_2 = 0xbb,
  };

  memcpy(bytes, &read_result, sizeof(hci_read_test_command_complete_t));

  return ZX_OK;
}

// zx_channel_write stub for use in bcm_hci_send_command which is the only place it is used in
// bt-hci-broadcom.c
zx_status_t zx_channel_write(zx_handle_t handle, uint32_t options, const void* bytes,
                             uint32_t num_bytes, const zx_handle_t* handles, uint32_t num_handles) {
  return ZX_OK;
}

static void create_bcm_hci_device(void) {
  // Dev is left mostly uninitialized because the only thing under test is firmware loading.
  dev = calloc(1, sizeof(bcm_hci_t));
  // TODO: Adding tests around the uart functionality requires a mock uart device.
  dev->is_uart = false;
}

static void release_bcm_hci_device(void) { free(dev); }

static void setup(void) { create_bcm_hci_device(); }

static void teardown(void) { release_bcm_hci_device(); }

TEST(BtHciBroadcomTest, ReportLoadFirmwareError) {
  setup();
  load_firmware_result = 999;  // Unique error code
  zx_status_t status = bcm_load_firmware(dev);
  EXPECT_EQ(load_firmware_result, status, "Failed to report load firmware error.");
  teardown();
}

TEST(BtHciBroadcomTest, LoadFirmwareErrorSuccess) {
  setup();
  load_firmware_result = ZX_OK;
  zx_status_t status = bcm_load_firmware(dev);
  EXPECT_EQ(load_firmware_result, status, "Failed to load firmware error.");
  teardown();
}

TEST(BtHciBroadcomTest, GetFeatures) {
  setup();
  bt_vendor_protocol_t vendor_proto = {};
  zx_status_t status = bcm_hci_get_protocol(dev, ZX_PROTOCOL_BT_VENDOR, &vendor_proto);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NE(vendor_proto.ops, NULL);
  // Don't expect exact features to avoid test fragility.
  EXPECT_NE(vendor_proto.ops->get_features(dev), 0u);
  teardown();
}

TEST(BtHciBroadcomTest, EncodeSetAclPrioritySuccess) {
  setup();
  bt_vendor_protocol_t vendor_proto = {};
  zx_status_t status = bcm_hci_get_protocol(dev, ZX_PROTOCOL_BT_VENDOR, &vendor_proto);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NE(vendor_proto.ops, NULL);

  uint8_t buffer[sizeof(bcm_set_acl_priority_cmd_t)];
  size_t actual_size = 0;
  bt_vendor_params_t params = {.set_acl_priority = {
                                   .connection_handle = 0xFF00,
                                   .priority = BT_VENDOR_ACL_PRIORITY_HIGH,
                                   .direction = BT_VENDOR_ACL_DIRECTION_SINK,
                               }};
  ASSERT_EQ(ZX_OK, vendor_proto.ops->encode_command(dev, BT_VENDOR_COMMAND_SET_ACL_PRIORITY,
                                                    &params, buffer, sizeof(buffer), &actual_size));
  ASSERT_EQ(sizeof(buffer), actual_size);
  uint8_t kExpectedBuffer[sizeof(buffer)] = {
      0x1A, 0xFD,  // OpCode
      0x04,        // size
      0x00, 0xFF,  // handle
      0x01,        // priority
      0x01,        // direction
  };
  EXPECT_EQ(0, memcmp(buffer, kExpectedBuffer, sizeof(buffer)));
  teardown();
}

TEST(BtHciBroadcomTest, EncodeSetAclPriorityBufferTooSmall) {
  setup();
  bt_vendor_protocol_t vendor_proto = {};
  zx_status_t status = bcm_hci_get_protocol(dev, ZX_PROTOCOL_BT_VENDOR, &vendor_proto);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NE(vendor_proto.ops, NULL);

  uint8_t buffer[sizeof(bcm_set_acl_priority_cmd_t) - 1];
  size_t actual_size = 0;
  bt_vendor_params_t params = {.set_acl_priority = {
                                   .connection_handle = 0xFF00,
                                   .priority = BT_VENDOR_ACL_PRIORITY_HIGH,
                                   .direction = BT_VENDOR_ACL_DIRECTION_SINK,
                               }};
  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL,
            vendor_proto.ops->encode_command(dev, BT_VENDOR_COMMAND_SET_ACL_PRIORITY, &params,
                                             buffer, sizeof(buffer), &actual_size));
  ASSERT_EQ(0, actual_size);
  teardown();
}

TEST(BtHciBroadcomTest, EncodeUnsupportedCommand) {
  setup();
  bt_vendor_protocol_t vendor_proto = {};
  zx_status_t status = bcm_hci_get_protocol(dev, ZX_PROTOCOL_BT_VENDOR, &vendor_proto);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_NE(vendor_proto.ops, NULL);

  uint8_t buffer[20];
  size_t actual_size = 0;
  bt_vendor_params_t params;
  const bt_vendor_command_t kUnsuportedCommand = 0xFF;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            vendor_proto.ops->encode_command(dev, kUnsuportedCommand, &params, buffer,
                                             sizeof(buffer), &actual_size));
  teardown();
}
