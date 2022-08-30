// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_BASE_INCLUDE_LIB_FIDL_TXN_HEADER_H_
#define LIB_FIDL_BASE_INCLUDE_LIB_FIDL_TXN_HEADER_H_

#include <zircon/fidl.h>

// These functions are declared `static inline` to make it available in source
// form to specific functions in early bootstrapping libraries (such as libc)
// which cannot be compiled with sanitizers.

__BEGIN_CDECLS

// Initialize a transaction header according to
// https://fuchsia.dev/fuchsia-src/reference/fidl/language/wire-format?hl=en#transactional-messages.
static inline void fidl_init_txn_header(fidl_message_header_t* out_hdr, zx_txid_t txid,
                                        uint64_t ordinal, uint8_t dynamic_flags) {
  *out_hdr = (fidl_message_header_t){
      .txid = txid,
      .at_rest_flags =
          {
              FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2,
          },
      .dynamic_flags = dynamic_flags,
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = ordinal,
  };
}

// Validate that a transaction header contains a supported magic number.
static inline zx_status_t fidl_validate_txn_header(const fidl_message_header_t* hdr) {
  return hdr->magic_number == kFidlWireFormatMagicNumberInitial ? ZX_OK
                                                                : ZX_ERR_PROTOCOL_NOT_SUPPORTED;
}

__END_CDECLS

#endif  // LIB_FIDL_BASE_INCLUDE_LIB_FIDL_TXN_HEADER_H_
