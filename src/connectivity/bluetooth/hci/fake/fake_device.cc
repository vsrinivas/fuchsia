// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_device.h"

#include <ddk/debug.h>
#include <ddk/protocol/bt/hci.h>
#include <lib/async/cpp/task.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdio>
#include <future>
#include <thread>

#include "src/connectivity/bluetooth/core/bt-host/testing/fake_peer.h"

using fuchsia::bluetooth::test::EmulatorSettings;
using FidlFakePeer = fuchsia::bluetooth::test::FakePeer;
using bt::DeviceAddress;
using bt::testing::FakeController;
using bt::testing::FakePeer;
using fuchsia::bluetooth::test::EmulatorError;
using fuchsia::bluetooth::test::HciEmulator_Publish_Result;

namespace bthci_fake {

const DeviceAddress kAddress0(DeviceAddress::Type::kLEPublic,
                              "00:00:00:00:00:01");
const DeviceAddress kAddress1(DeviceAddress::Type::kBREDR, "00:00:00:00:00:02");
const DeviceAddress kAddress2(DeviceAddress::Type::kLEPublic,
                              "00:00:00:00:00:03");

Device::Device(zx_device_t* device)
    : loop_(&kAsyncLoopConfigNoAttachToThread),
      parent_(device),
      hci_dev_(nullptr),
      emulator_dev_(nullptr),
      binding_(this) {}

#define DEV(c) static_cast<Device*>(c)

static zx_protocol_device_t bt_emulator_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = [](void* ctx, uint32_t proto_id, void* out_proto)
        -> zx_status_t { return DEV(ctx)->GetProtocol(proto_id, out_proto); },
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message =
        [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
          return DEV(ctx)->EmulatorMessage(msg, txn);
        }};

static zx_protocol_device_t bt_hci_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = [](void* ctx, uint32_t proto_id, void* out_proto)
        -> zx_status_t { return DEV(ctx)->GetProtocol(proto_id, out_proto); },
    .message = [](void* ctx, fidl_msg_t* msg,
                  fidl_txn_t* txn) { return DEV(ctx)->HciMessage(msg, txn); }};

static bt_hci_protocol_ops_t hci_protocol_ops = {
    .open_command_channel = [](void* ctx, zx_handle_t chan) -> zx_status_t {
      return DEV(ctx)->OpenChan(Channel::COMMAND, chan);
    },
    .open_acl_data_channel = [](void* ctx, zx_handle_t chan) -> zx_status_t {
      return DEV(ctx)->OpenChan(Channel::ACL, chan);
    },
    .open_snoop_channel = [](void* ctx, zx_handle_t chan) -> zx_status_t {
      return DEV(ctx)->OpenChan(Channel::SNOOP, chan);
    },
};

#undef DEV

zx_status_t Device::Bind() {
  zxlogf(TRACE, "bt-fake-hci: bind\n");

  std::lock_guard<std::mutex> lock(device_lock_);

  // TODO(BT-229): Don't publish a bt-hci device until receiving a
  // HciEmulator.Publish message.
  device_add_args_t hci_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt_hci_fake",
      .ctx = this,
      .ops = &bt_hci_device_ops,
      .proto_id = ZX_PROTOCOL_BT_HCI,
  };

  zx_status_t status = device_add(parent_, &hci_args, &hci_dev_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-hci-fake: could not add device: %d\n", status);
    return status;
  }

  // Publish the bt-emulator device as a child of the bt-hci device. This
  // provides a control channel to manipulate behavior.
  device_add_args_t emul_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt_hci_fake",
      .ctx = this,
      .ops = &bt_emulator_device_ops,
      .flags = DEVICE_ADD_NON_BINDABLE,
      .proto_id = ZX_PROTOCOL_BT_EMULATOR,
  };
  status = device_add(parent_, &emul_args, &emulator_dev_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "bt-hci-fake: could not add bt-emulator device: %d\n",
           status);
    device_remove(hci_dev_);
    return status;
  }

  // TODO(BT-229): Apply settings in Publish().
  FakeController::Settings settings;
  settings.ApplyDualModeDefaults();
  fake_device_ = fbl::AdoptRef(new FakeController());
  fake_device_->set_settings(settings);

  // A Sample LE remote peer for le-scan to pick up.
  // TODO(BT-229): add tooling for adding/removing fake devices
  const auto kAdvData0 = bt::CreateStaticByteBuffer(
      // Flags
      0x02, 0x01, 0x02,

      // Complete 16-bit service UUIDs
      0x05, 0x03, 0x0d, 0x18, 0x0f, 0x18,

      // Complete local name
      0x05, 0x09, 'F', 'a', 'k', 'e');
  auto peer = std::make_unique<FakePeer>(kAddress0, true, true);
  peer->SetAdvertisingData(kAdvData0);
  fake_device_->AddPeer(std::move(peer));

  // A Sample BR/EDR remote peer to interact with.
  peer = std::make_unique<FakePeer>(kAddress1, false, false);
  // A Toy Game
  peer->set_class_of_device(bt::DeviceClass({0x14, 0x08, 0x00}));
  fake_device_->AddPeer(std::move(peer));

  // Add a LE peer that always fails to connect.
  // TODO(BT-229): Allow this to be created programmatically by
  // clients of this driver.
  peer = std::make_unique<FakePeer>(kAddress2, true, true);
  peer->SetAdvertisingData(kAdvData0);
  peer->set_connect_response(bt::hci::StatusCode::kConnectionTimeout);
  fake_device_->AddPeer(std::move(peer));

  loop_.StartThread("bt_hci_fake");

  return status;
}

void Device::Release() {
  zxlogf(TRACE, "bt-fake-hci: release\n");
  delete this;
}

void Device::Unbind() {
  zxlogf(TRACE, "bt-fake-hci: unbind\n");
  {
    std::lock_guard<std::mutex> lock(device_lock_);

    // Clean up all FIDL channels and the underlying FakeController on the
    // dispatcher thread, due to the FakeController object's thread-safety
    // requirements. It is OK to capture references to members in the task since
    // this function will block until the dispatcher loop has terminated.
    async::PostTask(loop_.dispatcher(),
                    [binding = &binding_, dev = fake_device_, loop = &loop_] {
                      binding->Unbind();
                      dev->Stop();
                      loop->Quit();
                    });

    loop_.JoinThreads();

    zxlogf(TRACE, "bt-fake-hci: emulator dispatcher shut down\n");
  }

  device_remove(hci_dev_);
  hci_dev_ = nullptr;

  device_remove(emulator_dev_);
  emulator_dev_ = nullptr;
}

zx_status_t Device::HciMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  zxlogf(TRACE, "bt-fake-hci: HciMessage\n");
  return fuchsia_hardware_bluetooth_Hci_dispatch(this, txn, msg,
                                                 &hci_fidl_ops_);
}

zx_status_t Device::EmulatorMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  zxlogf(TRACE, "bt-fake-hci: EmulatorMessage\n");
  return fuchsia_hardware_bluetooth_Emulator_dispatch(this, txn, msg,
                                                      &emul_fidl_ops_);
}

zx_status_t Device::OpenChan(Channel chan_type, zx_handle_t in_h) {
  zxlogf(TRACE, "bt-fake-hci: open HCI channel\n");

  zx::channel in(in_h);
  std::lock_guard<std::mutex> lock(device_lock_);

  if (chan_type == Channel::COMMAND) {
    async::PostTask(loop_.dispatcher(),
                    [device = fake_device_, in = std::move(in)]() mutable {
                      device->StartCmdChannel(std::move(in));
                    });
  } else if (chan_type == Channel::ACL) {
    async::PostTask(loop_.dispatcher(),
                    [device = fake_device_, in = std::move(in)]() mutable {
                      device->StartAclChannel(std::move(in));
                    });
  } else if (chan_type == Channel::SNOOP) {
    async::PostTask(loop_.dispatcher(),
                    [device = fake_device_, in = std::move(in)]() mutable {
                      device->StartSnoopChannel(std::move(in));
                    });
  } else if (chan_type == Channel::EMULATOR) {
    async::PostTask(loop_.dispatcher(), [this, in = std::move(in)]() mutable {
      StartEmulatorInterface(std::move(in));
    });
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

void Device::StartEmulatorInterface(zx::channel chan) {
  zxlogf(TRACE, "bt-fake-hci: start HciEmulator interface\n");

  std::lock_guard<std::mutex> lock(device_lock_);

  if (binding_.is_bound()) {
    zxlogf(TRACE, "bt-fake-hci: HciEmulator channel already bound\n");
    return;
  }

  // Process HciEmulator messages on a thread that can safely access the
  // FakeController, which is thread-hostile.
  // TODO(BT-229): Remove the bt-hci device if the channel gets closed and close
  // the FakeController's HCI channels.
  binding_.Bind(std::move(chan), loop_.dispatcher());
}

void Device::Publish(EmulatorSettings settings, PublishCallback callback) {
  zxlogf(TRACE, "bt-fake-hci: HciEmulator.Publish\n");

  // TODO(BT-229): Implement. This logic is placeholder for basic verification.
  HciEmulator_Publish_Result result;
  result.set_err(EmulatorError::HCI_ALREADY_PUBLISHED);
  callback(std::move(result));
}

void Device::AddPeer(FidlFakePeer peer, AddPeerCallback callback) {
  // TODO(BT-229): Implement
}

void Device::RemovePeer(::fuchsia::bluetooth::PeerId id,
                        RemovePeerCallback callback) {
  // TODO(BT-229): Implement
}

void Device::WatchLeScanState(WatchLeScanStateCallback callback) {
  // TODO(BT-229): Implement
}

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out_proto) {
  // The bt-emulator device doesn't support a non-FIDL protocol.
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  bt_hci_protocol_t* hci_proto = static_cast<bt_hci_protocol_t*>(out_proto);
  hci_proto->ctx = this;
  hci_proto->ops = &hci_protocol_ops;

  return ZX_OK;
}

zx_status_t Device::OpenCommandChannel(void* ctx, zx_handle_t channel) {
  return static_cast<Device*>(ctx)->OpenChan(Channel::COMMAND, channel);
}

zx_status_t Device::OpenAclDataChannel(void* ctx, zx_handle_t channel) {
  return static_cast<Device*>(ctx)->OpenChan(Channel::ACL, channel);
}

zx_status_t Device::OpenSnoopChannel(void* ctx, zx_handle_t channel) {
  return static_cast<Device*>(ctx)->OpenChan(Channel::SNOOP, channel);
}

zx_status_t Device::OpenEmulatorChannel(void* ctx, zx_handle_t channel) {
  return static_cast<Device*>(ctx)->OpenChan(Channel::EMULATOR, channel);
}

}  // namespace bthci_fake
