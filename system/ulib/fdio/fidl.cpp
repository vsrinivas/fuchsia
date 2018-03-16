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

#include <fbl/function.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/string_view.h>
#include <lib/fidl/cpp/vector_view.h>
#include <fdio/debug.h>
#include <fdio/io.fidl.h>
#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/util.h>
#include <fdio/vfs.h>
#include <lib/zx/channel.h>

#include "private-fidl.h"

#define MXDEBUG 0

namespace {

void discard_handles(const zx_handle_t* handles, size_t count) {
    while (count-- > 0) {
        zx_handle_close(*handles++);
    }
}

// Convert a message to a primary object, validating that
// there are enough bytes to do this conversion.
//
// Returns nullptr if this conversion is invalid.
template <typename T>
T* to_primary(const fidl::Message* msg) {
    if (msg->bytes().actual() < sizeof(T)) {
        fprintf(stderr, "%s: Message (%u bytes) is smaller than primary (%zu bytes)\n",
                __PRETTY_FUNCTION__, msg->bytes().actual(), sizeof(T));
        return nullptr;
    }
    return reinterpret_cast<T*>(msg->bytes().data());
}

// Semantic sugar for creating a new FIDL request object,
// setting the TXID, and setting the ordinal.
template <typename T, uint32_t Ordinal>
T* new_request(zxrio_t* rio, fidl::Builder* builder) {
    T* request = builder->New<T>();
    zxrio_new_txid(rio, &request->hdr.txid);
    request->hdr.ordinal = Ordinal;
    return request;
}

template <typename T>
void* get_secondary(T* request) {
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(request) +
                                   FIDL_ALIGN(sizeof(T)));
}

void* next_secondary(void* secondary, size_t size) {
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(secondary) +
                                   FIDL_ALIGN(size));
}

// A small wrapper for "Call" which extracts the appropriate
// response status.
//
// Since outgoing / incoming handles are contained in fidl::Message,
// they will be automatically closed whenever it goes out of scope.
zx_status_t fidl_call(zx_handle_t h, fidl::Message* message) {
    zx_status_t rs = 0;
    zx_status_t r = message->Call(h, 0, ZX_TIME_INFINITE, &rs, message);
    if (r == ZX_ERR_CALL_FAILED) {
        return rs == ZX_OK ? ZX_ERR_IO : rs;
    } else if (r != ZX_OK) {
        return r;
    }
    return ZX_OK;
}

// zxrio_decode_request always takes ownership of the incoming handles.
//
// If ClearHandlesUnsafe is not called on the msg, all provided handles
// will automatically be closed by the destructor of |msg|.
zx_status_t zxrio_decode_request(fidl::Message* msg) {
    if (!msg->has_header()) {
        fprintf(stderr, "zxrio_decode_request: Missing header\n");
        return ZX_ERR_IO;
    }
    uint32_t op = msg->ordinal();
    const uint32_t hcount = msg->handles().actual();
    const zx_handle_t* handles = msg->handles().data();
    const uint32_t dsz = msg->bytes().actual();
    void* data = msg->bytes().data();

    if (!ZXRIO_FIDL_MSG(op)) {
        // Legacy RIO messages
        // Now, "msg->hcount" can be trusted once again.
        zxrio_msg_t* rio_msg = reinterpret_cast<zxrio_msg_t*>(data);
        memcpy(rio_msg->handle, handles, sizeof(zx_handle_t) * hcount);
        rio_msg->hcount = hcount;
        if (!is_rio_message_reply_valid(rio_msg, dsz)) {
            fprintf(stderr, "decoding: Invalid legacy msg: 0x%x\n", op);
            return ZX_ERR_INVALID_ARGS;
        }
        if (rio_msg->hcount != ZXRIO_HC(op)) {
            fprintf(stderr, "decoding: Unexpected # of handles\n");
            return ZX_ERR_IO;
        }

        msg->ClearHandlesUnsafe();
        return ZX_OK;
    }

    // FIDL objects which require additional secondary object validation
    switch (op) {
    case ZXFIDL_CLONE: {
        ObjectCloneRequest* request = to_primary<ObjectCloneRequest>(msg);
        if (request == nullptr) {
            return ZX_ERR_IO;
        } else if (hcount != 1 || request->object != FIDL_HANDLE_PRESENT) {
            fprintf(stderr, "ZXFIDL_CLONE failed: Missing handle\n");
            return ZX_ERR_IO;
        }
        request->object = handles[0];
        msg->ClearHandlesUnsafe();
        return ZX_OK;
    }
    case ZXFIDL_OPEN: {
        DirectoryOpenRequest* request = to_primary<DirectoryOpenRequest>(msg);
        if (request == nullptr) {
            return ZX_ERR_IO;
        } else if (hcount != 1 || request->object != FIDL_HANDLE_PRESENT) {
            fprintf(stderr, "ZXFIDL_OPEN failed: Missing handle\n");
            return ZX_ERR_IO;
        } else if (FIDL_ALIGN(request->path.size) +
                   FIDL_ALIGN(sizeof(DirectoryOpenRequest)) != dsz) {
            fprintf(stderr, "ZXFIDL_OPEN failed: Bad secondary size\n");
            return ZX_ERR_IO;
        } else if ((request->path.data != (void*) FIDL_ALLOC_PRESENT)) {
            fprintf(stderr, "ZXFIDL_OPEN failed: Bad secondary pointer\n");
            return ZX_ERR_IO;
        }

        request->object = handles[0];
        request->path.data = static_cast<char*>(get_secondary(request));
        msg->ClearHandlesUnsafe();
        return ZX_OK;
    }
    case ZXFIDL_WRITE: {
        FileWriteRequest* request = to_primary<FileWriteRequest>(msg);
        if (request == nullptr) {
            return ZX_ERR_IO;
        } else if (FIDL_ALIGN(request->data.count) +
                   FIDL_ALIGN(sizeof(FileWriteRequest)) != dsz) {
            fprintf(stderr, "ZXFIDL_WRITE failed: bad secondary\n");
            return ZX_ERR_IO;
        } else if (request->data.data != (void*) FIDL_ALLOC_PRESENT) {
            fprintf(stderr, "ZXFIDL_WRITE failed: bad secondary pointer\n");
            return ZX_ERR_IO;
        }
        request->data.data = get_secondary(request);
        return ZX_OK;
    }
    case ZXFIDL_IOCTL: {
        NodeIoctlRequest* request = to_primary<NodeIoctlRequest>(msg);
        if (request == nullptr) {
            fprintf(stderr, "ZXFIDL_IOCTL failed: missing response space\n");
            return ZX_ERR_IO;
        } else if ((request->handles.data != (void*) FIDL_ALLOC_PRESENT) ||
            (request->in.data != (void*) FIDL_ALLOC_PRESENT)) {
            fprintf(stderr, "ZXFIDL_IOCTL failed: missing necessary vector\n");
            return ZX_ERR_IO;
        }
        if (hcount != request->handles.count) {
            fprintf(stderr, "ZXFIDL_IOCTL failed: bad hcount\n");
            return ZX_ERR_IO;
        }
        request->handles.data = get_secondary(request);
        zx_handle_t* hptr = reinterpret_cast<zx_handle_t*>(request->handles.data);
        for (size_t i = 0; i < request->handles.count; i++) {
            if (hptr[i] != FIDL_HANDLE_PRESENT) {
                fprintf(stderr, "ZXFIDL_IOCTL: Handles are required; must be present\n");
                return ZX_ERR_IO;
            }
        }

        switch (IOCTL_KIND(request->opcode)) {
        case IOCTL_KIND_SET_HANDLE:
            if (request->handles.count != 1) {
                fprintf(stderr, "ZXFIDL_IOCTL: bad hcount (expected to set one)\n");
                return ZX_ERR_IO;
            }
            break;
        case IOCTL_KIND_SET_TWO_HANDLES:
            if (request->handles.count != 2) {
                fprintf(stderr, "ZXFIDL_IOCTL: bad hcount (expected to set two)\n");
                return ZX_ERR_IO;
            }
            break;
        case IOCTL_KIND_GET_HANDLE:
        case IOCTL_KIND_GET_TWO_HANDLES:
        case IOCTL_KIND_GET_THREE_HANDLES:
        default:
            if (request->handles.count != 0) {
                fprintf(stderr, "ZXFIDL_IOCTL: bad hcount (expected to set none)\n");
                return ZX_ERR_IO;
            }
        }

        size_t secondary_size = FIDL_ALIGN(request->handles.count * sizeof(zx_handle_t)) +
                                FIDL_ALIGN(request->in.count);
        if (FIDL_ALIGN(sizeof(NodeIoctlRequest)) + secondary_size != dsz) {
            fprintf(stderr, "ZXFIDL_IOCTL failed: bad secondary size\n");
            return ZX_ERR_IO;
        }

        // Patch up handles, pointers
        memcpy(request->handles.data, handles, hcount * sizeof(zx_handle_t));
        request->in.data = next_secondary(request->handles.data, hcount * sizeof(zx_handle_t));
        msg->ClearHandlesUnsafe();
        return ZX_OK;
    }
    case ZXFIDL_UNLINK: {
        DirectoryUnlinkRequest* request = to_primary<DirectoryUnlinkRequest>(msg);
        if (request == nullptr) {
            return ZX_ERR_IO;
        } else if (FIDL_ALIGN(request->path.size) +
                   FIDL_ALIGN(sizeof(DirectoryUnlinkRequest)) != dsz) {
            fprintf(stderr, "ZXFIDL_UNLINK failed: bad secondary\n");
            return ZX_ERR_IO;
        } else if (request->path.data != (void*) FIDL_ALLOC_PRESENT) {
            fprintf(stderr, "ZXFIDL_UNLINK failed: bad secondary pointer\n");
            return ZX_ERR_IO;
        }
        request->path.data = static_cast<char*>(get_secondary(request));
        return ZX_OK;
    }
    case ZXFIDL_WRITE_AT: {
        FileWriteAtRequest* request = to_primary<FileWriteAtRequest>(msg);
        if (request == nullptr) {
            return ZX_ERR_IO;
        } else if (FIDL_ALIGN(request->data.count) +
                   FIDL_ALIGN(sizeof(FileWriteAtRequest)) != dsz) {
            fprintf(stderr, "ZXFIDL_WRITE_AT failed: bad secondary\n");
            return ZX_ERR_IO;
        } else if (request->data.data != (void*) FIDL_ALLOC_PRESENT) {
            fprintf(stderr, "ZXFIDL_WRITE_AT failed: bad secondary pointer\n");
            return ZX_ERR_IO;
        }
        request->data.data = get_secondary(request);
        return ZX_OK;
    }
    case ZXFIDL_RENAME: {
        DirectoryRenameRequest* request = to_primary<DirectoryRenameRequest>(msg);
        if (request == nullptr) {
            return ZX_ERR_IO;
        } else if (FIDL_ALIGN(sizeof(DirectoryRenameRequest)) +
                   FIDL_ALIGN(request->src.size) + FIDL_ALIGN(request->dst.size)
                   != dsz) {
            fprintf(stderr, "ZXFIDL_RENAME failed: Bad secondary\n");
            return ZX_ERR_IO;
        } else if (request->src.data != (void*) FIDL_ALLOC_PRESENT ||
                   request->dst_parent_token != FIDL_HANDLE_PRESENT ||
                   request->dst.data != (void*) FIDL_ALLOC_PRESENT) {
            fprintf(stderr, "ZXFIDL_RENAME failed: Bad secondary pointer\n");
            return ZX_ERR_IO;
        }

        request->src.data = static_cast<char*>(get_secondary(request));
        request->dst_parent_token = handles[0];
        request->dst.data = static_cast<char*>(next_secondary(request->src.data,
                                                              request->src.size));
        msg->ClearHandlesUnsafe();
        return ZX_OK;
    }
    case ZXFIDL_LINK: {
        DirectoryLinkRequest* request = to_primary<DirectoryLinkRequest>(msg);
        if (request == nullptr) {
            return ZX_ERR_IO;
        } else if (FIDL_ALIGN(sizeof(DirectoryLinkRequest)) +
                   FIDL_ALIGN(request->src.size) + FIDL_ALIGN(request->dst.size)
                   != dsz) {
            fprintf(stderr, "ZXFIDL_LINK failed: Bad secondary\n");
            return ZX_ERR_IO;
        } else if (request->src.data != (void*) FIDL_ALLOC_PRESENT ||
                   request->dst_parent_token != FIDL_HANDLE_PRESENT ||
                   request->dst.data != (void*) FIDL_ALLOC_PRESENT) {
            fprintf(stderr, "ZXFIDL_LINK failed: Bad secondary pointer\n");
            return ZX_ERR_IO;
        }

        request->src.data = static_cast<char*>(get_secondary(request));
        request->dst_parent_token = handles[0];
        request->dst.data = static_cast<char*>(next_secondary(request->src.data,
                                                              request->src.size));
        msg->ClearHandlesUnsafe();
        return ZX_OK;
    }
    }

    return ZX_OK;
}

// Simplify a common pattern of encoding:
// Cast to the response message, set the size to the response message size, and
// insert the status into the response.
template <typename T>
T* encode_response_status(void* msg, zx_status_t status, uint32_t* sz) {
    T* response = static_cast<T*>(msg);
    *sz = sizeof(T);
    response->s = status;
    return response;
}

zx_status_t zxrio_encode_response(zx_status_t status, zxrio_msg_t* msg, uint32_t* sz,
                                  zx_handle_t* handles, uint32_t* hcount) {
    *hcount = 0;
    switch (msg->op) {
    case ZXFIDL_CLOSE: {
        encode_response_status<ObjectCloseResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_READ: {
        auto response = encode_response_status<FileReadResponse>(msg, status, sz);
        response->data.data = (void*) FIDL_ALLOC_PRESENT;
        if (response->s != ZX_OK) {
            response->data.count = 0;
        }
        *sz += static_cast<uint32_t>(FIDL_ALIGN(response->data.count));
        break;
    }
    case ZXFIDL_WRITE: {
        auto response = encode_response_status<FileWriteResponse>(msg, status, sz);
        if (response->s != ZX_OK) {
            response->actual = 0;
        }
        break;
    }
    case ZXFIDL_SEEK: {
        encode_response_status<FileSeekResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_STAT: {
        encode_response_status<NodeGetAttrResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_SETATTR: {
        encode_response_status<NodeSetAttrResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_READDIR: {
        auto response = encode_response_status<DirectoryReadDirentsResponse>(msg, status, sz);
        response->dirents.data = (void*) FIDL_ALLOC_PRESENT;
        if (response->s != ZX_OK) {
            response->dirents.count = 0;
        }
        *sz += static_cast<uint32_t>(FIDL_ALIGN(response->dirents.count));
        break;
    }
    case ZXFIDL_IOCTL: {
        auto response = encode_response_status<NodeIoctlResponse>(msg, status, sz);
        if (response->s != ZX_OK) {
            response->handles.count = 0;
            response->out.count = 0;
        }
        memcpy(handles, response->handles.data, response->handles.count * sizeof(zx_handle_t));

        zx_handle_t* hptr = reinterpret_cast<zx_handle_t*>(response->handles.data);
        for (size_t i = 0; i < response->handles.count; i++) {
            hptr[i] = FIDL_HANDLE_PRESENT;
        }
        *hcount = static_cast<uint32_t>(response->handles.count);
        response->handles.data = (void*) FIDL_ALLOC_PRESENT;
        response->out.data = (void*) FIDL_ALLOC_PRESENT;
        *sz += static_cast<uint32_t>(FIDL_ALIGN(response->handles.count * sizeof(zx_handle_t)) +
                                     FIDL_ALIGN(response->out.count));
        break;
    }
    case ZXFIDL_UNLINK: {
        encode_response_status<DirectoryUnlinkResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_READ_AT: {
        auto response = encode_response_status<FileReadAtResponse>(msg, status, sz);
        response->data.data = (void*) FIDL_ALLOC_PRESENT;
        if (response->s != ZX_OK) {
            response->data.count = 0;
        }
        *sz += static_cast<uint32_t>(response->data.count);
        break;
    }
    case ZXFIDL_WRITE_AT: {
        auto response = encode_response_status<FileWriteAtResponse>(msg, status, sz);
        if (response->s != ZX_OK) {
            response->actual = 0;
        }
        break;
    }
    case ZXFIDL_TRUNCATE: {
        encode_response_status<FileTruncateResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_RENAME: {
        encode_response_status<DirectoryRenameResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_SYNC: {
        encode_response_status<NodeSyncResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_LINK: {
        encode_response_status<DirectoryLinkResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_REWIND: {
        encode_response_status<DirectoryRewindResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_GET_VMO: {
        auto response = encode_response_status<FileGetVmoResponse>(msg, status, sz);
        if (response->s != ZX_OK) {
            response->vmo = FIDL_HANDLE_ABSENT;
        } else {
            handles[0] = response->vmo;
            *hcount = 1;
            response->vmo = FIDL_HANDLE_PRESENT;
        }
        break;
    }
    case ZXFIDL_GET_FLAGS: {
        encode_response_status<FileGetFlagsResponse>(msg, status, sz);
        break;
    }
    case ZXFIDL_SET_FLAGS: {
        encode_response_status<FileSetFlagsResponse>(msg, status, sz);
        break;
    }
    default:
        if (ZXRIO_FIDL_MSG(msg->op)) {
            fprintf(stderr, "Unsupported FIDL operation: 0x%x\n", msg->op);
            return ZX_ERR_NOT_SUPPORTED;
        }
        msg->arg = status;
        if ((msg->arg < 0) || !is_rio_message_valid(msg)) {
            // in the event of an error response or bad message
            // release all the handles and data payload
            discard_handles(msg->handle, msg->hcount);
            msg->datalen = 0;
            msg->hcount = 0;
            // specific errors are prioritized over the bad
            // message case which we represent as ZX_ERR_INTERNAL
            // to differentiate from ZX_ERR_IO on the near side
            // TODO(ZX-974): consider a better error code
            msg->arg = (msg->arg < 0) ? msg->arg : ZX_ERR_INTERNAL;
        }
        *sz = static_cast<uint32_t>(ZXRIO_HDR_SZ + msg->datalen);
        *hcount = msg->hcount;
        memcpy(handles, msg->handle, sizeof(zx_handle_t) * msg->hcount);
    }
    return ZX_OK;
}

} // namespace

bool is_rio_message_valid(zxrio_msg_t* msg) {
    if ((msg->datalen > FDIO_CHUNK_SIZE) || (msg->hcount > FDIO_MAX_HANDLES)) {
        return false;
    }
    return true;
}

bool is_rio_message_reply_valid(zxrio_msg_t* msg, uint32_t size) {
    if ((size < ZXRIO_HDR_SZ) || (msg->datalen != (size - ZXRIO_HDR_SZ))) {
        return false;
    }
    return is_rio_message_valid(msg);
}

zx_status_t zxrio_read_request(zx_handle_t h, zxrio_msg_t* rio_msg) {
    zx_status_t r;
    zx_handle_t handles[FDIO_MAX_HANDLES];
    fidl::Message msg(fidl::BytePart((uint8_t*) rio_msg, sizeof(zxrio_msg_t)),
                      fidl::HandlePart(handles, static_cast<uint32_t>(fbl::count_of(handles))));
    if ((r = msg.Read(h, 0)) != ZX_OK) {
        return r;
    }

    if ((r = zxrio_decode_request(&msg)) != ZX_OK) {
        fprintf(stderr, "zxrio_read_request failed to decode\n");
        return ZX_ERR_INVALID_ARGS;
    }

    return r;
}

zx_status_t zxrio_write_response(zx_handle_t h, zx_status_t status, zxrio_msg_t* msg) {
    // Encode
    uint32_t sz = 0;
    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t hcount = 0;
    if (zxrio_encode_response(status, msg, &sz, handles, &hcount) != ZX_OK) {
        fprintf(stderr, "zxrio_write_response: Failed to encode response\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    msg->op = ZXRIO_STATUS;

    // Transmit
    if ((status = zx_channel_write(h, 0, msg, sz, handles, hcount)) != ZX_OK) {
        discard_handles(handles, hcount);
    }

    return status;
}

// Always consumes cnxn.
zx_status_t fidl_clone_request(zx_handle_t srv, zx_handle_t cnxn, uint32_t flags) {
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));
    fidl::HandlePart handles(&cnxn, 1, 1);

    // Setup the request message header
    ObjectCloneRequest* request = builder.New<ObjectCloneRequest>();
    request->hdr.ordinal = ZXFIDL_CLONE;
    request->flags = flags;
    request->object = FIDL_HANDLE_PRESENT;

    fidl::Message message(builder.Finalize(), fbl::move(handles));
    return message.Write(srv, 0);
}

zx_status_t fidl_open_request(zx_handle_t srv, zx_handle_t cnxn, uint32_t flags,
                              uint32_t mode, const char* path, size_t pathlen) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));
    fidl::HandlePart handles(&cnxn, 1, 1);

    // Setup the request message header
    DirectoryOpenRequest* request = builder.New<DirectoryOpenRequest>();
    request->hdr.ordinal = ZXFIDL_OPEN;

    // Setup the request message primary
    request->flags = flags;
    request->mode = mode;
    request->path.data = (char*) FIDL_ALLOC_PRESENT;
    request->path.size = pathlen;
    request->object = FIDL_HANDLE_PRESENT;

    // Setup the request message secondary
    char* secondary = builder.NewArray<char>(static_cast<uint32_t>(pathlen));
    memcpy(secondary, path, pathlen);

    fidl::Message message(builder.Finalize(), fbl::move(handles));
    return message.Write(srv, 0);
}

zx_status_t fidl_close(zxrio_t* rio) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    new_request<ObjectCloseRequest, ZXFIDL_CLOSE>(rio, &builder);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<ObjectCloseResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    return response->s;
}

zx_status_t fidl_write(zxrio_t* rio, const void* data, uint64_t length,
                       uint64_t* actual) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<FileWriteRequest, ZXFIDL_WRITE>(rio, &builder);

    // Setup the request message primary
    request->data.count = length;
    request->data.data = (void*) FIDL_ALLOC_PRESENT;

    // Setup the request message secondary
    char* secondary = builder.NewArray<char>(static_cast<uint32_t>(length));
    memcpy(secondary, data, length);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileWriteResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    if (response->actual > length) {
        return ZX_ERR_IO;
    }
    *actual = response->actual;
    return response->s;
}

zx_status_t fidl_writeat(zxrio_t* rio, const void* data, uint64_t length,
                         off_t offset, uint64_t* actual) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<FileWriteAtRequest, ZXFIDL_WRITE_AT>(rio, &builder);

    // Setup the request message primary
    request->data.count = length;
    request->data.data = (void*) FIDL_ALLOC_PRESENT;
    request->offset = offset;

    // Setup the request message secondary
    char* secondary = builder.NewArray<char>(static_cast<uint32_t>(length));
    memcpy(secondary, data, length);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileWriteAtResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    if (response->actual > length) {
        return ZX_ERR_IO;
    }
    *actual = response->actual;
    return response->s;
}

zx_status_t fidl_read(zxrio_t* rio, void* data, uint64_t length, uint64_t* actual) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<FileReadRequest, ZXFIDL_READ>(rio, &builder);

    // Setup the request message primary
    request->count = length;

    // Setup the request message secondary
    char* secondary = builder.NewArray<char>(static_cast<uint32_t>(length));
    memcpy(secondary, data, length);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileReadResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }
    if ((response->data.data != (void*) FIDL_ALLOC_PRESENT) ||
        (message.bytes().actual() != FIDL_ALIGN(sizeof(FileReadResponse)) +
         FIDL_ALIGN(response->data.count))) {
        return ZX_ERR_IO;
    }
    response->data.data = get_secondary(response);

    // Extract data
    if (response->data.count > length) {
        return ZX_ERR_IO;
    }
    memcpy(data, response->data.data, response->data.count);
    *actual = response->data.count;
    return response->s;
}

zx_status_t fidl_readat(zxrio_t* rio, void* data, uint64_t length, off_t offset,
                        uint64_t* actual) {

    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<FileReadAtRequest, ZXFIDL_READ_AT>(rio, &builder);

    // Setup the request message primary
    request->count = length;
    request->offset = offset;

    // Setup the request message secondary
    char* secondary = builder.NewArray<char>(static_cast<uint32_t>(length));
    memcpy(secondary, data, length);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileReadAtResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }
    if ((response->data.data != (void*) FIDL_ALLOC_PRESENT) ||
        (message.bytes().actual() != FIDL_ALIGN(sizeof(FileReadAtResponse)) +
         FIDL_ALIGN(response->data.count))) {
        return ZX_ERR_IO;
    }
    response->data.data = get_secondary(response);

    // Extract data
    if (response->data.count > length) {
        return ZX_ERR_IO;
    }
    memcpy(data, response->data.data, response->data.count);
    *actual = response->data.count;
    return response->s;
}

static_assert(SEEK_SET == SeekOrigin_Start, "");
static_assert(SEEK_CUR == SeekOrigin_Current, "");
static_assert(SEEK_END == SeekOrigin_End, "");

zx_status_t fidl_seek(zxrio_t* rio, off_t offset, int whence, off_t* out) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<FileSeekRequest, ZXFIDL_SEEK>(rio, &builder);

    // Setup the request message primary
    request->offset = offset;
    request->start = whence;

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileSeekResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    } else if (response->s != ZX_OK) {
        return response->s;
    }

    return ZX_OK;
}

zx_status_t fidl_stat(zxrio_t* rio, size_t len, vnattr_t* out, size_t* out_sz) {
    ZX_DEBUG_ASSERT(len >= sizeof(vnattr_t));

    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    new_request<NodeGetAttrRequest, ZXFIDL_STAT>(rio, &builder);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<NodeGetAttrResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    } else if (response->s != ZX_OK) {
        return response->s;
    }

    // Translate NodeAttributes --> vnattr
    out->mode = response->attributes.mode;
    out->inode = response->attributes.id;
    out->size = response->attributes.content_size;
    out->blksize = VNATTR_BLKSIZE;
    out->blkcount = response->attributes.storage_size / VNATTR_BLKSIZE;
    out->nlink = response->attributes.link_count;
    out->create_time = response->attributes.creation_time;
    out->modify_time = response->attributes.modification_time;

    *out_sz = sizeof(vnattr_t);
    return ZX_OK;
}

zx_status_t fidl_setattr(zxrio_t* rio, const vnattr_t* attr) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<NodeSetAttrRequest, ZXFIDL_SETATTR>(rio, &builder);

    // Setup the request message primary
    // TODO(smklein): Replace with autogenerated constants
    const uint32_t kFlagCreationTime = 1;
    const uint32_t kFlagModificationTime = 2;
    static_assert(kFlagCreationTime == ATTR_CTIME, "SetAttr flags unaligned");
    static_assert(kFlagModificationTime == ATTR_MTIME, "SetAttr flags unaligned");
    request->flags = attr->valid;
    request->attributes.creation_time = attr->create_time;
    request->attributes.modification_time = attr->modify_time;

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<NodeSetAttrResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }
    return response->s;
}

zx_status_t fidl_sync(zxrio_t* rio) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    new_request<NodeSyncRequest, ZXFIDL_SYNC>(rio, &builder);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<NodeSyncResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    return response->s;
}

zx_status_t fidl_readdirents(zxrio_t* rio, void* data, size_t length, size_t* out_sz) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<DirectoryReadDirentsRequest, ZXFIDL_READDIR>(rio, &builder);

    // Setup the request message primary
    request->max_out = length;

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<DirectoryReadDirentsResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }
    if ((response->dirents.data != (void*) FIDL_ALLOC_PRESENT) ||
        (message.bytes().actual() != FIDL_ALIGN(sizeof(DirectoryReadDirentsResponse)) +
         FIDL_ALIGN(response->dirents.count))) {
        fprintf(stderr, "fidl_readdirents failed to decode response\n");
        return ZX_ERR_IO;
    }
    response->dirents.data = get_secondary(response);

    // Extract data
    if (response->dirents.count > length) {
        return ZX_ERR_IO;
    }
    memcpy(data, response->dirents.data, response->dirents.count);
    *out_sz = response->dirents.count;
    return response->s;
}

zx_status_t fidl_rewind(zxrio_t* rio) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    new_request<DirectoryRewindRequest, ZXFIDL_REWIND>(rio, &builder);
    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<DirectoryRewindResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }
    return response->s;
}

zx_status_t fidl_unlink(zxrio_t* rio, const char* name, size_t namelen) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    DirectoryUnlinkRequest* request =
            new_request<DirectoryUnlinkRequest, ZXFIDL_UNLINK>(rio, &builder);

    // Setup the request message primary
    request->path.size = namelen;
    request->path.data = (char*) FIDL_ALLOC_PRESENT;

    // Setup the request message secondary
    char* secondary = builder.NewArray<char>(static_cast<uint32_t>(namelen));
    memcpy(secondary, name, namelen);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());

    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<DirectoryUnlinkResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    return response->s;
}

zx_status_t fidl_truncate(zxrio_t* rio, uint64_t length) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<FileTruncateRequest, ZXFIDL_TRUNCATE>(rio, &builder);

    // Setup the request message primary
    request->length = length;

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileTruncateResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    return response->s;
}

zx_status_t fidl_rename(zxrio_t* rio, const char* src, size_t srclen,
                        zx_handle_t dst_token, const char* dst, size_t dstlen) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));
    fidl::HandlePart handles(&dst_token, 1, 1);

    // Setup the request message header
    auto request = new_request<DirectoryRenameRequest, ZXFIDL_RENAME>(rio, &builder);

    // Setup the request message primary
    request->src.size = srclen;
    request->src.data = (char*) FIDL_ALLOC_PRESENT;
    request->dst_parent_token = FIDL_HANDLE_PRESENT;
    request->dst.size = dstlen;
    request->dst.data = (char*) FIDL_ALLOC_PRESENT;

    // Setup the request message secondary
    char* secondary = builder.NewArray<char>(static_cast<uint32_t>(srclen));
    memcpy(secondary, src, srclen);
    secondary = builder.NewArray<char>(static_cast<uint32_t>(dstlen));
    memcpy(secondary, dst, dstlen);

    fidl::Message message(builder.Finalize(), fbl::move(handles));
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<DirectoryRenameResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    return response->s;
}

zx_status_t fidl_link(zxrio_t* rio, const char* src, size_t srclen,
                      zx_handle_t dst_token, const char* dst, size_t dstlen) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));
    fidl::HandlePart handles(&dst_token, 1, 1);

    // Setup the request message header
    auto request = new_request<DirectoryLinkRequest, ZXFIDL_LINK>(rio, &builder);

    // Setup the request message primary
    request->src.size = srclen;
    request->src.data = (char*) FIDL_ALLOC_PRESENT;
    request->dst_parent_token = FIDL_HANDLE_PRESENT;
    request->dst.size = dstlen;
    request->dst.data = (char*) FIDL_ALLOC_PRESENT;

    // Setup the request message secondary
    char* secondary = builder.NewArray<char>(static_cast<uint32_t>(srclen));
    memcpy(secondary, src, srclen);
    secondary = builder.NewArray<char>(static_cast<uint32_t>(dstlen));
    memcpy(secondary, dst, dstlen);

    fidl::Message message(builder.Finalize(), fbl::move(handles));
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<DirectoryLinkResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    return response->s;
}

zx_status_t fidl_ioctl(zxrio_t* rio, uint32_t op, const void* in_buf,
                       size_t in_len, void* out_buf, size_t out_len,
                       size_t* out_actual) {

    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));
    zx_handle_t handle_buffer[FDIO_MAX_HANDLES];
    fidl::HandlePart handles(handle_buffer, FDIO_MAX_HANDLES);

    // Setup the request message header
    auto request = new_request<NodeIoctlRequest, ZXFIDL_IOCTL>(rio, &builder);

    // Setup the request message primary
    request->opcode = op;
    request->max_out = out_len;
    request->handles.count = 0;
    request->handles.data = (void*) FIDL_ALLOC_PRESENT;
    request->in.count = in_len;
    request->in.data = (void*) FIDL_ALLOC_PRESENT;

    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        if (out_len < sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        if (out_len < 2 * sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        if (out_len < 3 * sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_SET_HANDLE:
        if (in_len < sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        request->handles.count = 1;
        break;
    case IOCTL_KIND_SET_TWO_HANDLES:
        if (in_len < 2 * sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        request->handles.count = 2;
        break;
    }

    if (request->handles.count) {
        uint32_t hcount = static_cast<uint32_t>(request->handles.count);
        handles.set_actual(hcount);
        auto secondary = builder.NewArray<zx_handle_t>(hcount);
        for (size_t i = 0; i < hcount; i++) {
            handle_buffer[i] = *((zx_handle_t*) in_buf + i);
            secondary[i] = FIDL_HANDLE_PRESENT;
        }
    }
    if (in_len > 0) {
        auto secondary = builder.NewArray<char>(static_cast<uint32_t>(in_len));
        memcpy(secondary, in_buf, in_len);
    }

    fidl::Message message(builder.Finalize(), fbl::move(handles));

    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        fprintf(stderr, "ioctl fidl call failure: %d\n", r);
        return r;
    }

    // Validate primary size
    auto response = to_primary<NodeIoctlResponse>(&message);
    if (response == nullptr) {
        fprintf(stderr, "failed to get ioctl primary\n");
        return ZX_ERR_IO;
    }

    // Validate primary
    if ((response->handles.data != (void*) FIDL_ALLOC_PRESENT) ||
        (response->out.data != (void*) FIDL_ALLOC_PRESENT)) {
        fprintf(stderr, "Ioctl: Decoding bad primary\n");
        return ZX_ERR_IO;
    }

    // Validate secondary
    if (response->handles.count != message.handles().actual()) {
        fprintf(stderr, "Ioctl: Decoding bad hcount\n");
        return ZX_ERR_IO;
    }
    const void* secondary = get_secondary(response);

    size_t expected_handles_len = FIDL_ALIGN(sizeof(zx_handle_t) *
                                             response->handles.count);
    size_t expected_data_len = FIDL_ALIGN(response->out.count);
    if (message.bytes().actual() != FIDL_ALIGN(sizeof(NodeIoctlResponse)) +
        expected_handles_len + expected_data_len) {
        fprintf(stderr, "Ioctl: Decoding bad output size\n");
        return ZX_ERR_IO;
    }

    if ((sizeof(zx_handle_t) * response->handles.count > out_len) ||
        (response->out.count > out_len)) {
        fprintf(stderr, "Ioctl: Decoding response larger than out_len\n");
        return ZX_ERR_IO;
    }

    if (response->s != ZX_OK) {
        return response->s;
    }

    size_t expected_handles = 0;
    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        expected_handles = 1;
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        expected_handles = 2;
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        expected_handles = 3;
        break;
    }

    // Extract handles
    if (expected_handles != message.handles().actual()) {
        fprintf(stderr, "ioctl client decode: Unexpected Handle count\n");
        return ZX_ERR_IO;
    }

    // Extract handles on top of data
    *out_actual = 0;
    if (response->out.count > 0) {
        memcpy(out_buf, secondary, response->out.count);
        *out_actual = response->out.count;
    }
    memcpy(out_buf, message.handles().data(),
           message.handles().actual() * sizeof(zx_handle_t));
    if (*out_actual == 0) {
        *out_actual = message.handles().actual() * sizeof(zx_handle_t);
    }

    message.ClearHandlesUnsafe();
    return ZX_OK;
}

zx_status_t fidl_getvmo(zxrio_t* rio, uint32_t flags, zx_handle_t* out) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));
    fidl::HandlePart handles(out, 1);

    // Setup the request message header
    auto request = new_request<FileGetVmoRequest, ZXFIDL_GET_VMO>(rio, &builder);

    // Setup the request message primary
    request->flags = flags;

    fidl::Message message(builder.Finalize(), fbl::move(handles));
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileGetVmoResponse>(&message);
    if (response == nullptr) {
        fprintf(stderr, "fidl_getvmo couldn't convert to primary\n");
        return ZX_ERR_IO;
    } else if (response->s != ZX_OK) {
        return response->s;
    } else if (message.handles().actual() != 1) {
        fprintf(stderr, "fidl_getvmo missing VMO\n");
        return ZX_ERR_IO;
    }

    // Already reading directly into |out|.
    if (response->vmo != FIDL_HANDLE_PRESENT) {
        fprintf(stderr, "fidl_getvmo: Missing response VMO\n");
        return ZX_ERR_IO;
    }
    message.ClearHandlesUnsafe();
    return ZX_OK;
}

zx_status_t fidl_getflags(zxrio_t* rio, uint32_t* outflags) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    new_request<FileGetFlagsRequest, ZXFIDL_GET_FLAGS>(rio, &builder);

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileGetFlagsResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    *outflags = response->flags;
    return response->s;
}

zx_status_t fidl_setflags(zxrio_t* rio, uint32_t flags) {
    // Prepare buffers for input & output
    char byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    fidl::Builder builder(byte_buffer, sizeof(byte_buffer));

    // Setup the request message header
    auto request = new_request<FileSetFlagsRequest, ZXFIDL_SET_FLAGS>(rio, &builder);

    // Setup the request message primary
    request->flags = flags;

    fidl::Message message(builder.Finalize(), fidl::HandlePart());
    zx_status_t r = fidl_call(zxrio_handle(rio), &message);
    if (r != ZX_OK) {
        return r;
    }

    // Validate primary size
    auto response = to_primary<FileSetFlagsResponse>(&message);
    if (response == nullptr) {
        return ZX_ERR_IO;
    }

    return response->s;
}
