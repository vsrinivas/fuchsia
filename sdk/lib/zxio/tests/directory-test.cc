// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zxio/zxio.h>

#include <string>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/tests/test_directory_server_base.h"
#include "sdk/lib/zxio/tests/test_file_server_base.h"

namespace {

constexpr std::string_view kTestPath("test_path");

class TestDirectoryServer : public zxio_tests::TestDirectoryServerBase {
 public:
  explicit TestDirectoryServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void Init(zx::event token) { token_ = std::move(token); }

  void DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) final {
    completer.Reply(fuchsia_io::wire::NodeInfoDeprecated::WithDirectory({}));
  }

  void Open(OpenRequestView request, OpenCompleter::Sync& completer) final {
    constexpr fuchsia_io::wire::OpenFlags kExpectedFlags =
        fuchsia_io::wire::OpenFlags::kRightReadable | fuchsia_io::wire::OpenFlags::kDescribe;
    if (request->flags != kExpectedFlags) {
      ADD_FAILURE() << "unexpected flags for Open request: " << std::showbase << std::hex
                    << static_cast<uint32_t>(request->flags) << " vs " << std::showbase << std::hex
                    << static_cast<uint32_t>(kExpectedFlags);
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    constexpr uint32_t kExpectedMode = 0u;
    if (request->mode != kExpectedMode) {
      ADD_FAILURE() << "unexpected mode for Open request: " << std::showbase << std::hex
                    << request->mode << " vs " << std::showbase << std::hex << kExpectedMode;
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    if (request->path.get() != kTestPath) {
      ADD_FAILURE() << "unexpected path for Open request: \"" << request->path.get() << "\" vs \""
                    << kTestPath << "\"";
      completer.Close(ZX_ERR_INVALID_ARGS);
      return;
    }
    if (open_calls_ != 0) {
      ADD_FAILURE() << "unexpected number of open calls: " << open_calls_;
      completer.Close(ZX_ERR_BAD_STATE);
      return;
    }
    open_calls_++;
    // Request looks good - generate an OnOpen event and bind to a File server.
    fidl::ServerEnd<fuchsia_io::File> file_server(request->object.TakeChannel());

    zx::event file_event;
    ASSERT_OK(zx::event::create(0u, &file_event));

    fuchsia_io::wire::FileObject file = {
        .event = std::move(file_event),
    };
    ASSERT_OK(fidl::WireSendEvent(file_server)
                  ->OnOpen(ZX_OK, fuchsia_io::wire::NodeInfoDeprecated::WithFile(
                                      fidl::ObjectView<decltype(file)>::FromExternal(&file))));
    fidl::BindServer(dispatcher_, std::move(file_server), &file_);
  }

  void GetToken(GetTokenCompleter::Sync& completer) final {
    zx::event dup;
    zx_status_t status = token_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
    if (status != ZX_OK) {
      ADD_FAILURE() << "Could not duplicate token handle: " << zx_status_get_string(status);
      completer.Close(ZX_ERR_INTERNAL);
      return;
    }
    completer.Reply(ZX_OK, std::move(dup));
  }

  void Unlink(UnlinkRequestView request, UnlinkCompleter::Sync& completer) final {
    unlinks_.emplace_back(request->name.get());
    completer.ReplySuccess();
  }

  const std::vector<std::string>& unlinks() const { return unlinks_; }

  void Link(LinkRequestView request, LinkCompleter::Sync& completer) final {
    links_.emplace_back(request->src.get(), request->dst.get());
    completer.Reply(ZX_OK);
  }

  const std::vector<std::pair<std::string, std::string>>& links() const { return links_; }

  void Rename(RenameRequestView request, RenameCompleter::Sync& completer) final {
    renames_.emplace_back(request->src.get(), request->dst.get());
    completer.ReplySuccess();
  }

  const std::vector<std::pair<std::string, std::string>>& renames() const { return renames_; }

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  int open_calls_ = 0;
  std::vector<std::string> unlinks_;
  std::vector<std::pair<std::string, std::string>> links_;
  std::vector<std::pair<std::string, std::string>> renames_;
  zx::event token_;
  zxio_tests::TestReadFileServer file_;
};

class Directory : public zxtest::Test {
 public:
  Directory()
      : server_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        directory_server_(server_loop_.dispatcher()) {}

  void SetUp() override {
    zx::status directory_ends = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_OK(directory_ends.status_value());
    auto [directory_client_end, directory_server_end] = std::move(directory_ends.value());

    zx::status node_ends = fidl::CreateEndpoints<fuchsia_io::Node>();
    ASSERT_OK(node_ends.status_value());
    auto [node_client_end, node_server_end] = std::move(node_ends.value());

    zx::event token;
    ASSERT_OK(zx::event::create(0, &token));
    ASSERT_NO_FATAL_FAILURE(directory_server_.Init(std::move(token)));
    fidl::BindServer(server_loop_.dispatcher(), std::move(directory_server_end),
                     &directory_server_);

    server_loop_.StartThread("directory_server_loop");
    server_running_ = true;

    ASSERT_OK(zxio_create(directory_client_end.TakeChannel().release(), &directory_storage_));
  }

  TestDirectoryServer& directory_server() { return directory_server_; }

  zxio_t* directory() { return &directory_storage_.io; }

  void StopServerThread() {
    server_loop_.Shutdown();
    server_running_ = false;
  }

  void TearDown() override {
    if (server_running_) {
      StopServerThread();
    }
  }

 private:
  bool server_running_ = false;
  async::Loop server_loop_;
  TestDirectoryServer directory_server_;
  zxio_storage_t directory_storage_;
};

TEST_F(Directory, Open) {
  fuchsia_io::wire::OpenFlags flags = fuchsia_io::wire::OpenFlags::kRightReadable;
  uint32_t mode = 0u;
  zxio_storage_t file_storage;
  ASSERT_OK(zxio_open(directory(), static_cast<uint32_t>(flags), mode, kTestPath.data(),
                      kTestPath.length(), &file_storage));
  zxio_t* file = &file_storage.io;

  ASSERT_OK(zxio_close(directory()));

  // Sanity check the zxio by reading some test data from the server.
  char buffer[sizeof(zxio_tests::TestReadFileServer::kTestData)];
  size_t actual = 0u;

  ASSERT_OK(zxio_read(file, buffer, sizeof(buffer), 0u, &actual));

  EXPECT_EQ(sizeof(buffer), actual);
  EXPECT_BYTES_EQ(buffer, zxio_tests::TestReadFileServer::kTestData, sizeof(buffer));

  ASSERT_OK(zxio_close(file));
}

TEST_F(Directory, Unlink) {
  constexpr std::string_view name = "full_name";
  ASSERT_OK(zxio_unlink(directory(), name.data(), name.length(), 0));

  // Test that a name length shorter than the null-terminated length of the string is interpreted
  // correctly.
  ASSERT_OK(zxio_unlink(directory(), name.data(), 2, 0));

  ASSERT_OK(zxio_close(directory()));

  StopServerThread();

  const std::vector unlinks = directory_server().unlinks();

  ASSERT_EQ(unlinks.size(), 2);

  EXPECT_EQ(unlinks[0], name);
  EXPECT_EQ(unlinks[1], "fu");
}

TEST_F(Directory, Link) {
  zx_handle_t directory_token;
  ASSERT_OK(zxio_token_get(directory(), &directory_token));

  constexpr std::string_view src = "src";
  constexpr std::string_view dst = "dst";
  ASSERT_OK(
      zxio_link(directory(), src.data(), src.length(), directory_token, dst.data(), dst.length()));

  // Test that a src length shorter than the null-terminated length of the string is interpreted
  // correctly.
  ASSERT_OK(zxio_token_get(directory(), &directory_token));
  ASSERT_OK(zxio_link(directory(), src.data(), 1, directory_token, dst.data(), dst.length()));

  // Test that a dst length shorter than the null-terminated length of the string is interpreted
  // correctly.
  ASSERT_OK(zxio_token_get(directory(), &directory_token));
  ASSERT_OK(zxio_link(directory(), src.data(), src.length(), directory_token, dst.data(), 1));

  ASSERT_OK(zxio_close(directory()));

  StopServerThread();

  const std::vector links = directory_server().links();

  ASSERT_EQ(links.size(), 3);

  // Expecting full source and dest names.
  EXPECT_EQ(links[0].first, "src");
  EXPECT_EQ(links[0].second, "dst");

  // Expecting truncated source name.
  EXPECT_EQ(links[1].first, "s");
  EXPECT_EQ(links[1].second, "dst");

  // Expecting truncated dest name.
  EXPECT_EQ(links[2].first, "src");
  EXPECT_EQ(links[2].second, "d");
}

TEST_F(Directory, Rename) {
  zx_handle_t directory_token;
  ASSERT_OK(zxio_token_get(directory(), &directory_token));

  constexpr std::string_view src = "src";
  constexpr std::string_view dst = "dst";
  ASSERT_OK(zxio_rename(directory(), src.data(), src.length(), directory_token, dst.data(),
                        dst.length()));

  // Test that a src length shorter than the null-terminated length of the string is interpreted
  // correctly.
  ASSERT_OK(zxio_token_get(directory(), &directory_token));
  ASSERT_OK(zxio_rename(directory(), src.data(), 1, directory_token, dst.data(), dst.length()));

  // Test that a dst length shorter than the null-terminated length of the string is interpreted
  // correctly.
  ASSERT_OK(zxio_token_get(directory(), &directory_token));
  ASSERT_OK(zxio_rename(directory(), src.data(), src.length(), directory_token, dst.data(), 1));

  ASSERT_OK(zxio_close(directory()));

  StopServerThread();

  const std::vector renames = directory_server().renames();

  ASSERT_EQ(renames.size(), 3);

  // Expecting full source and dest names.
  EXPECT_EQ(renames[0].first, "src");
  EXPECT_EQ(renames[0].second, "dst");

  // Expecting truncated source name.
  EXPECT_EQ(renames[1].first, "s");
  EXPECT_EQ(renames[1].second, "dst");

  // Expecting truncated dest name.
  EXPECT_EQ(renames[2].first, "src");
  EXPECT_EQ(renames[2].second, "d");
}

}  // namespace
