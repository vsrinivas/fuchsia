// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <ddk/protocol/bt/hci.h>
#include <lib/async/cpp/task.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdio>
#include <future>
#include <thread>

#include "src/connectivity/bluetooth/core/bt-host/testing/fake_peer.h"
#include "src/connectivity/bluetooth/hci/emulator/log.h"

namespace ftest = fuchsia::bluetooth::test;

using bt::DeviceAddress;
using bt::testing::FakeController;
using bt::testing::FakePeer;

namespace bt_hci_emulator {
namespace {

const DeviceAddress kAddress0(DeviceAddress::Type::kLEPublic,
                              "00:00:00:00:00:01");
const DeviceAddress kAddress1(DeviceAddress::Type::kBREDR, "00:00:00:00:00:02");
const DeviceAddress kAddress2(DeviceAddress::Type::kLEPublic,
                              "00:00:00:00:00:03");

FakeController::Settings SettingsFromFidl(
    const ftest::EmulatorSettings& input) {
  FakeController::Settings settings;
  if (input.has_hci_config() &&
      input.hci_config() == ftest::HciConfig::LE_ONLY) {
    settings.ApplyLEOnlyDefaults();
  } else {
    settings.ApplyDualModeDefaults();
  }

  if (input.has_address()) {
    settings.bd_addr =
        DeviceAddress(DeviceAddress::Type::kBREDR, input.address().bytes);
  }

  // TODO(armansito): Don't ignore "extended_advertising" setting when
  // supported.
  if (input.has_acl_buffer_settings()) {
    settings.acl_data_packet_length =
        input.acl_buffer_settings().data_packet_length;
    settings.total_num_acl_data_packets =
        input.acl_buffer_settings().total_num_data_packets;
  }

  if (input.has_le_acl_buffer_settings()) {
    settings.le_acl_data_packet_length =
        input.le_acl_buffer_settings().data_packet_length;
    settings.le_total_num_acl_data_packets =
        input.le_acl_buffer_settings().total_num_data_packets;
  }

  return settings;
}

}  // namespace

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

// NOTE: We do not implement unbind and release. The lifecycle of the bt-hci
// device is strictly tied to the bt-emulator device (i.e. it can never out-live
// bt-emulator). We handle its destruction in the bt_emulator_device_ops
// messages.
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
  logf(TRACE, "bind\n");

  std::lock_guard<std::mutex> lock(device_lock_);

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt_hci_emulator",
      .ctx = this,
      .ops = &bt_emulator_device_ops,
      .proto_id = ZX_PROTOCOL_BT_EMULATOR,
      .flags = DEVICE_ADD_NON_BINDABLE,
  };
  zx_status_t status = device_add(parent_, &args, &emulator_dev_);
  if (status != ZX_OK) {
    logf(ERROR, "could not add bt-emulator device: %s\n",
         zx_status_get_string(status));
    return status;
  }

  fake_device_ = fbl::AdoptRef(new FakeController());

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

  loop_.StartThread("bt_hci_emulator");

  return status;
}

void Device::Release() {
  logf(TRACE, "release\n");
  delete this;
}

void Device::Unbind() {
  logf(TRACE, "unbind\n");

  bool remove_hci_dev = false;
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

    logf(TRACE, "emulator dispatcher shut down\n");

    remove_hci_dev = (hci_dev_ != nullptr);

    // Destroy the FakeController here. Since |loop_| has been shutdown, we
    // don't expect it to be dereferenced again.
    fake_device_ = nullptr;
  }

  if (remove_hci_dev) {
    device_remove(hci_dev_);
    hci_dev_ = nullptr;
  }

  device_remove(emulator_dev_);
  emulator_dev_ = nullptr;
}

zx_status_t Device::HciMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  logf(TRACE, "HciMessage\n");
  return fuchsia_hardware_bluetooth_Hci_dispatch(this, txn, msg,
                                                 &hci_fidl_ops_);
}

zx_status_t Device::EmulatorMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  logf(TRACE, "EmulatorMessage\n");
  return fuchsia_hardware_bluetooth_Emulator_dispatch(this, txn, msg,
                                                      &emul_fidl_ops_);
}

zx_status_t Device::OpenChan(Channel chan_type, zx_handle_t in_h) {
  logf(TRACE, "open HCI channel\n");

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
  logf(TRACE, "start HciEmulator interface\n");

  std::lock_guard<std::mutex> lock(device_lock_);

  if (binding_.is_bound()) {
    logf(TRACE, "HciEmulator channel already bound\n");
    return;
  }

  // Process HciEmulator messages on a thread that can safely access the
  // FakeController, which is thread-hostile.
  // TODO(BT-229): Remove the bt-hci device if the channel gets closed and close
  // the FakeController's HCI channels.
  binding_.Bind(std::move(chan), loop_.dispatcher());
  binding_.set_error_handler([this](zx_status_t status) {
    logf(TRACE, "emulator channel closed (status: %s); unpublish device\n",
         zx_status_get_string(status));

    std::lock_guard<std::mutex> lock(device_lock_);
    fake_device_->Stop();
  });
}

void Device::Publish(ftest::EmulatorSettings in_settings,
                     PublishCallback callback) {
  logf(TRACE, "HciEmulator.Publish\n");

  std::lock_guard<std::mutex> lock(device_lock_);

  ftest::HciEmulator_Publish_Result result;
  if (hci_dev_) {
    result.set_err(ftest::EmulatorError::HCI_ALREADY_PUBLISHED);
    callback(std::move(result));
    return;
  }

  FakeController::Settings settings = SettingsFromFidl(in_settings);
  fake_device_->set_settings(settings);

  // Publish the bt-hci device.
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt_hci_emulator",
      .ctx = this,
      .ops = &bt_hci_device_ops,
      .proto_id = ZX_PROTOCOL_BT_HCI,
  };
  zx_status_t status = device_add(parent_, &args, &hci_dev_);
  if (status != ZX_OK) {
    result.set_err(ftest::EmulatorError::FAILED);
  } else {
    result.set_response(ftest::HciEmulator_Publish_Response{});
  }

  callback(std::move(result));
}

void Device::AddPeer(ftest::FakePeer peer, AddPeerCallback callback) {
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

}  // namespace bt_hci_emulator
