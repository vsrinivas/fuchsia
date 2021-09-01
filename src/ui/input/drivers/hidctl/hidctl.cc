// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hidctl.h"

#include <fidl/fuchsia.hardware.hidctl/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>

#include <memory>
#include <utility>

#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>

#include "src/ui/input/drivers/hidctl/hidctl_bind.h"

namespace hidctl {

zx_status_t HidCtl::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::unique_ptr<HidCtl>(new HidCtl(parent));
  zx_status_t status = dev->DdkAdd("hidctl");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not add device: %d", __func__, status);
  } else {
    // devmgr owns the memory now
    __UNUSED auto* ptr = dev.release();
  }
  return status;
}

void HidCtl::MakeHidDevice(MakeHidDeviceRequestView request,
                           MakeHidDeviceCompleter::Sync& completer) {
  // Create the sockets for Sending/Recieving fake HID reports.
  zx::socket local, remote;
  zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }

  // Create the fake HID device.
  uint8_t* report_desc_data = new uint8_t[request->rpt_desc.count()];
  memcpy(report_desc_data, request->rpt_desc.data(), request->rpt_desc.count());
  fbl::Array<const uint8_t> report_desc(report_desc_data, request->rpt_desc.count());
  auto hiddev = std::unique_ptr<hidctl::HidDevice>(
      new hidctl::HidDevice(zxdev(), request->config, std::move(report_desc), std::move(local)));

  status = hiddev->DdkAdd("hidctl-dev");
  if (status != ZX_OK) {
    zxlogf(ERROR, "hidctl: could not add hid device: %d", status);
    completer.Close(status);
    return;
  }
  // The device thread will be created in DdkInit.

  zxlogf(INFO, "hidctl: created hid device");
  // devmgr owns the memory until release is called
  __UNUSED auto ptr = hiddev.release();

  completer.Reply(std::move(remote));
}

HidCtl::HidCtl(zx_device_t* device) : DeviceType(device) {}

void HidCtl::DdkRelease() { delete this; }

int hid_device_thread(void* arg) {
  HidDevice* device = reinterpret_cast<HidDevice*>(arg);
  return device->Thread();
}

#define HID_SHUTDOWN ZX_USER_SIGNAL_7

HidDevice::HidDevice(zx_device_t* device, const fuchsia_hardware_hidctl::wire::HidCtlConfig& config,
                     fbl::Array<const uint8_t> report_desc, zx::socket data)
    : ddk::Device<HidDevice, ddk::Initializable, ddk::Unbindable>(device),
      boot_device_(config.boot_device),
      dev_class_(config.dev_class),
      report_desc_(std::move(report_desc)),
      data_(std::move(data)) {
  ZX_DEBUG_ASSERT(data_.is_valid());
}

HidDevice::~HidDevice() {
  int ret = thrd_join(thread_, nullptr);
  ZX_DEBUG_ASSERT(ret == thrd_success);
}

void HidDevice::DdkInit(ddk::InitTxn txn) {
  int ret = thrd_create_with_name(&thread_, hid_device_thread, reinterpret_cast<void*>(this),
                                  "hidctl-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);
  txn.Reply(ZX_OK);
}

void HidDevice::DdkRelease() {
  zxlogf(DEBUG, "hidctl: DdkRelease");
  delete this;
}

void HidDevice::DdkUnbind(ddk::UnbindTxn txn) {
  zxlogf(DEBUG, "hidctl: DdkUnbind");
  fbl::AutoLock lock(&lock_);
  if (data_.is_valid()) {
    // Prevent further writes to the socket
    zx_status_t status = data_.set_disposition(0, ZX_SOCKET_DISPOSITION_WRITE_DISABLED);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    // Signal the thread to shutdown
    status = data_.signal(0, HID_SHUTDOWN);
    ZX_DEBUG_ASSERT(status == ZX_OK);
    // The thread will reply to the unbind txn when it exits the loop.
    unbind_txn_ = std::move(txn);
  } else {
    // The thread has already shut down, can reply immediately.
    txn.Reply();
  }
}

zx_status_t HidDevice::HidbusQuery(uint32_t options, hid_info_t* info) {
  zxlogf(DEBUG, "hidctl: query");

  info->dev_num = 0;
  info->device_class = dev_class_;
  info->boot_device = boot_device_;
  return ZX_OK;
}

zx_status_t HidDevice::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  zxlogf(DEBUG, "hidctl: start");

  fbl::AutoLock lock(&lock_);
  if (client_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  client_ = ddk::HidbusIfcProtocolClient(ifc);
  return ZX_OK;
}

void HidDevice::HidbusStop() {
  zxlogf(DEBUG, "hidctl: stop");

  fbl::AutoLock lock(&lock_);
  client_.clear();
}

zx_status_t HidDevice::HidbusGetDescriptor(hid_description_type_t desc_type,
                                           uint8_t* out_data_buffer, size_t data_size,
                                           size_t* out_data_actual) {
  zxlogf(DEBUG, "hidctl: get descriptor %u", desc_type);

  if (out_data_buffer == nullptr || out_data_actual == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
    return ZX_ERR_NOT_FOUND;
  }

  if (data_size < report_desc_.size()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out_data_buffer, report_desc_.data(), report_desc_.size());
  *out_data_actual = report_desc_.size();
  return ZX_OK;
}

zx_status_t HidDevice::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data, size_t len,
                                       size_t* out_len) {
  zxlogf(DEBUG, "hidctl: get report type=%u id=%u", rpt_type, rpt_id);

  if (out_len == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO: send get report message over socket
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidDevice::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const uint8_t* data,
                                       size_t len) {
  zxlogf(DEBUG, "hidctl: set report type=%u id=%u", rpt_type, rpt_id);

  // TODO: send set report message over socket
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidDevice::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
  zxlogf(DEBUG, "hidctl: get idle");

  // TODO: send get idle message over socket
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidDevice::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
  zxlogf(DEBUG, "hidctl: set idle");

  // TODO: send set idle message over socket
  return ZX_OK;
}

zx_status_t HidDevice::HidbusGetProtocol(uint8_t* protocol) {
  zxlogf(DEBUG, "hidctl: get protocol");

  // TODO: send get protocol message over socket
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidDevice::HidbusSetProtocol(uint8_t protocol) {
  zxlogf(DEBUG, "hidctl: set protocol");

  // TODO: send set protocol message over socket
  return ZX_OK;
}

int HidDevice::Thread() {
  zxlogf(DEBUG, "hidctl: starting main thread");
  zx_signals_t pending;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[mtu_]);

  zx_status_t status = ZX_OK;
  const zx_signals_t wait = ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | HID_SHUTDOWN;
  while (true) {
    status = data_.wait_one(wait, zx::time::infinite(), &pending);
    if (status != ZX_OK) {
      zxlogf(ERROR, "hidctl: error waiting on data: %d", status);
      break;
    }

    if (pending & ZX_SOCKET_READABLE) {
      status = Recv(buf.get(), mtu_);
      if (status != ZX_OK) {
        break;
      }
    }
    if (pending & ZX_SOCKET_PEER_CLOSED) {
      zxlogf(DEBUG, "hidctl: socket closed (peer)");
      break;
    }
    if (pending & HID_SHUTDOWN) {
      zxlogf(DEBUG, "hidctl: socket closed (self)");
      break;
    }
  }

  zxlogf(INFO, "hidctl: device destroyed");
  {
    fbl::AutoLock lock(&lock_);
    data_.reset();
    // Check if the device has a pending unbind txn to reply to.
    if (unbind_txn_) {
      unbind_txn_->Reply();
    } else {
      // Request the device unbinding process to begin.
      DdkAsyncRemove();
    }
  }
  return static_cast<int>(status);
}

zx_status_t HidDevice::Recv(uint8_t* buffer, uint32_t capacity) {
  size_t actual = 0;
  zx_status_t status = ZX_OK;
  // Read all the datagrams out of the socket.
  while (status == ZX_OK) {
    status = data_.read(0u, buffer, capacity, &actual);
    if (status == ZX_ERR_SHOULD_WAIT || status == ZX_ERR_PEER_CLOSED) {
      break;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "hidctl: error reading data: %d", status);
      return status;
    }

    fbl::AutoLock lock(&lock_);
    if (unlikely(zxlog_level_enabled(DEBUG))) {
      zxlogf(DEBUG, "hidctl: received %zu bytes", actual);
      hexdump8_ex(buffer, actual, 0);
    }
    if (client_.is_valid()) {
      client_.IoQueue(buffer, actual, zx_clock_get_monotonic());
    }
  }
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = HidCtl::Create;
  return ops;
}();

}  // namespace hidctl

ZIRCON_DRIVER(hidctl, hidctl::driver_ops, "zircon", "0.1");
