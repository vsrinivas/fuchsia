// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include <mx/channel.h>

#include "apps/bluetooth/lib/common/device_address.h"
#include "apps/bluetooth/lib/hci/hci.h"
#include "apps/bluetooth/lib/hci/hci_constants.h"
#include "apps/bluetooth/lib/testing/fake_controller_base.h"
#include "lib/fxl/functional/cancelable_callback.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace testing {

class FakeDevice;

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
    void ApplyLEOnlyDefaults();
    void ApplyLegacyLEConfig();
    void ApplyLEConfig();

    // HCI settings.
    hci::HCIVersion hci_version;      // Default: HCIVersion::k5_0.
    uint8_t num_hci_command_packets;  // Default: 1
    uint64_t event_mask;
    uint64_t le_event_mask;

    // BD_ADDR (BR/EDR) or Public Device Address (LE)
    common::DeviceAddress bd_addr;

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

  // Current device scan state.
  struct LEScanState final {
    LEScanState();

    bool enabled;
    hci::LEScanType scan_type;
    uint16_t scan_interval;
    uint16_t scan_window;
    bool filter_duplicates;
    hci::LEOwnAddressType own_address_type;
    hci::LEScanFilterPolicy filter_policy;
  };

  // Constructor initializes the controller with the minimal default settings (equivalent to calling
  // Settings::ApplyDefaults()).
  FakeController(mx::channel cmd_channel, mx::channel acl_data_channel);
  ~FakeController() override;

  // Resets the controller settings.
  void set_settings(const Settings& settings) { settings_ = settings; }

  // Tells the FakeController to always respond to the given command opcode with the given HCI
  // status code.
  void SetDefaultResponseStatus(hci::OpCode opcode, hci::Status status);
  void ClearDefaultResponseStatus(hci::OpCode opcode);

  // Returns the current LE scan state.
  const LEScanState& le_scan_state() const { return le_scan_state_; }

  // Adds a fake remote device. This device will be used to during LE scan and connection
  // procedures.
  void AddLEDevice(std::unique_ptr<FakeDevice> le_device);

  // Sets a callback to invoked when the scan state changes.
  using ScanStateCallback = std::function<void(bool enabled)>;
  void SetScanStateCallback(const ScanStateCallback& callback,
                            fxl::RefPtr<fxl::TaskRunner> task_runner);

  // Sends a LE Meta event with the given parameters.
  void SendLEMetaEvent(hci::EventCode subevent_code, const void* params, uint8_t params_size);

 private:
  // Sends a HCI_Command_Complete event in response to the command with |opcode| and using the given
  // data as the parameter payload.
  void RespondWithCommandComplete(hci::OpCode opcode, const void* return_params,
                                  uint8_t return_params_size);

  // Sends a HCI_Command_Status event in response to the command with |opcode| and using the given
  // data as the parameter payload.
  void RespondWithCommandStatus(hci::OpCode opcode, hci::Status status);

  // If a default status has been configured for the given opcode, sends back an error response and
  // returns true. Returns false if no response was set.
  bool MaybeRespondWithDefaultStatus(hci::OpCode opcode);

  // Sends LE advertising reports for known LE devices, if a scan is currently enabled.
  void SendAdvertisingReports();

  // Called when a HCI_LE_Create_Connection command is received.
  void OnLECreateConnectionCommandReceived(const hci::LECreateConnectionCommandParams& params);

  // FakeControllerBase overrides:
  void OnCommandPacketReceived(
      const common::PacketView<hci::CommandHeader>& command_packet) override;
  void OnACLDataPacketReceived(const common::ByteBuffer& acl_data_packet) override;

  Settings settings_;
  LEScanState le_scan_state_;

  // Variables used for HCI_LE_Create_Connection/HCI_LE_Create_Connection_Cancel.
  uint16_t next_conn_handle_;
  fxl::CancelableClosure pending_le_connect_rsp_;
  common::DeviceAddress pending_le_connect_addr_;

  std::unordered_map<hci::OpCode, hci::Status> default_status_map_;
  std::vector<std::unique_ptr<FakeDevice>> le_devices_;

  ScanStateCallback scan_state_cb_;
  fxl::RefPtr<fxl::TaskRunner> scan_state_cb_task_runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeController);
};

}  // namespace testing
}  // namespace bluetooth
