// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/fidl/serialization_size.h"

namespace ledger {
namespace fidl_serialization {

size_t GetByteArraySize(size_t array_length) {
  return f1dl::internal::Align(array_length) + kArrayHeaderSize;
}

size_t GetEntrySize(size_t key_length) {
  size_t key_size = GetByteArraySize(key_length);
  size_t object_size = GetByteArraySize(kHandleSize);
  return kPointerSize + key_size + object_size +
         f1dl::internal::Align(kEnumSize);
}

size_t GetInlinedEntrySize(const InlinedEntryPtr& entry) {
  size_t key_size = kPointerSize + GetByteArraySize(entry->key->size());
  size_t object_size = kPointerSize + GetByteArraySize(entry->value->size());
  return kPointerSize + kStructHeaderSize + key_size + object_size +
         f1dl::internal::Align(kEnumSize);
}

}  // namespace fidl_serialization
}  //  namespace ledger
