// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "adb-function.h"

#include <lib/ddk/debug.h>
#include <zircon/assert.h>

#include <cstdint>
#include <optional>

#include <fbl/auto_lock.h>
#include <usb/peripheral.h>
#include <usb/request-cpp.h>

#include "src/developer/adb/drivers/usb-adb-function/usb_adb-bind.h"

namespace usb_adb_function {

namespace {

// CompleterType follows fidl::internal::WireCompleter<RequestType>::Async
template <typename CompleterType>
void CompleteTxn(CompleterType& completer, zx_status_t status) {
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

}  // namespace

void UsbAdbDevice::Start(StartRequestView request, StartCompleter::Sync& completer) {
  zx_status_t status = ZX_OK;
  bool enable_ep = false;
  {
    fbl::AutoLock _(&adb_mutex_);
    if (adb_binding_.has_value()) {
      status = ZX_ERR_ALREADY_BOUND;
    } else {
      adb_binding_ = fidl::BindServer<fidl::WireServer<fuchsia_hardware_adb::UsbAdbImpl>>(
          dispatcher_, std::move(request->interface), this,
          [this](auto* server, fidl::UnbindInfo info, auto server_end) {
            zxlogf(INFO, "Device closed with reason '%s'", info.FormatDescription().c_str());
            Stop();
          });
      enable_ep = Online();
      fbl::AutoLock _(&lock_);
      auto result = fidl::WireSendEvent(adb_binding_.value())->OnStatusChanged(status_);
      if (!result.ok()) {
        zxlogf(ERROR, "Could not call AdbInterface Status %s", result.error().status_string());
        status = ZX_ERR_IO;
      }
    }
  }
  // Configure endpoints as adb binding is set now.
  status = ConfigureEndpoints(enable_ep);
  CompleteTxn(completer, status);
}

void UsbAdbDevice::Stop() {
  {
    fbl::AutoLock _(&adb_mutex_);
    adb_binding_.reset();
  }
  // Disable endpoints.
  ConfigureEndpoints(false);
}

zx_status_t UsbAdbDevice::SendLocked(const fidl::VectorView<uint8_t>& buf) {
  if (!Online()) {
    return ZX_ERR_BAD_STATE;
  }

  std::optional<usb::Request<>> tx_request = bulk_in_reqs_.Get(usb_request_size_);
  if (!tx_request) {
    return ZX_ERR_SHOULD_WAIT;
  }

  // Send data
  tx_request->request()->header.length = buf.count();
  ssize_t bytes_copied = tx_request->CopyTo(buf.data(), buf.count(), 0);
  if (bytes_copied < 0) {
    zxlogf(ERROR, "Failed to copy data into send req (error %zd).", bytes_copied);
    bulk_in_reqs_.Add(std::move(tx_request.value()));
    return ZX_ERR_INTERNAL;
  }

  {
    fbl::AutoLock _(&lock_);
    pending_requests_++;
  }
  function_.RequestQueue(tx_request->take(), &tx_complete_);

  return ZX_OK;
}

void UsbAdbDevice::QueueTx(QueueTxRequestView request, QueueTxCompleter::Sync& completer) {
  size_t length = request->data.count();
  zx_status_t status;

  if (!Online() || length == 0) {
    zxlogf(INFO, "Invalid state - Online %d Length %zu", Online(), length);
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock _(&tx_mutex_);
  status = SendLocked(request->data);
  if (status == ZX_ERR_SHOULD_WAIT) {
    // No buffers available, queue it up
    tx_pending_infos_.emplace(txn_info_t{.buf = request->data, .completer = completer.ToAsync()});
  } else {
    CompleteTxn(completer, status);
  }
}

void UsbAdbDevice::Receive(ReceiveCompleter::Sync& completer) {
  fbl::AutoLock _(&rx_mutex_);

  // Return early during shutdown.
  if (!Online()) {
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  std::optional<usb::Request<>> pending_request = bulk_out_reqs_.Get(usb_request_size_);
  if (pending_request.has_value()) {
    void* data = NULL;
    auto status = pending_request->Mmap(&data);
    if (status == ZX_OK) {
      auto buffer = fidl::VectorView<uint8_t>::FromExternal(
          reinterpret_cast<uint8_t*>(data), pending_request->request()->response.actual);
      completer.ReplySuccess(buffer);

    } else {
      zxlogf(ERROR, "Failed to Mmap pending request - %d.", status);
      completer.ReplyError(status);
    }
    {
      fbl::AutoLock _(&lock_);
      pending_requests_++;
    }
    function_.RequestQueue(pending_request->take(), &rx_complete_);
  } else {
    fbl::AutoLock _(&adb_mutex_);
    rx_requests_.emplace(completer.ToAsync());
  }
}

zx_status_t UsbAdbDevice::InsertUsbRequest(usb::Request<> request, usb::RequestPool<>& pool) {
  {
    fbl::AutoLock _(&lock_);
    pending_requests_--;
    ZX_ASSERT(pending_requests_ >= 0);
    // Return without adding the request to the pool during shutdown.
    if (shutting_down_) {
      request.Release();
      if (pending_requests_ == 0) {
        ShutdownComplete();
      }
      return ZX_ERR_CANCELED;
    }
  }
  pool.Add(std::move(request));
  return ZX_OK;
}

void UsbAdbDevice::RxComplete(usb_request_t* req) {
  usb::Request<> request(req, parent_request_size_);

  // Return early during shutdown.
  {
    fbl::AutoLock _(&lock_);
    if (shutting_down_) {
      request.Release();
      pending_requests_--;
      ZX_ASSERT(pending_requests_ >= 0);
      if (pending_requests_ == 0) {
        ShutdownComplete();
      }
      return;
    }
  }

  fbl::AutoLock _(&rx_mutex_);
  if (req->response.status == ZX_ERR_IO_NOT_PRESENT) {
    InsertUsbRequest(std::move(request), bulk_out_reqs_);
    return;
  }

  if (req->response.status != ZX_OK) {
    zxlogf(ERROR, "RxComplete called with error %d.", req->response.status);
    function_.RequestQueue(request.take(), &rx_complete_);
    return;
  }

  if (req->response.status == ZX_OK) {
    fbl::AutoLock _(&adb_mutex_);
    if (!rx_requests_.empty()) {
      void* data = NULL;
      auto status = request.Mmap(&data);
      if (status == ZX_OK) {
        auto buffer = fidl::VectorView<uint8_t>::FromExternal(reinterpret_cast<uint8_t*>(data),
                                                              req->response.actual);
        rx_requests_.front().ReplySuccess(buffer);
      } else {
        zxlogf(ERROR, "Failed to Mmap request - %d.", status);
        rx_requests_.front().ReplyError(status);
      }
      rx_requests_.pop();
      function_.RequestQueue(request.take(), &rx_complete_);
    } else {
      InsertUsbRequest(std::move(request), bulk_out_reqs_);
    }
  }
}

void UsbAdbDevice::TxComplete(usb_request_t* req) {
  usb::Request<> request(req, parent_request_size_);
  std::optional<fidl::internal::WireCompleter<::fuchsia_hardware_adb::UsbAdbImpl::QueueTx>::Async>
      completer = std::nullopt;
  zx_status_t send_status = ZX_OK;

  {
    fbl::AutoLock _(&tx_mutex_);
    if (InsertUsbRequest(std::move(request), bulk_in_reqs_) != ZX_OK) {
      return;
    }
    // Do not queue requests if status is ZX_ERR_IO_NOT_PRESENT, as the underlying connection could
    // be disconnected or USB_RESET is being processed. Calling adb_send_locked in such scenario
    // will deadlock and crash the driver (see fxbug.dev/92793).
    if (req->response.status != ZX_ERR_IO_NOT_PRESENT) {
      if (!tx_pending_infos_.empty()) {
        if ((send_status = SendLocked(tx_pending_infos_.front().buf)) != ZX_ERR_SHOULD_WAIT) {
          completer = std::move(tx_pending_infos_.front().completer);
          tx_pending_infos_.pop();
        }
      }
    }
  }

  if (completer) {
    fbl::AutoLock _(&adb_mutex_);
    CompleteTxn(completer.value(), send_status);
  }
}

size_t UsbAdbDevice::UsbFunctionInterfaceGetDescriptorsSize() { return sizeof(descriptors_); }

void UsbAdbDevice::UsbFunctionInterfaceGetDescriptors(uint8_t* buffer, size_t buffer_size,
                                                      size_t* out_actual) {
  const size_t length = std::min(sizeof(descriptors_), buffer_size);
  std::memcpy(buffer, &descriptors_, length);
  *out_actual = length;
}

zx_status_t UsbAdbDevice::UsbFunctionInterfaceControl(const usb_setup_t* setup,
                                                      const uint8_t* write_buffer,
                                                      size_t write_size, uint8_t* out_read_buffer,
                                                      size_t read_size, size_t* out_read_actual) {
  if (out_read_actual != NULL) {
    *out_read_actual = 0;
  }

  return ZX_OK;
}

zx_status_t UsbAdbDevice::ConfigureEndpoints(bool enable) {
  zx_status_t status;
  fbl::AutoLock _(&rx_mutex_);
  // Configure endpoint if not already done.
  if (enable && !bulk_out_reqs_.is_empty()) {
    status = function_.ConfigEp(&descriptors_.bulk_out_ep, nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to Config BULK OUT ep - %d.", status);
      return status;
    }

    status = function_.ConfigEp(&descriptors_.bulk_in_ep, nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to Config BULK IN ep - %d.", status);
      return status;
    }

    // queue RX requests
    std::optional<usb::Request<>> pending_request;
    while ((pending_request = bulk_out_reqs_.Get(usb_request_size_))) {
      {
        fbl::AutoLock _(&lock_);
        pending_requests_++;
      }
      function_.RequestQueue(pending_request->take(), &rx_complete_);
    }
    zxlogf(INFO, "ADB endpoints configured.");
  } else {
    status = function_.DisableEp(bulk_out_addr());
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to disable BULK OUT ep - %d.", status);
      return status;
    }

    status = function_.DisableEp(bulk_in_addr());
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to disable BULK IN ep - %d.", status);
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t UsbAdbDevice::UsbFunctionInterfaceSetConfigured(bool configured, usb_speed_t speed) {
  zxlogf(INFO, "configured? - %d  speed - %d.", configured, speed);
  bool adb_configured = false;
  {
    fbl::AutoLock _(&lock_);
    status_ = fuchsia_hardware_adb::StatusFlags(configured);
    speed_ = speed;
  }

  {
    fbl::AutoLock _(&adb_mutex_);
    if (adb_binding_.has_value()) {
      fbl::AutoLock _(&lock_);
      auto result = fidl::WireSendEvent(adb_binding_.value())->OnStatusChanged(status_);
      if (!result.ok()) {
        zxlogf(ERROR, "Could not call AdbInterface Status - %d.", result.status());
        return ZX_ERR_IO;
      }
      adb_configured = true;
    }
  }

  // Enable endpoints only when USB is configured and ADB interface is set.
  return ConfigureEndpoints(configured && adb_configured);
}

zx_status_t UsbAdbDevice::UsbFunctionInterfaceSetInterface(uint8_t interface, uint8_t alt_setting) {
  zxlogf(INFO, "interface - %d alt_setting - %d.", interface, alt_setting);
  zx_status_t status;

  if (interface != descriptors_.adb_intf.b_interface_number || alt_setting > 1) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (alt_setting) {
    if ((status = function_.ConfigEp(&descriptors_.bulk_out_ep, NULL)) != ZX_OK ||
        (status = function_.ConfigEp(&descriptors_.bulk_in_ep, NULL)) != ZX_OK) {
      zxlogf(ERROR, "usb_function_config_ep failed - %d.", status);
    }
  } else {
    if ((status = function_.DisableEp(bulk_out_addr())) != ZX_OK ||
        (status = function_.DisableEp(bulk_in_addr())) != ZX_OK) {
      zxlogf(ERROR, "usb_function_disable_ep failed - %d.", status);
    }
  }

  fuchsia_hardware_adb::StatusFlags online;
  if (alt_setting && status == ZX_OK) {
    online = fuchsia_hardware_adb::StatusFlags::kOnline;

    // queue our IN reqs
    fbl::AutoLock _(&rx_mutex_);
    std::optional<usb::Request<>> pending_request;
    while ((pending_request = bulk_out_reqs_.Get(usb_request_size_))) {
      {
        fbl::AutoLock _(&lock_);
        pending_requests_++;
      }
      function_.RequestQueue(pending_request->take(), &rx_complete_);
    }
  }

  {
    fbl::AutoLock _(&lock_);
    status_ = online;
  }

  fbl::AutoLock _(&adb_mutex_);
  if (adb_binding_.has_value()) {
    fbl::AutoLock _(&lock_);
    auto result = fidl::WireSendEvent(adb_binding_.value())->OnStatusChanged(status_);
    if (!result.ok()) {
      zxlogf(ERROR, "Could not call AdbInterface Status.");
      return ZX_ERR_IO;
    }
  }

  return status;
}

void UsbAdbDevice::ShutdownComplete() {
  // Multiple threads/callbacks could observe pending_request == 0 and call ShutdownComplete
  // multiple times. Only call the callback if not already called.
  if (shutdown_callback_) {
    shutdown_callback_();
  }
}

void UsbAdbDevice::Shutdown() {
  // Start the shutdown process by setting the shutdown bool to true.
  // When the pipeline tries to submit requests, they will be immediately
  // free'd.
  {
    fbl::AutoLock _(&lock_);
    shutting_down_ = true;
  }

  // Disable endpoints to prevent new requests present in our
  // pipeline from getting queued.
  function_.DisableEp(bulk_out_addr());
  function_.DisableEp(bulk_in_addr());

  // Cancel all requests in the pipeline -- the completion handler
  // will free these requests as they come in.
  // Do not hold locks when calling this method. It might result in deadlock as completion callbacks
  // could be invoked during this call.
  function_.CancelAll(bulk_out_addr());
  function_.CancelAll(bulk_in_addr());

  {
    fbl::AutoLock _(&adb_mutex_);
    if (adb_binding_.has_value()) {
      adb_binding_->Unbind();
    }
    adb_binding_.reset();
    while (!rx_requests_.empty()) {
      rx_requests_.front().ReplyError(ZX_ERR_BAD_STATE);
      rx_requests_.pop();
    }
  }

  // Free all request pools.
  std::queue<txn_info_t> queue;
  {
    fbl::AutoLock _(&tx_mutex_);
    bulk_in_reqs_.Release();
    std::swap(queue, tx_pending_infos_);
  }

  while (!queue.empty()) {
    CompleteTxn(queue.front().completer, ZX_ERR_PEER_CLOSED);
    queue.pop();
  }

  {
    fbl::AutoLock _(&rx_mutex_);
    bulk_out_reqs_.Release();
  }

  // Call shutdown complete if all requests are released. This method will be called in completion
  // callbacks if pending requests exists.
  {
    fbl::AutoLock _(&lock_);
    if (pending_requests_ == 0) {
      ShutdownComplete();
    }
  }
}

void UsbAdbDevice::DdkUnbind(ddk::UnbindTxn txn) {
  {
    fbl::AutoLock _(&lock_);
    ZX_ASSERT(!shutdown_callback_);
    shutdown_callback_ = [unbind_txn = std::move(txn)]() mutable { unbind_txn.Reply(); };
  }
  Shutdown();
}

void UsbAdbDevice::DdkRelease() { delete this; }

void UsbAdbDevice::DdkSuspend(ddk::SuspendTxn txn) {
  {
    fbl::AutoLock _(&lock_);
    ZX_ASSERT(!shutdown_callback_);
    shutdown_callback_ = [suspend_txn = std::move(txn)]() mutable {
      suspend_txn.Reply(ZX_OK, suspend_txn.requested_state());
    };
  }
  Shutdown();
}

zx_status_t UsbAdbDevice::Init() {
  parent_request_size_ = function_.GetRequestSize();
  usb_request_size_ = usb::Request<>::RequestSize(parent_request_size_);

  auto status = function_.AllocInterface(&descriptors_.adb_intf.b_interface_number);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_function_alloc_interface failed - %d.", status);
    return status;
  }

  status = function_.AllocEp(USB_DIR_OUT, &descriptors_.bulk_out_ep.b_endpoint_address);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_function_alloc_ep failed - %d.", status);
    return status;
  }
  status = function_.AllocEp(USB_DIR_IN, &descriptors_.bulk_in_ep.b_endpoint_address);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb_function_alloc_ep failed - %d.", status);
    return status;
  }

  // Allocate bulk out usb requests.
  for (uint32_t i = 0; i < kBulkRxCount; i++) {
    std::optional<usb::Request<>> request;
    status = usb::Request<>::Alloc(&request, kBulkReqSize, bulk_out_addr(), parent_request_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Allocating bulk out request failed - %d.", status);
      return status;
    }
    {
      fbl::AutoLock _(&rx_mutex_);
      bulk_out_reqs_.Add(*std::move(request));
    }
  }

  // Allocate bulk in usb requests.
  for (uint32_t i = 0; i < kBulkTxCount; i++) {
    std::optional<usb::Request<>> request;
    status = usb::Request<>::Alloc(&request, kBulkReqSize, bulk_in_addr(), parent_request_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Allocating bulk in request failed %d", status);
      return status;
    }
    {
      fbl::AutoLock _(&tx_mutex_);
      bulk_in_reqs_.Add(*std::move(request));
    }
  }

  status = DdkAdd("usb-adb-function", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not add UsbAdbDevice %d.", status);
    return status;
  }

  function_.SetInterface(this, &usb_function_interface_protocol_ops_);
  return ZX_OK;
}

zx_status_t UsbAdbDevice::Bind(void* ctx, zx_device_t* parent) {
  auto adb = std::make_unique<UsbAdbDevice>(parent);
  if (!adb) {
    zxlogf(ERROR, "Could not create UsbAdbDevice.");
    return ZX_ERR_NO_MEMORY;
  }
  auto status = adb->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not init UsbAdbDevice - %d.", status);
    adb->DdkRelease();
    return status;
  }

  {
    // The DDK now owns this reference.
    __UNUSED auto released = adb.release();
  }
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = UsbAdbDevice::Bind;
  return ops;
}();

}  // namespace usb_adb_function

// clang-format off
ZIRCON_DRIVER(usb_adb, usb_adb_function::driver_ops, "zircon", "0.1");
