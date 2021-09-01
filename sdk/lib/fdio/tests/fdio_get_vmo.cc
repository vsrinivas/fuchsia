// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/io.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <sys/mman.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/rights.h>
#include <zircon/syscalls/object.h>

#include <algorithm>

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

namespace fuchsia_io = fuchsia_io;

struct Context {
  zx::vmo vmo;
  bool is_vmofile;
  bool supports_read_at;
  bool supports_seek;
  bool supports_get_buffer;
  size_t content_size;  // Must be <= zx_system_get_page_size().
  uint32_t last_flags;
};
class TestServer final : public fuchsia_io::testing::File_TestBase {
 public:
  explicit TestServer(Context* context) : context(context) {}

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    if (context->is_vmofile) {
      fuchsia_io::wire::Vmofile vmofile = {
          .offset = 0,
          .length = context->content_size,
      };

      zx_status_t status = context->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmofile.vmo);
      if (status != ZX_OK) {
        completer.Close(status);
        return;
      }

      completer.Reply(fuchsia_io::wire::NodeInfo::WithVmofile(
          fidl::ObjectView<fuchsia_io::wire::Vmofile>::FromExternal(&vmofile)));
    } else {
      fuchsia_io::wire::FileObject fo;
      completer.Reply(fuchsia_io::wire::NodeInfo::WithFile(
          fidl::ObjectView<fuchsia_io::wire::FileObject>::FromExternal(&fo)));
    }
  }

  void GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) override {
    completer.Reply(ZX_OK, {
                               .id = 5,
                               .content_size = context->content_size,
                               .storage_size = zx_system_get_page_size(),
                               .link_count = 1,
                           });
  }

  void ReadAt(ReadAtRequestView request, ReadAtCompleter::Sync& completer) override {
    if (!context->supports_read_at) {
      completer.Reply(ZX_ERR_NOT_SUPPORTED, fidl::VectorView<uint8_t>());
      return;
    }
    if (request->offset >= context->content_size) {
      completer.Reply(ZX_OK, fidl::VectorView<uint8_t>());
      return;
    }
    size_t actual = std::min(request->count, context->content_size - request->offset);
    std::vector<uint8_t> buffer(zx_system_get_page_size());
    zx_status_t status = context->vmo.read(buffer.data(), request->offset, actual);
    if (status != ZX_OK) {
      completer.Reply(status, fidl::VectorView<uint8_t>());
      return;
    }
    completer.Reply(ZX_OK, fidl::VectorView<uint8_t>::FromExternal(buffer.data(), actual));
  }

  void Seek(SeekRequestView request, SeekCompleter::Sync& completer) override {
    if (!context->supports_seek) {
      completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
    }
    completer.Reply(ZX_OK, 0);
  }

  void GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) override {
    context->last_flags = request->flags;

    if (!context->supports_get_buffer) {
      completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
      return;
    }

    fuchsia_mem::wire::Buffer buffer = {};
    buffer.size = context->content_size;

    zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY;
    rights |= (request->flags & fuchsia_io::wire::kVmoFlagRead) ? ZX_RIGHT_READ : 0;
    rights |= (request->flags & fuchsia_io::wire::kVmoFlagWrite) ? ZX_RIGHT_WRITE : 0;
    rights |= (request->flags & fuchsia_io::wire::kVmoFlagExec) ? ZX_RIGHT_EXECUTE : 0;

    zx_status_t status = ZX_OK;
    zx::vmo result;
    if (request->flags & fuchsia_io::wire::kVmoFlagPrivate) {
      rights |= ZX_RIGHT_SET_PROPERTY;
      uint32_t options = ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE;
      if (request->flags & fuchsia_io::wire::kVmoFlagExec) {
        // Creating a SNAPSHOT_AT_LEAST_ON_WRITE child removes ZX_RIGHT_EXECUTE even if the parent
        // VMO has it, but NO_WRITE changes this behavior so that the new handle doesn't have WRITE
        // and preserves EXECUTE.
        options |= ZX_VMO_CHILD_NO_WRITE;
      }
      status = context->vmo.create_child(options, 0, zx_system_get_page_size(), &result);
      if (status != ZX_OK) {
        completer.Reply(status, nullptr);
        return;
      }

      status = result.replace(rights, &result);
    } else {
      status = context->vmo.duplicate(rights, &result);
    }
    if (status != ZX_OK) {
      completer.Reply(status, nullptr);
      return;
    }

    buffer.vmo = std::move(result);
    completer.Reply(ZX_OK, fidl::ObjectView<fuchsia_mem::wire::Buffer>::FromExternal(&buffer));
  }

 private:
  Context* context;
};

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
  if (length > zx_system_get_page_size()) {
    return false;
  }
  std::vector<char> buffer(zx_system_get_page_size());
  zx_status_t status = vmo.read(buffer.data(), 0, buffer.size());
  if (status != ZX_OK) {
    return false;
  }
  return strncmp(string, buffer.data(), length) == 0;
}

void create_context_vmo(size_t size, zx::vmo* out_vmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(size, 0, &vmo));
  ASSERT_OK(
      vmo.replace(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY, &vmo));
  ASSERT_OK(vmo.replace_as_executable(zx::resource(), out_vmo));
}

TEST(GetVMOTest, Remote) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("fake-filesystem"));
  async_dispatcher_t* dispatcher = loop.dispatcher();

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::File>();
  ASSERT_OK(endpoints.status_value());

  Context context = {};
  context.is_vmofile = false;
  context.content_size = 43;
  context.supports_get_buffer = true;
  create_context_vmo(zx_system_get_page_size(), &context.vmo);
  ASSERT_OK(context.vmo.write("abcd", 0, 4));

  ASSERT_OK(fidl::BindSingleInFlightOnly(dispatcher, std::move(endpoints->server),
                                         std::make_unique<TestServer>(&context)));

  int raw_fd = -1;
  ASSERT_OK(fdio_fd_create(endpoints->client.channel().release(), &raw_fd));
  fbl::unique_fd fd(raw_fd);

  zx_rights_t expected_rights =
      ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_READ;

  zx::vmo received;
  EXPECT_OK(fdio_get_vmo_exact(fd.get(), received.reset_and_get_address()));
  EXPECT_EQ(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);
  EXPECT_EQ(fuchsia_io::wire::kVmoFlagRead | fuchsia_io::wire::kVmoFlagExact, context.last_flags);
  context.last_flags = 0;

  // The rest of these tests exercise methods which use VMO_FLAG_PRIVATE, in which case the returned
  // rights should also include SET_PROPERTY.
  expected_rights |= ZX_RIGHT_SET_PROPERTY;

  EXPECT_OK(fdio_get_vmo_clone(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);
  EXPECT_EQ(fuchsia_io::wire::kVmoFlagRead | fuchsia_io::wire::kVmoFlagPrivate, context.last_flags);
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  context.last_flags = 0;

  EXPECT_OK(fdio_get_vmo_copy(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);
  EXPECT_EQ(fuchsia_io::wire::kVmoFlagRead | fuchsia_io::wire::kVmoFlagPrivate, context.last_flags);
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  context.last_flags = 0;

  EXPECT_OK(fdio_get_vmo_exec(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights | ZX_RIGHT_EXECUTE);
  EXPECT_EQ(fuchsia_io::wire::kVmoFlagRead | fuchsia_io::wire::kVmoFlagExec |
                fuchsia_io::wire::kVmoFlagPrivate,
            context.last_flags);
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  context.last_flags = 0;

  context.supports_get_buffer = false;
  context.supports_read_at = true;
  EXPECT_OK(fdio_get_vmo_copy(fd.get(), received.reset_and_get_address()));
  EXPECT_NE(get_koid(context.vmo), get_koid(received));
  EXPECT_EQ(get_rights(received), expected_rights);
  EXPECT_EQ(fuchsia_io::wire::kVmoFlagRead | fuchsia_io::wire::kVmoFlagPrivate, context.last_flags);
  EXPECT_TRUE(vmo_starts_with(received, "abcd"));
  context.last_flags = 0;
}

TEST(GetVMOTest, VMOFile) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread("fake-filesystem"));
  async_dispatcher_t* dispatcher = loop.dispatcher();

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::File>();
  ASSERT_OK(endpoints.status_value());

  Context context = {};
  context.content_size = 43;
  context.is_vmofile = true;
  context.supports_seek = true;
  create_context_vmo(zx_system_get_page_size(), &context.vmo);
  ASSERT_OK(context.vmo.write("abcd", 0, 4));

  ASSERT_OK(fidl::BindSingleInFlightOnly(dispatcher, std::move(endpoints->server),
                                         std::make_unique<TestServer>(&context)));

  int raw_fd = -1;
  ASSERT_OK(fdio_fd_create(endpoints->client.channel().release(), &raw_fd));
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

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::File>();
  ASSERT_OK(endpoints.status_value());

  Context context = {};
  context.is_vmofile = false;
  context.content_size = 43;
  context.supports_get_buffer = true;
  create_context_vmo(zx_system_get_page_size(), &context.vmo);
  ASSERT_OK(context.vmo.write("abcd", 0, 4));

  ASSERT_OK(fidl::BindSingleInFlightOnly(dispatcher, std::move(endpoints->server),
                                         std::make_unique<TestServer>(&context)));

  int raw_fd = -1;
  ASSERT_OK(fdio_fd_create(endpoints->client.channel().release(), &raw_fd));
  fbl::unique_fd fd(raw_fd);

  size_t offset = 0;
  size_t len = 4;
  off_t fd_off = 0;
  zx_vm_option_t zx_options = PROT_READ | PROT_EXEC;
  uintptr_t ptr;
  ASSERT_OK(_mmap_file(offset, len, zx_options, MAP_SHARED, fd.get(), fd_off, &ptr));
  EXPECT_EQ(context.last_flags, fuchsia_io::wire::kVmoFlagRead | fuchsia_io::wire::kVmoFlagExec);
}

}  // namespace
