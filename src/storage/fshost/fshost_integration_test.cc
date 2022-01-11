// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/fshost_integration_test.h"

#include <lib/service/llcpp/service.h>
#include <sys/statfs.h>
#include <zircon/device/vfs.h>

namespace fshost {

static const char kTestFshostName[] = "test-fshost";
static const char kTestFshostCollection[] = "fshost-collection";
static const char kTestFshostUrl[] = "fuchsia-pkg://fuchsia.com/fshost-tests#meta/test-fshost.cm";

static const fuchsia_component_decl::wire::ChildRef kFshostChildRef{
    .name = kTestFshostName, .collection = kTestFshostCollection};

void FshostIntegrationTest::SetUp() {
  auto realm_client_end = service::Connect<fuchsia_component::Realm>();
  ASSERT_EQ(realm_client_end.status_value(), ZX_OK);
  realm_ = fidl::BindSyncClient(std::move(*realm_client_end));

  fidl::Arena allocator;
  fuchsia_component_decl::wire::CollectionRef collection_ref{.name = kTestFshostCollection};
  fuchsia_component_decl::wire::Child child_decl(allocator);
  child_decl.set_name(allocator, allocator, kTestFshostName)
      .set_url(allocator, allocator, kTestFshostUrl)
      .set_startup(fuchsia_component_decl::wire::StartupMode::kLazy);
  fuchsia_component::wire::CreateChildArgs child_args;
  auto create_res = realm_->CreateChild(collection_ref, child_decl, child_args);
  ASSERT_TRUE(create_res.ok() && !create_res->result.is_err());

  auto exposed_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(exposed_endpoints.status_value(), ZX_OK);
  auto open_res = realm_->OpenExposedDir(kFshostChildRef, std::move(exposed_endpoints->server));
  ASSERT_TRUE(open_res.ok() && !open_res->result.is_err());
  exposed_dir_ = fidl::BindSyncClient(std::move(exposed_endpoints->client));

  auto watcher_client_end =
      service::ConnectAt<fuchsia_fshost::BlockWatcher>(exposed_dir_.client_end());
  ASSERT_EQ(watcher_client_end.status_value(), ZX_OK);
  block_watcher_ = fidl::BindSyncClient(std::move(*watcher_client_end));
}

void FshostIntegrationTest::TearDown() {
  auto destroy_res = realm_->DestroyChild(kFshostChildRef);
  ASSERT_TRUE(destroy_res.ok() && !destroy_res->result.is_err());
}

void FshostIntegrationTest::PauseWatcher() const {
  auto res = block_watcher_->Pause();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res->status, ZX_OK);
}

void FshostIntegrationTest::ResumeWatcher() const {
  auto res = block_watcher_->Resume();
  ASSERT_EQ(res.status(), ZX_OK);
  ASSERT_EQ(res->status, ZX_OK);
}

std::pair<fbl::unique_fd, uint64_t> FshostIntegrationTest::WaitForMount(const std::string& name) {
  // The mount point will always exist so we expect open() to work regardless of whether the device
  // is actually mounted. We retry until the mount point has the expected filesystem type.
  //
  // This can be relatively slow on some bots (especially with asan) because it can involve lots of
  // complex process launching so use a high retry limit.
  constexpr int kMaxRetries = 30;
  for (int i = 0; i < kMaxRetries; i++) {
    auto root_endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
    EXPECT_EQ(root_endpoints.status_value(), ZX_OK);
    auto open_res = exposed_dir()->Open(fuchsia_io::wire::kOpenRightReadable, 0,
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
    if (buf.f_type != VFS_TYPE_MEMFS)
      return std::make_pair(std::move(fd), buf.f_type);

    sleep(1);
  }

  return std::make_pair(fbl::unique_fd(), 0);
}

}  // namespace fshost
