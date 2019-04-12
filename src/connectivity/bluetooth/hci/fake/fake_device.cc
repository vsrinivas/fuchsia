// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <future>
#include <thread>

#include <ddk/protocol/bt/hci.h>
#include <lib/async/cpp/task.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/connectivity/bluetooth/core/bt-host/testing/fake_peer.h"

#include "fake_device.h"

using ::bt::common::DeviceAddress;
using ::bt::testing::FakeController;
using ::bt::testing::FakePeer;

namespace bthci_fake {

const DeviceAddress kAddress0(DeviceAddress::Type::kLEPublic,
                              "00:00:00:00:00:01");
const DeviceAddress kAddress1(DeviceAddress::Type::kBREDR, "00:00:00:00:00:02");
const DeviceAddress kAddress2(DeviceAddress::Type::kLEPublic,
                              "00:00:00:00:00:03");

Device::Device(zx_device_t* device)
    : loop_(&kAsyncLoopConfigNoAttachToThread), parent_(device) {}

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t bthci_fake_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = [](void* ctx, uint32_t proto_id, void* out_proto)
        -> zx_status_t { return DEV(ctx)->GetProtocol(proto_id, out_proto); },
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .message = [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
      return DEV(ctx)->Message(msg, txn);
    }
};

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
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "bt_hci_fake";
  args.ctx = this;
  args.ops = &bthci_fake_device_ops;
  args.proto_id = ZX_PROTOCOL_BT_HCI;

  zx_status_t status = device_add(parent_, &args, &zxdev_);
  if (status != ZX_OK) {
    printf("bt_hci_fake: could not add device: %d\n", status);
    return status;
  }

  FakeController::Settings settings;
  settings.ApplyDualModeDefaults();
  std::lock_guard<std::mutex> lock(device_lock_);
  fake_device_ = fbl::AdoptRef(new FakeController());
  fake_device_->set_settings(settings);

  // A Sample LE remote peer for le-scan to pick up.
  // TODO(BT-229): add tooling for adding/removing fake devices
  const auto kAdvData0 = bt::common::CreateStaticByteBuffer(
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
  peer->set_class_of_device(bt::common::DeviceClass({0x14, 0x08, 0x00}));
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

void Device::Release() { delete this; }

void Device::Unbind() {
  std::lock_guard<std::mutex> lock(device_lock_);
  async::PostTask(loop_.dispatcher(), [loop = &loop_, fake_dev = fake_device_] {
    fake_dev->Stop();
    loop->Quit();
  });

  loop_.JoinThreads();

  device_remove(zxdev_);
}

zx_status_t Device::Message(fidl_msg_t* msg, fidl_txn_t* txn) {
  std::printf("%s\n", __func__);
  return fuchsia_hardware_bluetooth_Hci_dispatch(this, txn, msg, &fidl_ops_);
}

zx_status_t Device::OpenChan(Channel chan_type, zx_handle_t in_h) {
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
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out_proto) {
  if (proto_id != ZX_PROTOCOL_BT_HCI) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  bt_hci_protocol_t* hci_proto = static_cast<bt_hci_protocol_t*>(out_proto);
  hci_proto->ops = &hci_protocol_ops;
  hci_proto->ctx = this;

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

}  // namespace bthci_fake
