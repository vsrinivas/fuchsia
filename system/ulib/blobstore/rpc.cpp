// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <fs/vfs.h>

#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/util.h>
#include <fdio/vfs.h>

#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <blobstore/blobstore.h>

namespace blobstore {

zx_status_t VnodeBlob::GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                                  zxrio_object_info_t* extra) {
    *type = FDIO_PROTOCOL_REMOTE;
    if (IsDirectory()) {
        return ZX_OK;
    }
    zx_status_t r = GetReadableEvent(hnd);
    if (r < 0) {
        return r;
    }
    return ZX_OK;
}

}
