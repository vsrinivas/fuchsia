// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_EMULATOR_H_
#define SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_EMULATOR_H_

#include <fuchsia/bluetooth/test/cpp/fidl.h>
#include <fuchsia/hardware/bluetooth/c/fidl.h>
#include <fuchsia/hardware/bt/hci/c/banjo.h>
#include <fuchsia/hardware/test/c/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <queue>
#include <unordered_map>

#include "src/connectivity/bluetooth/core/bt-host/testing/fake_controller.h"
#include "src/connectivity/bluetooth/hci/virtual/peer.h"
#include "src/connectivity/bluetooth/lib/fidl/hanging_getter.h"

namespace bt_hci_virtual {

enum class Channel { ACL, COMMAND, SNOOP, EMULATOR };

class EmulatorDevice : public fuchsia::bluetooth::test::HciEmulator {
 public:
  explicit EmulatorDevice(zx_device_t* device);

  zx_status_t Bind(std::string_view name);
  void Unbind();
  void Release();

  zx_status_t HciMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx_status_t EmulatorMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx_status_t GetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t OpenChan(Channel chan_type, zx_handle_t chan);

  static zx_status_t OpenCommandChannel(void* ctx, zx_handle_t channel);
  static zx_status_t OpenAclDataChannel(void* ctx, zx_handle_t channel);
  static zx_status_t OpenSnoopChannel(void* ctx, zx_handle_t channel);
  static zx_status_t OpenEmulatorChannel(void* ctx, zx_handle_t channel);

 private:
  void StartEmulatorInterface(zx::channel chan);

  // fuchsia::bluetooth::test::HciEmulator overrides:
  void Publish(fuchsia::bluetooth::test::EmulatorSettings settings,
               PublishCallback callback) override;
  void AddLowEnergyPeer(fuchsia::bluetooth::test::LowEnergyPeerParameters params,
                        fidl::InterfaceRequest<fuchsia::bluetooth::test::Peer> request,
                        AddLowEnergyPeerCallback callback) override;
  void AddBredrPeer(fuchsia::bluetooth::test::BredrPeerParameters params,
                    fidl::InterfaceRequest<fuchsia::bluetooth::test::Peer> request,
                    AddBredrPeerCallback callback) override;
  void WatchControllerParameters(WatchControllerParametersCallback callback) override;
  void WatchLeScanStates(WatchLeScanStatesCallback callback) override;
  void WatchLegacyAdvertisingStates(WatchLegacyAdvertisingStatesCallback callback) override;

  // Helper function used to initialize BR/EDR and LE peers.
  void AddPeer(std::unique_ptr<Peer> peer);

  void OnControllerParametersChanged();
  void OnLegacyAdvertisingStateChanged();

  // Remove the bt-hci device.
  void UnpublishHci();

  void OnPeerConnectionStateChanged(const bt::DeviceAddress& address,
                                    bt::hci_spec::ConnectionHandle handle, bool connected,
                                    bool canceled);

  // Starts listening for command/event packets on the given channel.
  // Returns false if already listening on a command channel
  bool StartCmdChannel(zx::channel chan);

  // Starts listening for acl packets on the given channel.
  // Returns false if already listening on a acl channel
  bool StartAclChannel(zx::channel chan);

  void CloseCommandChannel();
  void CloseAclDataChannel();

  void SendEvent(std::unique_ptr<bt::hci::EventPacket> event);
  void SendAclPacket(std::unique_ptr<bt::hci::ACLDataPacket> packet);

  // Read and handle packets received over the channels.
  void HandleCommandPacket(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                           zx_status_t wait_status, const zx_packet_signal_t* signal);
  void HandleAclPacket(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                       zx_status_t wait_status, const zx_packet_signal_t* signal);

  static constexpr fuchsia_hardware_bluetooth_Hci_ops_t hci_fidl_ops_ = {
      .OpenCommandChannel = OpenCommandChannel,
      .OpenAclDataChannel = OpenAclDataChannel,
      .OpenSnoopChannel = OpenSnoopChannel,
  };

  static constexpr fuchsia_hardware_bluetooth_Emulator_ops_t emul_fidl_ops_ = {
      .Open = OpenEmulatorChannel,
  };

  // Responsible for running the thread-hostile fake_device_, along with other members listed below.
  // Device publishes a bt-hci child, which is bound to by a bt-host child, which talks to the
  // fake_device_ over some channels. As such, |loop_| cannot be safely shut down until Device's
  // children are released, i.e. loop_ and members responsible for servicing bt-host live past
  // Unbind, and are shut down upon Release.
  async::Loop loop_;

  zx_device_t* const parent_;

  // The device that implements the bt-hci protocol. |hci_dev_| will only be accessed on |loop_|,
  // and only in the following conditions:
  //   1. Initialized during Publish().
  //   2. Unpublished when the HciEmulator FIDL channel (i.e. |binding_|) gets closed, which gets
  //      processed on the |loop_| dispatcher.
  //   3. Unpublished in the DDK Unbind() call. While the Unbind method itself runs on a devhost
  //      thread, the Unpublish call is posted to |loop_| and joined upon during unbind, ensuring
  //      that |hci_dev_| is never accessed across threads.
  zx_device_t* hci_dev_;

  // The device that implements the bt-emulator protocol.
  zx_device_t* emulator_dev_;

  // All objects below are only accessed on the |loop_| dispatcher.
  bt::testing::FakeController fake_device_;

  // Binding for fuchsia.bluetooth.test.HciEmulator channel. |binding_| is only accessed on
  // |loop_|'s dispatcher.
  fidl::Binding<fuchsia::bluetooth::test::HciEmulator> binding_;

  // List of active peers that have been registered with us.
  std::unordered_map<bt::DeviceAddress, std::unique_ptr<Peer>> peers_;

  bt_lib_fidl::HangingGetter<fuchsia::bluetooth::test::ControllerParameters>
      controller_parameters_getter_;
  bt_lib_fidl::HangingVectorGetter<fuchsia::bluetooth::test::LegacyAdvertisingState>
      legacy_adv_state_getter_;

  zx::channel cmd_channel_;
  zx::channel acl_channel_;

  async::WaitMethod<EmulatorDevice, &EmulatorDevice::HandleCommandPacket> cmd_channel_wait_{this};
  async::WaitMethod<EmulatorDevice, &EmulatorDevice::HandleAclPacket> acl_channel_wait_{this};
};

}  // namespace bt_hci_virtual

#endif  // SRC_CONNECTIVITY_BLUETOOTH_HCI_VIRTUAL_EMULATOR_H_
