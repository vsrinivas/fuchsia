// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_EPITAPH_H_
#define LIB_FIDL_EPITAPH_H_

#include <zircon/types.h>

__BEGIN_CDECLS

#ifdef __Fuchsia__

// Sends an epitaph with the given values down the channel.
// See https://fuchsia.googlesource.com/docs/+/master/development/languages/fidl/languages/c.md#fidl_epitaph_write
zx_status_t fidl_epitaph_write(zx_handle_t channel, zx_status_t error);

#endif // __Fuchsia__

__END_CDECLS

#endif // LIB_FIDL_EPITAPH_H_
