// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "socket_channel_relay.h"

#include <utility>

#include <lib/async/default.h>

#include "lib/fxl/logging.h"

namespace btlib {
namespace l2cap {

namespace internal {

SocketChannelRelay::SocketChannelRelay(zx::socket socket,
                                       fbl::RefPtr<Channel> channel,
                                       DeactivationCallback deactivation_cb)
    : state_(RelayState::kActivating),
      socket_(std::move(socket)),
      channel_(channel),
      dispatcher_(async_get_default_dispatcher()),
      deactivation_cb_(std::move(deactivation_cb)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(socket_);
  FXL_DCHECK(channel_);

  // Note: binding |this| is safe, as BindWait() wraps the bound method inside
  // of a lambda which verifies that |this| hasn't been destroyed.
  BindWait(ZX_SOCKET_READABLE, "socket read waiter", &sock_read_waiter_,
           fit::bind_member(this, &SocketChannelRelay::OnSocketReadable));
  BindWait(ZX_SOCKET_PEER_CLOSED, "socket close waiter", &sock_close_waiter_,
           fit::bind_member(this, &SocketChannelRelay::OnSocketClosed));
}

SocketChannelRelay::~SocketChannelRelay() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  if (state_ != RelayState::kDeactivated) {
    FXL_VLOG(5) << "l2cap: Deactivating relay for channel " << channel_->id()
                << " in dtor; will require Channel's mutex";
    Deactivate();
  }
}

bool SocketChannelRelay::Activate() {
  FXL_DCHECK(state_ == RelayState::kActivating);

  // Note: we assume that BeginWait() does not synchronously dispatch any
  // events. The wait handler will assert otherwise.
  if (!BeginWait("socket close waiter", &sock_close_waiter_)) {
    // Perhaps |dispatcher| is already stopped.
    return false;
  }

  if (!BeginWait("socket read waiter", &sock_read_waiter_)) {
    // Perhaps |dispatcher| is already stopped.
    return false;
  }

  const auto self = weak_ptr_factory_.GetWeakPtr();
  const auto channel_id = channel_->id();
  const bool activate_success = channel_->Activate(
      [self, channel_id](SDU sdu) {
        // Note: this lambda _may_ be invoked synchronously.
        if (self) {
          self->OnChannelDataReceived(std::move(sdu));
        } else {
          FXL_VLOG(5) << "Ignoring SDU received on destroyed relay (channel_id="
                      << channel_id << ")";
        }
      },
      [self, channel_id] {
        if (self) {
          self->OnChannelClosed();
        } else {
          FXL_VLOG(5)
              << "Ignoring channel closure on destroyed relay (channel_id="
              << channel_id << ")";
        }
      },
      dispatcher_);
  if (!activate_success) {
    return false;
  }

  state_ = RelayState::kActivated;
  return true;
}

void SocketChannelRelay::Deactivate() { FXL_NOTIMPLEMENTED(); }

void SocketChannelRelay::DeactivateAndRequestDestruction() {
  Deactivate();
  if (deactivation_cb_) {
    // NOTE: deactivation_cb_ is expected to destroy |this|. Since |this|
    // owns deactivation_cb_, we move() deactivation_cb_ outside of |this|
    // before invoking the callback.
    auto moved_deactivation_cb = std::move(deactivation_cb_);
    moved_deactivation_cb(channel_->id());
  }
}

void SocketChannelRelay::OnSocketReadable(zx_status_t status) {
  FXL_DCHECK(state_ == RelayState::kActivated);
  if (!CopyFromSocketToChannel() ||
      !BeginWait("socket read waiter", &sock_read_waiter_)) {
    DeactivateAndRequestDestruction();
  }
}

void SocketChannelRelay::OnSocketClosed(zx_status_t status) {
  FXL_DCHECK(state_ == RelayState::kActivated);
  DeactivateAndRequestDestruction();
}

void SocketChannelRelay::OnChannelDataReceived(SDU sdu) {
  FXL_NOTIMPLEMENTED();
}

void SocketChannelRelay::OnChannelClosed() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(state_ != RelayState::kActivating);
  FXL_DCHECK(state_ != RelayState::kDeactivated);

  if (state_ == RelayState::kDeactivating) {
    FXL_LOG(INFO) << "l2cap: Ignoring " << __func__ << " on socket for channel "
                  << channel_->id() << " while deactivating";
    return;
  }

  FXL_DCHECK(state_ == RelayState::kActivated);
  DeactivateAndRequestDestruction();
}

bool SocketChannelRelay::CopyFromSocketToChannel() {
  // Subtle: we make the read buffer larger than the TX MTU, so that we can
  // detect truncated datagrams.
  const size_t read_buf_size = channel_->tx_mtu() + 1;

  // TODO(NET-1390): Consider yielding occasionally. As-is, we run the risk of
  // starving other SocketChannelRelays on the same |dispatcher| (and anyone
  // else on |dispatcher|), if a misbehaving process spams its L2CAP socket. And
  // even if starvation isn't an issue, latency/jitter might be.
  zx_status_t read_res;
  uint8_t read_buf[read_buf_size];
  do {
    size_t n_bytes_read = 0;
    read_res = socket_.read(0, read_buf, read_buf_size, &n_bytes_read);
    FXL_DCHECK(read_res == ZX_OK || read_res == ZX_ERR_SHOULD_WAIT ||
               read_res == ZX_ERR_PEER_CLOSED)
        << ": " << zx_status_get_string(read_res);
    FXL_DCHECK(n_bytes_read <= read_buf_size)
        << "(n_bytes_read=" << n_bytes_read
        << ", read_buf_size=" << read_buf_size << ")";

    if (read_res == ZX_ERR_SHOULD_WAIT) {
      FXL_VLOG(5) << "l2cap: Failed to read from socket for channel "
                  << channel_->id() << ": " << zx_status_get_string(read_res);
      return true;
    }

    if (read_res == ZX_ERR_PEER_CLOSED) {
      FXL_VLOG(5) << "l2cap: Failed to read from socket for channel "
                  << channel_->id() << ": " << zx_status_get_string(read_res);
      return false;
    }

    FXL_DCHECK(n_bytes_read > 0);
    if (n_bytes_read > channel_->tx_mtu()) {
      return false;
    }

    // TODO(NET-1391): For low latency and low jitter, IWBN to avoid allocating
    // dynamic memory on every read.
    bool write_success =
        channel_->Send(std::make_unique<common::DynamicByteBuffer>(
            common::BufferView(read_buf, n_bytes_read)));
    if (!write_success) {
      FXL_VLOG(5) << "l2cap: Failed to write " << n_bytes_read
                  << " bytes to channel " << channel_->id();
    }
  } while (read_res == ZX_OK);

  return true;
}

void SocketChannelRelay::BindWait(zx_signals_t trigger, const char* wait_name,
                                  async::Wait* wait,
                                  fit::function<void(zx_status_t)> handler) {
  wait->set_object(socket_.get());
  wait->set_trigger(trigger);
  wait->set_handler([self = weak_ptr_factory_.GetWeakPtr(),
                     channel_id = channel_->id(), wait_name,
                     expected_wait = wait,
                     dcheck_suffix = fxl::StringPrintf(
                         "(%s, channel_id=%d)", wait_name, channel_->id()),
                     handler = std::move(handler)](
                        async_dispatcher_t* actual_dispatcher,
                        async::WaitBase* actual_wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    FXL_DCHECK(self) << dcheck_suffix;
    FXL_DCHECK(actual_dispatcher == self->dispatcher_) << dcheck_suffix;
    FXL_DCHECK(actual_wait == expected_wait) << dcheck_suffix;
    FXL_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED) << dcheck_suffix;

    if (status == ZX_ERR_CANCELED) {  // Dispatcher is shutting down.
      FXL_VLOG(1) << "l2cap: " << wait_name
                  << " canceled on socket for channel " << channel_id;
      self->DeactivateAndRequestDestruction();
      return;
    }

    FXL_DCHECK(signal) << dcheck_suffix;
    FXL_DCHECK(signal->trigger == expected_wait->trigger()) << dcheck_suffix;
    FXL_DCHECK(self->thread_checker_.IsCreationThreadCurrent())
        << dcheck_suffix;
    FXL_DCHECK(self->state_ != RelayState::kActivating) << dcheck_suffix;
    FXL_DCHECK(self->state_ != RelayState::kDeactivated) << dcheck_suffix;

    if (self->state_ == RelayState::kDeactivating) {
      FXL_LOG(INFO) << "l2cap: Ignoring " << wait_name
                    << " on socket for channel " << channel_id
                    << " while deactivating";
      return;
    }
    handler(status);
  });
}

bool SocketChannelRelay::BeginWait(const char* wait_name, async::Wait* wait) {
  FXL_DCHECK(state_ != RelayState::kDeactivating);
  FXL_DCHECK(state_ != RelayState::kDeactivated);

  if (wait->is_pending()) {
    return true;
  }

  zx_status_t wait_res = wait->Begin(dispatcher_);
  FXL_DCHECK(wait_res == ZX_OK || wait_res == ZX_ERR_BAD_STATE);

  if (wait_res != ZX_OK) {
    FXL_LOG(ERROR) << "l2cap: Failed to enable waiting on " << wait_name << ": "
                   << zx_status_get_string(wait_res);
    return false;
  }

  return true;
}

}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
