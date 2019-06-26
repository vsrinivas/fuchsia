// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/fidl/serialization_size.h"

namespace ledger {
namespace fidl_serialization {

size_t GetByteVectorSize(size_t vector_length) { return Align(vector_length) + kVectorHeaderSize; }

size_t GetEntrySize(size_t key_length) {
  size_t key_size = GetByteVectorSize(key_length);
  size_t object_size = Align(kMemoryObjectSize);
  return key_size + object_size + Align(kPriorityEnumSize);
}

size_t GetInlinedEntrySize(const InlinedEntry& entry) {
  size_t key_size = GetByteVectorSize(entry.key.size());
  size_t object_size = kPointerSize;
  if (entry.inlined_value) {
    object_size += GetByteVectorSize(entry.inlined_value->value.size());
  }
  return key_size + object_size + Align(kPriorityEnumSize);
}

size_t GetDiffEntrySize(size_t key_length, int number_of_values) {
  size_t key_size = GetByteVectorSize(key_length);
  return key_size + number_of_values * (Align(kMemoryObjectSize) + Align(kPriorityEnumSize));
}

}  // namespace fidl_serialization
}  //  namespace ledger
