// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/mbuf.h"

#include <lib/counters.h>
#include <lib/fit/defer.h>
#include <lib/user_copy/user_ptr.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/type_traits.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

// Total amount of memory occupied by MBuf objects.
KCOUNTER(mbuf_total_bytes_count, "mbuf.total_bytes")

// Amount of memory occupied by MBuf objects on free lists.
KCOUNTER(mbuf_free_list_bytes_count, "mbuf.free_list_bytes")

MBufChain::~MBufChain() {
  while (!buffers_.is_empty()) {
    delete buffers_.pop_front();
  }
  while (!freelist_.is_empty()) {
    kcounter_add(mbuf_free_list_bytes_count, -static_cast<int64_t>(sizeof(MBufChain::MBuf)));
    delete freelist_.pop_front();
  }
}

zx_status_t MBufChain::Read(user_out_ptr<char> dst, size_t len, bool datagram, size_t* actual) {
  return ReadHelper(this, dst, len, datagram, actual);
}

zx_status_t MBufChain::Peek(user_out_ptr<char> dst, size_t len, bool datagram,
                            size_t* actual) const {
  return ReadHelper(this, dst, len, datagram, actual);
}

template <class T>
zx_status_t MBufChain::ReadHelper(T* chain, user_out_ptr<char> dst, size_t len, bool datagram,
                                  size_t* actual) {
  if (chain->size_ == 0) {
    *actual = 0;
    return ZX_OK;
  }

  if (datagram && len > chain->buffers_.front().pkt_len_)
    len = chain->buffers_.front().pkt_len_;

  size_t pos = 0;
  auto iter = chain->buffers_.begin();
  // To handle peeking and non-peeking, cache our read cursor offset and set it when we're done.
  uint32_t read_off = chain->read_cursor_off_;
  auto update_cursor = fit::defer([&] {
    if constexpr (!ktl::is_const<T>::value) {
      chain->read_cursor_off_ = read_off;
    }
  });

  while (pos < len && iter != chain->buffers_.end()) {
    const char* src = iter->data_ + read_off;
    size_t copy_len = ktl::min(static_cast<size_t>(iter->len_ - read_off), len - pos);
    zx_status_t status = dst.byte_offset(pos).copy_array_to_user(src, copy_len);
    if (status != ZX_OK) {
      // Record the fact that some data might have been read, even if the overall operation is
      // considered a failure.
      *actual = pos;
      return status;
    }

    pos += copy_len;

    if constexpr (ktl::is_const<T>::value) {
      read_off = 0;
      ++iter;
    } else {
      // TODO(fxbug.dev/34143): Note, we're advancing (consuming data) after each copy.  This means
      // that if a subsequent copy fails (perhaps because a the write to the user buffer
      // faults) data will be "dropped".  Consider changing this function to only advance (and
      // free) once all data has been successfully copied.

      read_off += static_cast<uint32_t>(copy_len);
      chain->size_ -= copy_len;

      if (read_off == iter->len_ || datagram) {
        if (datagram) {
          chain->size_ -= (iter->len_ - read_off);
        }
        if (chain->write_cursor_ == &(*iter)) {
          chain->write_cursor_ = nullptr;
        }
        chain->FreeMBuf(chain->buffers_.pop_front());
        iter = chain->buffers_.begin();
        // Start the next buffer at the beginning.
        read_off = 0;
      }
    }
  }

  // Drain any leftover mbufs in the datagram packet if we're consuming data.
  if constexpr (!ktl::is_const<T>::value) {
    if (datagram) {
      while (!chain->buffers_.is_empty() && chain->buffers_.front().pkt_len_ == 0) {
        MBuf* cur = chain->buffers_.pop_front();
        chain->size_ -= (cur->len_ - read_off);
        if (chain->write_cursor_ == cur) {
          DEBUG_ASSERT(chain->buffers_.is_empty());
          chain->write_cursor_ = nullptr;
        }
        chain->FreeMBuf(cur);
        read_off = 0;
      }
    }
  }

  *actual = pos;
  return ZX_OK;
}

zx_status_t MBufChain::WriteDatagram(user_in_ptr<const char> src, size_t len, size_t* written) {
  if (len == 0) {
    *written = 0;
    return ZX_ERR_INVALID_ARGS;
  }
  if (len > kSizeMax) {
    *written = 0;
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (len + size_ > kSizeMax) {
    *written = 0;
    return ZX_ERR_SHOULD_WAIT;
  }

  fbl::SinglyLinkedList<MBuf*> bufs;
  for (size_t need = 1 + ((len - 1) / MBuf::kPayloadSize); need != 0; need--) {
    auto buf = AllocMBuf();
    if (buf == nullptr) {
      while (!bufs.is_empty()) {
        FreeMBuf(bufs.pop_front());
      }
      *written = 0;
      return ZX_ERR_SHOULD_WAIT;
    }
    bufs.push_front(buf);
  }

  size_t pos = 0;
  for (auto& buf : bufs) {
    size_t copy_len = ktl::min(MBuf::kPayloadSize, len - pos);
    if (src.byte_offset(pos).copy_array_from_user(buf.data_, copy_len) != ZX_OK) {
      while (!bufs.is_empty()) {
        FreeMBuf(bufs.pop_front());
      }
      *written = 0;
      return ZX_ERR_INVALID_ARGS;  // Bad user buffer.
    }
    pos += copy_len;
    buf.len_ += static_cast<uint32_t>(copy_len);
  }

  bufs.front().pkt_len_ = static_cast<uint32_t>(len);

  // Successfully built the packet mbufs. Put it on the socket.
  while (!bufs.is_empty()) {
    auto next = bufs.pop_front();
    if (write_cursor_ == nullptr) {
      DEBUG_ASSERT(buffers_.is_empty());
      buffers_.push_front(next);
    } else {
      buffers_.insert_after(buffers_.make_iterator(*write_cursor_), next);
    }
    write_cursor_ = next;
  }

  *written = len;
  size_ += len;
  return ZX_OK;
}

zx_status_t MBufChain::WriteStream(user_in_ptr<const char> src, size_t len, size_t* written) {
  if (write_cursor_ == nullptr) {
    DEBUG_ASSERT(buffers_.is_empty());
    write_cursor_ = AllocMBuf();
    if (write_cursor_ == nullptr) {
      *written = 0;
      return ZX_ERR_SHOULD_WAIT;
    }
    buffers_.push_front(write_cursor_);
  }

  size_t pos = 0;
  while (pos < len) {
    if (write_cursor_->rem() == 0) {
      auto next = AllocMBuf();
      if (next == nullptr)
        break;
      buffers_.insert_after(buffers_.make_iterator(*write_cursor_), next);
      write_cursor_ = next;
    }
    char* dst = write_cursor_->data_ + write_cursor_->len_;
    size_t copy_len = ktl::min(write_cursor_->rem(), len - pos);
    if (size_ + copy_len > kSizeMax) {
      copy_len = kSizeMax - size_;
      if (copy_len == 0)
        break;
    }
    zx_status_t status = src.byte_offset(pos).copy_array_from_user(dst, copy_len);
    if (status != ZX_OK) {
      // TODO(fxbug.dev/34143): Note that although we set |written| for the benefit of the
      // socket dispatcher updating signals, ultimately we're not indicating to the caller
      // that data added so far in previous copies was written successfully. This means the
      // caller may try to re-send the same data again, leading to duplicate data. Consider
      // changing the socket dispatcher to forward this partial write information to the caller,
      // or consider not committing any of the new data until we can ensure success, or consider
      // putting the socket in a state where it can't succeed a subsequent write.
      *written = pos;
      return status;
    }

    pos += copy_len;
    write_cursor_->len_ += static_cast<uint32_t>(copy_len);
    size_ += copy_len;
  }

  *written = pos;

  if (pos == 0)
    return ZX_ERR_SHOULD_WAIT;

  return ZX_OK;
}

MBufChain::MBuf* MBufChain::AllocMBuf() {
  if (freelist_.is_empty()) {
    fbl::AllocChecker ac;
    MBuf* buf = new (&ac) MBuf();
    return (!ac.check()) ? nullptr : buf;
  }
  kcounter_add(mbuf_free_list_bytes_count, -static_cast<int64_t>(sizeof(MBufChain::MBuf)));
  return freelist_.pop_front();
}

void MBufChain::FreeMBuf(MBuf* buf) {
  buf->len_ = 0u;
  freelist_.push_front(buf);
  kcounter_add(mbuf_free_list_bytes_count, sizeof(MBufChain::MBuf));
}

MBufChain::MBuf::MBuf() { kcounter_add(mbuf_total_bytes_count, sizeof(MBufChain::MBuf)); }

MBufChain::MBuf::~MBuf() {
  kcounter_add(mbuf_total_bytes_count, -static_cast<int64_t>(sizeof(MBufChain::MBuf)));
}
