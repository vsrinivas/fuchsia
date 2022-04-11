// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket.h"

#include <assert.h>
#include <fidl/fuchsia.hardware.vsock/cpp/wire.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/io-buffer.h>
#include <lib/fit/defer.h>
#include <lib/stdcompat/functional.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>
#include <virtio/virtio.h>

namespace virtio {

static constexpr uint16_t kDataBacklog = 32;

static constexpr uint16_t kEventBacklog = 4;

static constexpr size_t kFrameSize = sizeof(virtio_vsock_hdr_t) + 468;

static constexpr uint16_t kRxId = 0u;
static constexpr uint16_t kTxId = 1u;
static constexpr uint16_t kEventId = 2u;

static virtio_vsock_hdr_t make_hdr(const SocketDevice::ConnectionKey& key, uint16_t op,
                                   uint32_t cid, const SocketDevice::CreditInfo& credit) {
  return virtio_vsock_hdr_t{
      .src_cid = cid,
      .dst_cid = key.addr.remote_cid,
      .src_port = key.addr.local_port,
      .dst_port = key.addr.remote_port,
      .len = 0,
      .type = 1,
      .op = op,
      .flags = op == VIRTIO_VSOCK_OP_SHUTDOWN ? 3u : 0u,
      .buf_alloc = credit.buf_alloc,
      .fwd_cnt = credit.fwd_count,
  };
}

SocketDevice::SocketDevice(zx_device_t* bus_device, zx::bti bti, std::unique_ptr<Backend> backend)
    : virtio::Device(bus_device, std::move(bti), std::move(backend)),
      DeviceType(bus_device),
      dispatch_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      rx_(this, kDataBacklog, kFrameSize),
      tx_(this, kDataBacklog, kFrameSize),
      event_(this, kEventBacklog, sizeof(virtio_vsock_event_t)),
      have_timer_(false),
      timer_wait_handler_(this),
      callback_closed_handler_(this) {}

SocketDevice::~SocketDevice() {}

void SocketDevice::Start(StartRequestView request, StartCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  if (callbacks_.client_end().is_valid()) {
    RemoveCallbacksLocked();
  }
  callbacks_ = fidl::BindSyncClient(std::move(request->cb));
  callback_closed_handler_.set_object(callbacks_.client_end().borrow().channel()->get());
  callback_closed_handler_.set_trigger(ZX_SOCKET_PEER_CLOSED);
  callback_closed_handler_.Begin(dispatch_loop_.dispatcher());

  // Go and process the rings to handle any pending rx descriptors and start
  // queueing new ones.
  UpdateRxRingLocked();

  completer.Reply(ZX_OK);
}

void SocketDevice::SendRst(SendRstRequestView request, SendRstCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  CleanupConAndRstLocked(request->addr);
  completer.Reply(ZX_OK);
}

void SocketDevice::SendShutdown(SendShutdownRequestView request,
                                SendShutdownCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  if (!callbacks_.client_end().is_valid()) {
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  auto conn = connections_.find(request->addr);
  if (conn == connections_.end() || conn->IsShuttingDown()) {
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  if (conn->BeginShutdown()) {
    SendOpLocked(conn.CopyPointer(), VIRTIO_VSOCK_OP_SHUTDOWN);
  }
  completer.Reply(ZX_OK);
}

void SocketDevice::SendRequest(SendRequestRequestView request,
                               SendRequestCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  if (!callbacks_.client_end().is_valid()) {
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  if (connections_.find(request->addr) != connections_.end()) {
    completer.Reply(ZX_ERR_ALREADY_BOUND);
    return;
  }
  fbl::AllocChecker ac;
  auto conn = fbl::MakeRefCountedChecked<Connection>(
      &ac, request->addr, std::move(request->data),
      cpp20::bind_front(&SocketDevice::ConnectionSocketSignalled, this), cid_, lock_);
  if (!ac.check()) {
    completer.Reply(ZX_ERR_NO_MEMORY);
    return;
  }
  connections_.insert(conn);
  SendOpLocked(conn, VIRTIO_VSOCK_OP_REQUEST);
  completer.Reply(ZX_OK);
}

void SocketDevice::SendResponse(SendResponseRequestView request,
                                SendResponseCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  if (!callbacks_.client_end().is_valid()) {
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  if (connections_.find(request->addr) != connections_.end()) {
    completer.Reply(ZX_ERR_ALREADY_BOUND);
    return;
  }
  fbl::AllocChecker ac;
  auto conn = fbl::MakeRefCountedChecked<Connection>(
      &ac, request->addr, std::move(request->data),
      cpp20::bind_front(&SocketDevice::ConnectionSocketSignalled, this), cid_, lock_);
  if (!ac.check()) {
    completer.Reply(ZX_ERR_NO_MEMORY);
    return;
  }

  conn->MakeActive(dispatch_loop_.dispatcher());

  connections_.insert(conn);
  SendOpLocked(conn, VIRTIO_VSOCK_OP_RESPONSE);
  completer.Reply(ZX_OK);
}

void SocketDevice::SendVmo(SendVmoRequestView request, SendVmoCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  if (!callbacks_.client_end().is_valid()) {
    completer.Reply(ZX_ERR_BAD_STATE);
    return;
  }

  auto conn = connections_.find(request->addr);
  if (conn == connections_.end()) {
    completer.Reply(ZX_ERR_NOT_FOUND);
    return;
  }
  // Forbid the zero length as the VMO transfer code will get confused.
  if (request->len == 0) {
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }
  zx_status_t result =
      conn->SetVmo(bti_, std::move(request->vmo), request->off, request->len, bti_contiguity_);
  if (result != ZX_OK) {
    completer.Reply(result);
    return;
  }
  ContinueTxLocked(false, conn.CopyPointer());

  completer.Reply(ZX_OK);
}

void SocketDevice::GetCid(GetCidRequestView request, GetCidCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);
  completer.Reply(cid_);
}

zx_status_t SocketDevice::Init() {
  fbl::AutoLock lock(&lock_);
  // It's a common part for all virtio devices: reset the device, notify
  // about the driver and negotiate supported features.
  DeviceReset();
  DriverStatusAck();
  if (!DeviceFeatureSupported(VIRTIO_F_VERSION_1)) {
    zxlogf(ERROR, "%s: Legacy virtio interface is not supported by this driver", tag());
    return ZX_ERR_NOT_SUPPORTED;
  }
  DriverFeatureAck(VIRTIO_F_VERSION_1);

  // Plan to clean up unless everything goes right.
  auto cleanup = fit::defer([this]() TA_NO_THREAD_SAFETY_ANALYSIS { ReleaseLocked(); });

  UpdateCidLocked();

  zx_status_t rc;
  rc = event_.Init(kEventId, bti());
  if (rc != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to allocate event ring: %s", tag(), zx_status_get_string(rc));
    return rc;
  }
  rc = rx_.Init(kRxId, bti());
  if (rc != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to allocate rx ring: %s", tag(), zx_status_get_string(rc));
    return rc;
  }
  rc = tx_.Init(kTxId, bti());
  if (rc != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to allocate tx ring: %s", tag(), zx_status_get_string(rc));
    return rc;
  }
  // Determine our bti contiguity.
  zx_info_bti_t bti_info;
  rc = bti_.get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), nullptr, nullptr);
  if (rc != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to determine BTI contiguity", tag());
    return rc;
  }
  bti_contiguity_ = bti_info.minimum_contiguity;

  // Start the interrupt thread and set the driver OK status
  StartIrqThread();

  // Start out dispatcher for connections
  rc = dispatch_loop_.StartThread("virtio-vsock-connection");
  if (rc != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to start dispatch thread: %s", tag(), zx_status_get_string(rc));
    return rc;
  }

  // Setup our timer for retrying TX operations.
  rc = zx::timer::create(ZX_TIMER_SLACK_CENTER, ZX_CLOCK_MONOTONIC, &tx_retry_timer_);
  if (rc != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create timer: %s", tag(), zx_status_get_string(rc));
    return rc;
  }
  timer_wait_handler_.set_object(tx_retry_timer_.get());
  timer_wait_handler_.set_trigger(ZX_TIMER_SIGNALED);

  // Initialize the zx_device and publish us.
  zx_status_t status = DdkAdd("virtio-vsock");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to add device: %s", tag(), zx_status_get_string(status));
    return status;
  }
  device_ = zxdev();
  event_.RefillRing();

  cleanup.cancel();
  DriverStatusOk();
  return ZX_OK;
}

void SocketDevice::DdkRelease() {
  fbl::AutoLock lock(&lock_);
  ReleaseLocked();
}

void SocketDevice::IrqRingUpdate() {
  fbl::AutoLock lock(&lock_);
  tx_.ProcessDescriptors([this](const ConnectionKey& key, uint64_t payload)
                             TA_NO_THREAD_SAFETY_ANALYSIS {
                               auto conn = connections_.find(key);
                               if (conn != connections_.end()) {
                                 if (conn->NotifyVmoTxComplete(payload)) {
                                   if (callbacks_.client_end().is_valid()) {
                                     callbacks_->SendVmoComplete(conn->GetKey().addr);
                                   }
                                 }
                               }
                             });
  event_.ProcessDescriptors<virtio_vsock_event_t>(
      [this](virtio_vsock_event_t* event, void* data, uint32_t data_len)
          TA_NO_THREAD_SAFETY_ANALYSIS {
            if (event->id == VIRTIO_VSOCK_EVENT_TRANSPORT_RESET) {
              TransportResetLocked();
            } else {
              zxlogf(ERROR, "%s: Received unknown event: %d", tag(), event->id);
            }
          });

  UpdateRxRingLocked();

  // Send any queued ops in any freed tx descriptors first, in preference to
  // any queued data transfers.
  while (!has_pending_op_.is_empty()) {
    auto conn = has_pending_op_.pop_front();
    uint16_t op = conn->TakePendingOp();
    if (!SendOp_RawLocked(conn->GetKey().addr, op, conn->GetCreditInfo())) {
      conn->QueueOp(op);
      has_pending_op_.push_front(conn);
      break;
    }
  }
  RetryTxLocked(false);
}

void SocketDevice::IrqConfigChange() {
  fbl::AutoLock lock(&lock_);
  uint32_t old_cid = cid_;
  UpdateCidLocked();
  if (cid_ != old_cid) {
    TransportResetLocked();
  }
}

void SocketDevice::ProcessRxDescriptor(virtio_vsock_hdr_t* header, void* data, uint32_t data_len) {
  if (header->dst_cid != cid_) {
    zxlogf(ERROR, " %s: Received message for cid %d, but believe our cid is %d", tag(),
           static_cast<uint32_t>(header->dst_cid), cid_);
    return;
  }

  ConnectionKey key = ConnectionKey::FromHdr(header);
  auto conn = connections_.find(key);
  if (conn != connections_.end()) {
    conn->UpdateCredit(header->buf_alloc, header->fwd_cnt);
  }

  if (header->op == VIRTIO_VSOCK_OP_RW) {
    if (conn == connections_.end()) {
      SendRstLocked(key);
    } else {
      if (!conn->Rx(data, data_len)) {
        NotifyAndCleanupConLocked(conn.CopyPointer());
      }
    }
  } else {
    RxOpLocked(conn, key, header->op);
  }
}

void SocketDevice::UpdateRxRingLocked() {
  // Refuse to process rx buffers if we don't have callbacks. If the callbacks
  // somehow vanish mid process then that's fine, we'll just dump a lot of
  // requests on the floor, but there's little else we can do.
  if (!callbacks_.client_end().is_valid()) {
    return;
  }
  rx_.ProcessDescriptors<virtio_vsock_hdr_t>(
      [this](virtio_vsock_hdr_t* header, void* data, uint32_t data_len)
          TA_NO_THREAD_SAFETY_ANALYSIS { this->ProcessRxDescriptor(header, data, data_len); });
}

void SocketDevice::RxOpLocked(ConnectionIterator conn, const ConnectionKey& key, uint16_t op) {
  switch (op) {
    case VIRTIO_VSOCK_OP_INVALID:
      zxlogf(ERROR, "%s: Received invalid op", tag());
      break;
    case VIRTIO_VSOCK_OP_REQUEST:
      // Don't care if we have a connection or not, just send it to the
      // service.
      if (callbacks_.client_end().is_valid()) {
        callbacks_->Request(key.addr);
      }
      break;
    case VIRTIO_VSOCK_OP_RESPONSE: {
      // Check for existing partial connection.
      if (conn == connections_.end()) {
        zxlogf(ERROR, "%s: Received response for unknown connection", tag());
        // We weren't trying to make a connection, so reject this
        SendRstLocked(key);
        break;
      }
      // Upgrade the channel.
      conn->MakeActive(dispatch_loop_.dispatcher());
      if (callbacks_.client_end().is_valid()) {
        callbacks_->Response(key.addr);
      }
      break;
    }
    case VIRTIO_VSOCK_OP_RST:
      if (conn != connections_.end()) {
        CleanupConLocked(conn.CopyPointer());
      }
      if (callbacks_.client_end().is_valid()) {
        callbacks_->Rst(key.addr);
      }
      break;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
      if (conn != connections_.end()) {
        // Shutdown and move into the zombie state until the service
        // confirms shutdown by sending the RST
        conn->Close(dispatch_loop_.dispatcher());
        DequeueTxLocked(conn.CopyPointer());
        DequeueOpLocked(conn.CopyPointer());
      }
      if (callbacks_.client_end().is_valid()) {
        callbacks_->Shutdown(key.addr);
      }
      break;
    case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
      if (conn == connections_.end()) {
        SendRstLocked(key);
      }
      if (QueuedForTxLocked(conn.CopyPointer())) {
        ContinueTxLocked(true, conn.CopyPointer());
      }
      break;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
      if (conn == connections_.end()) {
        SendRstLocked(key);
      } else {
        SendOpLocked(conn.CopyPointer(), VIRTIO_VSOCK_OP_CREDIT_UPDATE);
      }
      break;
    case VIRTIO_VSOCK_OP_RW:
      // This case should've gone to RxOp_Data
      zxlogf(ERROR, "%s: OP_RW not handled here", tag());
      break;
    default:
      zxlogf(ERROR, "%s: Unexpected op %d from host", tag(), op);
      break;
  }
}

bool SocketDevice::SendOp_RawLocked(const ConnectionKey& key, uint16_t op,
                                    const CreditInfo& credit) {
  virtio_vsock_hdr_t hdr = make_hdr(key, op, cid_, credit);
  // Grab a free descriptor
  uint16_t id;
  if (!tx_.AllocInPlace(&id)) {
    return false;
  }
  tx_.SetHeader(id, hdr);
  tx_.SubmitChain(id, 0);
  // Typically we call this in a path with a single TX, so minimal gains from
  // trying to coalesce this
  tx_.Kick();
  return true;
}

void SocketDevice::SendOpLocked(fbl::RefPtr<Connection> conn, uint16_t op) {
  // If there's a queue then keep queueing.
  if (!has_pending_op_.is_empty()) {
    // Store the op, updating if already queued.
    conn->QueueOp(op);
    QueueForOpLocked(conn);
    return;
  }

  CreditInfo credit = conn->GetCreditInfo();
  if (!SendOp_RawLocked(conn->GetKey(), op, credit)) {
    conn->QueueOp(op);
    QueueForOpLocked(conn);
  }
}

void SocketDevice::RetryTxLocked(bool force_credit_request) {
  for (auto it = has_pending_tx_.begin(); it != has_pending_tx_.end();) {
    auto prev = it;
    it++;
    ContinueTxLocked(force_credit_request, prev.CopyPointer());
  }
}

void SocketDevice::ContinueTxLocked(bool force_credit_request, fbl::RefPtr<Connection> conn) {
  zx_status_t status = conn->ContinueTx(force_credit_request, tx_, dispatch_loop_.dispatcher());
  if (status == ZX_OK || status == ZX_ERR_SHOULD_WAIT) {
    if (conn->HasPendingOp() && !QueuedForOpLocked(conn)) {
      SendOpLocked(conn, conn->TakePendingOp());
    }
    if (status == ZX_ERR_SHOULD_WAIT) {
      QueueForTxLocked(conn);
    } else if (status == ZX_OK) {
      DequeueTxLocked(conn);
    }
  } else {
    NotifyAndCleanupConLocked(conn);
  }
}

void SocketDevice::SendRstLocked(const ConnectionKey& key) {
  SendOp_RawLocked(key, VIRTIO_VSOCK_OP_RST, CreditInfo());
}

void SocketDevice::CleanupConLocked(fbl::RefPtr<Connection> conn) {
  conn->Close(dispatch_loop_.dispatcher());
  DequeueTxLocked(conn);
  DequeueOpLocked(conn);
  connections_.erase(*conn);
}

void SocketDevice::NotifyAndCleanupConLocked(fbl::RefPtr<Connection> conn) {
  if (callbacks_.client_end().is_valid()) {
    callbacks_->Rst(conn->GetKey().addr);
  }
  CleanupConLocked(conn);
}

void SocketDevice::CleanupConAndRstLocked(const ConnectionKey& key) {
  auto it = connections_.find(key);
  if (it != connections_.end()) {
    SendOpLocked(it.CopyPointer(), VIRTIO_VSOCK_OP_RST);
    CleanupConLocked(it.CopyPointer());
  } else {
    SendRstLocked(key);
  }
}

void SocketDevice::RemoveCallbacksLocked() {
  for (auto it = connections_.begin(); it != connections_.end(); it++) {
    SendOpLocked(it.CopyPointer(), VIRTIO_VSOCK_OP_RST);
    it->Close(dispatch_loop_.dispatcher());
  }
  connections_.clear();
  callback_closed_handler_.Cancel();
  callbacks_ = {};
  has_pending_tx_.clear();
  // We don't clear pending ops as we need our RST ops to finish sending.
}

bool SocketDevice::QueuedForTxLocked(fbl::RefPtr<Connection> conn) {
  return fbl::InContainer<PendingTxTag>(*conn);
}

void SocketDevice::QueueForTxLocked(fbl::RefPtr<Connection> conn) {
  if (!QueuedForTxLocked(conn)) {
    has_pending_tx_.push_back(conn);
    EnableTxRetryTimerLocked();
  }
}

void SocketDevice::DequeueTxLocked(fbl::RefPtr<Connection> conn) {
  if (QueuedForTxLocked(conn)) {
    has_pending_tx_.erase(*conn);
  }
}

bool SocketDevice::QueuedForOpLocked(fbl::RefPtr<Connection> conn) {
  return fbl::InContainer<PendingOpTag>(*conn);
}

void SocketDevice::QueueForOpLocked(fbl::RefPtr<Connection> conn) {
  if (!QueuedForOpLocked(conn)) {
    has_pending_op_.push_back(conn);
  }
}

void SocketDevice::DequeueOpLocked(fbl::RefPtr<Connection> conn) {
  if (QueuedForOpLocked(conn)) {
    has_pending_op_.erase(*conn);
  }
}

void SocketDevice::EnableTxRetryTimerLocked() {
  if (!have_timer_) {
    zx_status_t status = tx_retry_timer_.set(zx::deadline_after(zx::sec(1)), zx::sec(1));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to set timer %s", tag(), zx_status_get_string(status));
      return;
    }
    status = timer_wait_handler_.Begin(dispatch_loop_.dispatcher());
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to wait for timer %s", tag(), zx_status_get_string(status));
      return;
    }
    have_timer_ = true;
  }
}

void SocketDevice::TimerWaitHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                    zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    // Dispatcher shut down.
    return;
  }
  fbl::AutoLock lock(&lock_);
  have_timer_ = false;
  tx_retry_timer_.cancel();
  RetryTxLocked(true);
  if (!has_pending_tx_.is_empty()) {
    EnableTxRetryTimerLocked();
  }
}

void SocketDevice::CallbacksSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                      zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    // Dispatcher shut down.
    return;
  }
  fbl::AutoLock lock(&lock_);
  RemoveCallbacksLocked();
}
void SocketDevice::ConnectionSocketSignalled(zx_status_t status, const zx_packet_signal_t* signal,
                                             fbl::RefPtr<Connection> conn) {
  if (status != ZX_OK) {
    // Dispatcher shut down
    return;
  }
  fbl::AutoLock lock(&lock_);
  if (conn->IsShuttingDown()) {
    return;
  }
  if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
    NotifyAndCleanupConLocked(conn);
    return;
  }
  ContinueTxLocked(false, conn);
}

void SocketDevice::UpdateCidLocked() {
  virtio_vsock_config_t config;
  CopyDeviceConfig(&config, sizeof(config));
  cid_ = static_cast<uint32_t>(config.guest_cid);
}

void SocketDevice::ReleaseLocked() {
  RemoveCallbacksLocked();
  has_pending_op_.clear();

  // Shutting down the dispatch loop will remove any existing wait handlers for
  // things like timer_wait_handler_.
  dispatch_loop_.Shutdown();
  rx_.FreeBuffers();
  tx_.FreeBuffers();
  event_.FreeBuffers();
  virtio::Device::Release();
}

void SocketDevice::TransportResetLocked() {
  // Reload the CID when receiving a reset.
  zxlogf(INFO, "%s: Received transport reset!", tag());
  for (auto& it : connections_) {
    it.Close(dispatch_loop_.dispatcher());
  }
  connections_.clear();
  has_pending_tx_.clear();
  has_pending_op_.clear();
  UpdateCidLocked();
  if (callbacks_.client_end().is_valid()) {
    callbacks_->TransportReset(cid_);
  }
}

SocketDevice::IoBufferRing::IoBufferRing(virtio::Device* device, uint16_t count, uint32_t buf_size,
                                         bool host_write_only)
    : ring_(device), host_write_only_(host_write_only), count_(count), buf_size_(buf_size) {}

SocketDevice::IoBufferRing::~IoBufferRing() { FreeBuffers(); }

zx_status_t SocketDevice::IoBufferRing::Init(uint16_t index, const zx::bti& bti) {
  zx_status_t rc = ring_.Init(index, count_);
  if (rc != ZX_OK) {
    return rc;
  }
  rc = io_buffer_init(&io_buffer_, bti.get(), buf_size_ * count_,
                      IO_BUFFER_CONTIG | (host_write_only_ ? IO_BUFFER_RO : IO_BUFFER_RW));
  if (rc != ZX_OK) {
    return rc;
  }
  // Set the flags in all the descriptors if is host_write_only, this means the
  // device (aka host) can write to the buffers, but we as the driver may only
  // read from them.
  if (host_write_only_) {
    for (uint16_t id = 0; id < count_; id++) {
      struct vring_desc* desc = ring_.DescFromIndex(id);
      desc->addr = io_buffer_phys(&io_buffer_) + id * buf_size_;
      desc->len = buf_size_;
      desc->flags |= VRING_DESC_F_WRITE;
    }
  }
  return ZX_OK;
}

void SocketDevice::IoBufferRing::FreeBuffers() {
  if (io_buffer_is_valid(&io_buffer_)) {
    io_buffer_release(&io_buffer_);
  }
}

SocketDevice::RxIoBufferRing::RxIoBufferRing(virtio::Device* device, uint16_t count,
                                             uint32_t buf_size)
    : IoBufferRing(device, count, buf_size, true) {}

void SocketDevice::RxIoBufferRing::RefillRing() {
  assert(io_buffer_is_valid(&io_buffer_));
  bool needs_kick = false;
  uint16_t id;
  struct vring_desc* desc;
  while ((desc = ring_.AllocDescChain(1, &id))) {
    desc->len = buf_size_;
    ring_.SubmitChain(id);
    needs_kick = true;
  }
  if (needs_kick) {
    Kick();
  }
}

template <typename H, typename F>
void SocketDevice::RxIoBufferRing::ProcessDescriptors(F func) {
  ring_.IrqRingUpdate([this, &func](vring_used_elem* used_elem) {
    uint16_t last_id = static_cast<uint16_t>(used_elem->id);
    struct vring_desc* desc = ring_.DescFromIndex(last_id);
    if (desc->len < sizeof(H)) {
      zxlogf(ERROR, "Descriptor is too short");
    } else if ((desc->flags & VRING_DESC_F_NEXT) != 0) {
      zxlogf(ERROR, "Chained descriptors are not supported");
    } else {
      func(reinterpret_cast<H*>(GetRawDesc(last_id, sizeof(H))), GetRawDesc(last_id, 0, sizeof(H)),
           static_cast<uint32_t>(used_elem->len - sizeof(H)));
    }
    // Handle freeing arbitrarily long descriptor chains
    while ((desc->flags & VRING_DESC_F_NEXT) != 0) {
      uint16_t next_id = desc->next;
      ring_.FreeDesc(last_id);
      desc = ring_.DescFromIndex(last_id);
      last_id = next_id;
    }
    ring_.FreeDesc(last_id);
  });
  RefillRing();
}

SocketDevice::TxIoBufferRing::TxIoBufferRing(virtio::Device* device, uint16_t count,
                                             uint32_t buf_size)
    : IoBufferRing(device, count, buf_size, false) {}

void* SocketDevice::TxIoBufferRing::AllocInPlace(uint16_t* id) {
  struct vring_desc* desc = ring_.AllocDescChain(1, id);
  if (desc) {
    desc->addr = io_buffer_phys(&io_buffer_) + *id * buf_size_;
    return GetRawDesc(*id, 0, sizeof(virtio_vsock_hdr_t));
  }
  return nullptr;
}

bool SocketDevice::TxIoBufferRing::AllocIndirect(const ConnectionKey& key, uint16_t* id) {
  struct vring_desc* desc = ring_.AllocDescChain(2, id);
  if (!desc) {
    return false;
  }
  desc->addr = io_buffer_phys(&io_buffer_) + *id * buf_size_;
  *reinterpret_cast<ConnectionKey*>(
      GetRawDesc(*id, sizeof(ConnectionKey), sizeof(virtio_vsock_hdr_t))) = key;
  return true;
}

void SocketDevice::TxIoBufferRing::SetIndirectPayload(uint16_t id, uintptr_t payload) {
  struct vring_desc* desc = ring_.DescFromIndex(ring_.DescFromIndex(id)->next);
  desc->addr = payload;
}

void SocketDevice::TxIoBufferRing::SubmitChain(uint16_t id, uint32_t data_len) {
  struct vring_desc* desc = ring_.DescFromIndex(id);
  desc->len = sizeof(virtio_vsock_hdr_t);
  if ((desc->flags & VRING_DESC_F_NEXT) == 0) {
    desc->len += data_len;
  } else {
    desc = ring_.DescFromIndex(desc->next);
    desc->len = data_len;
  }
  ring_.SubmitChain(id);
}

void SocketDevice::TxIoBufferRing::FreeChain(uint16_t id) {
  struct vring_desc* desc = ring_.DescFromIndex(id);
  if ((desc->flags & VRING_DESC_F_NEXT) != 0) {
    ring_.FreeDesc(desc->next);
  }
  ring_.FreeDesc(id);
}

template <typename F>
void SocketDevice::TxIoBufferRing::ProcessDescriptors(F func) {
  ring_.IrqRingUpdate([this, &func](vring_used_elem* used_elem) {
    uint16_t id = static_cast<uint16_t>(used_elem->id);
    struct vring_desc* desc = ring_.DescFromIndex(id);
    if ((desc->flags & VRING_DESC_F_NEXT) != 0) {
      struct vring_desc* desc2 = ring_.DescFromIndex(desc->next);
      ConnectionKey* key = reinterpret_cast<ConnectionKey*>(
          GetRawDesc(id, sizeof(ConnectionKey), sizeof(virtio_vsock_hdr_t)));
      func(*key, desc2->addr);
      ring_.FreeDesc(desc->next);
    }
    ring_.FreeDesc(id);
  });
}

SocketDevice::Connection::Connection(const ConnectionKey& key, zx::socket data,
                                     SignalHandler wait_handler, uint32_t cid, fbl::Mutex& lock)
    : lock_(lock),
      key_(key),
      state_(CON_WAIT_RESPONSE),
      tx_count_(0),
      rx_count_(0),
      buf_alloc_(0),
      fwd_cnt_(0),
      data_(std::move(data)),
      wait_handler_(data_.get(), ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, 0,
                    async::Wait::Handler([this, wait_handler = std::move(wait_handler)](
                                             async_dispatcher_t* dispatcher, async::Wait* wait,
                                             zx_status_t status, const zx_packet_signal_t* signal) {
                      fbl::RefPtr<Connection> ref;
                      wait_handler_ref_.swap(ref);
                      wait_handler(status, signal, ref);
                    })),
      pending_vmo_(false),
      has_pending_op_(false),
      cid_(cid) {}

bool SocketDevice::Connection::PendingTx() {
  if (pending_vmo_) {
    return true;
  }
  return SocketTxPending();
}

bool SocketDevice::Connection::IsShuttingDown() {
  return state_ == Connection::CON_ZOMBIE || state_ == Connection::CON_SHUTTING_DOWN ||
         state_ == Connection::CON_WILL_SHUT_DOWN;
}

bool SocketDevice::Connection::BeginShutdown() {
  assert(!IsShuttingDown());
  if (PendingTx()) {
    state_ = Connection::CON_WILL_SHUT_DOWN;
    return false;
  }
  state_ = Connection::CON_SHUTTING_DOWN;
  return true;
}

bool SocketDevice::Connection::NotifyVmoTxComplete(uintptr_t paddr) {
  if (pending_vmo_ && vmo_.final_paddr_ == paddr) {
    vmo_.Release();
    pending_vmo_ = false;
    return true;
  }
  return false;
}

void SocketDevice::Connection::UpdateCredit(uint32_t buf, uint32_t fwd) {
  buf_alloc_ = buf;
  fwd_cnt_ = fwd;
}

void SocketDevice::Connection::MakeActive(async_dispatcher_t* disp) {
  if (state_ != Connection::CON_WAIT_RESPONSE) {
    zxlogf(ERROR, "Received response for already established connection");
    return;
  }
  BeginWait(disp);
  state_ = Connection::CON_ACTIVE;
}

bool SocketDevice::Connection::Rx(void* data, size_t len) {
  size_t written = 0;
  zx_status_t status = data_.write(0, data, len, &written);
  rx_count_ += static_cast<uint32_t>(written);
  // The way flow control works in vsock we should never end up in a
  // situation where the socket cannot hold the data. Therefore we consider
  // any failure to be catastrophic and terminate the connection.
  return status == ZX_OK && written == len;
}

SocketDevice::CreditInfo SocketDevice::Connection::GetCreditInfo() {
  zx_info_socket_t info;
  zx_status_t status = data_.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
  if (status == ZX_OK) {
    return CreditInfo(static_cast<uint32_t>(info.tx_buf_max),
                      static_cast<uint32_t>(info.tx_buf_size));
  } else {
    return CreditInfo();
  }
}

virtio_vsock_hdr_t SocketDevice::Connection::MakeHdr(uint16_t op) {
  return make_hdr(key_.addr, op, cid_, GetCreditInfo());
}

void SocketDevice::Connection::Close(async_dispatcher_t* dispatcher) {
  state_ = CON_ZOMBIE;
  zx_status_t __UNUSED status = async::PostTask(dispatcher, [this] {
    zx_status_t status = wait_handler_.Cancel();
    if (status == ZX_OK) {
      wait_handler_ref_.reset();
    };
  });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t SocketDevice::Connection::ContinueTx(bool force_credit_request, TxIoBufferRing& tx,
                                                 async_dispatcher_t* dispatcher) {
  if (pending_vmo_) {
    bool more = DoVmoTx(force_credit_request, tx);
    if (more) {
      return ZX_ERR_SHOULD_WAIT;
    }
    // If the vmo has fully transmitted then we are allowed to start transmitting
    // data from the socket again, so we fall through to check the socket.
  }
  if (SocketTxPending()) {
    return DoSocketTx(force_credit_request, tx, dispatcher);
  } else {
    BeginWait(dispatcher);
  }
  return ZX_OK;
}

zx_status_t SocketDevice::Connection::SetVmo(zx::bti& bti, zx::vmo vmo, uint64_t offset,
                                             uint64_t len, uint64_t bti_contiguity) {
  if (pending_vmo_) {
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t result = vmo_.Set(bti, std::move(vmo), offset, len, bti_contiguity);
  if (result != ZX_OK) {
    return result;
  }
  pending_vmo_ = true;
  return result;
}

void SocketDevice::Connection::QueueOp(uint16_t new_op) {
  // RW operations don't get queued here
  assert(new_op != VIRTIO_VSOCK_OP_RW);
  if (!has_pending_op_) {
    pending_op_ = new_op;
    has_pending_op_ = true;
    return;
  }
  // We preference RST, then SHUTDOWN for ops since we never want to
  // overwrite those. Then we preference CREDIT_REQUEST, since if we
  // overwrite a CREDIT_UPDATE this is fine as the REQUEST will contain
  // an update anyway. The only other op we send is REQUEST and RESPONSE
  // and they will never queue over themselves or other ops, except for
  // RST, which have already taken care of.
  if (pending_op_ == VIRTIO_VSOCK_OP_RST || new_op == VIRTIO_VSOCK_OP_RST) {
    pending_op_ = VIRTIO_VSOCK_OP_RST;
  } else if (pending_op_ == VIRTIO_VSOCK_OP_SHUTDOWN || new_op == VIRTIO_VSOCK_OP_SHUTDOWN) {
    pending_op_ = VIRTIO_VSOCK_OP_SHUTDOWN;
  } else if (pending_op_ == VIRTIO_VSOCK_OP_CREDIT_REQUEST ||
             new_op == VIRTIO_VSOCK_OP_CREDIT_REQUEST) {
    pending_op_ = VIRTIO_VSOCK_OP_CREDIT_REQUEST;
  } else {
    pending_op_ = new_op;
  }
}

bool SocketDevice::Connection::HasPendingOp() { return has_pending_op_; }
uint16_t SocketDevice::Connection::TakePendingOp() {
  assert(HasPendingOp());
  has_pending_op_ = false;
  return pending_op_;
}

size_t SocketDevice::Connection::GetHash(const ConnectionKey& key) {
  return key.addr.local_port + key.addr.remote_port + key.addr.remote_cid;
}

const SocketDevice::ConnectionKey& SocketDevice::Connection::GetKey() const { return key_; }

void SocketDevice::Connection::CountTx(uint32_t len) {
  // Previous peer_free amount.
  uint32_t prev_peer_free = buf_alloc_ - (tx_count_ - fwd_cnt_);
  // Determine our projected 'peer_free' amount after this.
  uint32_t next_peer_free = buf_alloc_ - ((tx_count_ + len) - fwd_cnt_);
  // Have we crossed the threshold of 40% or 80% used?
  uint32_t prev_util = 100 - ((prev_peer_free * 100) / buf_alloc_);
  uint32_t next_util = 100 - ((next_peer_free * 100) / buf_alloc_);
  if ((prev_util < 40 && next_util >= 40) || (prev_util < 80 && next_util >= 80)) {
    QueueOp(VIRTIO_VSOCK_OP_CREDIT_REQUEST);
  }
  tx_count_ += len;
}

bool SocketDevice::Connection::SocketTxPending() {
  zx_info_socket_t info;
  zx_status_t status = data_.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return false;
  }
  return info.rx_buf_size != 0;
}

bool SocketDevice::Connection::DoVmoTx(bool force_credit_request, TxIoBufferRing& tx) {
  bool needs_kick = false;
  auto auto_kick = fit::defer([&needs_kick, &tx]() {
    if (needs_kick) {
      tx.Kick();
    }
  });
  while (vmo_.transfer_length_ > 0) {
    uint32_t peer_free = GetPeerFree(force_credit_request);
    if (peer_free == 0) {
      return true;
    }

    uint16_t id;
    if (!tx.AllocIndirect(key_, &id)) {
      return true;
    }
    uint32_t len = static_cast<uint32_t>(vmo_.NextChunkLen(peer_free));
    uintptr_t paddr = vmo_.Consume(len);
    tx.SetIndirectPayload(id, paddr);

    virtio_vsock_hdr_t hdr = MakeHdr(VIRTIO_VSOCK_OP_RW);
    hdr.len = len;
    tx.SetHeader(id, hdr);
    tx.SubmitChain(id, len);
    needs_kick = true;
    CountTx(len);
  }
  return false;
}

zx_status_t SocketDevice::Connection::DoSocketTx(bool force_credit_request, TxIoBufferRing& tx,
                                                 async_dispatcher_t* dispatcher) {
  bool needs_kick = false;
  auto auto_kick = fit::defer([&needs_kick, &tx]() {
    if (needs_kick) {
      tx.Kick();
    }
  });
  zx_status_t status;
  do {
    uint32_t peer_free = GetPeerFree(force_credit_request);
    if (peer_free == 0) {
      return ZX_ERR_SHOULD_WAIT;
    }

    uint16_t id;
    void* data = tx.AllocInPlace(&id);
    if (!data) {
      return ZX_ERR_SHOULD_WAIT;
    }

    size_t read_raw;
    status = data_.read(
        0, data,
        std::min(static_cast<uint32_t>(kFrameSize - sizeof(virtio_vsock_hdr_t)), peer_free),
        &read_raw);
    uint32_t read = static_cast<uint32_t>(read_raw);
    if (status == ZX_OK) {
      virtio_vsock_hdr_t hdr = MakeHdr(VIRTIO_VSOCK_OP_RW);
      hdr.len = read;
      tx.SetHeader(id, hdr);
      tx.SubmitChain(id, read);
      needs_kick = true;
      CountTx(read);
    } else {
      tx.FreeChain(id);
    }
  } while (status == ZX_OK);
  if (status == ZX_ERR_SHOULD_WAIT) {
    BeginWait(dispatcher);
    // We have received all the data off the socket, so the correct thing to return
    // to the caller is ZX_OK so that it doesn't think there is still TX pending.
    return ZX_OK;
  }

  return status;
}

void SocketDevice::Connection::BeginWait(async_dispatcher_t* disp) {
  fbl::RefPtr<Connection> wait_ref = fbl::RefPtr(this);
  zx_status_t __UNUSED status = async::PostTask(disp, [wait_ref, disp] {
    fbl::AutoLock lock(&wait_ref->lock_);

    if (!wait_ref->wait_handler_.is_pending()) {
      wait_ref->wait_handler_ref_ = wait_ref;
      zx_status_t status = wait_ref->wait_handler_.Begin(disp);
      if (status != ZX_OK) {
        ZX_DEBUG_ASSERT(status == ZX_ERR_BAD_STATE);
        wait_ref->wait_handler_ref_.reset();
      }
    }
  });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

uint32_t SocketDevice::Connection::GetPeerFree(bool request_credit) {
  uint32_t peer_free = buf_alloc_ - (tx_count_ - fwd_cnt_);
  if (peer_free == 0 && request_credit) {
    QueueOp(VIRTIO_VSOCK_OP_CREDIT_REQUEST);
  }
  return peer_free;
}

zx_status_t SocketDevice::Connection::VmoWalker::Set(zx::bti& bti, zx::vmo vmo, uint64_t offset,
                                                     uint64_t len, uint64_t bti_contiguity) {
  Release();
  vmo_ = std::move(vmo);
  contiguity_ = bti_contiguity;
  transfer_offset_ = offset;
  transfer_length_ = len;
  // Construct a base pointer that is aligned to the contiguity
  base_addr_ = fbl::round_down(offset, contiguity_);
  // Determine an extended range to take into account the rounding amount
  uint64_t full_range = fbl::round_up((offset - base_addr_) + len, contiguity_);
  size_t num_paddr = full_range / contiguity_;

  fbl::AllocChecker ac;
  paddrs_ = fbl::MakeArray<zx_paddr_t>(&ac, num_paddr);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = bti.pin(ZX_BTI_PERM_READ | ZX_BTI_COMPRESS, vmo_, base_addr_, full_range,
                               paddrs_.data(), paddrs_.size(), &pinned_pages_);
  if (status != ZX_OK) {
    Release();
  }
  return status;
}

void SocketDevice::Connection::VmoWalker::Release() {
  if (pinned_pages_.is_valid()) {
    pinned_pages_.unpin();
  }
  pinned_pages_.reset();
  vmo_.reset();
  final_paddr_ = 0;
  paddrs_.reset();
}

uint64_t SocketDevice::Connection::VmoWalker::NextChunkLen(uint64_t max) {
  // First constrain max by the remaining transfer
  uint64_t next_len = std::min(max, transfer_length_);
  // Determine the end of the current contiguity region
  uint64_t contiguity_area_end = fbl::round_up(transfer_offset_ + 1, contiguity_);
  uint64_t max_in_contiguity = contiguity_area_end - transfer_offset_;
  // Take the minimum of our transfer and the contiguity
  return std::min(next_len, max_in_contiguity);
}

zx_paddr_t SocketDevice::Connection::VmoWalker::Consume(uint64_t len) {
  assert(NextChunkLen(len) >= len);
  // No need to subtract base_addr off transfer_offset since base_addr is
  // already defined to be aligned to contiguity_ and so is factored out of the
  // mod operation.
  uint64_t contiguity_offset = transfer_offset_ % contiguity_;
  zx_paddr_t ret = paddrs_[(transfer_offset_ - base_addr_) / contiguity_] + contiguity_offset;
  transfer_offset_ += len;
  transfer_length_ -= len;
  if (transfer_length_ == 0) {
    final_paddr_ = ret;
  }
  return ret;
}

}  // namespace virtio
