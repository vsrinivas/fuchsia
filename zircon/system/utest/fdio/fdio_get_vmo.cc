// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/bind.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <sys/mman.h>
#include <zircon/limits.h>
#include <zircon/rights.h>
#include <zircon/syscalls/object.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

// We redeclare _mmap_file because it is implemented as part of fdio and we care
// about its behavior with respect to other things it calls within fdio.  The
// canonical declaration of this function lives in
// zircon/third_party/ulib/musl/src/internal/stdio_impl.h, but including that
// header is fraught.  The implementation in fdio just declares and exports the
// symbol inline, so I think it's reasonable for this test to declare it itself
// and depend on it the same way musl does.
extern "C" zx_status_t _mmap_file(size_t offset, size_t len, zx_vm_option_t zx_options, int flags,
                                  int fd, off_t fd_off, uintptr_t* out);

namespace {

struct Context {
  zx::vmo vmo;
  bool is_vmofile;
  bool supports_read_at;
  bool supports_seek;
  bool supports_get_buffer;
  size_t content_size;  // Must be <= ZX_PAGE_SIZE.
  uint32_t last_flags;
};

zx_status_t FileClone(void* ctx, uint32_t flags, zx_handle_t object) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FileClose(void* ctx, fidl_txn_t* txn) { return fuchsia_io_NodeClose_reply(txn, ZX_OK); }

zx_status_t FileDescribe(void* ctx, fidl_txn_t* txn) {
  Context* context = reinterpret_cast<Context*>(ctx);
  fuchsia_io_NodeInfo info;
  memset(&info, 0, sizeof(info));
  if (context->is_vmofile) {
    zx::vmo vmo;
    zx_status_t status = context->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
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

zx_status_t FileSync(void* ctx, fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

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

zx_status_t FileRead(void* ctx, uint64_t count, fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

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

zx_status_t FileGetFlags(void* ctx, fidl_txn_t* txn) { return ZX_ERR_NOT_SUPPORTED; }

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

  // TODO(fxb/37091): This should just have GET_PROPERTY, not SET_PROPERTY, but currently this
  // mimics what most filesystems do.
  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
  rights |= (flags & fuchsia_io_VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
  rights |= (flags & fuchsia_io_VMO_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
  rights |= (flags & fuchsia_io_VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;

  zx_status_t status = ZX_OK;
  zx::vmo result;
  if (flags & fuchsia_io_VMO_FLAG_PRIVATE) {
    uint32_t options = ZX_VMO_CHILD_COPY_ON_WRITE;
    if (flags & fuchsia_io_VMO_FLAG_EXEC) {
      // Creating a COPY_ON_WRITE child removes ZX_RIGHT_EXECUTE even if the parent VMO has it, but
      // NO_WRITE changes this behavior so that the new handle doesn't have WRITE and preserves
      // EXECUTE.
      options |= ZX_VMO_CHILD_NO_WRITE;
    }
    status = context->vmo.create_child(options, 0, ZX_PAGE_SIZE, &result);
    if (status != ZX_OK) {
      return fuchsia_io_FileGetBuffer_reply(txn, status, nullptr);
    }

    status = result.replace(rights, &result);
  } else {
    status = context->vmo.duplicate(rights, &result);
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

zx_koid_t get_koid(const zx::object_base& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.koid : ZX_KOID_INVALID;
}

zx_rights_t get_rights(const zx::object_base& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.rights : ZX_RIGHT_NONE;
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

void create_context_vmo(size_t size, zx::vmo* out_vmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(size, 0, &vmo));
  // TODO(fxb/37091): This should just have GET_PROPERTY, not SET_PROPERTY, but currently this
  // mimics what most filesystems do.
  ASSERT_OK(vmo.replace(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY, &vmo));
  ASSERT_OK(vmo.replace_as_executable(zx::handle(), out_vmo));
}

TEST(GetVMOTest, Remote) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("fake-filesystem"));
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  Context context = {};
  context.is_vmofile = false;
  context.content_size = 43;
  context.supports_get_buffer = true;
  create_context_vmo(ZX_PAGE_SIZE, &context.vmo);
  ASSERT_OK(context.vmo.write("abcd", 0, 4));

  ASSERT_OK(fidl_bind(dispatcher, server.release(),
                      reinterpret_cast<fidl_dispatch_t*>(fuchsia_io_File_dispatch), &context,
                      &kFileOps));

  int raw_fd = -1;
  ASSERT_OK(fdio_fd_create(client.release(), &raw_fd));
  fbl::unique_fd fd(raw_fd);

  // TODO(fxb/37091): This should just have GET_PROPERTY, not SET_PROPERTY, but currently this
  // mimics what most filesystems do.
  zx_rights_t expected_rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY | ZX_RIGHT_READ;

  zx::vmo received;
  EXPECT_OK(fdio_get_vmo_exact(fd.get(), received.reset_and_get_address()));
  EXPECT_EQ(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);
  EXPECT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_EXACT, context.last_flags);
  context.last_flags = 0;

  // The rest of these tests exercise methods which use VMO_FLAG_PRIVATE, in which case the returned
  // rights should also include SET_PROPERTY.
  expected_rights |= ZX_RIGHT_SET_PROPERTY;

  EXPECT_OK(fdio_get_vmo_clone(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);
  EXPECT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_PRIVATE, context.last_flags);
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  context.last_flags = 0;

  EXPECT_OK(fdio_get_vmo_copy(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);
  EXPECT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_PRIVATE, context.last_flags);
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  context.last_flags = 0;

  EXPECT_OK(fdio_get_vmo_exec(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights | ZX_RIGHT_EXECUTE);
  EXPECT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_EXEC | fuchsia_io_VMO_FLAG_PRIVATE,
            context.last_flags);
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  context.last_flags = 0;

  context.supports_get_buffer = false;
  context.supports_read_at = true;
  EXPECT_OK(fdio_get_vmo_copy(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);
  EXPECT_EQ(fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_PRIVATE, context.last_flags);
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  context.last_flags = 0;
}

TEST(GetVMOTest, VMOFile) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("fake-filesystem"));
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  Context context = {};
  context.content_size = 43;
  context.is_vmofile = true;
  context.supports_seek = true;
  create_context_vmo(ZX_PAGE_SIZE, &context.vmo);
  ASSERT_OK(context.vmo.write("abcd", 0, 4));

  ASSERT_OK(fidl_bind(dispatcher, server.release(),
                      reinterpret_cast<fidl_dispatch_t*>(fuchsia_io_File_dispatch), &context,
                      &kFileOps));

  int raw_fd = -1;
  ASSERT_OK(fdio_fd_create(client.release(), &raw_fd));
  fbl::unique_fd fd(raw_fd);
  context.supports_seek = false;

  zx_rights_t expected_rights =
      ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_READ;

  zx::vmo received;
  EXPECT_OK(fdio_get_vmo_exact(fd.get(), received.reset_and_get_address()));
  EXPECT_EQ(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);

  // The rest of these tests exercise methods which use VMO_FLAG_PRIVATE, in which case the returned
  // rights should also include SET_PROPERTY.
  expected_rights |= ZX_RIGHT_SET_PROPERTY;

  EXPECT_OK(fdio_get_vmo_clone(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  EXPECT_EQ(get_rights(received), expected_rights);

  EXPECT_OK(fdio_get_vmo_copy(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  EXPECT_EQ(get_rights(received), expected_rights);

  EXPECT_OK(fdio_get_vmo_exec(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  EXPECT_EQ(get_rights(received), expected_rights | ZX_RIGHT_EXECUTE);
}

// Verify that mmap (or rather the internal fdio function used to implement mmap, _mmap_file, works
// with PROT_EXEC).
TEST(MmapFileTest, ProtExecWorks) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("fake-filesystem"));
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));

  Context context = {};
  context.is_vmofile = false;
  context.content_size = 43;
  context.supports_get_buffer = true;
  create_context_vmo(ZX_PAGE_SIZE, &context.vmo);
  ASSERT_OK(context.vmo.write("abcd", 0, 4));

  ASSERT_OK(fidl_bind(dispatcher, server.release(),
                      reinterpret_cast<fidl_dispatch_t*>(fuchsia_io_File_dispatch), &context,
                      &kFileOps));

  int raw_fd = -1;
  ASSERT_OK(fdio_fd_create(client.release(), &raw_fd));
  fbl::unique_fd fd(raw_fd);

  size_t offset = 0;
  size_t len = 4;
  off_t fd_off = 0;
  zx_vm_option_t zx_options = PROT_READ | PROT_EXEC;
  uintptr_t ptr;
  ASSERT_OK(_mmap_file(offset, len, zx_options, MAP_SHARED, fd.get(), fd_off, &ptr));
  EXPECT_EQ(context.last_flags, fuchsia_io_VMO_FLAG_READ | fuchsia_io_VMO_FLAG_EXEC);
}

}  // namespace
