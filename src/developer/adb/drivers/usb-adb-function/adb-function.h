// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_ADB_DRIVERS_USB_ADB_FUNCTION_ADB_FUNCTION_H_
#define SRC_DEVELOPER_ADB_DRIVERS_USB_ADB_FUNCTION_ADB_FUNCTION_H_

#include <fidl/fuchsia.hardware.adb/cpp/wire.h>
#include <fuchsia/hardware/usb/function/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/driver.h>
#include <zircon/compiler.h>

#include <optional>
#include <queue>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>

namespace usb_adb_function {

constexpr uint32_t kBulkReqSize = 2048;
constexpr uint32_t kBulkTxCount = 16;
constexpr uint32_t kBulkRxCount = 16;
constexpr uint16_t kBulkMaxPacket = 512;

class UsbAdbDevice;
using UsbAdb = ddk::Device<UsbAdbDevice, ddk::Suspendable, ddk::Unbindable,
                           ddk::Messageable<fuchsia_hardware_adb::Device>::Mixin>;

// Implements USB ADB function driver.
// Components implementing ADB protocol should open a AdbImpl FIDL connection to dev-class/adb/xxx
// supported by this class to queue ADB messages. ADB protocol component can provide a client
// end channel to AdbInterface during Start method call to receive ADB messages sent by the host.
class UsbAdbDevice : public UsbAdb,
                     public ddk::UsbFunctionInterfaceProtocol<UsbAdbDevice>,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_ADB>,
                     fidl::WireServer<fuchsia_hardware_adb::UsbAdbImpl> {
 public:
  explicit UsbAdbDevice(zx_device_t* parent)
      : UsbAdb(parent), function_(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {
    loop_.StartThread("usb-adb-loop");
  }

  ~UsbAdbDevice() { loop_.Shutdown(); }

  // Driver bind method.
  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  // DDK lifecycle methods.
  void DdkRelease();
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);

  // UsbFunctionInterface methods.
  size_t UsbFunctionInterfaceGetDescriptorsSize();
  void UsbFunctionInterfaceGetDescriptors(uint8_t* out_descriptors_buffer, size_t descriptors_size,
                                          size_t* out_descriptors_actual);
  zx_status_t UsbFunctionInterfaceControl(const usb_setup_t* setup, const uint8_t* write_buffer,
                                          size_t write_size, uint8_t* out_read_buffer,
                                          size_t read_size, size_t* out_read_actual);
  zx_status_t UsbFunctionInterfaceSetConfigured(bool configured, usb_speed_t speed);
  zx_status_t UsbFunctionInterfaceSetInterface(uint8_t interface, uint8_t alt_setting);

  // fuchsia_hardware_adb::Device methods.
  void Start(StartRequestView request, StartCompleter::Sync& completer) override;

  // Helper method called when fuchsia_hardware_adb::Device closes.
  void Stop();

  // fuchsia_hardware_adb::UsbAdbImpl methods.
  void QueueTx(QueueTxRequestView request, QueueTxCompleter::Sync& completer) override;
  void Receive(ReceiveCompleter::Sync& completer) override;

 private:
  // Structure to store pending transfer requests when there are not enough USB request buffers.
  struct txn_info_t {
    fidl::VectorView<uint8_t> buf;
    fidl::internal::WireCompleter<::fuchsia_hardware_adb::UsbAdbImpl::QueueTx>::Async completer;
  };

  // Initialize endpoints and request pools.
  zx_status_t Init();

  // Helper method to perform bookkeeping and insert requests back to the free pool.
  zx_status_t InsertUsbRequest(usb::Request<> request, usb::RequestPool<>& pool);

  // Helper method to get free request buffer and queue the request for transmitting.
  zx_status_t SendLocked(const fidl::VectorView<uint8_t>& buf) __TA_REQUIRES(tx_mutex_);

  // USB request completion callback methods.
  void TxComplete(usb_request_t* req);
  void RxComplete(usb_request_t* req);

  // Helper method to configure endpoints
  zx_status_t ConfigureEndpoints(bool enable);

  uint8_t bulk_out_addr() const { return descriptors_.bulk_out_ep.b_endpoint_address; }
  uint8_t bulk_in_addr() const { return descriptors_.bulk_in_ep.b_endpoint_address; }

  bool Online() const {
    fbl::AutoLock _(&lock_);
    return (status_ == fuchsia_hardware_adb::StatusFlags::kOnline) && !shutting_down_;
  }

  // Shutdown operations by disabling endpoints, releasing requests and stop further queueing of
  // requests.
  void Shutdown();
  // Called when shutdown is in progress and all pending requests are completed. Invokes shutdown
  // completion callback.
  void ShutdownComplete() __TA_REQUIRES(lock_);

  ddk::UsbFunctionProtocolClient function_;
  size_t parent_request_size_ = 0;
  // Size of a usb request taking into account parent request size + alignment + internal
  // bookkeeping. This is calculated by Usb::Request<>::RequestSize() method.
  size_t usb_request_size_ = 0;
  usb_speed_t speed_ = 0;

  async::Loop loop_;

  // UsbAdbImpl service binding. This is created when client calls Start.
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_adb::UsbAdbImpl>> adb_binding_
      __TA_GUARDED(adb_mutex_);

  // BULK IN / Transfer USB request buffer pool.
  usb::RequestPool<> bulk_in_reqs_ __TA_GUARDED(tx_mutex_){};
  // Queue of pending transfer requests that need to be transmitted once the BULK IN request buffers
  // become available.
  std::queue<txn_info_t> tx_pending_infos_ __TA_GUARDED(tx_mutex_);

  // BULK out / Receive USB request buffer pool.
  usb::RequestPool<> bulk_out_reqs_ __TA_GUARDED(rx_mutex_){};
  // Queue of pending Receive requests from client.
  std::queue<fidl::internal::WireCompleter<::fuchsia_hardware_adb::UsbAdbImpl::Receive>::Async>
      rx_requests_ __TA_GUARDED(adb_mutex_);

  // Set once the interface is configured.
  fuchsia_hardware_adb::StatusFlags status_ __TA_GUARDED(lock_);

  // Count of requests currently passed down to controller and awaiting completion.
  // The driver is shutdown only after all pending requests are completed.
  int32_t pending_requests_ __TA_GUARDED(lock_) = 0;

  // Holds suspend/unbind DDK callback to be invoked once shutdown is complete.
  fit::callback<void()> shutdown_callback_ __TA_GUARDED(lock_);
  bool shutting_down_ __TA_GUARDED(lock_) = false;

  // This driver uses 4 locks to avoid race conditions in different sub-parts of the
  // driver. tx_mutex_ is used to avoid race conditions w.r.t transmit buffers. rx_mutex_ is used
  // to avoid race conditions w.r.t receive buffers. adb_mutex_ is used to serialize concurrent
  // access to adb_binding_ which is set/unset by a higher level component during the lifetime of
  // the driver. lock_ is used for all driver internal states. Alternatively a single lock (lock_)
  // could have been used for TX, RX and driver states, but that will serialize TX methods w.r.t RX.
  // Hence the separation of locks.
  //
  // NOTE: In order to maintain reentrancy, do not hold any lock when invoking callbacks/methods
  // that can reenter the driver methods.
  //
  // As for lock ordering, tx_mutex_/rx_mutex_ must be the highest order lock i.e. it must be
  // acquired before lock_ (and adb_mutex_) when both locks are held. tx_mutex_ and rx_mutex_ are
  // never acquired together.

  //  tx_mutex_ must be acquired before lock_ when both locks are held.
  fbl::Mutex tx_mutex_ __TA_ACQUIRED_BEFORE(lock_);
  //  rx_mutex_ must be acquired before lock_ and adb_mutex_ when multiple locks are held.
  fbl::Mutex rx_mutex_ __TA_ACQUIRED_BEFORE(lock_);
  // Currently, adb_interface_ is used only in the RX path, therefore it must be
  // acquired after rx_mutex_ and before lock_(lock_ should be the inner most lock in all cases),
  // when multiple locks are held. Alternatively, a reader writer lock could have be used.
  fbl::Mutex adb_mutex_ __TA_ACQUIRED_AFTER(rx_mutex_) __TA_ACQUIRED_BEFORE(lock_);
  // Lock for guarding driver states. This should be held for only a short duration and is the inner
  // most lock in all cases.
  mutable fbl::Mutex lock_;

  // Completion callbacks.
  usb_request_complete_callback_t rx_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* req) {
            ZX_DEBUG_ASSERT(ctx != nullptr);
            reinterpret_cast<UsbAdbDevice*>(ctx)->RxComplete(req);
          },
      .ctx = this,
  };
  usb_request_complete_callback_t tx_complete_ = {
      .callback =
          [](void* ctx, usb_request_t* req) {
            ZX_DEBUG_ASSERT(ctx != nullptr);
            reinterpret_cast<UsbAdbDevice*>(ctx)->TxComplete(req);
          },
      .ctx = this,
  };

  // USB ADB interface descriptor.
  struct {
    usb_interface_descriptor_t adb_intf;
    usb_endpoint_descriptor_t bulk_out_ep;
    usb_endpoint_descriptor_t bulk_in_ep;
  } descriptors_ = {
      .adb_intf =
          {
              .b_length = sizeof(usb_interface_descriptor_t),
              .b_descriptor_type = USB_DT_INTERFACE,
              .b_interface_number = 0,  // set later during AllocInterface
              .b_alternate_setting = 0,
              .b_num_endpoints = 2,
              .b_interface_class = USB_CLASS_VENDOR,
              .b_interface_sub_class = USB_SUBCLASS_ADB,
              .b_interface_protocol = USB_PROTOCOL_ADB,
              .i_interface = 0,  // This is set in adb
          },
      .bulk_out_ep =
          {
              .b_length = sizeof(usb_endpoint_descriptor_t),
              .b_descriptor_type = USB_DT_ENDPOINT,
              .b_endpoint_address = 0,  // set later during AllocEp
              .bm_attributes = USB_ENDPOINT_BULK,
              .w_max_packet_size = htole16(kBulkMaxPacket),
              .b_interval = 0,
          },
      .bulk_in_ep =
          {
              .b_length = sizeof(usb_endpoint_descriptor_t),
              .b_descriptor_type = USB_DT_ENDPOINT,
              .b_endpoint_address = 0,  // set later during AllocEp
              .bm_attributes = USB_ENDPOINT_BULK,
              .w_max_packet_size = htole16(kBulkMaxPacket),
              .b_interval = 0,
          },
  };
};

}  // namespace usb_adb_function

#endif  // SRC_DEVELOPER_ADB_DRIVERS_USB_ADB_FUNCTION_ADB_FUNCTION_H_
