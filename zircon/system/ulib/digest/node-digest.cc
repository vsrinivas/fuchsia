// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <algorithm>

#include <digest/node-digest.h>
#include <fbl/algorithm.h>

namespace digest {

zx_status_t NodeDigest::SetNodeSize(size_t node_size) {
  if (!IsValidNodeSize(node_size)) {
    return ZX_ERR_INVALID_ARGS;
  }
  node_size_ = node_size;
  return ZX_OK;
}

zx_status_t NodeDigest::Reset(size_t data_off, size_t data_len) {
  if (data_len < data_off || !IsAligned(data_off)) {
    return ZX_ERR_INVALID_ARGS;
  }
  to_append_ = std::min(data_len - data_off, node_size_);
  pad_len_ = node_size_ - to_append_;
  digest_.Init();
  uint64_t locality = id_ ^ data_off;
  digest_.Update(&locality, sizeof(locality));
  uint32_t length = static_cast<uint32_t>(to_append_);
  digest_.Update(&length, sizeof(length));
  // Handle special zero-length case immediately.
  if (length == 0) {
    digest_.Final();
  }
  return ZX_OK;
}

size_t NodeDigest::Append(const void* buf, size_t buf_len) {
  size_t len = std::min(buf_len, to_append_);
  if (len == 0) {
    return 0;
  }
  digest_.Update(buf, len);
  to_append_ -= len;
  if (to_append_ == 0) {
    static const uint8_t kZeroes[64] = {0};
    while (pad_len_ > sizeof(kZeroes)) {
      digest_.Update(kZeroes, sizeof(kZeroes));
      pad_len_ -= sizeof(kZeroes);
    }
    digest_.Update(kZeroes, pad_len_);
    digest_.Final();
  }
  return len;
}

}  // namespace digest
