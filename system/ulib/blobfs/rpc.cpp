// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <blobfs/blobfs.h>
#include <fs/vfs.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace blobfs {

zx_status_t VnodeBlob::GetHandles(uint32_t flags, zx_handle_t* hnd, uint32_t* type,
                                  zxrio_node_info_t* extra) {
    if (IsDirectory()) {
        *type = fuchsia_io_NodeInfoTag_directory;
        return ZX_OK;
    }
    *type = fuchsia_io_NodeInfoTag_file;
    zx_status_t r = GetReadableEvent(hnd);
    if (r < 0) {
        return r;
    }

    return ZX_OK;
}

}
