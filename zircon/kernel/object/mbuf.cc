// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/mbuf.h"

#include <lib/counters.h>
#include <lib/user_copy/user_ptr.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/type_traits.h>

#define LOCAL_TRACE 0

constexpr size_t MBufChain::MBuf::kHeaderSize;
constexpr size_t MBufChain::MBuf::kMallocSize;
constexpr size_t MBufChain::MBuf::kPayloadSize;
constexpr size_t MBufChain::kSizeMax;

// Total amount of memory occupied by MBuf objects.
KCOUNTER(mbuf_total_bytes_count, "mbuf.total_bytes")

// Amount of memory occupied by MBuf objects on free lists.
KCOUNTER(mbuf_free_list_bytes_count, "mbuf.free_list_bytes")

size_t MBufChain::MBuf::rem() const { return kPayloadSize - (off_ + len_); }

MBufChain::~MBufChain() {
  while (!tail_.is_empty()) {
    delete tail_.pop_front();
  }
  while (!freelist_.is_empty()) {
    kcounter_add(mbuf_free_list_bytes_count, -sizeof(MBufChain::MBuf));
    delete freelist_.pop_front();
  }
}

bool MBufChain::is_full() const { return size_ >= kSizeMax; }

bool MBufChain::is_empty() const { return size_ == 0; }

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

  if (datagram && len > chain->tail_.front().pkt_len_)
    len = chain->tail_.front().pkt_len_;

  size_t pos = 0;
  auto iter = chain->tail_.begin();
  while (pos < len && iter != chain->tail_.end()) {
    const char* src = iter->data_ + iter->off_;
    size_t copy_len = ktl::min(static_cast<size_t>(iter->len_), len - pos);
    zx_status_t status = dst.byte_offset(pos).copy_array_to_user(src, copy_len);
    if (status != ZX_OK) {
      return status;
    }

    pos += copy_len;

    if constexpr (ktl::is_const<T>::value) {
      ++iter;
    } else {
      // TODO(ZX-4366): Note, we're advancing (consuming data) after each copy.  This means
      // that if a subsequent copy fails (perhaps because a the write to the user buffer
      // faults) data will be "dropped".  Consider changing this function to only advance (and
      // free) once all data has been successfully copied.

      iter->off_ += static_cast<uint32_t>(copy_len);
      iter->len_ -= static_cast<uint32_t>(copy_len);
      chain->size_ -= copy_len;

      if (iter->len_ == 0 || datagram) {
        chain->size_ -= iter->len_;
        if (chain->head_ == &(*iter))
          chain->head_ = nullptr;
        chain->FreeMBuf(chain->tail_.pop_front());
        iter = chain->tail_.begin();
      }
    }
  }

  // Drain any leftover mbufs in the datagram packet if we're consuming data.
  if constexpr (!ktl::is_const<T>::value) {
    if (datagram) {
      while (!chain->tail_.is_empty() && chain->tail_.front().pkt_len_ == 0) {
        MBuf* cur = chain->tail_.pop_front();
        chain->size_ -= cur->len_;
        if (chain->head_ == cur)
          chain->head_ = nullptr;
        chain->FreeMBuf(cur);
      }
    }
  }

  *actual = pos;
  return ZX_OK;
}

zx_status_t MBufChain::WriteDatagram(user_in_ptr<const char> src, size_t len, size_t* written) {
  if (len == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (len > kSizeMax)
    return ZX_ERR_OUT_OF_RANGE;
  if (len + size_ > kSizeMax)
    return ZX_ERR_SHOULD_WAIT;

  fbl::SinglyLinkedList<MBuf*> bufs;
  for (size_t need = 1 + ((len - 1) / MBuf::kPayloadSize); need != 0; need--) {
    auto buf = AllocMBuf();
    if (buf == nullptr) {
      while (!bufs.is_empty())
        FreeMBuf(bufs.pop_front());
      return ZX_ERR_SHOULD_WAIT;
    }
    bufs.push_front(buf);
  }

  size_t pos = 0;
  for (auto& buf : bufs) {
    size_t copy_len = ktl::min(MBuf::kPayloadSize, len - pos);
    if (src.byte_offset(pos).copy_array_from_user(buf.data_, copy_len) != ZX_OK) {
      while (!bufs.is_empty())
        FreeMBuf(bufs.pop_front());
      return ZX_ERR_INVALID_ARGS;  // Bad user buffer.
    }
    pos += copy_len;
    buf.len_ += static_cast<uint32_t>(copy_len);
  }

  bufs.front().pkt_len_ = static_cast<uint32_t>(len);

  // Successfully built the packet mbufs. Put it on the socket.
  while (!bufs.is_empty()) {
    auto next = bufs.pop_front();
    if (head_ == nullptr) {
      tail_.push_front(next);
    } else {
      tail_.insert_after(tail_.make_iterator(*head_), next);
    }
    head_ = next;
  }

  *written = len;
  size_ += len;
  return ZX_OK;
}

zx_status_t MBufChain::WriteStream(user_in_ptr<const char> src, size_t len, size_t* written) {
  if (head_ == nullptr) {
    head_ = AllocMBuf();
    if (head_ == nullptr)
      return ZX_ERR_SHOULD_WAIT;
    tail_.push_front(head_);
  }

  size_t pos = 0;
  while (pos < len) {
    if (head_->rem() == 0) {
      auto next = AllocMBuf();
      if (next == nullptr)
        break;
      tail_.insert_after(tail_.make_iterator(*head_), next);
      head_ = next;
    }
    char* dst = head_->data_ + head_->off_ + head_->len_;
    size_t copy_len = ktl::min(head_->rem(), len - pos);
    if (size_ + copy_len > kSizeMax) {
      copy_len = kSizeMax - size_;
      if (copy_len == 0)
        break;
    }
    zx_status_t status = src.byte_offset(pos).copy_array_from_user(dst, copy_len);
    if (status != ZX_OK) {
      // TODO(ZX-4366): Note, we're not indicating to the caller that data added so far in
      // previous copies was written successfully.  This means the caller may try to re-send
      // the same data again, leading to duplicate data.  Consider changing this function to
      // report that some data was written so far, or consider not committing any of the new
      // data until we can ensure success, or consider putting the socket in a state where it
      // can't succeed a subsequent write.
      return status;
    }

    pos += copy_len;
    head_->len_ += static_cast<uint32_t>(copy_len);
    size_ += copy_len;
  }

  if (pos == 0)
    return ZX_ERR_SHOULD_WAIT;

  *written = pos;
  return ZX_OK;
}

MBufChain::MBuf* MBufChain::AllocMBuf() {
  if (freelist_.is_empty()) {
    fbl::AllocChecker ac;
    MBuf* buf = new (&ac) MBuf();
    return (!ac.check()) ? nullptr : buf;
  }
  kcounter_add(mbuf_free_list_bytes_count, -sizeof(MBufChain::MBuf));
  return freelist_.pop_front();
}

void MBufChain::FreeMBuf(MBuf* buf) {
  buf->off_ = 0u;
  buf->len_ = 0u;
  freelist_.push_front(buf);
  kcounter_add(mbuf_free_list_bytes_count, sizeof(MBufChain::MBuf));
}

MBufChain::MBuf::MBuf() { kcounter_add(mbuf_total_bytes_count, sizeof(MBufChain::MBuf)); }

MBufChain::MBuf::~MBuf() { kcounter_add(mbuf_total_bytes_count, -sizeof(MBufChain::MBuf)); }
