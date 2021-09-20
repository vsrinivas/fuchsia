// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/fshost_integration_test.h"

#include <sys/statfs.h>

namespace devmgr {

void FshostIntegrationTest::SetUp() {
  fidl::SynchronousInterfacePtr<fuchsia::sys2::Realm> realm;
  std::string service_name = std::string("/svc/") + fuchsia::sys2::Realm::Name_;
  auto status = zx::make_status(
      fdio_service_connect(service_name.c_str(), realm.NewRequest().TakeChannel().get()));
  ASSERT_TRUE(status.is_ok());
  fuchsia::sys2::Realm_BindChild_Result result;
  status = zx::make_status(realm->BindChild(fuchsia::sys2::ChildRef{.name = "test-fshost"},
                                            exposed_dir_.NewRequest(), &result));
  ASSERT_TRUE(status.is_ok() && !result.is_err());

  // Describe so that we discover errors early.
  fuchsia::io::NodeInfo info;
  ASSERT_EQ(exposed_dir_->Describe(&info), ZX_OK);

  zx::channel request;
  status = zx::make_status(zx::channel::create(0, &watcher_channel_, &request));
  ASSERT_EQ(status.status_value(), ZX_OK);
  status = zx::make_status(
      exposed_dir_->Open(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE, 0,
                         llcpp::fuchsia::fshost::BlockWatcher::Name,
                         fidl::InterfaceRequest<fuchsia::io::Node>(std::move(request))));
  ASSERT_EQ(status.status_value(), ZX_OK);
}

void FshostIntegrationTest::PauseWatcher() const {
  auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Pause(watcher_channel_.borrow());
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result->status, ZX_OK);
}

void FshostIntegrationTest::ResumeWatcher() const {
  auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Resume(watcher_channel_.borrow());
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result->status, ZX_OK);
}

fbl::unique_fd FshostIntegrationTest::WaitForMount(const std::string& name,
                                                   uint64_t expected_fs_type) {
  // The mount point will always exist so we expect open() to work regardless of whether the device
  // is actually mounted. We retry until the mount point has the expected filesystem type. Retry 10
  // times and then give up.
  for (int i = 0; i < 10; i++) {
    fidl::SynchronousInterfacePtr<fuchsia::io::Node> root;
    zx_status_t status =
        exposed_dir()->Open(fuchsia::io::OPEN_RIGHT_READABLE, 0, name, root.NewRequest());
    EXPECT_EQ(ZX_OK, status);
    if (status != ZX_OK)
      return fbl::unique_fd();

    fbl::unique_fd fd;
    status = fdio_fd_create(root.Unbind().TakeChannel().release(), fd.reset_and_get_address());
    EXPECT_EQ(ZX_OK, status);
    if (status != ZX_OK)
      return fbl::unique_fd();

    struct statfs buf;
    EXPECT_EQ(fstatfs(fd.get(), &buf), 0);
    if (buf.f_type == expected_fs_type)
      return fd;

    sleep(1);
  }

  return fbl::unique_fd();
}

}  // namespace devmgr
