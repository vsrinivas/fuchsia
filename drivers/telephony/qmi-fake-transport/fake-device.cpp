// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <future>
#include <thread>
#include <lib/zx/channel.h>
#include <lib/async/cpp/task.h>
#include <zircon/device/qmi-transport.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include "fake-device.h"

namespace qmi_fake {

Device::Device(zx_device_t* device) : parent_(device) {}

#define DEV(c) static_cast<Device*>(c)
static zx_protocol_device_t qmi_fake_device_ops = {
    .version = DEVICE_OPS_VERSION,
    .unbind = [](void* ctx) { DEV(ctx)->Unbind(); },
    .get_protocol = [](void* ctx, uint32_t proto_id, void* out_proto)
      -> zx_status_t { return DEV(ctx)->GetProtocol(proto_id, out_proto); },
    .release = [](void* ctx) { DEV(ctx)->Release(); },
    .ioctl = [](void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                void* out_buf, size_t out_len,
                size_t* out_actual) -> zx_status_t {
      return DEV(ctx)->Ioctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    },
};
#undef DEV

zx_status_t Device::Bind() {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "qmi-fake";
  args.ctx = this;
  args.ops = &qmi_fake_device_ops;
  args.proto_id = ZX_PROTOCOL_QMI_TRANSPORT;

  zx_status_t status = device_add(parent_, &args, &zxdev_);
  if (status != ZX_OK) {
    printf("qmi-fake: could not add device: %d\n", status);
    return status;
  }

  return status;
}

void Device::Release() { delete this; }

void Device::Unbind() {
  device_remove(zxdev_);
}

zx_status_t Device::Ioctl(uint32_t op, const void* in_buf, size_t in_len,
                          void* out_buf, size_t out_len, size_t* out_actual) {
  printf("%s\n", __func__);
  zx_handle_t* reply = static_cast<zx_handle_t*>(out_buf);
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;

  if (op == IOCTL_QMI_GET_CHANNEL) {
    status = OpenChan(reply);
  }

  if (status == ZX_OK) {
    *out_actual = sizeof(*reply);
  }
  return status;
}

zx_status_t Device::GetProtocol(uint32_t proto_id, void* out_proto) {
  printf("%s\n", __func__);
  if (proto_id != ZX_PROTOCOL_QMI_TRANSPORT) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t Device::OpenChan(zx_handle_t* out_channel) {
  zx::channel in, out;
  auto status = zx::channel::create(0, &out, &in);
  if (status != ZX_OK) {
    printf("qmi-fake: could not create channel: %d\n", status);
    return status;
  }
  *out_channel = out.release();
  hold_chan_ = in.release();

  return ZX_OK;
}

}  // namespace qmi_fake
