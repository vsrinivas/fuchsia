// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>
#include <lib/zxio/zxio.h>

#include <string>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/tests/test_directory_server_base.h"
#include "sdk/lib/zxio/tests/test_file_server_base.h"

namespace {

constexpr auto kTestPath = std::string_view("test_path");

class TestDirectoryServer : public zxio_tests::TestDirectoryServerBase {
 public:
  explicit TestDirectoryServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) final {
    fidl::Arena fidl_allocator;
    auto node_info = fuchsia_io::wire::NodeInfo::WithDirectory(fidl_allocator);
    completer.Reply(std::move(node_info));
  }

  void Open(OpenRequestView request, OpenCompleter::Sync& completer) final {
    constexpr uint32_t kExpectedFlags =
        fuchsia_io::wire::kOpenRightReadable | fuchsia_io::wire::kOpenFlagDescribe;
    if (request->flags != kExpectedFlags) {
      ADD_FAILURE("unexpected flags for Open request: 0x%x vs 0x%x", request->flags,
                  kExpectedFlags);
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    constexpr uint32_t kExpectedMode = 0u;
    if (request->mode != kExpectedMode) {
      ADD_FAILURE("unexpected mode for Open request: 0x%x vs 0x%x", request->mode, kExpectedMode);
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    if (request->path.get() != kTestPath) {
      ADD_FAILURE("unexpected path for Open request: \"%s\" vs \"%s\"", request->path.data(),
                  kTestPath.data());
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    if (open_calls_ != 0) {
      ADD_FAILURE("unexpected number of open calls: %d", open_calls_);
      completer.Close(ZX_ERR_BAD_STATE);
      return;
    }
    open_calls_++;
    // Request looks good - generate an OnOpen event and bind to a File server.
    fidl::ServerEnd<fuchsia_io::File> file_server(request->object.TakeChannel());

    zx::event file_event;
    ASSERT_OK(zx::event::create(0u, &file_event));

    fuchsia_io::wire::FileObject file = {.event = std::move(file_event)};
    fidl::Arena fidl_allocator;
    auto node_info = fuchsia_io::wire::NodeInfo::WithFile(fidl_allocator, std::move(file));

    fidl::WireEventSender<fuchsia_io::File> sender(std::move(file_server));
    ASSERT_OK(sender.OnOpen(ZX_OK, std::move(node_info)));

    auto file_request = fidl::ServerEnd(std::move(sender.server_end()));
    fidl::BindServer(dispatcher_, std::move(file_request), &file_);
  }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  int open_calls_ = 0;
  zxio_tests::TestReadFileServer file_;
};

}  // namespace

TEST(Directory, Open) {
  zx::status directory_ends = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(directory_ends.status_value());
  auto [directory_client_end, directory_server_end] = std::move(directory_ends.value());

  zx::status node_ends = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_OK(node_ends.status_value());
  auto [node_client_end, node_server_end] = std::move(node_ends.value());

  async::Loop server_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  TestDirectoryServer directory_server(server_loop.dispatcher());
  fidl::BindServer(server_loop.dispatcher(), std::move(directory_server_end), &directory_server);

  server_loop.StartThread("directory_server_loop");

  zxio_storage_t directory_storage;
  ASSERT_OK(zxio_create(directory_client_end.TakeChannel().release(), &directory_storage));
  zxio_t* directory = &directory_storage.io;

  uint32_t flags = fuchsia_io::wire::kOpenRightReadable;
  uint32_t mode = 0u;
  zxio_storage_t file_storage;
  ASSERT_OK(zxio_open(directory, flags, mode, kTestPath.data(), kTestPath.length(), &file_storage));
  zxio_t* file = &file_storage.io;

  ASSERT_OK(zxio_close(directory));

  // Sanity check the zxio by reading some test data from the server.
  char buffer[sizeof(zxio_tests::TestReadFileServer::kTestData)];
  size_t actual = 0u;

  ASSERT_OK(zxio_read(file, buffer, sizeof(buffer), 0u, &actual));

  EXPECT_EQ(sizeof(buffer), actual);
  EXPECT_BYTES_EQ(buffer, zxio_tests::TestReadFileServer::kTestData, sizeof(buffer));

  ASSERT_OK(zxio_close(file));

  server_loop.Shutdown();
}
