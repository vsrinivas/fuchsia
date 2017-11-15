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

zx_status_t VnodeBlob::GetHandles(uint32_t flags, zx_handle_t* hnds, size_t* hcount,
                                  uint32_t* type, void* extra, uint32_t* esize) {
    *type = FDIO_PROTOCOL_REMOTE;
    if (IsDirectory()) {
        *hcount = 0;
        return ZX_OK;
    }
    zx_status_t r = GetReadableEvent(&hnds[0]);
    if (r < 0) {
        return r;
    }
    *hcount = 1;
    return ZX_OK;
}

}
