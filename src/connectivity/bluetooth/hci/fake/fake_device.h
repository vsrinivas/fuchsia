// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/bt/hci.h>
#include <ddk/protocol/test.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/bluetooth/test/cpp/fidl.h>
#include <fuchsia/hardware/bluetooth/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "src/connectivity/bluetooth/core/bt-host/testing/fake_controller.h"

namespace bthci_fake {

enum class Channel {
  ACL,
  COMMAND,
  SNOOP,
  EMULATOR,
};

class Device : public fuchsia::bluetooth::test::HciEmulator {
 public:
  explicit Device(zx_device_t* device);

  zx_status_t Bind();
  void Unbind();
  void Release();

  zx_status_t HciMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t EmulatorMessage(fidl_msg_t* msg, fidl_txn_t* txn);
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
  void AddPeer(fuchsia::bluetooth::test::FakePeer peer,
               AddPeerCallback callback) override;
  void RemovePeer(fuchsia::bluetooth::PeerId id,
                  RemovePeerCallback callback) override;
  void WatchLeScanState(WatchLeScanStateCallback callback) override;

  static constexpr fuchsia_hardware_bluetooth_Hci_ops_t hci_fidl_ops_ = {
      .OpenCommandChannel = OpenCommandChannel,
      .OpenAclDataChannel = OpenAclDataChannel,
      .OpenSnoopChannel = OpenSnoopChannel,
  };

  static constexpr fuchsia_hardware_bluetooth_Emulator_ops_t emul_fidl_ops_ = {
      .Open = OpenEmulatorChannel,
  };

  std::mutex device_lock_;

  async::Loop loop_ __TA_GUARDED(device_lock_);
  fbl::RefPtr<bt::testing::FakeController> fake_device_
      __TA_GUARDED(device_lock_);

  zx_device_t* parent_;

  // The device that implements the bt-hci protocol.
  zx_device_t* hci_dev_;

  // The device that implements the bt-emulator protocol.
  zx_device_t* emulator_dev_;

  // Binding for fuchsia.bluetooth.test.HciEmulator channel.
  fidl::Binding<fuchsia::bluetooth::test::HciEmulator> binding_
      __TA_GUARDED(device_lock_);
};

}  // namespace bthci_fake
