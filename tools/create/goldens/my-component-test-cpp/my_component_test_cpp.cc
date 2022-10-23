// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys2/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/result.h>
#include <zircon/types.h>

namespace my_component_test_cpp {

class MyComponentTestCppIntegrationTest : public testing::Test {
 public:
  void SetUp() {
    // Code here will be called immediately before each test
  }


  void TearDown() {
    // Code here will be called immediately after each test
  }
};

TEST_F(MyComponentTestCppIntegrationTest, TestMethod) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Connect to the component(s) under test using the Realm protocol, e.g.
  // This assumes that the child component exposes the `fuchsia.component.Binder`
  // protocol.
  // If your component exposes another capability, you connect to it directly.
  // ```
  //  fidl::SynchronousInterfacePtr<fuchsia::sys2::Realm> realm;
  //  std::string realm_service = std::string("/svc/")
  //      + fuchsia::sys2::Realm::Name_;
  //  EXPECT_EQ(ZX_OK,
  //          fdio_service_connect(
  //              realm_service.c_str(),
  //              realm.NewRequest().TakeChannel().get()));
  //
  // fidl::SynchronousInterfacePtr<fuchsia::io::Directory> exposed_dir;
  // fuchsia::sys2::Realm_OpenExposedDir_Result result;
  // realm->OpenExposedDir(fuchsia::sys2::ChildRef{.name = "hello-world"},
  //    exposed_dir.NewRequest(), &result);
  // EXPECT_EQ(exposed_dir->Open("fuchsia.component.Binder"), ZX_OK);
  // zx::channel handle, request;
  // EXPECT_EQ(zx::channel::create(0, &handle, &request), ZX_OK);
  // EXPECT_EQ(exposed_dir->Open(fuchsia::io::OpenFlags::DIRECTORY |
  // fuchsia::io::OpenFlags::RIGHT_READABLE,
  //                   fuchsia::io::MODE_TYPE_DIRECTORY, fuchsia::component::Binder::Name_,
  //                   fidl::InterfaceRequest<fuchsia::component::Binder>(std::move(request)))),
  //                   ZX_OK);
  // ```


  // Use the ArchiveReader to access inspect data, e.g.
  // ```
  //   fuchsia::diagnostics::ArchiveAccessorPtr accessor;
  //  std::string archive_service = std::string("/svc/")
  //      + fuchsia::diagnostics::ArchiveAccessor::Name_;
  //  EXPECT_EQ(ZX_OK,
  //          fdio_service_connect(
  //              archive_service.c_str(),
  //              accessor.NewRequest(loop.dispatcher()).TakeChannel().release()));
  //  inspect::contrib::ArchiveReader reader(std::move(accessor),
  //      {"hello-world:root"});
  //
  // reader.SnapshotInspectUntilPresent({kComponentSelector}).then(...);
  // ```


  // Add test conditions here, e.g.
  // ```
  // const std::string expected_string = test_function();
  // ```

  FX_LOGS(DEBUG) << "Initialized.";
  loop.RunUntilIdle();

  // Assert conditions here, e.g.
  // ```
  // ASSERT_EQ!(expected_string, "Hello World!");
  // ```
}

}  // namespace my_component_test_cpp
