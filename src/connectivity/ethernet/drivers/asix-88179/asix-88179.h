// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_ASIX_88179_ASIX_88179_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_ASIX_88179_ASIX_88179_H_

#include <lib/operation/ethernet.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <optional>
#include <queue>
#include <thread>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <fbl/auto_lock.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace eth {

class Asix88179Ethernet;

using DeviceType = ddk::Device<Asix88179Ethernet, ddk::Initializable, ddk::Unbindable>;

class Asix88179Ethernet : public DeviceType,
                          public ddk::EthernetImplProtocol<Asix88179Ethernet, ddk::base_protocol> {
 public:
  Asix88179Ethernet(const Asix88179Ethernet&) = delete;
  Asix88179Ethernet(Asix88179Ethernet&&) = delete;
  Asix88179Ethernet& operator=(const Asix88179Ethernet&) = delete;
  Asix88179Ethernet& operator=(Asix88179Ethernet&&) = delete;

  explicit Asix88179Ethernet(zx_device_t* parent) : DeviceType(parent) {}

  // DDK Hooks.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkUnbind(ddk::UnbindTxn txn);

  zx_status_t EthernetImplQuery(uint32_t options, ethernet_info_t* info);
  void EthernetImplStop();
  zx_status_t EthernetImplStart(const ethernet_ifc_protocol_t* ifc);
  void EthernetImplQueueTx(uint32_t options, ethernet_netbuf_t* netbuf,
                           ethernet_impl_queue_tx_callback completion_cb, void* cookie);
  zx_status_t EthernetImplSetParam(uint32_t param, int32_t value, const void* data,
                                   size_t data_size);
  void EthernetImplGetBti(zx::bti* bti) { bti->reset(); }

  static zx_status_t Bind(void* ctx, zx_device_t* device);

 private:
  zx_status_t Initialize();

  zx_status_t InitializeRegisters();

  template <typename T>
  zx_status_t ReadMac(uint8_t register_address, T* data) TA_REQ(lock_);

  template <typename T>
  zx_status_t WriteMac(uint8_t register_address, const T& data) TA_REQ(lock_);

  zx_status_t ReadPhy(uint8_t register_address, uint16_t* data) TA_REQ(lock_);

  zx_status_t WritePhy(uint8_t register_address, uint16_t data) TA_REQ(lock_);

  zx_status_t ConfigureBulkIn(uint8_t plsr) TA_REQ(lock_);

  zx_status_t ConfigureMediumMode() TA_REQ(lock_);

  zx_status_t Receive(usb::Request<>& request) TA_REQ(lock_);

  zx_status_t RequestAppend(usb::Request<>& request, const eth::BorrowedOperation<>& netbuf);

  void ReadComplete(usb_request_t* request);

  void WriteComplete(usb_request_t* request);

  void InterruptComplete(usb_request_t* usb_request);

  void Shutdown();

  zx_status_t TwiddleRcrBit(uint16_t bit, bool on) TA_REQ(lock_);

  zx_status_t SetPromisc(bool on) TA_REQ(lock_);

  zx_status_t SetMulticastPromisc(bool on) TA_REQ(lock_);

  void SetFilterBit(const uint8_t* mac, uint8_t* filter) TA_REQ(lock_);

  zx_status_t SetMulticastFilter(int32_t n_addresses, const uint8_t* address_bytes,
                                 size_t address_size) TA_REQ(lock_);

  template <uint8_t N>
  void DumpRegister(const char* name, uint8_t register_address) TA_REQ(lock_);

  void DumpRegs() TA_REQ(lock_);

  int InterruptThread();

  uint8_t mac_addr_[ETH_MAC_SIZE] TA_GUARDED(lock_) = {};

  bool online_ TA_GUARDED(lock_) = false;

  bool multicast_filter_overflow_ TA_GUARDED(lock_) = false;

  // pool of free USB requests
  usb::RequestPool<> free_read_pool_ TA_GUARDED(lock_);

  std::optional<usb::Request<>> interrupt_request_ TA_GUARDED(lock_);

  // callback interface to attached ethernet layer
  ddk::EthernetIfcProtocolClient ifc_ TA_GUARDED(lock_);

  bool running_ TA_GUARDED(lock_) = false;

  fbl::Mutex lock_;

  // List of requests that have pending data. Used to buffer data if a USB transaction is in
  // flight. Additional data must be appended to the tail of the list, or if that's full, a
  // request from free_write_reqs must be added to the list.
  usb::RequestQueue<> pending_usb_transmit_queue_ TA_GUARDED(tx_lock_);

  eth::BorrowedOperationQueue<> pending_netbuf_queue_ TA_GUARDED(tx_lock_);

  fbl::Mutex tx_lock_;

  usb::RequestPool<> free_write_pool_;

  usb::UsbDevice usb_;

  zx::duration rx_endpoint_delay_ = {};  // wait time between 2 recv requests
  zx::duration tx_endpoint_delay_ = {};  // wait time between 2 transmit requests

  uint8_t bulk_in_address_ = 0;
  uint8_t bulk_out_address_ = 0;
  uint8_t interrupt_address_ = 0;
  uint8_t interface_number_ = 0;

  size_t parent_req_size_ = 0;

  std::optional<ddk::InitTxn> init_txn_;

  thrd_t interrupt_thread_ = {};

  sync_completion_t interrupt_completion_;

  std::thread cancel_thread_;

  usb_request_complete_t read_request_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            reinterpret_cast<Asix88179Ethernet*>(ctx)->ReadComplete(request);
          },
      .ctx = this,
  };

  usb_request_complete_t write_request_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            reinterpret_cast<Asix88179Ethernet*>(ctx)->WriteComplete(request);
          },
      .ctx = this,
  };

  usb_request_complete_t interrupt_request_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            reinterpret_cast<Asix88179Ethernet*>(ctx)->InterruptComplete(request);
          },
      .ctx = this,
  };
};

}  // namespace eth

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_ASIX_88179_ASIX_88179_H_
