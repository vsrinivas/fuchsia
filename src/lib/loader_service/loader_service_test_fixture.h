// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LOADER_SERVICE_LOADER_SERVICE_TEST_FIXTURE_H_
#define SRC_LIB_LOADER_SERVICE_LOADER_SERVICE_TEST_FIXTURE_H_

#include <fuchsia/ldsvc/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/zx/resource.h>

#include <memory>
#include <vector>

#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/loader_service/loader_service.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace loader {
namespace test {

struct TestDirectoryEntry {
  std::string path;
  std::string file_contents;
  bool executable;

  TestDirectoryEntry(std::string path, std::string file_contents, bool executable)
      : path(std::move(path)), file_contents(std::move(file_contents)), executable(executable) {}
};

class LoaderServiceTest : public gtest::RealLoopFixture {
 public:
  LoaderServiceTest()
      : fs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        loader_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  virtual void TearDown() override;

  // Either this or CreateTestDirectory should only be called once per test case. This would be in
  // SetUp but we want to allow test defined directory contents.
  template <typename T>
  void CreateTestLoader(std::vector<TestDirectoryEntry> config, std::shared_ptr<T>* loader) {
    fbl::unique_fd root_fd;
    ASSERT_NO_FATAL_FAILURE(CreateTestDirectory(std::move(config), &root_fd));
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    *loader = T::Create(loader_loop_.dispatcher(), std::move(root_fd), test_info->name());
  }

  // Either this or CreateTestLoader should only be called once per test case. This would be in
  // SetUp but we want to allow test defined directory contents.
  void CreateTestDirectory(std::vector<TestDirectoryEntry> config, fbl::unique_fd* root_fd);

  // Add a directory entry to the given VnodeDir. Can be used to add entries mid-test case using
  // root_dir() below.
  void AddDirectoryEntry(const fbl::RefPtr<memfs::VnodeDir>& root, TestDirectoryEntry entry);

  // Exercise a LoadObject call and assert that the result matches `expected`.
  //
  // This takes a non-const reference because LLCPP SyncClient's generated methods are non-const.
  void LoadObject(llcpp::fuchsia::ldsvc::Loader::SyncClient& client, std::string name,
                  zx::status<std::string> expected);

  // Exercise a Config call and assert that the result matches `expected`.
  //
  // This takes a non-const reference because LLCPP SyncClient's generated methods are non-const.
  void Config(llcpp::fuchsia::ldsvc::Loader::SyncClient& client, std::string config,
              zx::status<zx_status_t> expected);

  // Helper function to interact with fuchsia.security.resource.Vmex
  static zx::status<zx::unowned_resource> GetVmexResource();

  async::Loop& fs_loop() { return fs_loop_; }
  async::Loop& loader_loop() { return loader_loop_; }
  fbl::RefPtr<memfs::VnodeDir>& root_dir() { return root_dir_; }

 private:
  async::Loop fs_loop_;
  async::Loop loader_loop_;
  std::unique_ptr<memfs::Vfs> vfs_;
  fbl::RefPtr<memfs::VnodeDir> root_dir_;
};

}  // namespace test
}  // namespace loader

#endif  // SRC_LIB_LOADER_SERVICE_LOADER_SERVICE_TEST_FIXTURE_H_
