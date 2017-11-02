// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/remote-dir.h>

#include <unittest/unittest.h>

namespace {

bool test_remote_dir() {
    BEGIN_TEST;

    zx::channel server, client;
    ASSERT_EQ(ZX_OK, zx::channel::create(0u, &server, &client));

    zx_handle_t client_handle = client.get();
    auto dir = fbl::AdoptRef<fs::RemoteDir>(new fs::RemoteDir(fbl::move(client)));

    // get attributes
    vnattr_t attr;
    EXPECT_EQ(ZX_OK, dir->Getattr(&attr));
    EXPECT_EQ(V_TYPE_DIR | V_IRUSR, attr.mode);
    EXPECT_EQ(1, attr.nlink);

    // get remote properties
    EXPECT_TRUE(dir->IsRemote());
    EXPECT_EQ(client_handle, dir->GetRemote());

    // detaching the remote mount isn't allowed
    EXPECT_TRUE(!dir->DetachRemote());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(remote_dir_tests)
RUN_TEST(test_remote_dir)
END_TEST_CASE(remote_dir_tests)
