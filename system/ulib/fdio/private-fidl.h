// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>

__BEGIN_CDECLS

// TODO(abarth): Remove all these functions once FDIO is rehosted on ZXIO.
zx_status_t fidl_ioctl(zx_handle_t h, uint32_t op, const void* in_buf,
                       size_t in_len, void* out_buf, size_t out_len,
                       size_t* out_actual);

__END_CDECLS
