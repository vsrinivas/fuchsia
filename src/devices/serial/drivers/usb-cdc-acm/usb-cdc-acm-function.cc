// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/hardware/usb/function/cpp/banjo.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/usb-peripheral.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>
#include <zircon/hw/usb/hid.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <memory>
#include <vector>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <fbl/algorithm.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#define BULK_MAX_PACKET 512

namespace fake_usb_cdc_acm_function {

// Acts as a fake USB device for CDC-ACM serial tests. Stores a single write's worth of data and
// echos it back on the next read, unless the write is exactly a single '0' byte, in which case
// the next read will be an empty response.
class FakeUsbCdcAcmFunction;
using DeviceType = ddk::Device<FakeUsbCdcAcmFunction, ddk::Unbindable>;
class FakeUsbCdcAcmFunction : public DeviceType,
                              public ddk::UsbFunctionInterfaceProtocol<FakeUsbCdcAcmFunction> {
 public:
  explicit FakeUsbCdcAcmFunction(zx_device_t* parent) : DeviceType(parent), function_(parent) {}
  zx_status_t Bind();

  // |ddk::Device| mix-in implementations.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  size_t UsbFunctionInterfaceGetDescriptorsSize();
  void UsbFunctionInterfaceGetDescriptors(void* out_descriptors_buffer, size_t descriptors_size,
                                          size_t* out_descriptors_actual);
  zx_status_t UsbFunctionInterfaceControl(const usb_setup_t* setup, const void* write_buffer,
                                          size_t write_size, void* out_read_buffer,
                                          size_t read_size, size_t* out_read_actual);
  zx_status_t UsbFunctionInterfaceSetConfigured(bool configured, usb_speed_t speed);
  zx_status_t UsbFunctionInterfaceSetInterface(uint8_t interface, uint8_t alt_setting);

 private:
  int Thread();
  void DataInComplete() TA_REQ(mtx_);
  void DataOutComplete() TA_REQ(mtx_);
  void RequestQueue(usb_request_t* req, const usb_request_complete_t* completion);
  void CompletionCallback(usb_request_t* req);

  ddk::UsbFunctionProtocolClient function_;

  struct FakeUscCdcAcmDescriptor {
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

void FakeUsbCdcAcmFunction::CompletionCallback(usb_request_t* req) {
  fbl::AutoLock lock(&mtx_);
  if (req == data_in_req_->request()) {
    data_in_req_complete_ = true;
  } else if (req == data_out_req_->request()) {
    data_out_req_complete_ = true;
  }
  event_.Signal();
}

void FakeUsbCdcAcmFunction::RequestQueue(usb_request_t* req,
                                         const usb_request_complete_t* completion) {
  atomic_fetch_add(&pending_request_count_, 1);
  function_.RequestQueue(req, completion);
}

void FakeUsbCdcAcmFunction::DataInComplete() {}

void FakeUsbCdcAcmFunction::DataOutComplete() {
  if (data_out_req_->request()->response.status != ZX_OK) {
    return;
  }
  std::vector<uint8_t> data(data_out_req_->request()->response.actual);
  __UNUSED size_t copied =
      usb_request_copy_from(data_out_req_->request(), data.data(), data.size(), 0);

  usb_request_complete_t complete = {
      .callback =
          [](void* ctx, usb_request_t* req) {
            return static_cast<FakeUsbCdcAcmFunction*>(ctx)->CompletionCallback(req);
          },
      .ctx = this,
  };

  // Queue up another write.
  RequestQueue(data_out_req_->request(), &complete);

  // Queue up the exact same read data, unless the read was a single '0', in which case queue an
  // empty response.
  if (data.size() == 1 && data[0] == '0') {
    data.clear();
  }
  data_in_req_->request()->header.length = data.size();
  data_in_req_->request()->header.ep_address = bulk_in_addr_;

  copied = data_in_req_->CopyTo(data.data(), data.size(), 0);

  RequestQueue(data_in_req_->request(), &complete);
}

int FakeUsbCdcAcmFunction::Thread() {
  while (true) {
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

size_t FakeUsbCdcAcmFunction::UsbFunctionInterfaceGetDescriptorsSize() { return descriptor_size_; }

void FakeUsbCdcAcmFunction::UsbFunctionInterfaceGetDescriptors(void* out_descriptors_buffer,
                                                               size_t descriptors_size,
                                                               size_t* out_descriptors_actual) {
  memcpy(out_descriptors_buffer, &descriptor_, std::min(descriptors_size, descriptor_size_));
  *out_descriptors_actual = descriptor_size_;
}

zx_status_t FakeUsbCdcAcmFunction::UsbFunctionInterfaceControl(
    const usb_setup_t* setup, const void* write_buffer, size_t write_size, void* out_read_buffer,
    size_t read_size, size_t* out_read_actual) {
  if (out_read_actual) {
    *out_read_actual = 0;
  }
  return ZX_OK;
}

zx_status_t FakeUsbCdcAcmFunction::UsbFunctionInterfaceSetConfigured(bool configured,
                                                                     usb_speed_t speed) {
  fbl::AutoLock lock(&mtx_);
  zx_status_t status;

  if (configured) {
    if (configured_) {
      return ZX_OK;
    }
    configured_ = true;

    if ((status = function_.ConfigEp(&descriptor_.bulk_in, nullptr)) != ZX_OK ||
        (status = function_.ConfigEp(&descriptor_.bulk_out, nullptr)) != ZX_OK) {
      zxlogf(ERROR, "usb-cdc-acm-function: usb_function_config_ep failed");
    }
    // queue first read on OUT endpoint
    usb_request_complete_t complete = {
        .callback =
            [](void* ctx, usb_request_t* req) {
              return static_cast<FakeUsbCdcAcmFunction*>(ctx)->CompletionCallback(req);
            },
        .ctx = this,
    };
    zxlogf(INFO, "usb-cdc-acm-function: about to configure!");
    if (data_out_req_) {
      zxlogf(INFO, "We have data out!");
    }
    RequestQueue(data_out_req_->request(), &complete);
  } else {
    configured_ = false;
  }
  return ZX_OK;
}

zx_status_t FakeUsbCdcAcmFunction::UsbFunctionInterfaceSetInterface(uint8_t interface,
                                                                    uint8_t alt_setting) {
  return ZX_OK;
}

zx_status_t FakeUsbCdcAcmFunction::Bind() {
  fbl::AutoLock lock(&mtx_);

  descriptor_size_ = sizeof(descriptor_);
  descriptor_.interface = {
      .bLength = sizeof(usb_interface_descriptor_t),
      .bDescriptorType = USB_DT_INTERFACE,
      .bInterfaceNumber = 0,
      .bAlternateSetting = 0,
      .bNumEndpoints = 2,
      .bInterfaceClass = USB_CLASS_COMM,
      .bInterfaceSubClass = USB_CDC_SUBCLASS_ABSTRACT,
      .bInterfaceProtocol = 1,
      .iInterface = 0,
  };
  descriptor_.bulk_in = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_IN,  // set later
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = htole16(BULK_MAX_PACKET),
      .bInterval = 0,
  };
  descriptor_.bulk_out = {
      .bLength = sizeof(usb_endpoint_descriptor_t),
      .bDescriptorType = USB_DT_ENDPOINT,
      .bEndpointAddress = USB_ENDPOINT_OUT,  // set later
      .bmAttributes = USB_ENDPOINT_BULK,
      .wMaxPacketSize = htole16(BULK_MAX_PACKET),
      .bInterval = 0,
  };

  active_ = true;
  atomic_init(&pending_request_count_, 0);

  parent_req_size_ = function_.GetRequestSize();

  zx_status_t status = function_.AllocInterface(&descriptor_.interface.bInterfaceNumber);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbCdcAcmFunction: usb_function_alloc_interface failed");
    return status;
  }
  status = function_.AllocEp(USB_DIR_IN, &descriptor_.bulk_in.bEndpointAddress);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbCdcAcmFunction: usb_function_alloc_ep failed");
    return status;
  }
  status = function_.AllocEp(USB_DIR_OUT, &descriptor_.bulk_out.bEndpointAddress);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FakeUsbCdcAcmFunction: usb_function_alloc_ep failed");
    return status;
  }

  bulk_in_addr_ = descriptor_.bulk_in.bEndpointAddress;
  bulk_out_addr_ = descriptor_.bulk_out.bEndpointAddress;

  status = usb::Request<>::Alloc(&data_out_req_, BULK_MAX_PACKET, bulk_out_addr_, parent_req_size_);
  if (status != ZX_OK) {
    return status;
  }
  status = usb::Request<>::Alloc(&data_in_req_, BULK_MAX_PACKET, bulk_in_addr_, parent_req_size_);
  if (status != ZX_OK) {
    return status;
  }

  status = DdkAdd("usb-cdc-acm-function");
  if (status != ZX_OK) {
    return status;
  }
  function_.SetInterface(this, &usb_function_interface_protocol_ops_);

  thrd_create(
      &thread_, [](void* ctx) { return static_cast<FakeUsbCdcAcmFunction*>(ctx)->Thread(); }, this);

  return ZX_OK;
}

void FakeUsbCdcAcmFunction::DdkUnbind(ddk::UnbindTxn txn) {
  fbl::AutoLock lock(&mtx_);
  active_ = false;
  event_.Signal();
  lock.release();

  int retval;
  thrd_join(thread_, &retval);

  txn.Reply();
}

void FakeUsbCdcAcmFunction::DdkRelease() { delete this; }

zx_status_t bind(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "FakeUsbCdcAcmFunction: binding driver");
  auto dev = std::make_unique<FakeUsbCdcAcmFunction>(parent);
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

}  // namespace fake_usb_cdc_acm_function

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_cdc_acm_function, fake_usb_cdc_acm_function::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_FUNCTION),
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_COMM),
    BI_MATCH_IF(EQ, BIND_USB_SUBCLASS, USB_CDC_SUBCLASS_ABSTRACT),
ZIRCON_DRIVER_END(usb_cdc_acm_function)
    // clang-format on
