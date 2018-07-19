// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <zircon/assert.h>
#include <zircon/device/device.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <fbl/auto_call.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>

#include "private-fidl.h"

#define MXDEBUG 0

namespace {

zx_status_t TxnReply(fidl_txn_t* txn, const fidl_msg_t* msg) {
    auto connection = reinterpret_cast<zxfidl_connection_t*>(txn);
    auto hdr = static_cast<fidl_message_header_t*>(msg->bytes);
    hdr->txid = connection->txid;
    return zx_channel_write(connection->channel, 0, msg->bytes, msg->num_bytes,
                            msg->handles, msg->num_handles);
};

// Don't actually send anything on a channel when completing this operation.
// This is useful for mocking out "close" requests.
zx_status_t TxnNullReply(fidl_txn_t* reply, const fidl_msg_t* msg) {
    return ZX_OK;
}

zx_status_t HandleRpcClose(zxfidl_cb_t cb, void* cookie) {
    fuchsia_io_ObjectCloseRequest request;
    memset(&request, 0, sizeof(request));
    request.hdr.ordinal = ZXFIDL_CLOSE;
    fidl_msg_t msg = {
        .bytes = &request,
        .handles = NULL,
        .num_bytes = sizeof(request),
        .num_handles = 0u,
    };

    zxfidl_connection_t connection = {
        .txn = {
            .reply = TxnNullReply,
        },
        .channel = ZX_HANDLE_INVALID,
        .txid = 0,
    };

    // Remote side was closed.
    cb(&msg, reinterpret_cast<fidl_txn_t*>(&connection), cookie);
    return ERR_DISPATCHER_DONE;
}

zx_status_t HandleRpc(zx_handle_t h, zxfidl_cb_t cb, void* cookie) {
    uint8_t bytes[ZXFIDL_MAX_MSG_BYTES];
    zx_handle_t handles[ZXFIDL_MAX_MSG_HANDLES];
    fidl_msg_t msg = {
        .bytes = bytes,
        .handles = handles,
        .num_bytes = 0,
        .num_handles = 0,
    };

    zx_status_t r = zx_channel_read(h, 0, bytes, handles, countof(bytes),
                                    countof(handles), &msg.num_bytes,
                                    &msg.num_handles);
    if (r != ZX_OK) {
        return r;
    }

    auto cleanup = fbl::MakeAutoCall([&] {
        zx_handle_close_many(msg.handles, msg.num_handles);
    });

    if (msg.num_bytes < sizeof(fidl_message_header_t)) {
        return ZX_ERR_IO;
    }

    fidl_message_header_t& header = *reinterpret_cast<fidl_message_header_t*>(msg.bytes);
    zxfidl_connection_t connection = {
        .txn = {
            .reply = TxnReply,
        },
        .channel = h,
        .txid = header.txid,
    };

    // Callback is responsible for decoding the message, and closing
    // any associated handles.
    cleanup.cancel();
    r = cb(&msg, &connection.txn, cookie);
    return r;
}

} // namespace

zx_status_t zxfidl_handler(zx_handle_t h, zxfidl_cb_t cb, void* cookie) {
    if (h == ZX_HANDLE_INVALID) {
        return HandleRpcClose(cb, cookie);
    } else {
        ZX_ASSERT(zx_object_get_info(h, ZX_INFO_HANDLE_VALID, NULL, 0,
                                     NULL, NULL) == ZX_OK);
        return HandleRpc(h, cb, cookie);
    }
}

// Always consumes cnxn.
zx_status_t fidl_clone_request(zx_handle_t srv, zx_handle_t cnxn, uint32_t flags) {
    return fuchsia_io_ObjectClone(srv, flags, cnxn);
}

// Always consumes cnxn.
zx_status_t fidl_open_request(zx_handle_t srv, zx_handle_t cnxn, uint32_t flags,
                              uint32_t mode, const char* path, size_t pathlen) {
    return fuchsia_io_DirectoryOpen(srv, flags, mode, path, pathlen, cnxn);
}

zx_status_t fidl_close(zxrio_t* rio) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_ObjectClose(zxrio_handle(rio), &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_write(zxrio_t* rio, const void* data, uint64_t length,
                       uint64_t* actual) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileWrite(zxrio_handle(rio), static_cast<const uint8_t*>(data),
                                          length, &status, actual)) != ZX_OK) {
        return io_status;
    }
    if (*actual > length) {
        return ZX_ERR_IO;
    }
    return status;
}

zx_status_t fidl_writeat(zxrio_t* rio, const void* data, uint64_t length,
                         off_t offset, uint64_t* actual) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileWriteAt(zxrio_handle(rio), static_cast<const uint8_t*>(data),
                                                         length, offset, &status,
                                                         actual)) != ZX_OK) {
        return io_status;
    }
    if (*actual > length) {
        return ZX_ERR_IO;
    }
    return status;
}

zx_status_t fidl_read(zxrio_t* rio, void* data, uint64_t length, uint64_t* actual) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileRead(zxrio_handle(rio), length, &status,
                                         static_cast<uint8_t*>(data), length, actual)) != ZX_OK) {
        return io_status;
    }
    if (*actual > length) {
        return ZX_ERR_IO;
    }
    return status;
}

zx_status_t fidl_readat(zxrio_t* rio, void* data, uint64_t length, off_t offset,
                        uint64_t* actual) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileReadAt(zxrio_handle(rio), length, offset, &status,
                                           static_cast<uint8_t*>(data), length, actual)) != ZX_OK) {
        return io_status;
    }
    if (*actual > length) {
        return ZX_ERR_IO;
    }
    return status;
}

static_assert(SEEK_SET == fuchsia_io_SeekOrigin_Start, "");
static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_Current, "");
static_assert(SEEK_END == fuchsia_io_SeekOrigin_End, "");

zx_status_t fidl_seek(zxrio_t* rio, off_t offset, int whence, off_t* out) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileSeek(zxrio_handle(rio), offset, whence, &status,
                                         reinterpret_cast<uint64_t*>(out))) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_stat(zxrio_t* rio, size_t len, vnattr_t* out, size_t* out_sz) {
    ZX_DEBUG_ASSERT(len >= sizeof(vnattr_t));

    fuchsia_io_NodeAttributes attr;
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_NodeGetAttr(zxrio_handle(rio),
                                            &status, &attr)) != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }

    // Translate NodeAttributes --> vnattr
    out->mode = attr.mode;
    out->inode = attr.id;
    out->size = attr.content_size;
    out->blksize = VNATTR_BLKSIZE;
    out->blkcount = attr.storage_size / VNATTR_BLKSIZE;
    out->nlink = attr.link_count;
    out->create_time = attr.creation_time;
    out->modify_time = attr.modification_time;

    *out_sz = sizeof(vnattr_t);
    return ZX_OK;
}

zx_status_t fidl_setattr(zxrio_t* rio, const vnattr_t* attr) {
    // Setup the request message primary
    // TODO(smklein): Replace with autogenerated constants
    const uint32_t kFlagCreationTime = 1;
    const uint32_t kFlagModificationTime = 2;
    static_assert(kFlagCreationTime == ATTR_CTIME, "SetAttr flags unaligned");
    static_assert(kFlagModificationTime == ATTR_MTIME, "SetAttr flags unaligned");
    uint32_t flags = attr->valid;
    fuchsia_io_NodeAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.creation_time = attr->create_time;
    attrs.modification_time = attr->modify_time;

    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_NodeSetAttr(zxrio_handle(rio), flags, &attrs,
                                            &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_sync(zxrio_t* rio) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_NodeSync(zxrio_handle(rio), &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_readdirents(zxrio_t* rio, void* data, size_t length, size_t* out_sz) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_DirectoryReadDirents(zxrio_handle(rio), length,
                                                     &status, static_cast<uint8_t*>(data),
                                                     length, out_sz)) != ZX_OK) {
        return io_status;
    }
    if (*out_sz > length) {
        return ZX_ERR_IO;
    }
    return status;
}

zx_status_t fidl_rewind(zxrio_t* rio) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_DirectoryRewind(zxrio_handle(rio), &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_unlink(zxrio_t* rio, const char* name, size_t namelen) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_DirectoryUnlink(zxrio_handle(rio), name, namelen,
                                                &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_truncate(zxrio_t* rio, uint64_t length) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileTruncate(zxrio_handle(rio), length,
                                             &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_rename(zxrio_t* rio, const char* src, size_t srclen,
                        zx_handle_t dst_token, const char* dst, size_t dstlen) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_DirectoryRename(zxrio_handle(rio), src, srclen,
                                                dst_token, dst, dstlen,
                                                &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_link(zxrio_t* rio, const char* src, size_t srclen,
                      zx_handle_t dst_token, const char* dst, size_t dstlen) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_DirectoryLink(zxrio_handle(rio), src, srclen,
                                              dst_token, dst, dstlen,
                                              &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_ioctl(zxrio_t* rio, uint32_t op, const void* in_buf,
                       size_t in_len, void* out_buf, size_t out_len,
                       size_t* out_actual) {
    size_t in_handle_count = 0;
    size_t out_handle_count = 0;
    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        out_handle_count = 1;
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        out_handle_count = 2;
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        out_handle_count = 3;
        break;
    case IOCTL_KIND_SET_HANDLE:
        in_handle_count = 1;
        break;
    case IOCTL_KIND_SET_TWO_HANDLES:
        in_handle_count = 2;
        break;
    }

    if (in_len < in_handle_count * sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (out_len < out_handle_count * sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t hbuf[out_handle_count];
    size_t out_handle_actual;
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_NodeIoctl(zxrio_handle(rio), op,
                                          out_len, static_cast<const zx_handle_t*>(in_buf),
                                          in_handle_count, static_cast<const uint8_t*>(in_buf),
                                          in_len, &status, hbuf,
                                          out_handle_count, &out_handle_actual,
                                          static_cast<uint8_t*>(out_buf), out_len,
                                          out_actual)) != ZX_OK) {
        return io_status;
    }

    if (status != ZX_OK) {
        zx_handle_close_many(hbuf, out_handle_actual);
        return status;
    }
    if (out_handle_actual != out_handle_count) {
        zx_handle_close_many(hbuf, out_handle_actual);
        return ZX_ERR_IO;
    }

    memcpy(out_buf, hbuf, out_handle_count * sizeof(zx_handle_t));
    return ZX_OK;
}

zx_status_t fidl_getvmo(zxrio_t* rio, uint32_t flags, zx_handle_t* out) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileGetVmo(zxrio_handle(rio), flags, &status,
                                           out)) != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (*out == ZX_HANDLE_INVALID) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

zx_status_t fidl_getflags(zxrio_t* rio, uint32_t* outflags) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileGetFlags(zxrio_handle(rio), &status,
                                             outflags)) != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t fidl_setflags(zxrio_t* rio, uint32_t flags) {
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_FileSetFlags(zxrio_handle(rio), flags,
                                             &status)) != ZX_OK) {
        return io_status;
    }
    return status;
}
