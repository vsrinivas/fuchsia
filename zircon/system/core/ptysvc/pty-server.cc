// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pty-server.h"

#include "pty-client-vnode.h"
#include "pty-client.h"

PtyServer::PtyServer(zx::eventpair local, zx::eventpair remote, fs::Vfs* vfs)
    : local_(std::move(local)), remote_(std::move(remote)), vfs_(vfs) {}

PtyServer::~PtyServer() = default;

zx_status_t PtyServer::Create(fbl::RefPtr<PtyServer>* out, fs::Vfs* vfs) {
  zx::eventpair local, remote;
  zx_status_t status = zx::eventpair::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  // Create the FIFO in the "hung-up" state.  Note that this is considered
  // "readable" so that clients will try to read and see an EOF condition via a
  // 0-byte response with ZX_OK.
  local.signal_peer(0, ::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE |
                           ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP);
  *out = fbl::MakeRefCounted<PtyServer>(std::move(local), std::move(remote), vfs);
  return ZX_OK;
}

zx_status_t PtyServer::Read(void* data, size_t count, size_t* out_actual) {
  if (count == 0) {
    *out_actual = 0;
    return ZX_OK;
  }

  bool eof = false;

  bool was_full = rx_fifo_.is_full();
  size_t length = rx_fifo_.Read(data, count);
  if (rx_fifo_.is_empty()) {
    if (clients_.is_empty()) {
      eof = true;
    } else if (length > 0) {
      // We only need to clear the READABLE signal if we read anything.
      local_.signal_peer(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, 0);
    }
  }
  if (was_full && length > 0) {
    if (active_) {
      active_->AssertWritableSignal();
    }
  }

  if (length > 0) {
    *out_actual = length;
    return ZX_OK;
  } else if (eof) {
    *out_actual = 0;
    return ZX_OK;
  } else {
    return ZX_ERR_SHOULD_WAIT;
  }
}

zx_status_t PtyServer::Write(const void* data, size_t count, size_t* out_actual) {
  size_t length;
  zx_status_t status;
  if ((status = Send(data, count, &length)) != ZX_OK) {
    return status;
  } else {
    *out_actual = length;
    return ZX_OK;
  }
}

zx_status_t PtyServer::CreateClient(uint32_t id, zx::channel client_request) {
  unsigned num_clients = 0;

  // Make sure we don't already have a client with the requested id.
  for (auto& client : clients_) {
    if (client.id() == id) {
      return ZX_ERR_INVALID_ARGS;
    }
    num_clients++;
  }

  fbl::RefPtr<PtyClient> client;
  zx_status_t status = PtyClient::Create(fbl::RefPtr(this), id, &client);
  if (status != ZX_OK) {
    return status;
  }
  clients_.push_back(client);

  auto vnode = fbl::MakeRefCounted<PtyClientVnode>(client);
  status = vfs_->Serve(vnode, std::move(client_request), fs::VnodeConnectionOptions::ReadWrite());
  if (status != ZX_OK) {
    return status;
  }

  if (active_ == nullptr) {
    MakeActive(client);
  }
  if (id == 0) {
    control_ = client;
    if (events_) {
      control_->AssertEventSignal();
    }
  }

  if (num_clients == 0) {
    // if there were no clients, make sure we take server
    // out of HANGUP and READABLE, where it landed if all
    // its clients had closed
    local_.signal_peer(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE |
                           ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP,
                       0);
  }

  client->AdjustSignals();
  return ZX_OK;
}

void PtyServer::RemoveClient(PtyClient* client) {
  ZX_ASSERT(client->server().get() == this);

  if (client->is_control()) {
    control_.reset();
  }

  if (client->is_active()) {
    // signal controlling client, if there is one
    if (control_) {
      // Note that in the implementation this is ported from, DEVICE_SIGNAL_HANGUP is never cleared
      // after being asserted by this?  This seems likely to be a bug.
      control_->AssertActiveHungup();
    }
    active_.reset();
  }

  clients_.erase(*client);

  // signal server, if the last client has gone away
  if (clients_.is_empty()) {
    local_.signal_peer(::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE,
                       ::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE |
                           ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP);
  }
}

zx_status_t PtyServer::Recv(const void* data, size_t len, size_t* actual, bool* is_full) {
  if (len == 0) {
    *actual = 0;
    return ZX_OK;
  }

  bool was_empty = rx_fifo_.is_empty();
  *actual = rx_fifo_.Write(data, len, false);
  if (was_empty && *actual) {
    local_.signal_peer(0, ::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE);
  }

  *is_full = rx_fifo_.is_full();

  if (*actual == 0) {
    return ZX_ERR_SHOULD_WAIT;
  } else {
    return ZX_OK;
  }
}

zx_status_t PtyServer::Send(const void* data, size_t len, size_t* actual) {
  if (!active_) {
    *actual = 0;
    return ZX_ERR_PEER_CLOSED;
  }

  if (len == 0) {
    *actual = 0;
    return ZX_OK;
  }

  Fifo* client_fifo = active_->rx_fifo();
  if (client_fifo->is_full()) {
    return ZX_ERR_SHOULD_WAIT;
  }

  bool was_empty = client_fifo->is_empty();
  if (active_->in_raw_mode()) {
    *actual = client_fifo->Write(data, len, false);
  } else {
    if (len > Fifo::kSize) {
      len = Fifo::kSize;
    }
    auto ch = static_cast<const uint8_t*>(data);
    unsigned n = 0;
    unsigned evt = 0;
    while (n < len) {
      // The ASCII code that Ctrl-C generates
      constexpr uint8_t kCtrlC = 0x3;
      if (*ch++ == kCtrlC) {
        evt = ::llcpp::fuchsia::hardware::pty::EVENT_INTERRUPT;
        break;
      }
      n++;
    }
    size_t r = client_fifo->Write(data, n, false);
    if ((r == n) && evt) {
      // consume the event
      r++;
      events_ |= evt;
      if (control_) {
        control_->AssertEventSignal();
      }
    }
    *actual = r;
  }
  if (was_empty && !client_fifo->is_empty()) {
    active_->AssertReadableSignal();
  }
  if (client_fifo->is_full()) {
    local_.signal_peer(::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE, 0);
  }
  return ZX_OK;
}

zx_status_t PtyServer::MakeActive(uint32_t id) {
  for (auto& client : clients_) {
    if (client.id() == id) {
      MakeActive(fbl::RefPtr(&client));
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_FOUND;
}

void PtyServer::MakeActive(fbl::RefPtr<PtyClient> client) {
  if (active_ == client) {
    return;
  }

  if (active_) {
    active_->DeAssertWritableSignal();
  }
  active_ = std::move(client);
  active_->AssertWritableSignal();

  zx_signals_t to_clear = ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP;
  zx_signals_t to_set = 0;
  if (active_->rx_fifo()->is_full()) {
    to_clear |= ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE;
  } else {
    to_set |= ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE;
  }

  local_.signal_peer(to_clear, to_set);
}

void PtyServer::Shutdown() {
  for (auto& client : clients_) {
    // inform clients that server is gone
    client.AssertHangupSignal();
  }
  active_.reset();
}

uint32_t PtyServer::DrainEvents() {
  uint32_t events = events_;
  events_ = 0;
  if (active_ == nullptr) {
    events |= ::llcpp::fuchsia::hardware::pty::EVENT_HANGUP;
  }
  control_->DeAssertEventSignal();
  return events;
}
