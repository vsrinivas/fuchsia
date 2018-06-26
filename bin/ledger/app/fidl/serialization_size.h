// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_FIDL_SERIALIZATION_SIZE_H_
#define PERIDOT_BIN_LEDGER_APP_FIDL_SERIALIZATION_SIZE_H_

#include <stddef.h>

#include <zircon/fidl.h>
#include <zircon/types.h>

#include "peridot/bin/ledger/fidl/include/types.h"

namespace ledger {
namespace fidl_serialization {

// Maximal size of data that will be returned inline.
constexpr size_t kMaxInlineDataSize = ZX_CHANNEL_MAX_MSG_BYTES * 9 / 10;
constexpr size_t kMaxMessageHandles = ZX_CHANNEL_MAX_MSG_HANDLES;

// TODO(mariagl): Remove dependency on FIDL internal structure layout, see
// LE-449.
const size_t kPointerSize = sizeof(uintptr_t);
const size_t kStatusEnumSize = sizeof(int32_t);
const size_t kHandleSize = sizeof(zx_handle_t);
const size_t kVectorHeaderSize = sizeof(fidl_vector_t);
const size_t kPriorityEnumSize = sizeof(int32_t);
const size_t kMessageHeaderSize = sizeof(fidl_message_header_t);

inline size_t Align(size_t n) { return FIDL_ALIGN(n); }

// The overhead for storing the pointer, the timestamp (int64) and the two
// arrays.
constexpr size_t kPageChangeHeaderSize =
    kPointerSize + sizeof(int64_t) + 2 * kVectorHeaderSize;

// Returns the fidl size of a byte vector with the given length.
size_t GetByteVectorSize(size_t vector_length);

// Returns the fidl size of an Entry holding a key with the given length.
size_t GetEntrySize(size_t key_length);

// Returns the fidl size of an InlinedEntry.
size_t GetInlinedEntrySize(const InlinedEntry& entry);

// Size of an object stored in memory (and accessed by a handle).
constexpr size_t kMemoryObjectSize = 2 * kPointerSize + kHandleSize;

// Returns the size of a DiffEntry object
size_t GetDiffEntrySize(size_t key_length, int number_of_values);

}  // namespace fidl_serialization
}  //  namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_FIDL_SERIALIZATION_SIZE_H_
