// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/loader_service/loader_service.h"

#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>

#include <ldmsg/ldmsg.h>

#include "src/lib/loader_service/loader_service_test_fixture.h"

#define ASSERT_OK(expr) ASSERT_EQ(ZX_OK, expr)
#define EXPECT_OK(expr) EXPECT_EQ(ZX_OK, expr)

namespace loader {
namespace test {
namespace {

namespace fldsvc = ::llcpp::fuchsia::ldsvc;

TEST_F(LoaderServiceTest, ConnectBindDone) {
  std::shared_ptr<LoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("libfoo.so", "science", true);
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  {
    auto status = loader->Connect();
    ASSERT_TRUE(status.is_ok());
    fldsvc::Loader::SyncClient client(std::move(status.value()));
    EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("science")));

    // Done should cleanly shutdown connection from server side.
    ASSERT_TRUE(client.Done().ok());
    ASSERT_EQ(client.LoadObject("libfoo.so").status(), ZX_ERR_PEER_CLOSED);
  }

  // Should be able to still make new connections.
  {
    zx::channel client_chan, server_chan;
    ASSERT_OK(zx::channel::create(0, &client_chan, &server_chan));
    auto status = loader->Bind(std::move(server_chan));
    ASSERT_TRUE(status.is_ok());
    fldsvc::Loader::SyncClient client(std::move(client_chan));
    EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("science")));
  }
}

TEST_F(LoaderServiceTest, OpenConnectionsKeepLoaderAlive) {
  fbl::unique_fd root_fd;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("libfoo.so", "science", true);
  ASSERT_NO_FATAL_FAILURE(CreateTestDirectory(std::move(config), &root_fd));

  // Grab the raw zx_handle_t for the root_fd's channel for use below.
  fdio_t* fdio = fdio_unsafe_fd_to_io(root_fd.get());
  zx::unowned_channel fd_channel(fdio_unsafe_borrow_channel(fdio));
  fdio_unsafe_release(fdio);

  const ::testing::TestInfo* const test_info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  auto loader =
      LoaderService::Create(loader_loop().dispatcher(), std::move(root_fd), test_info->name());

  fldsvc::Loader::SyncClient client1, client2;
  {
    auto status = loader->Connect();
    ASSERT_TRUE(status.is_ok());
    client1 = fldsvc::Loader::SyncClient(std::move(status.value()));
  }
  {
    auto status = loader->Connect();
    ASSERT_TRUE(status.is_ok());
    client2 = fldsvc::Loader::SyncClient(std::move(status.value()));
  }

  // Drop our copy of the LoaderService. Open connections should continue working.
  loader.reset();

  // Should still be able to Clone any open connection.
  zx::channel client_chan, server_chan;
  ASSERT_OK(zx::channel::create(0, &client_chan, &server_chan));
  auto result = client2.Clone(std::move(server_chan));
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result.Unwrap()->rv);
  fldsvc::Loader::SyncClient client3(std::move(client_chan));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client1, "libfoo.so", zx::ok("science")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client2, "libfoo.so", zx::ok("science")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client3, "libfoo.so", zx::ok("science")));

  // Note this closes the channels from the client side rather than using Done, which is exercised
  // in another test, since this is closer to real Loader usage.
  client1 = fldsvc::Loader::SyncClient();
  EXPECT_NO_FATAL_FAILURE(LoadObject(client2, "libfoo.so", zx::ok("science")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client3, "libfoo.so", zx::ok("science")));

  // Connection cloned from another should work the same as connections created from LoaderService.
  client2 = fldsvc::Loader::SyncClient();
  EXPECT_NO_FATAL_FAILURE(LoadObject(client3, "libfoo.so", zx::ok("science")));

  // Verify that the directory fd used to create the loader is properly closed once all connections
  // are closed.
  ASSERT_OK(fd_channel->get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
  client3 = fldsvc::Loader::SyncClient();
  // Must shutdown the loader_loop (which joins its thread) to ensure this is not racy. Otherwise
  // the server FIDL bindings may not have handled the client-side channel closure yet.
  loader_loop().Shutdown();
  ASSERT_EQ(ZX_ERR_BAD_HANDLE,
            fd_channel->get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
}

TEST_F(LoaderServiceTest, LoadObject) {
  std::shared_ptr<LoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("libfoo.so", "science", true);
  config.emplace_back("libnoexec.so", "rules", false);
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("science")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libmissing.so", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libnoexec.so", zx::error(ZX_ERR_ACCESS_DENIED)));
}

TEST_F(LoaderServiceTest, Config) {
  std::shared_ptr<LoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("asan/libfoo.so", "black", true);
  config.emplace_back("asan/libasan_only.so", "lives", true);
  config.emplace_back("libfoo.so", "must", true);
  config.emplace_back("libno_san.so", "matter", true);
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("must")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::ok("matter")));

  ASSERT_NO_FATAL_FAILURE(Config(client, "asan", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("black")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::ok("lives")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::ok("matter")));

  ASSERT_NO_FATAL_FAILURE(Config(client, "asan!", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("black")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::ok("lives")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::error(ZX_ERR_NOT_FOUND)));

  ASSERT_NO_FATAL_FAILURE(Config(client, "ubsan", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("must")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::ok("matter")));

  // '!' mid-string should do nothing special, same as non-existing directory
  ASSERT_NO_FATAL_FAILURE(Config(client, "ubsa!n", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("must")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::ok("matter")));

  ASSERT_NO_FATAL_FAILURE(Config(client, "ubsan!", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::error(ZX_ERR_NOT_FOUND)));

  // Config can be reset back to default.
  ASSERT_NO_FATAL_FAILURE(Config(client, "", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("must")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::ok("matter")));
}

// Each new connection to the loader service should act as if Config has not yet been called, even
// if it had been called on the connection it was cloned from.
TEST_F(LoaderServiceTest, ClonedConnectionHasDefaultConfig) {
  std::shared_ptr<LoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("asan/libfoo.so", "black", true);
  config.emplace_back("asan/libasan_only.so", "lives", true);
  config.emplace_back("libno_san.so", "matter", true);
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  ASSERT_NO_FATAL_FAILURE(Config(client, "asan", zx::ok(ZX_OK)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::ok("black")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::ok("lives")));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::ok("matter")));

  zx::channel client_chan, server_chan;
  ASSERT_OK(zx::channel::create(0, &client_chan, &server_chan));
  auto result = client.Clone(std::move(server_chan));
  ASSERT_TRUE(result.ok());
  ASSERT_OK(result.Unwrap()->rv);
  {
    fldsvc::Loader::SyncClient client(std::move(client_chan));
    EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libfoo.so", zx::error(ZX_ERR_NOT_FOUND)));
    EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libasan_only.so", zx::error(ZX_ERR_NOT_FOUND)));
    EXPECT_NO_FATAL_FAILURE(LoadObject(client, "libno_san.so", zx::ok("matter")));
  }
}

TEST_F(LoaderServiceTest, InvalidLoadObject) {
  std::shared_ptr<LoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("libfoo.so", "science", true);
  config.emplace_back("asan/libfoo.so", "rules", true);
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "/", zx::error(ZX_ERR_NOT_FILE)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "..", zx::error(ZX_ERR_INVALID_ARGS)));
  EXPECT_NO_FATAL_FAILURE(LoadObject(client, "asan", zx::error(ZX_ERR_NOT_FILE)));
}

TEST_F(LoaderServiceTest, InvalidConfig) {
  std::shared_ptr<LoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  fldsvc::Loader::SyncClient client(std::move(status.value()));

  EXPECT_NO_FATAL_FAILURE(Config(client, "!", zx::ok(ZX_ERR_INVALID_ARGS)));
  EXPECT_NO_FATAL_FAILURE(Config(client, "/", zx::ok(ZX_ERR_INVALID_ARGS)));
  EXPECT_NO_FATAL_FAILURE(Config(client, "foo/", zx::ok(ZX_ERR_INVALID_ARGS)));
  EXPECT_NO_FATAL_FAILURE(Config(client, "foo/bar", zx::ok(ZX_ERR_INVALID_ARGS)));
}

// fuchsia.ldsvc.Loader is manually implemented in //zircon/system/ulib/ldmsg, and this
// implementation is the one used by our musl-based ld.so dynamic linker/loader. In other words,
// that implementation is used to send most Loader client requests. Test interop with it.
void LoadObjectLdmsg(const zx::channel& client, const char* object_name, zx::status<> expected) {
  size_t req_len;
  ldmsg_req_t req = {};
  req.header.ordinal = LDMSG_OP_LOAD_OBJECT;
  req.header.magic_number = kFidlWireFormatMagicNumberInitial;
  zx_status_t status = ldmsg_req_encode(&req, &req_len, object_name, strlen(object_name));
  ASSERT_OK(status);

  ldmsg_rsp_t rsp = {};
  zx::vmo result;
  zx_channel_call_args_t call = {
      .wr_bytes = &req,
      .wr_handles = nullptr,
      .rd_bytes = &rsp,
      .rd_handles = result.reset_and_get_address(),
      .wr_num_bytes = static_cast<uint32_t>(req_len),
      .wr_num_handles = 0,
      .rd_num_bytes = sizeof(rsp),
      .rd_num_handles = 1,
  };

  uint32_t actual_bytes, actual_handles;
  status = client.call(0, zx::time::infinite(), &call, &actual_bytes, &actual_handles);
  ASSERT_OK(status);
  ASSERT_EQ(actual_bytes, ldmsg_rsp_get_size(&rsp));
  ASSERT_EQ(rsp.header.ordinal, req.header.ordinal);

  EXPECT_EQ(rsp.rv, expected.status_value());
  EXPECT_EQ(result.is_valid(), expected.is_ok());
}

TEST_F(LoaderServiceTest, InteropWithLdmsg_LoadObject) {
  std::shared_ptr<LoaderService> loader;
  std::vector<TestDirectoryEntry> config;
  config.emplace_back("libfoo.so", "science", true);
  config.emplace_back("libnoexec.so", "rules", false);
  ASSERT_NO_FATAL_FAILURE(CreateTestLoader(std::move(config), &loader));

  auto status = loader->Connect();
  ASSERT_TRUE(status.is_ok());
  zx::channel client = std::move(status).value();

  EXPECT_NO_FATAL_FAILURE(LoadObjectLdmsg(client, "libfoo.so", zx::ok()));
  EXPECT_NO_FATAL_FAILURE(LoadObjectLdmsg(client, "libmissing.so", zx::error(ZX_ERR_NOT_FOUND)));
  EXPECT_NO_FATAL_FAILURE(LoadObjectLdmsg(client, "libnoexec.so", zx::error(ZX_ERR_ACCESS_DENIED)));
}

}  // namespace
}  // namespace test
}  // namespace loader
