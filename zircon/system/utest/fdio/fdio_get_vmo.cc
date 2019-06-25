// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <fbl/unique_fd.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/bind.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <zircon/limits.h>

#include <algorithm>
#include <string>

#include <zxtest/zxtest.h>

namespace {

struct Context {
    zx::vmo vmo;
    bool is_vmofile;
    bool supports_read_at;
    bool supports_seek;
    bool supports_get_buffer;
    size_t content_size; // Must be <= ZX_PAGE_SIZE.
    uint32_t last_flags;
};

zx_status_t FileClone(void* ctx, uint32_t flags, zx_handle_t object) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileClose(void* ctx, fidl_txn_t* txn) {
    return fuchsia_io_NodeClose_reply(txn, ZX_OK);
}

zx_status_t FileDescribe(void* ctx, fidl_txn_t* txn) {
    Context* context = reinterpret_cast<Context*>(ctx);
    fuchsia_io_NodeInfo info;
    memset(&info, 0, sizeof(info));
    if (context->is_vmofile) {
        zx::vmo vmo;
        zx_status_t status = context->vmo.duplicate(
            ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_READ, &vmo);
        if (status != ZX_OK) {
            return status;
        }
        info.tag = fuchsia_io_NodeInfoTag_vmofile;
        info.vmofile.vmo = vmo.release();
        info.vmofile.offset = 0;
        info.vmofile.length = context->content_size;
    } else {
        info.tag = fuchsia_io_NodeInfoTag_file;
    }
    return fuchsia_io_NodeDescribe_reply(txn, &info);
}

zx_status_t FileSync(void* ctx, fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileGetAttr(void* ctx, fidl_txn_t* txn) {
    Context* context = reinterpret_cast<Context*>(ctx);
    fuchsia_io_NodeAttributes attributes = {};
    attributes.id = 5;
    attributes.content_size = context->content_size;
    attributes.storage_size = ZX_PAGE_SIZE;
    attributes.link_count = 1;
    return fuchsia_io_NodeGetAttr_reply(txn, ZX_OK, &attributes);
}

zx_status_t FileSetAttr(void* ctx, uint32_t flags, const fuchsia_io_NodeAttributes* attributes,
                        fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileIoctl(void* ctx, uint32_t opcode, uint64_t max_out, const zx_handle_t* handles_data,
                      size_t handles_count, const uint8_t* in_data, size_t in_count,
                      fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileRead(void* ctx, uint64_t count, fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileReadAt(void* ctx, uint64_t count, uint64_t offset, fidl_txn_t* txn) {
    Context* context = reinterpret_cast<Context*>(ctx);
    if (!context->supports_read_at) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (offset >= context->content_size) {
        return fuchsia_io_FileRead_reply(txn, ZX_OK, nullptr, 0);
    }
    size_t actual = std::min(count, context->content_size - offset);
    uint8_t buffer[ZX_PAGE_SIZE];
    zx_status_t status = context->vmo.read(buffer, offset, actual);
    if (status != ZX_OK) {
        return fuchsia_io_FileRead_reply(txn, status, nullptr, 0);
    }
    return fuchsia_io_FileRead_reply(txn, ZX_OK, buffer, actual);
}

zx_status_t FileWrite(void* ctx, const uint8_t* data_data, size_t data_count, fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileWriteAt(void* ctx, const uint8_t* data_data, size_t data_count, uint64_t offset,
                        fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileSeek(void* ctx, int64_t offset, fuchsia_io_SeekOrigin start, fidl_txn_t* txn) {
    Context* context = reinterpret_cast<Context*>(ctx);
    if (!context->supports_seek) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return fuchsia_io_FileSeek_reply(txn, ZX_OK, 0);
}

zx_status_t FileTruncate(void* ctx, uint64_t length, fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileGetFlags(void* ctx, fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileSetFlags(void* ctx, uint32_t flags, fidl_txn_t* txn) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileGetBuffer(void* ctx, uint32_t flags, fidl_txn_t* txn) {
    Context* context = reinterpret_cast<Context*>(ctx);
    context->last_flags = flags;

    if (!context->supports_get_buffer) {
        return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr);
    }

    fuchsia_mem_Buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.size = context->content_size;

    zx_status_t status = ZX_OK;
    zx::vmo result;
    if (flags & fuchsia_io_VMO_FLAG_PRIVATE) {
        status = context->vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0,
                                           ZX_PAGE_SIZE, &result);
    } else {
        status = context->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &result);
    }
    if (status != ZX_OK) {
        return fuchsia_io_FileGetBuffer_reply(txn, status, nullptr);
    }

    buffer.vmo = result.release();
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_OK, &buffer);
}

constexpr fuchsia_io_File_ops_t kFileOps = [] {
    fuchsia_io_File_ops_t ops = {};
    ops.Clone = FileClone;
    ops.Close = FileClose;
    ops.Describe = FileDescribe;
    ops.Sync = FileSync;
    ops.GetAttr = FileGetAttr;
    ops.SetAttr = FileSetAttr;
    ops.Ioctl = FileIoctl;
    ops.Read = FileRead;
    ops.ReadAt = FileReadAt;
    ops.Write = FileWrite;
    ops.WriteAt = FileWriteAt;
    ops.Seek = FileSeek;
    ops.Truncate = FileTruncate;
    ops.GetFlags = FileGetFlags;
    ops.SetFlags = FileSetFlags;
    ops.GetBuffer = FileGetBuffer;
    return ops;
}();

zx_koid_t get_koid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

bool vmo_starts_with(const zx::vmo& vmo, const char* string) {
    size_t length = strlen(string);
    if (length > ZX_PAGE_SIZE) {
        return false;
    }
    char buffer[ZX_PAGE_SIZE];
    zx_status_t status = vmo.read(buffer, 0, sizeof(buffer));
    if (status != ZX_OK) {
        return false;
    }
    return strncmp(string, buffer, length) == 0;
}

TEST(GetVMOTest, Remote) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_OK(loop.StartThread("fake-filesystem"));
    async_dispatcher_t* dispatcher = loop.dispatcher();

    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));

    Context context = {};
    context.is_vmofile = false;
    context.content_size = 43;
    context.supports_get_buffer = true;
    ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &context.vmo));
    ASSERT_OK(context.vmo.write("abcd", 0, 4));

    ASSERT_OK(fidl_bind(dispatcher, server.release(),
                               reinterpret_cast<fidl_dispatch_t*>(fuchsia_io_File_dispatch),
                               &context, &kFileOps));

    int raw_fd = -1;
    ASSERT_OK(fdio_fd_create(client.release(), &raw_fd));
    fbl::unique_fd fd(raw_fd);

    zx::vmo received;
    ASSERT_OK(fdio_get_vmo_exact(fd.get(), received.reset_and_get_address()));
    ASSERT_EQ(get_koid(context.vmo.get()), get_koid(received.get()));
    ASSERT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_EXACT, context.last_flags);
    context.last_flags = 0;

    ASSERT_OK(fdio_get_vmo_clone(fd.get(), received.reset_and_get_address()));
    ASSERT_NE(get_koid(context.vmo.get()), get_koid(received.get()));
    ASSERT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_PRIVATE, context.last_flags);
    ASSERT_TRUE(vmo_starts_with(received, "abcd"));
    context.last_flags = 0;

    ASSERT_OK(fdio_get_vmo_copy(fd.get(), received.reset_and_get_address()));
    ASSERT_NE(get_koid(context.vmo.get()), get_koid(received.get()));
    ASSERT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_PRIVATE, context.last_flags);
    ASSERT_TRUE(vmo_starts_with(received, "abcd"));
    context.last_flags = 0;

    context.supports_get_buffer = false;
    context.supports_read_at = true;
    ASSERT_OK(fdio_get_vmo_copy(fd.get(), received.reset_and_get_address()));
    ASSERT_NE(get_koid(context.vmo.get()), get_koid(received.get()));
    ASSERT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_PRIVATE, context.last_flags);
    ASSERT_TRUE(vmo_starts_with(received, "abcd"));
    context.last_flags = 0;
}

TEST(GetVMOTest, VMOFile) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_OK(loop.StartThread("fake-filesystem"));
    async_dispatcher_t* dispatcher = loop.dispatcher();

    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));

    Context context = {};
    context.content_size = 43;
    context.is_vmofile = true;
    context.supports_seek = true;
    ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &context.vmo));
    ASSERT_OK(context.vmo.write("abcd", 0, 4));

    ASSERT_OK(fidl_bind(dispatcher, server.release(),
                               reinterpret_cast<fidl_dispatch_t*>(fuchsia_io_File_dispatch),
                               &context, &kFileOps));

    int raw_fd = -1;
    ASSERT_OK(fdio_fd_create(client.release(), &raw_fd));
    fbl::unique_fd fd(raw_fd);
    context.supports_seek = false;

    zx::vmo received;
    ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_get_vmo_exact(fd.get(), received.reset_and_get_address()));

    ASSERT_OK(fdio_get_vmo_clone(fd.get(), received.reset_and_get_address()));
    ASSERT_NE(get_koid(context.vmo.get()), get_koid(received.get()));
    ASSERT_TRUE(vmo_starts_with(received, "abcd"));

    ASSERT_OK(fdio_get_vmo_copy(fd.get(), received.reset_and_get_address()));
    ASSERT_NE(get_koid(context.vmo.get()), get_koid(received.get()));
    ASSERT_TRUE(vmo_starts_with(received, "abcd"));
}

TEST(GetVMOTest, VMOFilePage) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_OK(loop.StartThread("fake-filesystem"));
    async_dispatcher_t* dispatcher = loop.dispatcher();

    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));

    Context context = {};
    context.content_size = ZX_PAGE_SIZE;
    context.is_vmofile = true;
    context.supports_seek = true;
    ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &context.vmo));
    ASSERT_OK(context.vmo.write("abcd", 0, 4));

    ASSERT_OK(fidl_bind(dispatcher, server.release(),
                               reinterpret_cast<fidl_dispatch_t*>(fuchsia_io_File_dispatch),
                               &context, &kFileOps));

    int raw_fd = -1;
    ASSERT_OK(fdio_fd_create(client.release(), &raw_fd));
    fbl::unique_fd fd(raw_fd);
    context.supports_seek = false;

    zx::vmo received;
    ASSERT_OK(fdio_get_vmo_exact(fd.get(), received.reset_and_get_address()));
    ASSERT_EQ(get_koid(context.vmo.get()), get_koid(received.get()));
}

} // namespace
