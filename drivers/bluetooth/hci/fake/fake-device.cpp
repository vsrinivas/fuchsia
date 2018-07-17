// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <future>
#include <thread>

#include <ddk/protocol/bt-hci.h>
#include <lib/async/cpp/task.h>
#include <zircon/device/bt-hci.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "garnet/drivers/bluetooth/lib/testing/fake_device.h"

#include "fake-device.h"

using ::btlib::common::DeviceAddress;
using ::btlib::testing::FakeController;
using ::btlib::testing::FakeDevice;

namespace bthci_fake {

const DeviceAddress kAddress0(DeviceAddress::Type::kLEPublic,
                              "00:00:00:00:00:01");
const DeviceAddress kAddress1(DeviceAddress::Type::kBREDR, "00:00:00:00:00:02");

Device::Device(zx_device_t* device)
    : loop_(&kAsyncLoopConfigNoAttachToThread), parent_(device) {}

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t bthci_fake_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = [](void* ctx, uint32_t proto_id, void* out_proto)
        -> zx_status_t { return DEV(ctx)->GetProtocol(proto_id, out_proto); },
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .ioctl = [](void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                void* out_buf, size_t out_len,
                size_t* out_actual) -> zx_status_t {
      return DEV(ctx)->Ioctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    },
};

static bt_hci_protocol_ops_t hci_protocol_ops = {
    .open_command_channel = [](void* ctx, zx_handle_t* chan) -> zx_status_t {
      return DEV(ctx)->OpenChan(Channel::COMMAND, chan);
    },
    .open_acl_data_channel = [](void* ctx, zx_handle_t* chan) -> zx_status_t {
      return DEV(ctx)->OpenChan(Channel::ACL, chan);
    },
    .open_snoop_channel = [](void* ctx, zx_handle_t* chan) -> zx_status_t {
      return DEV(ctx)->OpenChan(Channel::SNOOP, chan);
    },
};
#undef DEV

zx_status_t Device::Bind() {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "bthci-fake";
  args.ctx = this;
  args.ops = &bthci_fake_device_ops;
  args.proto_id = ZX_PROTOCOL_BT_HCI;

  zx_status_t status = device_add(parent_, &args, &zxdev_);
  if (status != ZX_OK) {
    printf("bthci-fake: could not add device: %d\n", status);
    return status;
  }

  FakeController::Settings settings;
  settings.ApplyDualModeDefaults();
  std::lock_guard<std::mutex> lock(device_lock_);
  fake_device_ = fbl::AdoptRef(new FakeController());
  fake_device_->set_settings(settings);

  // A Sample LE remote device for le-scan to pick up.
  // TODO(bwb): add tooling for adding/removing fake devices
  const auto kAdvData0 = btlib::common::CreateStaticByteBuffer(
      // Flags
      0x02, 0x01, 0x02,

      // Complete 16-bit service UUIDs
      0x05, 0x03, 0x0d, 0x18, 0x0f, 0x18,

      // Complete local name
      0x05, 0x09, 'F', 'a', 'k', 'e');
  auto device = std::make_unique<FakeDevice>(kAddress0, true, true);
  device->SetAdvertisingData(kAdvData0);
  fake_device_->AddDevice(std::move(device));

  // A Sample BR/EDR remote device to interact with.
  device = std::make_unique<FakeDevice>(kAddress1, false, false);
  // A Toy Game
  device->set_class_of_device(btlib::common::DeviceClass({0x14, 0x08, 0x00}));
  fake_device_->AddDevice(std::move(device));

  loop_.StartThread("bthci-fake");

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

zx_status_t Device::Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                          void* out_buf, size_t out_len, size_t* out_actual) {
  std::printf("%s\n", __func__);
  zx_handle_t* reply = static_cast<zx_handle_t*>(out_buf);
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;

  if (op == IOCTL_BT_HCI_GET_COMMAND_CHANNEL) {
    status = OpenChan(Channel::COMMAND, reply);
  } else if (op == IOCTL_BT_HCI_GET_ACL_DATA_CHANNEL) {
    status = OpenChan(Channel::ACL, reply);
  }

  if (status == ZX_OK) {
    *out_actual = sizeof(*reply);
  }
  return status;
}

zx_status_t Device::OpenChan(Channel chan_type, zx_handle_t* out_channel) {
  zx::channel in, out;
  auto status = zx::channel::create(0, &out, &in);
  if (status != ZX_OK) {
    printf("bthci-fake: could not create channel (type %i): %d\n", chan_type, status);
    return status;
  }
  *out_channel = out.release();
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

}  // namespace bthci_fake
