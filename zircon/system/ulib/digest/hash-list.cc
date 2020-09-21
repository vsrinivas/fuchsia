// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>

#include <digest/digest.h>
#include <digest/hash-list.h>
#include <digest/node-digest.h>

namespace digest {
namespace internal {

// HashListBase<T>

zx_status_t HashListBase::Align(size_t *data_off, size_t *buf_len) const {
  size_t buf_end;
  if (add_overflow(*data_off, *buf_len, &buf_end) || buf_end > data_len_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *data_off = node_digest_.PrevAligned(*data_off);
  *buf_len = std::min(node_digest_.NextAligned(buf_end), data_len_) - *data_off;
  return ZX_OK;
}

zx_status_t HashListBase::SetDataLength(size_t data_len) {
  if (data_len > node_digest_.MaxAligned()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  data_off_ = 0;
  data_len_ = data_len;
  list_off_ = 0;
  list_len_ = 0;
  if (data_len_ == 0) {
    return node_digest_.Reset(data_off_, data_len_);
  }
  return ZX_OK;
}

size_t HashListBase::GetListOffset(size_t data_off) const {
  return node_digest_.ToNode(data_off) * GetDigestSize();
}

size_t HashListBase::GetListLength() const {
  return std::max(GetListOffset(node_digest_.NextAligned(data_len_)), GetDigestSize());
}

bool HashListBase::IsValidRange(size_t data_off, size_t buf_len) {
  size_t buf_end;
  return !add_overflow(data_off, buf_len, &buf_end) && buf_end <= data_len_;
}

zx_status_t HashListBase::ProcessData(const uint8_t *buf, size_t buf_len, size_t data_off) {
  if (list_len_ == 0) {
    return ZX_ERR_BAD_STATE;
  }
  if (!IsValidRange(data_off, buf_len)) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t rc;
  data_off_ = data_off;
  list_off_ = GetListOffset(data_off);
  while (buf_len != 0) {
    if (node_digest_.IsAligned(data_off_) &&
        (rc = node_digest_.Reset(data_off_, data_len_)) != ZX_OK) {
      return rc;
    }
    size_t chunk = node_digest_.Append(buf, buf_len);
    buf += chunk;
    buf_len -= chunk;
    data_off_ += chunk;
    if (node_digest_.IsAligned(data_off_) || data_off_ == data_len_) {
      HandleOne();
    }
  }
  return ZX_OK;
}

void HashListBase::HandleOne() {
  HandleOne(node_digest_.get());
  list_off_ += GetDigestSize();
}

// HashList<T>

template <typename T>
zx_status_t HashList<T>::SetList(T *list, size_t list_len) {
  if (list == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (list_len < GetListLength()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  // Reset the state of the list.
  SetDataLength(data_len());
  list_ = list;
  set_list_len(list_len);
  if (data_len() == 0) {
    HandleOne();
  }
  return ZX_OK;
}

}  // namespace internal

// HashListCreator

// Forward template declaration
template zx_status_t internal::HashList<uint8_t>::SetList(uint8_t *list, size_t list_len);

zx_status_t HashListCreator::Append(const void *buf, size_t buf_len) {
  return this->ProcessData(static_cast<const uint8_t *>(buf), buf_len, this->data_off());
}

void HashListCreator::HandleOne(const Digest &digest) {
  digest.CopyTo(list() + list_off(), GetDigestSize());
}

// HashListVerifier

// Forward template declaration
template zx_status_t internal::HashList<const uint8_t>::SetList(const uint8_t *list,
                                                                size_t list_len);

zx_status_t HashListVerifier::Verify(const void *buf, size_t buf_len, size_t data_off) {
  zx_status_t rc;
  verified_ = true;
  if (data_len() == 0) {
    rc = SetList(list(), list_len());
  } else {
    rc = this->ProcessData(static_cast<const uint8_t *>(buf), buf_len, data_off);
  }
  if (rc != ZX_OK) {
    return rc;
  }
  return verified_ ? ZX_OK : ZX_ERR_IO_DATA_INTEGRITY;
}

bool HashListVerifier::IsValidRange(size_t data_off, size_t buf_len) {
  size_t buf_end;
  if (data_off == data_len() && buf_len == 0) {
    return true;
  } else if (!IsAligned(data_off) || add_overflow(data_off, buf_len, &buf_end)) {
    return false;
  } else if (buf_end < data_len()) {
    return IsAligned(buf_end);
  } else {
    return buf_end == data_len();
  }
}

void HashListVerifier::HandleOne(const Digest &digest) {
  verified_ &= (digest.Equals(list() + list_off(), GetDigestSize()));
}

size_t CalculateHashListSize(size_t data_size, size_t node_size) {
  NodeDigest node_digest;
  ZX_ASSERT_MSG(node_digest.SetNodeSize(node_size) == ZX_OK, "node_size=%lu", node_size);
  size_t digest_size = node_digest.len();
  return std::max(node_digest.ToNode(node_digest.NextAligned(data_size)) * digest_size,
                  digest_size);
}

}  // namespace digest
