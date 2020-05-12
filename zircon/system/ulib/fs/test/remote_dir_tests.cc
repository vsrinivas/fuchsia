// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <fs/remote_dir.h>
#include <zxtest/zxtest.h>

namespace {

TEST(RemoteDir, ApiTest) {
  zx::channel server, client;
  ASSERT_EQ(ZX_OK, zx::channel::create(0u, &server, &client));

  zx_handle_t client_handle = client.get();
  auto dir = fbl::AdoptRef<fs::RemoteDir>(new fs::RemoteDir(std::move(client)));

  // get attributes
  fs::VnodeAttributes attr;
  EXPECT_EQ(ZX_OK, dir->GetAttributes(&attr));
  EXPECT_EQ(V_TYPE_DIR | V_IRUSR, attr.mode);
  EXPECT_EQ(1, attr.link_count);

  // get remote properties
  EXPECT_TRUE(dir->IsRemote());
  EXPECT_EQ(client_handle, dir->GetRemote());

  // detaching the remote mount isn't allowed
  EXPECT_TRUE(!dir->DetachRemote());
}

}  // namespace
