// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/process/lifecycle/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <memory>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <zxtest/zxtest.h>

#include "fs-manager.h"
#include "fs/synchronous_vfs.h"
#include "fshost-fs-provider.h"
#include "metrics.h"
#include "registry.h"
#include "registry_vnode.h"
#include "src/storage/fshost/block-watcher.h"

namespace devmgr {
namespace {

namespace fio = ::llcpp::fuchsia::io;

std::unique_ptr<cobalt_client::Collector> MakeCollector() {
  return std::make_unique<cobalt_client::Collector>(
      std::make_unique<cobalt_client::InMemoryLogger>());
}

// Test that when no filesystems have been added to the fshost vnode, it
// stays empty.
TEST(VnodeTestCase, NoFilesystems) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  auto fshost_vn = fbl::MakeRefCounted<fshost::RegistryVnode>(loop.dispatcher(), dir);

  fbl::RefPtr<fs::Vnode> node;
  EXPECT_EQ(ZX_ERR_NOT_FOUND, dir->Lookup("0", &node));
}

// Test that when filesystem has been added to the fshost vnode, it appears
// in the supplied remote tracking directory.
TEST(VnodeTestCase, AddFilesystem) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  auto fshost_vn = fbl::MakeRefCounted<fshost::RegistryVnode>(loop.dispatcher(), dir);

  // Adds a new filesystem to the fshost service node.
  // This filesystem should appear as a new entry within |dir|.
  auto endpoints = fidl::CreateEndpoints<::llcpp::fuchsia::io::Directory>();
  ASSERT_OK(endpoints.status_value());

  fidl::UnownedClientEnd<::llcpp::fuchsia::io::Directory> client_value = endpoints->client.borrow();
  ASSERT_OK(fshost_vn->AddFilesystem(std::move(endpoints->client)));
  fbl::RefPtr<fs::Vnode> node;
  ASSERT_OK(dir->Lookup("0", &node));
  EXPECT_EQ(node->GetRemote(), client_value);
}

TEST(VnodeTestCase, AddFilesystemThroughFidl) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  // set up registry service
  auto registry_endpoints = fidl::CreateEndpoints<::llcpp::fuchsia::fshost::Registry>();
  ASSERT_OK(registry_endpoints.status_value());
  auto [registry_client, registry_server] = std::move(registry_endpoints.value());

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  auto fshost_vn = std::make_unique<fshost::RegistryVnode>(loop.dispatcher(), dir);
  auto server_binding =
      fidl::BindServer(loop.dispatcher(), std::move(registry_server), std::move(fshost_vn));
  ASSERT_TRUE(server_binding.is_ok());

  // make a new "vfs" "client" that doesn't really point anywhere.
  auto vfs_endpoints = fidl::CreateEndpoints<::llcpp::fuchsia::io::Directory>();
  ASSERT_OK(vfs_endpoints.status_value());
  auto [vfs_client, vfs_server] = std::move(vfs_endpoints.value());
  zx_info_handle_basic_t vfs_client_info;
  ASSERT_OK(vfs_client.channel().get_info(ZX_INFO_HANDLE_BASIC, &vfs_client_info,
                                          sizeof(vfs_client_info), nullptr, nullptr));

  // register the filesystem through the fidl interface
  auto resp = ::llcpp::fuchsia::fshost::Registry::Call::RegisterFilesystem(registry_client,
                                                                           std::move(vfs_client));
  ASSERT_TRUE(resp.ok());
  ASSERT_OK(resp.value().s);

  // confirm that the filesystem was registered
  fbl::RefPtr<fs::Vnode> node;
  ASSERT_OK(dir->Lookup("0", &node));
  zx_info_handle_basic_t vfs_remote_info;
  auto remote = node->GetRemote();
  ASSERT_OK(remote.channel()->get_info(ZX_INFO_HANDLE_BASIC, &vfs_remote_info,
                                       sizeof(vfs_remote_info), nullptr, nullptr));
  EXPECT_EQ(vfs_remote_info.koid, vfs_client_info.koid);
}

class FakeDriverManagerAdmin final
    : public llcpp::fuchsia::device::manager::Administrator::Interface {
 public:
  void Suspend(uint32_t flags, SuspendCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
  }

  void UnregisterSystemStorageForShutdown(
      UnregisterSystemStorageForShutdownCompleter::Sync& completer) override {
    unregister_was_called_ = true;
    completer.Reply(ZX_OK);
  }

  bool UnregisterWasCalled() { return unregister_was_called_; }

 private:
  std::atomic<bool> unregister_was_called_ = false;
};

// Test that the manager performs the shutdown procedure correctly with respect to externally
// observable behaviors.
TEST(FsManagerTestCase, ShutdownSignalsCompletion) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  FakeDriverManagerAdmin driver_admin;
  auto admin_endpoints = fidl::CreateEndpoints<llcpp::fuchsia::device::manager::Administrator>();
  ASSERT_TRUE(admin_endpoints.is_ok());
  ASSERT_TRUE(fidl::BindServer(loop.dispatcher(), std::move(admin_endpoints->server), &driver_admin)
                  .is_ok());

  zx::channel dir_request, lifecycle_request;
  FsManager manager(nullptr, std::make_unique<FsHostMetrics>(MakeCollector()));
  Config config;
  BlockWatcher watcher(manager, &config);
  ASSERT_OK(manager.Initialize(std::move(dir_request), std::move(lifecycle_request),
                               std::move(admin_endpoints->client), nullptr, watcher));

  // The manager should not have exited yet: No one has asked for the shutdown.
  EXPECT_FALSE(manager.IsShutdown());

  // Once we trigger shutdown, we expect a shutdown signal.
  sync_completion_t callback_called;
  manager.Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_OK(status);
    sync_completion_signal(callback_called);
  });
  manager.WaitForShutdown();
  EXPECT_OK(sync_completion_wait(&callback_called, ZX_TIME_INFINITE));
  EXPECT_TRUE(driver_admin.UnregisterWasCalled());

  // It's an error if shutdown gets called twice, but we expect the callback to still get called
  // with the appropriate error message since the shutdown function has no return value.
  sync_completion_reset(&callback_called);
  manager.Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_EQ(status, ZX_ERR_INTERNAL);
    sync_completion_signal(callback_called);
  });
  EXPECT_OK(sync_completion_wait(&callback_called, ZX_TIME_INFINITE));
}

// Test that the manager shuts down the filesystems given a call on the lifecycle channel
TEST(FsManagerTestCase, LifecycleStop) {
  zx::channel dir_request, lifecycle_request, lifecycle;
  zx_status_t status = zx::channel::create(0, &lifecycle_request, &lifecycle);
  ASSERT_OK(status);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  FakeDriverManagerAdmin driver_admin;
  auto admin_endpoints = fidl::CreateEndpoints<llcpp::fuchsia::device::manager::Administrator>();
  ASSERT_TRUE(admin_endpoints.is_ok());
  ASSERT_TRUE(fidl::BindServer(loop.dispatcher(), std::move(admin_endpoints->server), &driver_admin)
                  .is_ok());

  FsManager manager(nullptr, std::make_unique<FsHostMetrics>(MakeCollector()));
  Config config;
  BlockWatcher watcher(manager, &config);
  ASSERT_OK(manager.Initialize(std::move(dir_request), std::move(lifecycle_request),
                               std::move(admin_endpoints->client), nullptr, watcher));

  // The manager should not have exited yet: No one has asked for an unmount.
  EXPECT_FALSE(manager.IsShutdown());

  // Call stop on the lifecycle channel
  llcpp::fuchsia::process::lifecycle::Lifecycle::SyncClient client(std::move(lifecycle));
  auto result = client.Stop();
  ASSERT_OK(result.status());

  // the lifecycle channel should be closed now
  zx_signals_t pending;
  EXPECT_OK(client.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &pending));
  EXPECT_TRUE(pending & ZX_CHANNEL_PEER_CLOSED);

  // Now we expect a shutdown signal.
  manager.WaitForShutdown();
  EXPECT_TRUE(driver_admin.UnregisterWasCalled());
}

class MockDirectoryAdminOpener : public fio::DirectoryAdmin::Interface {
 public:
  void Open(uint32_t flags, uint32_t mode, fidl::StringView path, fidl::ServerEnd<fio::Node> object,
            OpenCompleter::Sync& completer) override {
    saved_open_flags = flags;
    saved_open_count += 1;
    saved_path = path.get();
  }

  // Below here are a pile of stubs that aren't called in this test. Only Open() above is used.

  // fuchsia.io/Node:
  void Clone(uint32_t flags, fidl::ServerEnd<llcpp::fuchsia::io::Node> object,
             CloneCompleter::Sync& completer) override {}
  void Close(CloseCompleter::Sync& completer) override {}
  void Describe(DescribeCompleter::Sync& completer) override {}
  void Sync(SyncCompleter::Sync& completer) override {}
  void GetAttr(GetAttrCompleter::Sync& completer) override {}
  void SetAttr(uint32_t flags, llcpp::fuchsia::io::NodeAttributes attributes,
               SetAttrCompleter::Sync& completer) override {}
  void NodeGetFlags(NodeGetFlagsCompleter::Sync& completer) override {}
  void NodeSetFlags(uint32_t flags, NodeSetFlagsCompleter::Sync& completer) override {}

  // fuchsia.io/Directory:
  void Unlink(fidl::StringView path, UnlinkCompleter::Sync& completer) override {}
  void ReadDirents(uint64_t max_out, ReadDirentsCompleter::Sync& completer) override {}
  void Rewind(RewindCompleter::Sync& completer) override {}
  void GetToken(GetTokenCompleter::Sync& completer) override {}
  void Rename(fidl::StringView src, zx::handle dst_parent_token, fidl::StringView dst,
              RenameCompleter::Sync& completer) override {}
  void Link(fidl::StringView src, zx::handle dst_parent_token, fidl::StringView dst,
            LinkCompleter::Sync& completer) override {}
  void Watch(uint32_t mask, uint32_t options, zx::channel watcher,
             WatchCompleter::Sync& completer) override {}
  void AddInotifyFilter(llcpp::fuchsia::io2::InotifyWatchMask filters, fidl::StringView path,
                        uint32_t watch_descriptor, zx::socket socket,
                        fidl::ServerEnd<llcpp::fuchsia::io2::Inotifier> controller,
                        AddInotifyFilterCompleter::Sync& completer) override {}

  // fuchsia.io/DirectoryAdmin:
  void Mount(fidl::ClientEnd<llcpp::fuchsia::io::Directory> remote,
             MountCompleter::Sync& completer) override {}
  void MountAndCreate(fidl::ClientEnd<llcpp::fuchsia::io::Directory> remote, fidl::StringView name,
                      uint32_t flags, MountAndCreateCompleter::Sync& completer) override {}
  void Unmount(UnmountCompleter::Sync& completer) override {}
  void UnmountNode(UnmountNodeCompleter::Sync& completer) override {}
  void QueryFilesystem(QueryFilesystemCompleter::Sync& completer) override {}
  void GetDevicePath(GetDevicePathCompleter::Sync& completer) override {}

  // Test fields used for validation.
  uint32_t saved_open_flags = 0;
  uint32_t saved_open_count = 0;
  std::string saved_path;
};

// Test that asking FshostFsProvider for blobexec opens /fs/blob from the
// current installed namespace with the EXEC right
TEST(FshostFsProviderTestCase, CloneBlobExec) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_get_installed(&ns));

  // Mock out an object that implements DirectoryOpen and records some state;
  // bind it to the server handle.  Install it at /fs.
  auto admin = fidl::CreateEndpoints<fio::DirectoryAdmin>();
  ASSERT_OK(admin.status_value());

  auto server_ptr = std::make_unique<MockDirectoryAdminOpener>();
  auto& server = *server_ptr;
  ASSERT_TRUE(
      fidl::BindServer(loop.dispatcher(), std::move(admin->server), std::move(server_ptr)).is_ok());

  fdio_ns_bind(ns, "/fs", admin->client.channel().release());

  // Verify that requesting blobexec gets you the handle at /fs/blob, with the
  // permissions expected.
  FshostFsProvider provider;
  zx::channel blobexec = provider.CloneFs("blobexec");

  // Force a describe call on the target of the Open, to resolve the Open.  We
  // expect this to fail because our mock just closes the channel after Open.
  int fd;
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, fdio_fd_create(blobexec.release(), &fd));

  EXPECT_EQ(1, server.saved_open_count);
  uint32_t expected_flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_EXECUTABLE |
                            ZX_FS_RIGHT_ADMIN | ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_NOREMOTE;
  EXPECT_EQ(expected_flags, server.saved_open_flags);
  EXPECT_STR_EQ("blob", server.saved_path);

  // Tear down.
  ASSERT_OK(fdio_ns_unbind(ns, "/fs"));
}

}  // namespace
}  // namespace devmgr
