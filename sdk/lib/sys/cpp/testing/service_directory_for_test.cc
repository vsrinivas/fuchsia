// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/sys/cpp/testing/service_directory_for_test.h"

namespace sys {
namespace testing {

ServiceDirectoryForTest::ServiceDirectoryForTest(
    zx::channel directory, std::unique_ptr<vfs::PseudoDir> svc_dir)
    : ServiceDirectory(std::move(directory)), svc_dir_(std::move(svc_dir)) {}

ServiceDirectoryForTest::~ServiceDirectoryForTest() = default;

std::shared_ptr<ServiceDirectoryForTest> ServiceDirectoryForTest::Create(
    async_dispatcher_t* dispatcher) {
  auto svc_dir = std::make_unique<vfs::PseudoDir>();
  fidl::InterfaceHandle<fuchsia::io::Directory> directory_ptr;
  svc_dir->Serve(fuchsia::io::OPEN_RIGHT_READABLE,
                 directory_ptr.NewRequest().TakeChannel(), dispatcher);

  return std::make_shared<ServiceDirectoryForTest>(directory_ptr.TakeChannel(),
                                                   std::move(svc_dir));
}

}  // namespace testing
}  // namespace sys