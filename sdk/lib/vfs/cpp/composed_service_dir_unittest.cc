// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <test/placeholders/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/composed_service_dir.h>
#include <lib/vfs/cpp/service.h>
#include <lib/vfs/cpp/testing/dir_test_util.h>

#include <gtest/gtest.h>

namespace {

using vfs_tests::Dirent;

class EchoImpl : public test::placeholders::Echo {
 public:
  EchoImpl(std::string ans) : ans_(ans) {}

  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override { callback(ans_); }

 private:
  std::string ans_;
};

class ComposedServiceDirectorySimpleTest : public vfs_tests::DirConnection {
 public:
  vfs::internal::Directory* GetDirectoryNode() override { return &dir_; }

  ComposedServiceDirectorySimpleTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    dir_.AddService("echo1", GetFakeService("echo1", loop_.dispatcher()));
    dir_.AddService("echo2", GetFakeService("echo2", loop_.dispatcher()));
    dir_.AddService("echo3", GetFakeService("echo3", loop_.dispatcher()));
    backing_dir_.AddEntry("echo1", GetFakeService("fallback_echo1", loop_.dispatcher()));
    backing_dir_.AddEntry("echo4", GetFakeService("fallback_echo4", loop_.dispatcher()));
    backing_dir_.AddEntry("echo5", GetFakeService("fallback_echo5", loop_.dispatcher()));

    dir_.set_fallback(GetConnection(&backing_dir_));
    loop_.StartThread("vfs test thread");
  }

  fuchsia::io::DirectoryPtr GetConnection(vfs::internal::Directory* dir) {
    fuchsia::io::DirectoryPtr ptr;
    dir->Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
               ptr.NewRequest().TakeChannel(), loop_.dispatcher());
    return ptr;
  }

  fuchsia::io::DirectorySyncPtr GetSyncConnection(vfs::internal::Directory* dir) {
    fuchsia::io::DirectorySyncPtr ptr;
    dir->Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
               ptr.NewRequest().TakeChannel(), loop_.dispatcher());
    return ptr;
  }

 protected:
  vfs::PseudoDir backing_dir_;
  vfs::ComposedServiceDir dir_;
  std::vector<std::unique_ptr<EchoImpl>> echo_impls_;
  fidl::BindingSet<test::placeholders::Echo> bindings_;
  async::Loop loop_;

  std::unique_ptr<vfs::Service> GetFakeService(const std::string& ans,
                                               async_dispatcher_t* dispatcher) {
    auto echo = std::make_unique<EchoImpl>(ans);
    auto service = std::make_unique<vfs::Service>(bindings_.GetHandler(echo.get(), dispatcher));
    echo_impls_.push_back(std::move(echo));
    return service;
  }
};

TEST_F(ComposedServiceDirectorySimpleTest, ConnectToServices) {
  auto dir_connection = GetConnection(&dir_);
  sys::ServiceDirectory service_directory(std::move(dir_connection));
  std::string services[] = {"echo1", "echo2", "echo3"};

  // connect to services and test
  for (auto& s : services) {
    test::placeholders::EchoSyncPtr ptr;
    service_directory.Connect(s, ptr.NewRequest().TakeChannel());
    fidl::StringPtr ans;
    ptr->EchoString("hello", &ans);
    ASSERT_TRUE(ans.has_value());
    EXPECT_EQ(s, ans.value());
  }

  std::string fallback_services[] = {"echo4", "echo5"};

  for (auto& s : fallback_services) {
    test::placeholders::EchoSyncPtr ptr;
    service_directory.Connect(s, ptr.NewRequest().TakeChannel());
    fidl::StringPtr ans;
    ptr->EchoString("hello", &ans);
    ASSERT_TRUE(ans.has_value());
    EXPECT_EQ("fallback_" + s, ans.value());
  }
}

TEST_F(ComposedServiceDirectorySimpleTest, ReadDir) {
  auto ptr = GetSyncConnection(&dir_);

  // Since directory entries "echo4" and "echo5" are in the fallback dir,
  // they are incorrectly excluded here due to a bug.  See fxbug.dev/55769.
  std::vector<Dirent> expected_dirents = {
      Dirent::DirentForDot(),
      Dirent::DirentForService("echo1"),
      Dirent::DirentForService("echo2"),
      Dirent::DirentForService("echo3"),
  };
  AssertReadDirents(ptr, 1024, expected_dirents);
}

}  // namespace
