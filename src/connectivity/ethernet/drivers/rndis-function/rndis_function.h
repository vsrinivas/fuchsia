// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_RNDIS_FUNCTION_RNDIS_FUNCTION_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_RNDIS_FUNCTION_RNDIS_FUNCTION_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/operation/ethernet.h>
#include <zircon/hw/usb/cdc.h>

#include <array>
#include <queue>
#include <thread>

#include <ddk/protocol/usb.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/usb/function.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/connectivity/ethernet/lib/rndis/rndis.h"

class RndisFunction;

using RndisFunctionType = ddk::Device<RndisFunction, ddk::UnbindableNew, ddk::Suspendable>;

class RndisFunction : public RndisFunctionType,
                      public ddk::UsbFunctionInterfaceProtocol<RndisFunction>,
                      public ddk::EthernetImplProtocol<RndisFunction, ddk::base_protocol> {
 public:
  explicit RndisFunction(zx_device_t* parent)
      : RndisFunctionType(parent),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        function_(parent) {}

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkRelease();

  size_t UsbFunctionInterfaceGetDescriptorsSize();
  void UsbFunctionInterfaceGetDescriptors(void* out_descriptors_buffer, size_t descriptors_size,
                                          size_t* out_descriptors_actual);
  zx_status_t UsbFunctionInterfaceControl(const usb_setup_t* setup, const void* write_buffer,
                                          size_t write_size, void* out_read_buffer,
                                          size_t read_size, size_t* out_read_actual);
  zx_status_t UsbFunctionInterfaceSetConfigured(bool configured, usb_speed_t speed);
  zx_status_t UsbFunctionInterfaceSetInterface(uint8_t interface, uint8_t alt_setting);

  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  void EthernetImplStop();
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc_);
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                   size_t data_size);
  void EthernetImplGetBti(zx::bti* bti) { bti->reset(); }

  static zx_status_t Create(void* ctx, zx_device_t* dev);
  zx_status_t Bind();

 private:
  zx_status_t HandleCommand(const void* buffer, size_t size);
  zx_status_t HandleResponse(void* buffer, size_t size, size_t* actual);
  zx_status_t Halt();
  void Reset();

  std::optional<std::vector<uint8_t>> QueryOid(uint32_t oid, void* input, size_t length);
  zx_status_t SetOid(uint32_t oid, const uint8_t* input, size_t length);

  void Shutdown();
  void ShutdownComplete() __TA_REQUIRES(lock_);

  void ReadComplete(usb_request_t* request);
  void WriteComplete(usb_request_t* request);
  void NotificationComplete(usb_request_t* request);

  void ReceiveLocked(usb::Request<>& request) __TA_REQUIRES(lock_);
  void NotifyLocked() __TA_REQUIRES(lock_);
  void IndicateConnectionStatus(bool connected);

  uint8_t NotificationAddress() { return descriptors_.notification_ep.bEndpointAddress; }
  uint8_t BulkInAddress() { return descriptors_.in_ep.bEndpointAddress; }
  uint8_t BulkOutAddress() { return descriptors_.out_ep.bEndpointAddress; }

  bool Online() const __TA_REQUIRES(lock_) { return ifc_.is_valid() && rndis_ready_; }

  static constexpr size_t kNotificationMaxPacketSize = 8;
  static constexpr size_t kRequestPoolSize = 8;
  static constexpr size_t kMtu = RNDIS_MAX_XFER_SIZE;

  static constexpr uint32_t kVendorId = 0x44070b00;
  static constexpr char kVendorDescription[] = "Google";
  static constexpr uint16_t kVendorDriverVersionMajor = 1;
  static constexpr uint16_t kVendorDriverVersionMinor = 0;

  async::Loop loop_;

  ddk::EthernetIfcProtocolClient ifc_ __TA_GUARDED(lock_);
  ddk::UsbFunctionProtocolClient function_;
  size_t usb_request_size_;

  fbl::Mutex lock_;
  bool rndis_ready_ __TA_GUARDED(lock_) = false;
  bool shutting_down_ __TA_GUARDED(lock_) = false;
  uint32_t link_speed_ __TA_GUARDED(lock_) = 0;
  std::array<uint8_t, ETH_MAC_SIZE> mac_addr_;

  // Stats.
  std::atomic<uint32_t> transmit_ok_ = 0;
  std::atomic<uint32_t> receive_ok_ = 0;
  std::atomic<uint32_t> transmit_errors_ = 0;
  std::atomic<uint32_t> receive_errors_ = 0;
  std::atomic<uint32_t> transmit_no_buffer_ = 0;

  std::queue<std::vector<uint8_t>> control_responses_ __TA_GUARDED(lock_);

  usb::RequestPool<> free_notify_pool_ __TA_GUARDED(lock_);
  usb::RequestPool<> free_read_pool_ __TA_GUARDED(lock_);
  usb::RequestPool<> free_write_pool_ __TA_GUARDED(lock_);

  size_t pending_requests_ __TA_GUARDED(lock_) = 0;

  std::optional<fit::function<void()>> shutdown_callback_;

  usb_request_complete_t read_request_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            auto rndis = reinterpret_cast<RndisFunction*>(ctx);
            async::PostTask(rndis->loop_.dispatcher(),
                            [rndis, request]() { rndis->ReadComplete(request); });
          },
      .ctx = this,
  };

  usb_request_complete_t write_request_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            auto rndis = reinterpret_cast<RndisFunction*>(ctx);
            async::PostTask(rndis->loop_.dispatcher(),
                            [rndis, request]() { rndis->WriteComplete(request); });
          },
      .ctx = this,
  };

  usb_request_complete_t notification_request_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            auto rndis = reinterpret_cast<RndisFunction*>(ctx);
            async::PostTask(rndis->loop_.dispatcher(),
                            [rndis, request]() { rndis->NotificationComplete(request); });
          },
      .ctx = this,
  };

  struct {
    usb_interface_assoc_descriptor_t assoc;
    usb_interface_descriptor_t communication_interface;
    usb_cs_header_interface_descriptor_t cdc_header;
    usb_cs_call_mgmt_interface_descriptor_t call_mgmt;
    usb_cs_abstract_ctrl_mgmt_interface_descriptor_t acm;
    usb_cs_union_interface_descriptor_1_t cdc_union;
    usb_endpoint_descriptor_t notification_ep;

    usb_interface_descriptor_t data_interface;
    usb_endpoint_descriptor_t out_ep;
    usb_endpoint_descriptor_t in_ep;
  } __PACKED descriptors_;
};

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_RNDIS_FUNCTION_RNDIS_FUNCTION_H_
