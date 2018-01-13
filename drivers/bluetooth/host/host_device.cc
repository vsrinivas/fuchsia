// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "host_device.h"

#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/hci/device_wrapper.h"
#include "lib/fsl/threading/create_thread.h"

#include "host.h"

namespace bthost {

HostDevice::HostDevice(zx_device_t* device) : dev_(nullptr), parent_(device) {
  FXL_DCHECK(parent_);

  dev_proto_.version = DEVICE_OPS_VERSION;
  dev_proto_.unbind = &HostDevice::DdkUnbind;
  dev_proto_.release = &HostDevice::DdkRelease;
  dev_proto_.ioctl = &HostDevice::DdkIoctl;
}

zx_status_t HostDevice::Bind() {
  FXL_VLOG(1) << "bthost: bind";

  std::lock_guard<std::mutex> lock(mtx_);

  bt_hci_protocol_t hci_proto;
  zx_status_t status =
      device_get_protocol(parent_, ZX_PROTOCOL_BT_HCI, &hci_proto);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "bthost: Failed to obtain bt-hci protocol ops: "
                   << zx_status_get_string(status);
    return status;
  }

  if (!hci_proto.ops) {
    FXL_LOG(ERROR) << "bthost: bt-hci device ops required!";
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!hci_proto.ops->open_command_channel) {
    FXL_LOG(ERROR) << "bthost: bt-hci op required: open_command_channel";
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!hci_proto.ops->open_acl_data_channel) {
    FXL_LOG(ERROR) << "bthost: bt-hci op required: open_acl_data_channel";
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!hci_proto.ops->open_snoop_channel) {
    FXL_LOG(ERROR) << "bthost: bt-hci op required: open_snoop_channel";
    return ZX_ERR_NOT_SUPPORTED;
  }

  // We are required to publish a device before returning from Bind but we
  // haven't fully initialized the adapter yet. We create the bt-host device as
  // invisible until initialization completes on the host thread. We also
  // disallow other drivers from directly binding to it.
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "bt-host",
      .ctx = this,
      .ops = &dev_proto_,
      .proto_id = ZX_PROTOCOL_BT_HOST,
      .flags = DEVICE_ADD_NON_BINDABLE | DEVICE_ADD_INVISIBLE,
  };

  status = device_add(parent_, &args, &dev_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "bthost: Failed to publish device: "
                   << zx_status_get_string(status);
    return status;
  }

  std::thread host_thread = fsl::CreateThread(&host_thread_runner_, "bt-host");

  // Send the bootstrap message.
  host_thread_runner_->PostTask([hci_proto, this] {
    std::lock_guard<std::mutex> lock(mtx_);
    host_ = fxl::MakeRefCounted<Host>(hci_proto);
    host_->Initialize([host = host_, this](bool success) {
      {
        std::lock_guard<std::mutex> lock(mtx_);

        // Abort if CleanUp has been called.
        if (!host_thread_runner_)
          return;

        if (success) {
          FXL_VLOG(1) << "bthost: Adapter initialized; make device visible";
          device_make_visible(dev_);
          return;
        }

        FXL_LOG(ERROR) << "bthost: Failed to initialize adapter";
        CleanUp();
      }

      host->ShutDown();
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
    });
  });

  host_thread.detach();

  return ZX_OK;
}

void HostDevice::Unbind() {
  FXL_VLOG(1) << "bthost: unbind";

  std::lock_guard<std::mutex> lock(mtx_);

  host_thread_runner_->PostTask([host = host_] {
    host->ShutDown();
    fsl::MessageLoop::GetCurrent()->QuitNow();
  });

  CleanUp();
}

void HostDevice::Release() {
  FXL_VLOG(1) << "bthost: release";
  delete this;
}

zx_status_t HostDevice::Ioctl(uint32_t op,
                              const void* in_buf,
                              size_t in_len,
                              void* out_buf,
                              size_t out_len,
                              size_t* actual) {
  FXL_VLOG(1) << "bthost: ioctl";

  // TODO(armansito): implement

  return ZX_ERR_NOT_SUPPORTED;
}

void HostDevice::CleanUp() {
  host_ = nullptr;
  host_thread_runner_ = nullptr;

  device_remove(dev_);
  dev_ = nullptr;
}

}  // namespace bthost
