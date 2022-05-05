// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/txn_header.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>

void fidl_init_txn_header(fidl_message_header_t* out_hdr, zx_txid_t txid, uint64_t ordinal,
                          uint8_t dynamic_flags) {
  out_hdr->txid = txid;
  out_hdr->at_rest_flags[0] = FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2;
  out_hdr->at_rest_flags[1] = 0;
  out_hdr->dynamic_flags = dynamic_flags;
  out_hdr->magic_number = kFidlWireFormatMagicNumberInitial;
  out_hdr->ordinal = ordinal;
}

zx_status_t fidl_validate_txn_header(const fidl_message_header_t* hdr) {
  return hdr->magic_number == kFidlWireFormatMagicNumberInitial ? ZX_OK
                                                                : ZX_ERR_PROTOCOL_NOT_SUPPORTED;
}
