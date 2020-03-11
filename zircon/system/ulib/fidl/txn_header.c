// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/txn_header.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>

void fidl_init_txn_header(fidl_message_header_t* out_hdr, zx_txid_t txid, uint64_t ordinal) {
  out_hdr->txid = txid;
  out_hdr->flags[0] = 0;
  out_hdr->flags[1] = 0;
  out_hdr->flags[2] = 0;
  out_hdr->magic_number = kFidlWireFormatMagicNumberInitial;
  out_hdr->ordinal = ordinal;
}

zx_status_t fidl_validate_txn_header(const fidl_message_header_t* hdr) {
  return hdr->magic_number == kFidlWireFormatMagicNumberInitial ? ZX_OK
                                                                : ZX_ERR_PROTOCOL_NOT_SUPPORTED;
}
