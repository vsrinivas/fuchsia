// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include <mx/channel.h>

#include "apps/bluetooth/lib/common/device_address.h"
#include "apps/bluetooth/lib/hci/fake_controller_base.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "lib/ftl/macros.h"

namespace bluetooth {
namespace hci {
namespace test {

// FakeController emulates a real Bluetooth controller. It can be configured to respond to HCI
// commands in a predictable manner.
class FakeController : public FakeControllerBase {
 public:
  // Global settings for the FakeController. These can be used to initialize a FakeController and/or
  // to re-configure an existing one.
  struct Settings final {
    // The default constructor initializes all fields to 0, unless another default is specified
    // below.
    Settings();
    ~Settings() = default;

    void ApplyDefaults();
    void ApplyLEOnlyConfig();

    // HCI settings.
    // Default: HCIVersion::k5_0.
    HCIVersion hci_version;
    // Default: 1
    uint8_t num_hci_command_packets;
    uint64_t event_mask;
    uint64_t le_event_mask;

    // BD_ADDR (BR/EDR) or Public Device Address (LE)
    common::DeviceAddressBytes bd_addr;

    // Local supported features and commands.
    uint64_t lmp_features_page0;
    uint64_t lmp_features_page1;
    uint64_t lmp_features_page2;
    uint64_t le_features;
    uint64_t le_supported_states;
    uint8_t supported_commands[64];

    // Buffer Size.
    uint16_t acl_data_packet_length;
    uint8_t total_num_acl_data_packets;
    uint16_t le_acl_data_packet_length;
    uint8_t le_total_num_acl_data_packets;
  };

  FakeController(const Settings& settings, mx::channel cmd_channel, mx::channel acl_data_channel);
  ~FakeController() override;

  // Resets the controller settings.
  void set_settings(const Settings& settings) { settings_ = settings; }

  // Tells the FakeController to always respond to the given command opcode with the given HCI
  // status code.
  void SetDefaultResponseStatus(OpCode opcode, Status status);
  void ClearDefaultResponseStatus(OpCode opcode);

 private:
  // Sends a HCI_Command_Complete event in response to the command with |opcode| and using the given
  // data as the parameter payload.
  void RespondWithCommandComplete(OpCode opcode, void* return_params, uint8_t return_params_size);

  // If a default status has been configured for the given opcode, sends back an error response and
  // returns true. Returns false if no response was set.
  bool MaybeRespondWithDefaultStatus(OpCode opcode);

  // FakeControllerBase overrides:
  void OnCommandPacketReceived(const CommandPacket& command_packet) override;
  void OnACLDataPacketReceived(const common::ByteBuffer& acl_data_packet) override;

  Settings settings_;
  std::unordered_map<OpCode, Status> default_status_map_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FakeController);
};

}  // namespace test
}  // namesapce hci
}  // namespace bluetooth
