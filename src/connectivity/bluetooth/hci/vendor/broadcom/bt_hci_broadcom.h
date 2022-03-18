// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_BROADCOM_BT_HCI_BROADCOM_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_BROADCOM_BT_HCI_BROADCOM_H_

#include <fidl/fuchsia.hardware.bluetooth/cpp/wire.h>
#include <fuchsia/hardware/bt/hci/c/banjo.h>
#include <fuchsia/hardware/bt/vendor/cpp/banjo.h>
#include <fuchsia/hardware/serialimpl/async/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/wait.h>

#include <ddktl/device.h>

#include "packets.h"

namespace bt_hci_broadcom {

class BtHciBroadcom;
using BtHciBroadcomType =
    ddk::Device<BtHciBroadcom, ddk::GetProtocolable, ddk::Initializable, ddk::Unbindable,
                ddk::Messageable<fuchsia_hardware_bluetooth::Hci>::Mixin>;

class BtHciBroadcom : public BtHciBroadcomType, public ddk::BtVendorProtocol<BtHciBroadcom> {
 public:
  // |dispatcher| will be used for the initialization thread if non-null.
  explicit BtHciBroadcom(zx_device_t* parent, async_dispatcher_t* dispatcher);

  // Static bind function for the ZIRCON_DRIVER() declaration. Binds this device and passes
  // ownership to the driver manager.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Static bind function that accepts a dispatcher for the initialization thread. This is useful
  // for testing.
  static zx_status_t Create(void* ctx, zx_device_t* parent, async_dispatcher_t* dispatcher);

  // DDK mixins:
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_proto);

  // ddk::BtVendorProtocol mixins:
  bt_vendor_features_t BtVendorGetFeatures();
  zx_status_t BtVendorEncodeCommand(bt_vendor_command_t command, const bt_vendor_params_t* params,
                                    uint8_t* out_encoded_buffer, size_t encoded_size,
                                    size_t* out_encoded_actual);

 private:
  static constexpr size_t kMacAddrLen = 6;

  // ddk::Messageable mixins:
  void OpenCommandChannel(OpenCommandChannelRequestView request,
                          OpenCommandChannelCompleter::Sync& completer) override;
  void OpenAclDataChannel(OpenAclDataChannelRequestView request,
                          OpenAclDataChannelCompleter::Sync& completer) override;
  void OpenSnoopChannel(OpenSnoopChannelRequestView request,
                        OpenSnoopChannelCompleter::Sync& completer) override;

  // Truly private, internal helper methods:

  static zx_status_t EncodeSetAclPriorityCommand(bt_vendor_set_acl_priority_params_t params,
                                                 void* out_buffer, size_t buffer_size,
                                                 size_t* actual_size);

  fpromise::promise<std::vector<uint8_t>, zx_status_t> SendCommand(const void* command,
                                                                   size_t length);

  // Waits for a "readable" signal from the command channel and reads the next event.
  fpromise::promise<std::vector<uint8_t>, zx_status_t> ReadEvent();

  fpromise::promise<void, zx_status_t> SetBaudRate(uint32_t baud_rate);

  fpromise::promise<void, zx_status_t> SetBdaddr(const std::array<uint8_t, kMacAddrLen>& bdaddr);

  fpromise::result<std::array<uint8_t, kMacAddrLen>, zx_status_t> GetBdaddrFromBootloader();

  fpromise::promise<> LogControllerFallbackBdaddr();

  fpromise::promise<void, zx_status_t> LoadFirmware();

  fpromise::promise<void, zx_status_t> SendVmoAsCommands(zx::vmo vmo, size_t size, size_t offset);

  fpromise::promise<> Initialize();

  void OnInitializeComplete(zx_status_t status);

  zx_status_t Bind();

  bt_hci_protocol_t hci_;
  serial_impl_async_protocol_t serial_;
  zx::channel command_channel_;
  // true if underlying transport is UART
  bool is_uart_;
  std::optional<ddk::InitTxn> init_txn_;

  // Only present in production. Created during initialization.
  std::optional<async::Loop> loop_;
  // In production, this is |loop_|'s dispatcher. In tests, this is the test dispatcher.
  async_dispatcher_t* dispatcher_;
  // The executor for |dispatcher_|, created during initialization.
  std::optional<async::Executor> executor_;
};

}  // namespace bt_hci_broadcom

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_VENDOR_BROADCOM_BT_HCI_BROADCOM_H_
