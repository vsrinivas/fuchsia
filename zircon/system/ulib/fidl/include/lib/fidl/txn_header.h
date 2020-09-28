// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_TXN_HEADER_H_
#define LIB_FIDL_TXN_HEADER_H_

#include <zircon/fidl.h>

__BEGIN_CDECLS

// TODO(fxbug.dev/38643): make this inline
// Initialize a txn header as per the Transaction Header v3 proposal (FTP-037)
void fidl_init_txn_header(fidl_message_header_t* out_hdr, zx_txid_t txid, uint64_t ordinal);

zx_status_t fidl_validate_txn_header(const fidl_message_header_t* hdr);

__END_CDECLS

#endif  // LIB_FIDL_TXN_HEADER_H_
