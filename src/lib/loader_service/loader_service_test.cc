// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/loader_service/loader_service.h"

#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <fuchsia/security/resource/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/zx/channel.h>
#include <lib/zx/object.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/limits.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>
#include <ldmsg/ldmsg.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace loader {
namespace {

namespace fldsvc = ::llcpp::fuchsia::ldsvc;
namespace fsec = ::llcpp::fuchsia::security::resource;

#define ASSERT_OK(expr) ASSERT_EQ(ZX_OK, expr)
#define EXPECT_OK(expr) EXPECT_EQ(ZX_OK, expr)

struct TestDirectoryEntry {
  std::string path;
  std::string file_contents;
  bool executable;

  TestDirectoryEntry(std::string path, std::string file_contents, bool executable)
      : path(std::move(path)), file_contents(std::move(file_contents)), executable(executable) {}
};

zx::status<zx::unowned_resource> GetVmexResource() {
  static const std::string kVmexResourcePath = "/svc/" + std::string(fsec::Vmex::Name);

  static zx::resource vmex_resource;
  if (!vmex_resource.is_valid()) {
    zx::channel client, server;
    auto status = zx::make_status(zx::channel::create(0, &client, &server));
    if (status.is_error()) {
      return status.take_error();
    }
    status = zx::make_status(fdio_service_connect(kVmexResourcePath.c_str(), server.release()));
    if (status.is_error()) {
      return status.take_error();
    }

    auto result = fsec::Vmex::Call::Get(client.borrow());
    if (!result.ok()) {
      return zx::error(result.status());
    }
    vmex_resource = std::move(result.Unwrap()->vmex);
  }
  return zx::ok(vmex_resource.borrow());
}

void AddDirectoryEntry(const fbl::RefPtr<memfs::VnodeDir>& root, TestDirectoryEntry entry) {
  ASSERT_FALSE(entry.path.empty() || entry.path.front() == '/' || entry.path.back() == '/');

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(entry.file_contents.data(), 0, entry.file_contents.size()));
  if (entry.executable) {
    auto vmex_rsrc = GetVmexResource();
    ASSERT_OK(vmex_rsrc.status_value());
    ASSERT_TRUE(vmex_rsrc.value()->is_valid());
    ASSERT_OK(vmo.replace_as_executable(*vmex_rsrc.value(), &vmo));
  }

  fbl::RefPtr<memfs::VnodeDir> dir(root);
  std::string_view view(entry.path);
  while (true) {
    size_t next = view.find('/');
    if (next == std::string_view::npos) {
      // No more subdirectories; create vnode for vmo, then done!
      ASSERT_FALSE(view.empty());
      ASSERT_OK(dir->CreateFromVmo(view, vmo.release(), 0, entry.file_contents.size()));
      return;
    } else {
      // Create subdirectory if it doesn't already exist.
      std::string_view subdir(view.substr(0, next));
      ASSERT_FALSE(subdir.empty());

      fbl::RefPtr<fs::Vnode> out;
      zx_status_t status = dir->Lookup(&out, subdir);
      if (status == ZX_ERR_NOT_FOUND) {
        status = dir->Create(&out, subdir, S_IFDIR);
      }
      ASSERT_OK(status);

      dir = fbl::RefPtr<memfs::VnodeDir>::Downcast(std::move(out));
      view.remove_prefix(next + 1);
    }
  }
}

template <typename T>
class LoaderServiceTestTemplate : public gtest::RealLoopFixture {
 public:
  LoaderServiceTestTemplate()
      : fs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        loader_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  // This should only be called once per test case. This would be in SetUp but we want to allow test
  // defined directory contents.
  void CreateTestLoader(std::vector<TestDirectoryEntry> config, std::shared_ptr<T>* loader) {
    fbl::unique_fd root_fd;
    ASSERT_NO_FATAL_FAILURE(CreateTestDirectory(std::move(config), &root_fd));
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    *loader = T::Create(loader_loop_.dispatcher(), std::move(root_fd), test_info->name());
  }

  // This should only be called once per test case. This would be in SetUp but we want to allow test
  // defined directory contents.
  void CreateTestDirectory(std::vector<TestDirectoryEntry> config, fbl::unique_fd* root_fd) {
    ASSERT_FALSE(vfs_);
    ASSERT_FALSE(root_dir_);

    ASSERT_OK(memfs::Vfs::Create("<tmp>", &vfs_, &root_dir_));
    vfs_->SetDispatcher(fs_loop_.dispatcher());

    for (auto entry : config) {
      ASSERT_NO_FATAL_FAILURE(AddDirectoryEntry(root_dir_, entry));
    }

    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    ASSERT_OK(vfs_->ServeDirectory(fbl::RefPtr(root_dir_), std::move(server)));

    // Must start fs_loop before fdio_fd_create, since that will attempt to Describe the directory.
    ASSERT_OK(fs_loop_.StartThread("fs_loop"));
    ASSERT_OK(fdio_fd_create(client.release(), root_fd->reset_and_get_address()));

    // The loader needs a separate thread from the FS because it uses synchronous fd-based I/O.
    ASSERT_OK(loader_loop_.StartThread("loader_loop"));
  }

  virtual void TearDown() override {
    if (vfs_) {
      std::optional<zx_status_t> shutdown_status;
      vfs_->Shutdown([&](zx_status_t status) { shutdown_status = status; });
      RunLoopUntil([&]() { return shutdown_status.has_value(); });
      ASSERT_OK(shutdown_status.value());
    }
  }

  async::Loop& loader_loop() { return loader_loop_; }
  fbl::RefPtr<memfs::VnodeDir>& root_dir() { return root_dir_; }

 private:
  async::Loop fs_loop_;
  async::Loop loader_loop_;
  std::unique_ptr<memfs::Vfs> vfs_;
  fbl::RefPtr<memfs::VnodeDir> root_dir_;
};

using LoaderServiceTest = LoaderServiceTestTemplate<LoaderService>;

zx_rights_t get_rights(const zx::object_base& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return status == ZX_OK ? info.rights : ZX_RIGHT_NONE;
}

// This takes a non-const reference because LLCPP SyncClient's generated methods are non-const.
void LoadObject(fldsvc::Loader::SyncClient& client, std::string name,
                zx::status<std::string> contents) {
  auto result = client.LoadObject(fidl::unowned_str(name));
  ASSERT_TRUE(result.ok());
  auto response = result.Unwrap();
  ASSERT_EQ(contents.status_value(), response->rv);

  zx::vmo vmo = std::move(response->object);
  if (contents.is_error()) {
    ASSERT_FALSE(vmo.is_valid());
  } else {
    ASSERT_TRUE(vmo.is_valid());
    ASSERT_EQ(get_rights(vmo) & ZX_RIGHT_EXECUTE, ZX_RIGHT_EXECUTE);

    char data[ZX_PAGE_SIZE] = {};
    ASSERT_OK(vmo.read(data, 0, ZX_PAGE_SIZE));
    ASSERT_EQ(std::string(data), contents.value());
  }
}

// This takes a non-const reference because LLCPP SyncClient's generated methods are non-const.
void Config(fldsvc::Loader::SyncClient& client, std::string config,
            zx::status<zx_status_t> expected) {
  auto result = client.Config(fidl::StringView(fidl::unowned_ptr(config.data()), config.size()));
  ASSERT_EQ(result.status(), expected.status_value());
  if (expected.is_ok()) {
    ASSERT_EQ(result.Unwrap()->rv, expected.value());
  }
}

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
}  // namespace loader
