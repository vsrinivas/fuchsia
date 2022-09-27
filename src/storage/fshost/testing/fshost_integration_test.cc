// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/testing/fshost_integration_test.h"

#include <dirent.h>
#include <fidl/fuchsia.component.decl/cpp/wire_types.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/vfs.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/sys/component/cpp/service_client.h>
#include <sys/statfs.h>

namespace fshost::testing {

#ifndef TEST_COMPONENT_NAME
#error Need to specify TEST_COMPONENT_NAME
#endif

static const char kTestFshostName[] = TEST_COMPONENT_NAME;
static const char kTestFshostCollection[] = "fshost-collection";
static const char kTestFshostUrl[] =
    "fuchsia-pkg://fuchsia.com/fshost-tests#meta/" TEST_COMPONENT_NAME ".cm";

static const fuchsia_component_decl::wire::ChildRef kFshostChildRef{
    .name = kTestFshostName, .collection = kTestFshostCollection};

void FshostIntegrationTest::SetUp() {
  auto realm_client_end = component::Connect<fuchsia_component::Realm>();
  ASSERT_EQ(realm_client_end.status_value(), ZX_OK);
  realm_ = fidl::WireSyncClient(std::move(*realm_client_end));

  fidl::Arena allocator;
  auto child_decl = fuchsia_component_decl::wire::Child::Builder(allocator)
                        .name(kTestFshostName)
                        .url(kTestFshostUrl)
                        .startup(fuchsia_component_decl::wire::StartupMode::kLazy)
                        .Build();
  fuchsia_component_decl::wire::CollectionRef collection_ref{.name = kTestFshostCollection};
  fuchsia_component::wire::CreateChildArgs child_args;
  auto create_res = realm_->CreateChild(collection_ref, child_decl, child_args);
  ASSERT_TRUE(create_res.ok() && !create_res->is_error());

  auto exposed_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(exposed_endpoints.status_value(), ZX_OK);
  auto open_res = realm_->OpenExposedDir(kFshostChildRef, std::move(exposed_endpoints->server));
  ASSERT_TRUE(open_res.ok() && !open_res->is_error());
  exposed_dir_ = fidl::WireSyncClient(std::move(exposed_endpoints->client));

  auto watcher_client_end =
      component::ConnectAt<fuchsia_fshost::BlockWatcher>(exposed_dir_.client_end());
  ASSERT_EQ(watcher_client_end.status_value(), ZX_OK);
  block_watcher_ = fidl::WireSyncClient(std::move(*watcher_client_end));
}

void FshostIntegrationTest::TearDown() {
  auto destroy_res = realm_->DestroyChild(kFshostChildRef);
  ASSERT_TRUE(destroy_res.ok() && !destroy_res->is_error());
}

void FshostIntegrationTest::ResetFshost() {
  TearDown();
  SetUp();
}

std::string FshostIntegrationTest::DataFilesystemFormat() {
  if (strlen(DATA_FILESYSTEM_FORMAT) == 0) {
    return "minfs";
  }
  return DATA_FILESYSTEM_FORMAT;
}

std::string FshostIntegrationTest::FshostComponentName() { return kTestFshostName; }

std::string FshostIntegrationTest::FshostComponentCollection() { return kTestFshostCollection; }

void FshostIntegrationTest::PauseWatcher() const {
  auto res = block_watcher_->Pause();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res.value().status, ZX_OK);
}

void FshostIntegrationTest::ResumeWatcher() const {
  auto res = block_watcher_->Resume();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res.value().status, ZX_OK);
}

std::pair<fbl::unique_fd, uint64_t> FshostIntegrationTest::WaitForMount(
    const std::string& name) const {
  // The mount point will always exist so we expect open() to work regardless of whether the device
  // is actually mounted. We retry until the mount point has the expected filesystem type.
  //
  // This can be relatively slow on some bots (especially with asan) because it can involve lots of
  // complex process launching so use a high retry limit.
  constexpr int kMaxRetries = 30;
  for (int i = 0; i < kMaxRetries; i++) {
    auto root_endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
    EXPECT_EQ(root_endpoints.status_value(), ZX_OK);
    auto open_res = exposed_dir()->Open(fuchsia_io::wire::OpenFlags::kRightReadable, 0,
                                        fidl::StringView::FromExternal(name),
                                        std::move(root_endpoints->server));
    EXPECT_EQ(open_res.status(), ZX_OK);
    if (open_res.status() != ZX_OK)
      return std::make_pair(fbl::unique_fd(), 0);

    fbl::unique_fd fd;
    zx_status_t status =
        fdio_fd_create(root_endpoints->client.TakeChannel().release(), fd.reset_and_get_address());
    EXPECT_EQ(ZX_OK, status);
    if (status != ZX_OK)
      return std::make_pair(fbl::unique_fd(), 0);

    struct statfs buf;
    EXPECT_EQ(fstatfs(fd.get(), &buf), 0) << ": " << strerror(errno);
    if (buf.f_type != fuchsia_fs::VfsType::kMemfs)
      return std::make_pair(std::move(fd), buf.f_type);

    sleep(1);
  }

  return std::make_pair(fbl::unique_fd(), 0);
}

inspect::Hierarchy FshostIntegrationTest::TakeSnapshot() const {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("inspect-snapshot-thread");
  async::Executor executor(loop.dispatcher());

  fuchsia::inspect::TreePtr tree;
  async_dispatcher_t* dispatcher = executor.dispatcher();
  zx_status_t status = fdio_service_connect_at(exposed_dir().client_end().handle()->get(),
                                               "diagnostics/fuchsia.inspect.Tree",
                                               tree.NewRequest(dispatcher).TakeChannel().release());
  ZX_ASSERT_MSG(status == ZX_OK, "Failed to connect to inspect service: %s",
                zx_status_get_string(status));

  std::condition_variable cv;
  std::mutex m;
  bool done = false;
  fpromise::result<inspect::Hierarchy> hierarchy_or_error;

  auto promise = inspect::ReadFromTree(std::move(tree))
                     .then([&](fpromise::result<inspect::Hierarchy>& result) {
                       {
                         std::unique_lock<std::mutex> lock(m);
                         hierarchy_or_error = std::move(result);
                         done = true;
                       }
                       cv.notify_all();
                     });

  executor.schedule_task(std::move(promise));

  std::unique_lock<std::mutex> lock(m);
  cv.wait(lock, [&done]() { return done; });

  loop.Quit();
  loop.JoinThreads();

  ZX_ASSERT_MSG(hierarchy_or_error.is_ok(), "Failed to obtain inspect tree snapshot!");
  return hierarchy_or_error.take_value();
}

}  // namespace fshost::testing
