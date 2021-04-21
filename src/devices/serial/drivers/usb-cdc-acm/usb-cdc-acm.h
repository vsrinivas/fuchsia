// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SERIAL_DRIVERS_USB_CDC_ACM_USB_CDC_ACM_H_
#define SRC_DEVICES_SERIAL_DRIVERS_USB_CDC_ACM_USB_CDC_ACM_H_

#include <fuchsia/hardware/serialimpl/cpp/banjo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/types.h>

#include <thread>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace usb_cdc_acm_serial {

class UsbCdcAcmDevice;
using DeviceType = ddk::Device<UsbCdcAcmDevice, ddk::Unbindable, ddk::Readable, ddk::Writable>;
class UsbCdcAcmDevice : public DeviceType,
                        public ddk::SerialImplProtocol<UsbCdcAcmDevice, ddk::base_protocol> {
 public:
  explicit UsbCdcAcmDevice(zx_device_t* parent) : DeviceType(parent), usb_client_(parent) {}
  ~UsbCdcAcmDevice() = default;

  zx_status_t Bind();

  // |ddk::Device| mix-in implementations.
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);
  zx_status_t DdkRead(void* data, size_t len, zx_off_t off, size_t* actual);
  zx_status_t DdkWrite(const void* buf, size_t length, zx_off_t off, size_t* actual);

  // ddk::SerialImpl implementations.
  zx_status_t SerialImplGetInfo(serial_port_info_t* info);
  zx_status_t SerialImplConfig(uint32_t baud_rate, uint32_t flags);
  zx_status_t SerialImplEnable(bool enable);
  zx_status_t SerialImplRead(uint8_t* data, size_t len, size_t* actual);
  zx_status_t SerialImplWrite(const uint8_t* buf, size_t length, size_t* actual);
  zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb);

 private:
  void ReadComplete(usb_request_t* request);
  void WriteComplete(usb_request_t* request);
  zx_status_t ConfigureDevice(uint32_t baud_rate, uint32_t flags);

  // Notifies |notify_cb_| if the state is updated (|need_to_notify_cb_| is true), and
  // resets |need_to_notify_cb_| to false.
  void NotifyCallback() __TA_EXCLUDES(lock_);

  // Checks the readable and writeable state of the systemand updates |state_| and
  // |need_to_notify_cb_|. Any caller of this is responsible for calling NotifyCallback
  // once the lock is released.
  void CheckStateLocked() __TA_REQUIRES(lock_);

  fbl::Mutex lock_;

  // USB connection, endpoints address, request size, and current configuration.
  ddk::UsbProtocolClient usb_client_ = {};
  uint8_t bulk_in_addr_ = 0;
  uint8_t bulk_out_addr_ = 0;
  size_t parent_req_size_ = 0;
  uint32_t baud_rate_ = 0;
  uint32_t config_flags_ = 0;

  // Queues of free USB write requests and completed reads not yet read by the upper layer.
  usb::RequestQueue<> free_write_queue_ __TA_GUARDED(lock_);
  usb::RequestQueue<> completed_reads_queue_ __TA_GUARDED(lock_);

  // SerialImpl readable/writeable and enabled state.
  uint32_t state_ = 0;
  bool enabled_ = false;

  // Current offset into the first completed read request.
  size_t read_offset_ = 0;

  // SerialImpl port info and callback.
  serial_port_info_t serial_port_info_ __TA_GUARDED(lock_);
  bool need_to_notify_cb_ = false;
  serial_notify_t notify_cb_ = {};

  // Thread to cancel requests if the device is unbound.
  std::thread cancel_thread_;

  // USB callback functions.
  usb_request_complete_callback_t read_request_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            reinterpret_cast<UsbCdcAcmDevice*>(ctx)->ReadComplete(request);
          },
      .ctx = this,
  };
  usb_request_complete_callback_t write_request_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            reinterpret_cast<UsbCdcAcmDevice*>(ctx)->WriteComplete(request);
          },
      .ctx = this,
  };
};

}  // namespace usb_cdc_acm_serial

#endif  // SRC_DEVICES_SERIAL_DRIVERS_USB_CDC_ACM_USB_CDC_ACM_H_
