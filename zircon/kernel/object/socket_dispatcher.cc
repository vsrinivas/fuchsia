// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/socket_dispatcher.h"

#include <assert.h>
#include <err.h>
#include <lib/counters.h>
#include <lib/user_copy/user_ptr.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>
#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <object/handle.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_socket_create_count, "dispatcher.socket.create")
KCOUNTER(dispatcher_socket_destroy_count, "dispatcher.socket.destroy")

// static
zx_status_t SocketDispatcher::Create(uint32_t flags, KernelHandle<SocketDispatcher>* handle0,
                                     KernelHandle<SocketDispatcher>* handle1, zx_rights_t* rights) {
  LTRACE_ENTRY;

  if (flags & ~ZX_SOCKET_CREATE_MASK)
    return ZX_ERR_INVALID_ARGS;

  fbl::AllocChecker ac;

  zx_signals_t starting_signals = ZX_SOCKET_WRITABLE;

  auto holder0 = fbl::AdoptRef(new (&ac) PeerHolder<SocketDispatcher>());
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;
  auto holder1 = holder0;

  KernelHandle new_handle0(
      fbl::AdoptRef(new (&ac) SocketDispatcher(ktl::move(holder0), starting_signals, flags)));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  KernelHandle new_handle1(
      fbl::AdoptRef(new (&ac) SocketDispatcher(ktl::move(holder1), starting_signals, flags)));
  if (!ac.check())
    return ZX_ERR_NO_MEMORY;

  new_handle0.dispatcher()->Init(new_handle1.dispatcher());
  new_handle1.dispatcher()->Init(new_handle0.dispatcher());

  *rights = default_rights();
  *handle0 = ktl::move(new_handle0);
  *handle1 = ktl::move(new_handle1);
  return ZX_OK;
}

SocketDispatcher::SocketDispatcher(fbl::RefPtr<PeerHolder<SocketDispatcher>> holder,
                                   zx_signals_t starting_signals, uint32_t flags)
    : PeeredDispatcher(ktl::move(holder), starting_signals),
      flags_(flags),
      read_threshold_(0),
      write_threshold_(0),
      read_disabled_(false) {
  kcounter_add(dispatcher_socket_create_count, 1);
}

SocketDispatcher::~SocketDispatcher() { kcounter_add(dispatcher_socket_destroy_count, 1); }

// This is called before either SocketDispatcher is accessible from threads other than the one
// initializing the socket, so it does not need locking.
void SocketDispatcher::Init(fbl::RefPtr<SocketDispatcher> other) TA_NO_THREAD_SAFETY_ANALYSIS {
  peer_ = ktl::move(other);
  peer_koid_ = peer_->get_koid();
}

void SocketDispatcher::on_zero_handles_locked() { canary_.Assert(); }

void SocketDispatcher::OnPeerZeroHandlesLocked() {
  canary_.Assert();

  UpdateStateLocked(ZX_SOCKET_WRITABLE, ZX_SOCKET_PEER_CLOSED);
}

zx_status_t SocketDispatcher::UserSignalSelfLocked(uint32_t clear_mask, uint32_t set_mask) {
  canary_.Assert();
  UpdateStateLocked(clear_mask, set_mask);
  return ZX_OK;
}

zx_status_t SocketDispatcher::Shutdown(uint32_t how) {
  canary_.Assert();

  LTRACE_ENTRY;

  const bool shutdown_read = how & ZX_SOCKET_SHUTDOWN_READ;
  const bool shutdown_write = how & ZX_SOCKET_SHUTDOWN_WRITE;

  Guard<fbl::Mutex> guard{get_lock()};

  zx_signals_t signals = GetSignalsStateLocked();
  // If we're already shut down in the requested way, return immediately.
  const uint32_t want_signals = (shutdown_read ? ZX_SOCKET_PEER_WRITE_DISABLED : 0) |
                                (shutdown_write ? ZX_SOCKET_WRITE_DISABLED : 0);
  const uint32_t have_signals =
      signals & (ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_WRITE_DISABLED);
  if (want_signals == have_signals) {
    return ZX_OK;
  }
  zx_signals_t clear_mask = 0u;
  zx_signals_t set_mask = 0u;
  if (shutdown_read) {
    read_disabled_ = true;
    set_mask |= ZX_SOCKET_PEER_WRITE_DISABLED;
  }
  if (shutdown_write) {
    clear_mask |= ZX_SOCKET_WRITABLE;
    set_mask |= ZX_SOCKET_WRITE_DISABLED;
  }
  UpdateStateLocked(clear_mask, set_mask);

  // Our peer already be closed - if so, we've already updated our own bits so we are done. If the
  // peer is done, we need to notify them of the state change.
  if (peer_ != nullptr) {
    AssertHeld(*peer_->get_lock());
    return peer_->ShutdownOtherLocked(how);
  }

  return ZX_OK;
}

zx_status_t SocketDispatcher::ShutdownOtherLocked(uint32_t how) {
  canary_.Assert();

  const bool shutdown_read = how & ZX_SOCKET_SHUTDOWN_READ;
  const bool shutdown_write = how & ZX_SOCKET_SHUTDOWN_WRITE;

  zx_signals_t clear_mask = 0u;
  zx_signals_t set_mask = 0u;
  if (shutdown_read) {
    clear_mask |= ZX_SOCKET_WRITABLE;
    set_mask |= ZX_SOCKET_WRITE_DISABLED;
  }
  if (shutdown_write) {
    read_disabled_ = true;
    set_mask |= ZX_SOCKET_PEER_WRITE_DISABLED;
  }

  UpdateStateLocked(clear_mask, set_mask);
  return ZX_OK;
}

zx_status_t SocketDispatcher::Write(user_in_ptr<const char> src, size_t len, size_t* nwritten) {
  canary_.Assert();

  LTRACE_ENTRY;

  Guard<fbl::Mutex> guard{get_lock()};

  if (!peer_)
    return ZX_ERR_PEER_CLOSED;
  zx_signals_t signals = GetSignalsStateLocked();
  if (signals & ZX_SOCKET_WRITE_DISABLED)
    return ZX_ERR_BAD_STATE;

  if (len == 0) {
    *nwritten = 0;
    return ZX_OK;
  }
  if (len != static_cast<size_t>(static_cast<uint32_t>(len)))
    return ZX_ERR_INVALID_ARGS;

  AssertHeld(*peer_->get_lock());
  return peer_->WriteSelfLocked(src, len, nwritten);
}

zx_status_t SocketDispatcher::WriteSelfLocked(user_in_ptr<const char> src, size_t len,
                                              size_t* written) {
  canary_.Assert();

  if (is_full())
    return ZX_ERR_SHOULD_WAIT;

  bool was_empty = is_empty();

  size_t st = 0u;
  zx_status_t status;
  if (flags_ & ZX_SOCKET_DATAGRAM) {
    status = data_.WriteDatagram(src, len, &st);
  } else {
    status = data_.WriteStream(src, len, &st);
  }
  if (status)
    return status;

  zx_signals_t clear = 0u;
  zx_signals_t set = 0u;

  if (st > 0) {
    if (was_empty)
      set |= ZX_SOCKET_READABLE;
    // Assert signal if we go above the read threshold
    if ((read_threshold_ > 0) && (data_.size() >= read_threshold_))
      set |= ZX_SOCKET_READ_THRESHOLD;
    if (set) {
      UpdateStateLocked(0u, set);
    }
    if (peer_) {
      size_t peer_write_threshold = peer_->write_threshold_;
      // If free space falls below threshold, de-signal
      if ((peer_write_threshold > 0) && ((data_.max_size() - data_.size()) < peer_write_threshold))
        clear |= ZX_SOCKET_WRITE_THRESHOLD;
    }
  }

  if (peer_ && is_full())
    clear |= ZX_SOCKET_WRITABLE;

  if (clear) {
    AssertHeld(*peer_->get_lock());
    peer_->UpdateStateLocked(clear, 0u);
  }

  *written = st;
  return status;
}

zx_status_t SocketDispatcher::Read(ReadType type, user_out_ptr<char> dst, size_t len,
                                   size_t* nread) {
  canary_.Assert();

  LTRACE_ENTRY;

  Guard<fbl::Mutex> guard{get_lock()};

  if (len != (size_t)((uint32_t)len))
    return ZX_ERR_INVALID_ARGS;

  if (is_empty()) {
    if (!peer_)
      return ZX_ERR_PEER_CLOSED;
    // If reading is disabled on our end and we're empty, we'll never become readable again.
    // Return a different error to let the caller know.
    if (read_disabled_)
      return ZX_ERR_BAD_STATE;
    return ZX_ERR_SHOULD_WAIT;
  }

  size_t actual = 0;
  if (type == ReadType::kPeek) {
    zx_status_t status = data_.Peek(dst, len, flags_ & ZX_SOCKET_DATAGRAM, &actual);
    if (status != ZX_OK) {
      return status;
    }
  } else {
    bool was_full = is_full();

    zx_status_t status = data_.Read(dst, len, flags_ & ZX_SOCKET_DATAGRAM, &actual);
    if (status != ZX_OK) {
      return status;
    }

    zx_signals_t clear = 0u;
    zx_signals_t set = 0u;

    // Deassert signal if we fell below the read threshold
    if ((read_threshold_ > 0) && (data_.size() < read_threshold_))
      clear |= ZX_SOCKET_READ_THRESHOLD;

    if (is_empty()) {
      clear |= ZX_SOCKET_READABLE;
    }
    if (set || clear) {
      UpdateStateLocked(clear, set);
      clear = set = 0u;
    }
    if (peer_) {
      // Assert (write threshold) signal if space available is above
      // threshold.
      size_t peer_write_threshold = peer_->write_threshold_;
      if (peer_write_threshold > 0 && ((data_.max_size() - data_.size()) >= peer_write_threshold))
        set |= ZX_SOCKET_WRITE_THRESHOLD;
      if (was_full && (actual > 0))
        set |= ZX_SOCKET_WRITABLE;
      if (set) {
        AssertHeld(*peer_->get_lock());
        peer_->UpdateStateLocked(0u, set);
      }
    }
  }

  *nread = actual;
  return ZX_OK;
}

void SocketDispatcher::GetInfo(zx_info_socket_t* info) const {
  canary_.Assert();
  Guard<fbl::Mutex> guard{get_lock()};
  *info = zx_info_socket_t{
      .options = flags_,
      .rx_buf_max = data_.max_size(),
      .rx_buf_size = data_.size(),
      .rx_buf_available = data_.size(flags_ & ZX_SOCKET_DATAGRAM),
      .tx_buf_max = 0,
      .tx_buf_size = 0,
  };
  if (peer_) {
    AssertHeld(*peer_->get_lock());  // Alias of this->get_lock().
    info->tx_buf_max = peer_->data_.max_size();
    info->tx_buf_size = peer_->data_.size();
  }
}

size_t SocketDispatcher::GetReadThreshold() const {
  canary_.Assert();
  Guard<fbl::Mutex> guard{get_lock()};
  return read_threshold_;
}

size_t SocketDispatcher::GetWriteThreshold() const {
  canary_.Assert();
  Guard<fbl::Mutex> guard{get_lock()};
  return write_threshold_;
}

zx_status_t SocketDispatcher::SetReadThreshold(size_t value) {
  canary_.Assert();
  Guard<fbl::Mutex> guard{get_lock()};
  if (value > data_.max_size())
    return ZX_ERR_INVALID_ARGS;
  read_threshold_ = value;
  // Setting 0 disables thresholding. Deassert signal unconditionally.
  if (value == 0) {
    UpdateStateLocked(ZX_SOCKET_READ_THRESHOLD, 0u);
  } else {
    if (data_.size() >= read_threshold_) {
      // Assert signal if we have queued data above the read threshold
      UpdateStateLocked(0u, ZX_SOCKET_READ_THRESHOLD);
    } else {
      // De-assert signal if we upped threshold and queued data drops below
      UpdateStateLocked(ZX_SOCKET_READ_THRESHOLD, 0u);
    }
  }
  return ZX_OK;
}

zx_status_t SocketDispatcher::SetWriteThreshold(size_t value) {
  canary_.Assert();
  Guard<fbl::Mutex> guard{get_lock()};
  if (peer_ == NULL)
    return ZX_ERR_PEER_CLOSED;
  AssertHeld(*peer_->get_lock());
  if (value > peer_->data_.max_size())
    return ZX_ERR_INVALID_ARGS;
  write_threshold_ = value;
  // Setting 0 disables thresholding. Deassert signal unconditionally.
  if (value == 0) {
    UpdateStateLocked(ZX_SOCKET_WRITE_THRESHOLD, 0u);
  } else {
    // Assert signal if we have available space above the write threshold
    if ((peer_->data_.max_size() - peer_->data_.size()) >= write_threshold_) {
      // Assert signal if we have available space above the write threshold
      UpdateStateLocked(0u, ZX_SOCKET_WRITE_THRESHOLD);
    } else {
      // De-assert signal if we upped threshold and available space drops below
      UpdateStateLocked(ZX_SOCKET_WRITE_THRESHOLD, 0u);
    }
  }
  return ZX_OK;
}
