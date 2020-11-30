// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/cpp/fidl.h>
#include <fuchsia/ldsvc/cpp/fidl.h>
#include <fuchsia/process/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <zircon/errors.h>

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/sys/appmgr/integration_tests/sandbox/namespace_test.h"

TEST_F(NamespaceTest, SomeServices) {
  // Only allowlisted service is available.
  fuchsia::sys::LoaderSyncPtr loader;
  fuchsia::process::ResolverSyncPtr resolver;
  ConnectToService(loader.NewRequest());
  ConnectToService(resolver.NewRequest());
  RunLoopUntilIdle();

  fuchsia::sys::PackagePtr pkg;
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, loader->LoadUrl("some-url", &pkg));

  zx_status_t status;
  zx::vmo exe;
  fidl::InterfaceHandle<fuchsia::ldsvc::Loader> svc;
  EXPECT_EQ(ZX_OK, resolver->Resolve("some_url", &status, &exe, &svc));

  // readdir should list services in sandbox.
  std::vector<std::string> files;
  ASSERT_TRUE(files::ReadDirContents("/svc", &files));

  // Remove debug service if present due variant build.
  files.erase(std::remove(files.begin(), files.end(), fuchsia::debugdata::DebugData::Name_),
              files.end());

  EXPECT_THAT(
      files, ::testing::UnorderedElementsAre(".", "fuchsia.sys.Environment",
                                             "fuchsia.process.Resolver", "fuchsia.logger.LogSink"));
}
