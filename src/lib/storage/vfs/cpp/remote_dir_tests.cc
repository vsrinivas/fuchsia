// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/remote_dir.h"

namespace {

TEST(RemoteDir, ApiTest) {
  auto dir_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(ZX_OK, dir_endpoints.status_value());

  fidl::UnownedClientEnd<fuchsia_io::Directory> unowned_client = dir_endpoints->client.borrow();
  auto dir = fbl::MakeRefCounted<fs::RemoteDir>(std::move(dir_endpoints->client));

  // get attributes
  fs::VnodeAttributes attr;
  EXPECT_EQ(ZX_OK, dir->GetAttributes(&attr));
  EXPECT_EQ(V_TYPE_DIR | V_IRUSR, attr.mode);
  EXPECT_EQ(1, attr.link_count);

  // get remote properties
  EXPECT_TRUE(dir->IsRemote());
  EXPECT_EQ(unowned_client, dir->GetRemote());
}

}  // namespace
