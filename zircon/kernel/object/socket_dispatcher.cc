// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/socket_dispatcher.h"

#include <assert.h>
#include <lib/counters.h>
#include <lib/user_copy/user_ptr.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/types.h>

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
zx::result<SocketDispatcher::Disposition> SocketDispatcher::Disposition::TryFrom(
    uint32_t disposition) {
  switch (disposition) {
    case 0:
      return zx::success(Disposition(Disposition::Value::kNone));
    case ZX_SOCKET_DISPOSITION_WRITE_DISABLED:
      return zx::success(Disposition(Disposition::Value::kWriteDisabled));
    case ZX_SOCKET_DISPOSITION_WRITE_ENABLED:
      return zx::success(Disposition(Disposition::Value::kWriteEnabled));
    default:
      return zx::error(ZX_ERR_INVALID_ARGS);
  }
}

SocketDispatcher::Disposition::Disposition(SocketDispatcher::Disposition::Value disposition)
    : value_(disposition) {}

SocketDispatcher::Disposition::operator Value() const { return value_; }

// static
zx_status_t SocketDispatcher::Create(uint32_t flags, KernelHandle<SocketDispatcher>* handle0,
                                     KernelHandle<SocketDispatcher>* handle1, zx_rights_t* rights) {
  LTRACE_ENTRY;

  if (flags & ~ZX_SOCKET_CREATE_MASK)
    return ZX_ERR_INVALID_ARGS;

  fbl::AllocChecker ac;

  zx_signals_t starting_signals = ZX_SOCKET_WRITABLE;

  auto holder0 = fbl::AdoptRef(new (&ac) PeerHolderType());
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

  new_handle0.dispatcher()->InitPeer(new_handle1.dispatcher());
  new_handle1.dispatcher()->InitPeer(new_handle0.dispatcher());

  *rights = default_rights();
  *handle0 = ktl::move(new_handle0);
  *handle1 = ktl::move(new_handle1);
  return ZX_OK;
}

SocketDispatcher::SocketDispatcher(fbl::RefPtr<PeerHolderType> holder,
                                   zx_signals_t starting_signals, uint32_t flags)
    : PeeredDispatcher(ktl::move(holder), starting_signals),
      flags_(flags),
      read_threshold_(0),
      write_threshold_(0),
      read_disabled_(false) {
  kcounter_add(dispatcher_socket_create_count, 1);
}

SocketDispatcher::~SocketDispatcher() { kcounter_add(dispatcher_socket_destroy_count, 1); }

void SocketDispatcher::on_zero_handles_locked() { canary_.Assert(); }

void SocketDispatcher::OnPeerZeroHandlesLocked() {
  canary_.Assert();

  UpdateStateLocked(ZX_SOCKET_WRITABLE, ZX_SOCKET_PEER_CLOSED);
}

namespace {
void ExtendMasksFromDisposition(SocketDispatcher::Disposition disposition, zx_signals_t& clear_mask,
                                zx_signals_t& set_mask, zx_signals_t& clear_mask_peer,
                                zx_signals_t& set_mask_peer) {
  switch (disposition) {
    case SocketDispatcher::Disposition::kWriteDisabled:
      clear_mask |= ZX_SOCKET_WRITABLE;
      set_mask |= ZX_SOCKET_WRITE_DISABLED;
      set_mask_peer |= ZX_SOCKET_PEER_WRITE_DISABLED;
      break;
    case SocketDispatcher::Disposition::kWriteEnabled:
      clear_mask |= ZX_SOCKET_WRITE_DISABLED;
      set_mask |= ZX_SOCKET_WRITABLE;
      clear_mask_peer |= ZX_SOCKET_PEER_WRITE_DISABLED;
      break;
    case SocketDispatcher::Disposition::kNone:
      break;
  }
}
}  // namespace

void SocketDispatcher::UpdateReadStatus(Disposition disposition_peer) {
  switch (disposition_peer) {
    case Disposition::kWriteDisabled:
      read_disabled_ = true;
      break;
    case Disposition::kWriteEnabled:
      read_disabled_ = false;
      break;
    case Disposition::kNone:
      break;
  }
}

bool SocketDispatcher::IsDispositionStateValid(Disposition disposition_peer) const {
  // All the data written by an endpoint must be read by the other endpoint before writes can be
  // re-enabled, as per /docs/reference/syscalls/socket_set_disposition.md.
  return !(disposition_peer == Disposition::kWriteEnabled && !is_empty() &&
           GetSignalsStateLocked() & ZX_SOCKET_PEER_WRITE_DISABLED);
}

zx_status_t SocketDispatcher::SetDisposition(Disposition disposition,
                                             Disposition disposition_peer) {
  canary_.Assert();

  LTRACE_ENTRY;

  if (disposition == Disposition::kNone && disposition_peer == Disposition::kNone) {
    // Nothing to do, return early.
    return ZX_OK;
  }

  zx_signals_t clear_mask = 0u, set_mask = 0u, clear_mask_peer = 0u, set_mask_peer = 0u;
  ExtendMasksFromDisposition(disposition, clear_mask, set_mask, clear_mask_peer, set_mask_peer);
  Guard<CriticalMutex> guard{get_lock()};

  if (!IsDispositionStateValid(disposition_peer)) {
    return ZX_ERR_BAD_STATE;
  }

  if (peer() != nullptr) {
    AssertHeld(*peer()->get_lock());
    if (!peer()->IsDispositionStateValid(disposition)) {
      return ZX_ERR_BAD_STATE;
    }
    ExtendMasksFromDisposition(disposition_peer, clear_mask_peer, set_mask_peer, clear_mask,
                               set_mask);
    peer()->UpdateReadStatus(disposition);
    peer()->UpdateStateLocked(clear_mask_peer, set_mask_peer);
  }

  UpdateReadStatus(disposition_peer);
  UpdateStateLocked(clear_mask, set_mask);

  return ZX_OK;
}

zx_status_t SocketDispatcher::Write(user_in_ptr<const char> src, size_t len, size_t* nwritten) {
  canary_.Assert();

  LTRACE_ENTRY;

  Guard<CriticalMutex> guard{get_lock()};

  if (peer() == nullptr)
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

  AssertHeld(*peer()->get_lock());
  return peer()->WriteSelfLocked(src, len, nwritten, guard);
}

zx_status_t SocketDispatcher::WriteSelfLocked(user_in_ptr<const char> src, size_t len,
                                              size_t* written, Guard<CriticalMutex>& guard) {
  canary_.Assert();

  if (is_full())
    return ZX_ERR_SHOULD_WAIT;

  bool was_empty = is_empty();

  size_t st = 0u;
  zx_status_t status;

  // TODO(fxb/99589): Perform user copying while holding the dispatcher lock is generally not
  // not allowed, but is exempted here while a fix for sockets is developed. Performing the
  // MBufChain operations (which do the actual user copy) with tracking disabled will allow the
  // user copy to go through, with side effect of reducing the effectiveness of any other lockdep
  // detections that might involve this lock for the duration of the operation..
  guard.CallUntracked([&] {
    AssertHeld(*get_lock());
    if (flags_ & ZX_SOCKET_DATAGRAM) {
      status = data_.WriteDatagram(src, len, &st);
    } else {
      status = data_.WriteStream(src, len, &st);
    }
  });

  // Regardless of the status, data may have been added, and so we need to update the signals.

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
    if (peer()) {
      size_t peer_write_threshold = peer()->write_threshold_;
      // If free space falls below threshold, de-signal
      if ((peer_write_threshold > 0) && ((data_.max_size() - data_.size()) < peer_write_threshold))
        clear |= ZX_SOCKET_WRITE_THRESHOLD;
    }
  }

  if (peer() && is_full())
    clear |= ZX_SOCKET_WRITABLE;

  if (clear) {
    AssertHeld(*peer()->get_lock());
    peer()->UpdateStateLocked(clear, 0u);
  }

  if (status == ZX_OK) {
    *written = st;
  }
  return status;
}

zx_status_t SocketDispatcher::Read(ReadType type, user_out_ptr<char> dst, size_t len,
                                   size_t* nread) {
  canary_.Assert();

  LTRACE_ENTRY;

  Guard<CriticalMutex> guard{get_lock()};

  if (len != (size_t)((uint32_t)len))
    return ZX_ERR_INVALID_ARGS;

  if (is_empty()) {
    if (peer() == nullptr)
      return ZX_ERR_PEER_CLOSED;
    // If reading is disabled on our end and we're empty, we'll never become readable again.
    // Return a different error to let the caller know.
    if (read_disabled_)
      return ZX_ERR_BAD_STATE;
    return ZX_ERR_SHOULD_WAIT;
  }

  size_t actual = 0;
  if (type == ReadType::kPeek) {
    zx_status_t status = ZX_OK;
    // TODO(fxb/99589): See comment in WriteSelfLocked on why we use CallUntracked.
    guard.CallUntracked([&] {
      AssertHeld(*get_lock());
      status = data_.Peek(dst, len, flags_ & ZX_SOCKET_DATAGRAM, &actual);
    });
    if (status != ZX_OK) {
      return status;
    }
  } else {
    bool was_full = is_full();

    zx_status_t status = ZX_OK;
    // TODO(fxb/99589): See comment in WriteSelfLocked on why we use CallUntracked.
    guard.CallUntracked([&] {
      AssertHeld(*get_lock());
      status = data_.Read(dst, len, flags_ & ZX_SOCKET_DATAGRAM, &actual);
    });
    // Regardless of the status, data may have been consumed, and so we need to update the signals.

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
    if (peer()) {
      // Assert (write threshold) signal if space available is above
      // threshold.
      size_t peer_write_threshold = peer()->write_threshold_;
      if (peer_write_threshold > 0 && ((data_.max_size() - data_.size()) >= peer_write_threshold))
        set |= ZX_SOCKET_WRITE_THRESHOLD;
      if (was_full && (actual > 0))
        set |= ZX_SOCKET_WRITABLE;
      if (set) {
        AssertHeld(*peer()->get_lock());
        peer()->UpdateStateLocked(0u, set);
      }
    }
    if (status != ZX_OK) {
      return status;
    }
  }

  *nread = actual;
  return ZX_OK;
}

void SocketDispatcher::GetInfo(zx_info_socket_t* info) const {
  canary_.Assert();
  Guard<CriticalMutex> guard{get_lock()};
  *info = zx_info_socket_t{
      .options = flags_,
      .padding1 = {},
      .rx_buf_max = data_.max_size(),
      .rx_buf_size = data_.size(),
      .rx_buf_available = data_.size(flags_ & ZX_SOCKET_DATAGRAM),
      .tx_buf_max = 0,
      .tx_buf_size = 0,
  };
  if (peer()) {
    AssertHeld(*peer()->get_lock());  // Alias of this->get_lock().
    info->tx_buf_max = peer()->data_.max_size();
    info->tx_buf_size = peer()->data_.size();
  }
}

size_t SocketDispatcher::GetReadThreshold() const {
  canary_.Assert();
  Guard<CriticalMutex> guard{get_lock()};
  return read_threshold_;
}

size_t SocketDispatcher::GetWriteThreshold() const {
  canary_.Assert();
  Guard<CriticalMutex> guard{get_lock()};
  return write_threshold_;
}

zx_status_t SocketDispatcher::SetReadThreshold(size_t value) {
  canary_.Assert();
  Guard<CriticalMutex> guard{get_lock()};
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
  Guard<CriticalMutex> guard{get_lock()};
  if (peer() == NULL)
    return ZX_ERR_PEER_CLOSED;
  AssertHeld(*peer()->get_lock());
  if (value > peer()->data_.max_size())
    return ZX_ERR_INVALID_ARGS;
  write_threshold_ = value;
  // Setting 0 disables thresholding. Deassert signal unconditionally.
  if (value == 0) {
    UpdateStateLocked(ZX_SOCKET_WRITE_THRESHOLD, 0u);
  } else {
    // Assert signal if we have available space above the write threshold
    if ((peer()->data_.max_size() - peer()->data_.size()) >= write_threshold_) {
      // Assert signal if we have available space above the write threshold
      UpdateStateLocked(0u, ZX_SOCKET_WRITE_THRESHOLD);
    } else {
      // De-assert signal if we upped threshold and available space drops below
      UpdateStateLocked(ZX_SOCKET_WRITE_THRESHOLD, 0u);
    }
  }
  return ZX_OK;
}
