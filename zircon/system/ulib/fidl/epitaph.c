// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__

#include <lib/fidl/epitaph.h>
#include <lib/fidl/txn_header.h>
#include <string.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

zx_status_t fidl_epitaph_write(zx_handle_t channel, zx_status_t error) {
  fidl_epitaph_t epitaph;
  memset(&epitaph, 0, sizeof(epitaph));
  fidl_init_txn_header(&epitaph.hdr, 0, kFidlOrdinalEpitaph);
  epitaph.error = error;

  return zx_channel_write(channel, 0, &epitaph, sizeof(epitaph), NULL, 0);
}

#endif  // __Fuchsia__
