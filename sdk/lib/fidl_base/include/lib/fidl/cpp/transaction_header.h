// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_BASE_INCLUDE_LIB_FIDL_CPP_TRANSACTION_HEADER_H_
#define LIB_FIDL_BASE_INCLUDE_LIB_FIDL_CPP_TRANSACTION_HEADER_H_

#include <lib/fidl/txn_header.h>

#include <type_traits>

namespace fidl {

// Defines the possible flag values for the FIDL transaction header
// dynamic_flags field.
enum class MessageDynamicFlags : uint8_t {
  kStrictMethod = FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD,
  kFlexibleMethod = FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_FLEXIBLE_METHOD,
};

inline void InitTxnHeader(fidl_message_header_t* out_hdr, zx_txid_t txid, uint64_t ordinal,
                          MessageDynamicFlags dynamic_flags) {
  fidl_init_txn_header(out_hdr, txid, ordinal,
                       static_cast<std::underlying_type_t<MessageDynamicFlags>>(dynamic_flags));
}

// Returns true if the transaction header is for a flexible interaction.
inline bool IsFlexibleInteraction(const fidl_message_header_t* hdr) {
  return (hdr->dynamic_flags & FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_FLEXIBLE_METHOD) != 0;
}
}  // namespace fidl

#endif  // LIB_FIDL_BASE_INCLUDE_LIB_FIDL_CPP_TRANSACTION_HEADER_H_
