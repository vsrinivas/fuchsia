// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <fs/vfs.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/vfs.h>

#include <magenta/device/vfs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "blobstore-private.h"

namespace blobstore {

mx_status_t VnodeBlob::GetHandles(uint32_t flags, mx_handle_t* hnds,
                                  uint32_t* type, void* extra, uint32_t* esize) {
    *type = MXIO_PROTOCOL_REMOTE;
    if (IsDirectory()) {
        return 0;
    }
    mx_status_t r = GetReadableEvent(&hnds[0]);
    if (r < 0) {
        return r;
    }
    return 1;
}

}
