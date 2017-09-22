// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/fidl/serialization_size.h"

namespace ledger {
namespace fidl_serialization {

size_t GetByteArraySize(size_t array_length) {
  return array_length + kArrayHeaderSize;
}

size_t GetEntrySize(size_t key_length) {
  size_t key_size = key_length + kArrayHeaderSize;
  size_t object_size = kHandleSize;
  return kPointerSize + key_size + object_size + kEnumSize;
}

size_t GetInlinedEntrySize(const InlinedEntryPtr& entry) {
  size_t key_size = entry->key.size() + kArrayHeaderSize;
  size_t object_size = entry->value.size() + kArrayHeaderSize;
  return kPointerSize + key_size + object_size + kEnumSize;
}

}  // namespace fidl_serialization
}  //  namespace ledger
