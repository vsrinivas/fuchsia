// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_FIXTURE_H_
#define SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_FIXTURE_H_

#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fdio/directory.h>

namespace devmgr {

class FshostIntegrationTest : public testing::Test {
 public:
  void SetUp() override {
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

  const fidl::SynchronousInterfacePtr<fuchsia::io::Directory>& exposed_dir() const {
    return exposed_dir_;
  }

  const zx::channel& watcher_channel() const { return watcher_channel_; }

  void PauseWatcher() const {
    auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Pause(watcher_channel_.borrow());
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->status, ZX_OK);
  }

  void ResumeWatcher() const {
    auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Resume(watcher_channel_.borrow());
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->status, ZX_OK);
  }

 private:
  fidl::SynchronousInterfacePtr<fuchsia::io::Directory> exposed_dir_;
  zx::channel watcher_channel_;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_FSHOST_INTEGRATION_TEST_FIXTURE_H_
