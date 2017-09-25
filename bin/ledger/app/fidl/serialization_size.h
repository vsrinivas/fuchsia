// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_FIDL_SERIALIZATION_SIZE_H_
#define PERIDOT_BIN_LEDGER_APP_FIDL_SERIALIZATION_SIZE_H_

#include <stddef.h>
#include <zircon/types.h>

#include "lib/ledger/fidl/ledger.fidl.h"

namespace ledger {
namespace fidl_serialization {

// Maximal size of data that will be returned inline.
constexpr size_t kMaxInlineDataSize = ZX_CHANNEL_MAX_MSG_BYTES * 9 / 10;
constexpr size_t kMaxMessageHandles = ZX_CHANNEL_MAX_MSG_HANDLES;

const size_t kArrayHeaderSize = sizeof(fidl::internal::Array_Data<char>);
const size_t kPointerSize = sizeof(uint64_t);
const size_t kEnumSize = sizeof(int32_t);
const size_t kHandleSize = sizeof(int32_t);

// The overhead for storing the pointer, the timestamp (int64) and the two
// arrays.
constexpr size_t kPageChangeHeaderSize =
    kPointerSize + sizeof(int64_t) + 2 * kArrayHeaderSize;

// Returns the fidl size of a byte array with the given length.
size_t GetByteArraySize(size_t array_length);

// Returns the fidl size of an Entry holding a key with the given length.
size_t GetEntrySize(size_t key_length);

// Returns the fidl size of an InlinedEntry.
size_t GetInlinedEntrySize(const InlinedEntryPtr& entry);

}  // namespace fidl_serialization
}  //  namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_FIDL_SERIALIZATION_SIZE_H_
