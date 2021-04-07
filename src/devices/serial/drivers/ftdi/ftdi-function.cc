// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/hardware/usb/function/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb/hid.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <fbl/algorithm.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>

#include "src/devices/serial/drivers/ftdi/ftdi_function_bind.h"

#define BULK_MAX_PACKET 512
#define FTDI_STATUS_SIZE 2

namespace fake_ftdi_function {

class FakeFtdiFunction;
using DeviceType = ddk::Device<FakeFtdiFunction, ddk::Unbindable>;
class FakeFtdiFunction : public DeviceType {
 public:
  FakeFtdiFunction(zx_device_t* parent) : DeviceType(parent), function_(parent) {}
  zx_status_t Bind();
  // |ddk::Device|
  void DdkUnbind(ddk::UnbindTxn txn);
  // |ddk::Device|
  void DdkRelease();

  static size_t UsbFunctionInterfaceGetDescriptorsSize(void* ctx);

  static void UsbFunctionInterfaceGetDescriptors(void* ctx, uint8_t* out_descriptors_buffer,
                                                 size_t descriptors_size,
                                                 size_t* out_descriptors_actual);
  static zx_status_t UsbFunctionInterfaceControl(void* ctx, const usb_setup_t* setup,
                                                 const uint8_t* write_buffer, size_t write_size,
                                                 uint8_t* out_read_buffer, size_t read_size,
                                                 size_t* out_read_actual);
  static zx_status_t UsbFunctionInterfaceSetConfigured(void* ctx, bool configured,
                                                       usb_speed_t speed);
  static zx_status_t UsbFunctionInterfaceSetInterface(void* ctx, uint8_t interface,
                                                      uint8_t alt_setting);

 private:
  int Thread();
  void DataInComplete() TA_REQ(mtx_);
  void DataOutComplete() TA_REQ(mtx_);
  void RequestQueue(usb_request_t* req, const usb_request_complete_t* completion);
  void CompletionCallback(usb_request_t* req);

  usb_function_interface_protocol_ops_t function_interface_ops_{
      .get_descriptors_size = UsbFunctionInterfaceGetDescriptorsSize,
      .get_descriptors = UsbFunctionInterfaceGetDescriptors,
      .control = UsbFunctionInterfaceControl,
      .set_configured = UsbFunctionInterfaceSetConfigured,
      .set_interface = UsbFunctionInterfaceSetInterface,
  };
  ddk::UsbFunctionProtocolClient function_;

  struct fake_ftdi_descriptor_t {
    usb_interface_descriptor_t interface;
    usb_endpoint_descriptor_t bulk_in;
    usb_endpoint_descriptor_t bulk_out;
  } __PACKED descriptor_;

  size_t descriptor_size_ = 0;

  size_t parent_req_size_ = 0;
  uint8_t bulk_out_addr_ = 0;
  uint8_t bulk_in_addr_ = 0;

  std::optional<usb::Request<>> data_in_req_ TA_GUARDED(mtx_);
  bool data_in_req_complete_ TA_GUARDED(mtx_) = false;

  std::optional<usb::Request<>> data_out_req_ TA_GUARDED(mtx_);
  bool data_out_req_complete_ TA_GUARDED(mtx_) = false;

  fbl::ConditionVariable event_ TA_GUARDED(mtx_);
  fbl::Mutex mtx_;

  bool configured_ = false;
  bool active_ = false;
  thrd_t thread_ = {};
  std::atomic<int> pending_request_count_;
};

void FakeFtdiFunction::CompletionCallback(usb_request_t* req) {
  fbl::AutoLock lock(&mtx_);
  if (req == data_in_req_->request()) {
    data_in_req_complete_ = true;
  } else if (req == data_out_req_->request()) {
    data_out_req_complete_ = true;
  }
  event_.Signal();
}

void FakeFtdiFunction::RequestQueue(usb_request_t* req, const usb_request_complete_t* completion) {
  atomic_fetch_add(&pending_request_count_, 1);
  function_.RequestQueue(req, completion);
}

void FakeFtdiFunction::DataInComplete() {}

void FakeFtdiFunction::DataOutComplete() {
  if (data_out_req_->request()->response.status != ZX_OK) {
    return;
  }
  std::vector<uint8_t> data(data_out_req_->request()->response.actual);
  // std::vector should zero-initialize
  __UNUSED size_t copied =
      usb_request_copy_from(data_out_req_->request(), data.data(), data.size(), 0);

  usb_request_complete_t complete = {
      .callback =
          [](void* ctx, usb_request_t* req) {
            return static_cast<FakeFtdiFunction*>(ctx)->CompletionCallback(req);
          },
      .ctx = this,
  };

  // Queue up another write.
  RequestQueue(data_out_req_->request(), &complete);

  // Queue up exact same read data. The first FTDI_STATUS_SIZE bytes should
  // be left alone.
  data_in_req_->request()->header.length = data.size() + FTDI_STATUS_SIZE;
  data_in_req_->request()->header.ep_address = bulk_in_addr_;

  copied = data_in_req_->CopyTo(data.data(), data.size(), FTDI_STATUS_SIZE);

  RequestQueue(data_in_req_->request(), &complete);
}

int FakeFtdiFunction::Thread() {
  while (1) {
    fbl::AutoLock lock(&mtx_);
    if (!(data_in_req_complete_ || data_out_req_complete_ || (!active_))) {
      event_.Wait(&mtx_);
    }
    if (!active_ && !atomic_load(&pending_request_count_)) {
      return 0;
    }
    if (data_in_req_complete_) {
      atomic_fetch_add(&pending_request_count_, -1);
      data_in_req_complete_ = false;
      DataInComplete();
    }
    if (data_out_req_complete_) {
      atomic_fetch_add(&pending_request_count_, -1);
      data_out_req_complete_ = false;
      DataOutComplete();
    }
  }
  return 0;
}

size_t FakeFtdiFunction::UsbFunctionInterfaceGetDescriptorsSize(void* ctx) {
  FakeFtdiFunction* func = static_cast<FakeFtdiFunction*>(ctx);
  return func->descriptor_size_;
}

void FakeFtdiFunction::UsbFunctionInterfaceGetDescriptors(void* ctx,
                                                          uint8_t* out_descriptors_buffer,
                                                          size_t descriptors_size,
                                                          size_t* out_descriptors_actual) {
  FakeFtdiFunction* func = static_cast<FakeFtdiFunction*>(ctx);
  memcpy(out_descriptors_buffer, &func->descriptor_,
         std::min(descriptors_size, func->descriptor_size_));
  *out_descriptors_actual = func->descriptor_size_;
}

zx_status_t FakeFtdiFunction::UsbFunctionInterfaceControl(
    void* ctx, const usb_setup_t* setup, const uint8_t* write_buffer, size_t write_size,
    uint8_t* out_read_buffer, size_t read_size, size_t* out_read_actual) {
  if (out_read_actual) {
    *out_read_actual = 0;
  }
  return ZX_OK;
}

zx_status_t FakeFtdiFunction::UsbFunctionInterfaceSetConfigured(void* ctx, bool configured,
                                                                usb_speed_t speed) {
  FakeFtdiFunction* func = static_cast<FakeFtdiFunction*>(ctx);
  fbl::AutoLock lock(&func->mtx_);
  zx_status_t status;

  if (configured) {
    if (func->configured_) {
      return ZX_OK;
    }
    func->configured_ = true;

    if ((status = func->function_.ConfigEp(&func->descriptor_.bulk_in, nullptr)) != ZX_OK ||
        (status = func->function_.ConfigEp(&func->descriptor_.bulk_out, nullptr)) != ZX_OK) {
      zxlogf(ERROR, "ftdi-function: usb_function_config_ep failed");
    }
    // queue first read on OUT endpoint
    usb_request_complete_t complete = {
        .callback =
            [](void* ctx, usb_request_t* req) {
              return static_cast<FakeFtdiFunction*>(ctx)->CompletionCallback(req);
            },
        .ctx = ctx,
    };
    zxlogf(INFO, "ftdi-function: about to configure!");
    if (func->data_out_req_) {
      zxlogf(INFO, "We have data out!");
    }
    func->RequestQueue(func->data_out_req_->request(), &complete);
  } else {
    func->configured_ = false;
  }
  return ZX_OK;
}

zx_status_t FakeFtdiFunction::UsbFunctionInterfaceSetInterface(void* ctx, uint8_t interface,
                                                               uint8_t alt_setting) {
  return ZX_OK;
}

zx_status_t FakeFtdiFunction::Bind() {
  fbl::AutoLock lock(&mtx_);

  descriptor_size_ = sizeof(descriptor_);
  descriptor_.interface = {
      .b_length = sizeof(usb_interface_descriptor_t),
      .b_descriptor_type = USB_DT_INTERFACE,
      .b_interface_number = 0,
      .b_alternate_setting = 0,
      .b_num_endpoints = 2,
      .b_interface_class = 0xFF,
      .b_interface_sub_class = 0xFF,
      .b_interface_protocol = 0xFF,
      .i_interface = 0,
  };
  descriptor_.bulk_in = {
      .b_length = sizeof(usb_endpoint_descriptor_t),
      .b_descriptor_type = USB_DT_ENDPOINT,
      .b_endpoint_address = USB_ENDPOINT_IN,  // set later
      .bm_attributes = USB_ENDPOINT_BULK,
      .w_max_packet_size = htole16(BULK_MAX_PACKET),
      .b_interval = 0,
  };
  descriptor_.bulk_out = {
      .b_length = sizeof(usb_endpoint_descriptor_t),
      .b_descriptor_type = USB_DT_ENDPOINT,
      .b_endpoint_address = USB_ENDPOINT_OUT,  // set later
      .bm_attributes = USB_ENDPOINT_BULK,
      .w_max_packet_size = htole16(BULK_MAX_PACKET),
      .b_interval = 0,
  };

  active_ = true;
  atomic_init(&pending_request_count_, 0);

  parent_req_size_ = function_.GetRequestSize();

  zx_status_t status = function_.AllocInterface(&descriptor_.interface.b_interface_number);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeFtdiFunction: usb_function_alloc_interface failed");
    return status;
  }
  status = function_.AllocEp(USB_DIR_IN, &descriptor_.bulk_in.b_endpoint_address);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeFtdiFunction: usb_function_alloc_ep failed");
    return status;
  }
  status = function_.AllocEp(USB_DIR_OUT, &descriptor_.bulk_out.b_endpoint_address);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeFtdiFunction: usb_function_alloc_ep failed");
    return status;
  }

  bulk_in_addr_ = descriptor_.bulk_in.b_endpoint_address;
  bulk_out_addr_ = descriptor_.bulk_out.b_endpoint_address;

  status = usb::Request<>::Alloc(&data_out_req_, BULK_MAX_PACKET, bulk_out_addr_, parent_req_size_);
  if (status != ZX_OK) {
    return status;
  }
  status = usb::Request<>::Alloc(&data_in_req_, BULK_MAX_PACKET, bulk_in_addr_, parent_req_size_);
  if (status != ZX_OK) {
    return status;
  }

  status = DdkAdd("usb-hid-function");
  if (status != ZX_OK) {
    return status;
  }
  function_.SetInterface(this, &function_interface_ops_);

  thrd_create(
      &thread_, [](void* ctx) { return static_cast<FakeFtdiFunction*>(ctx)->Thread(); }, this);

  return ZX_OK;
}

void FakeFtdiFunction::DdkUnbind(ddk::UnbindTxn txn) {
  fbl::AutoLock lock(&mtx_);
  active_ = false;
  event_.Signal();
  lock.release();

  int retval;
  thrd_join(thread_, &retval);

  txn.Reply();
}

void FakeFtdiFunction::DdkRelease() { delete this; }

zx_status_t bind(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<FakeFtdiFunction>(parent);
  zx_status_t status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    dev.release();
  }
  return ZX_OK;
}
static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bind;
  return ops;
}();

}  // namespace fake_ftdi_function

ZIRCON_DRIVER(ftdi_function, fake_ftdi_function::driver_ops, "zircon", "0.1");
