// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>
#include <fbl/auto_lock.h>
#include "../client.h"
#include "../controller.h"
namespace display {


TEST(DispTest, NoOpTest) {
    EXPECT_OK(ZX_OK);
}

TEST(DispTest, ClientVSyncOk) {
    zx::channel server_chl, client_chl;
    zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
    EXPECT_OK(status);
    Controller controller(nullptr);
    ClientProxy clientproxy(&controller, false, std::move(server_chl));
    fbl::AutoLock lock(controller.mtx());
    clientproxy.EnableVsync(true);
    status = clientproxy.OnDisplayVsync(0, 0, nullptr, 0);
    EXPECT_OK(status);
    uint32_t num_bytes = 0u;
    uint32_t num_handles = 0u;
    uint8_t data[100];
    status = client_chl.read(0u, data, nullptr, 100, 0, &num_bytes, &num_handles);
    EXPECT_OK(status);

    clientproxy.CloseTest();
}

TEST(DispTest, ClientVSynPeerClosed) {
    zx::channel server_chl, client_chl;
    zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
    EXPECT_OK(status);
    Controller controller(nullptr);
    ClientProxy clientproxy(&controller, false, std::move(server_chl));
    fbl::AutoLock lock(controller.mtx());
    clientproxy.EnableVsync(true);
    client_chl.reset();
    status = clientproxy.OnDisplayVsync(0, 0, nullptr, 0);
    EXPECT_TRUE(status == ZX_ERR_PEER_CLOSED);
    clientproxy.CloseTest();
}

TEST(DispTest, ClientVSyncNotSupported) {
    zx::channel server_chl, client_chl;
    zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
    EXPECT_OK(status);
    Controller controller(nullptr);
    ClientProxy clientproxy(&controller, false, std::move(server_chl));
    fbl::AutoLock lock(controller.mtx());
    status = clientproxy.OnDisplayVsync(0, 0, nullptr, 0);
    EXPECT_TRUE(status == ZX_ERR_NOT_SUPPORTED);
    clientproxy.CloseTest();
}

TEST(DispTest, ClientVSyncWrongContext1) {
    zx::channel server_chl, client_chl;
    zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
    EXPECT_OK(status);
    Controller controller(nullptr);
    ClientProxy clientproxy(&controller, false, std::move(server_chl));
    ASSERT_DEATH([&clientproxy] {
        clientproxy.EnableVsync(true);
    }, "controller_->mtx() not held! \n");
    clientproxy.CloseTest();
}

TEST(DispTest, ClientVSyncWrongContext2) {
    zx::channel server_chl, client_chl;
    zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
    EXPECT_OK(status);
    Controller controller(nullptr);
    ClientProxy clientproxy(&controller, false, std::move(server_chl));
    {
        fbl::AutoLock lock(controller.mtx());
        clientproxy.EnableVsync(true);
    }
    ASSERT_DEATH([&clientproxy] {
        clientproxy.OnDisplayVsync(0, 0, nullptr, 0);
    }, "controller_->mtx() not held! \n");
    clientproxy.CloseTest();
}

#if 0
// This test will cause an OOM which might lead to other tests failing. Enable this test
// locally only
TEST(DispTest, ClientVSyncOom) {
    zx::channel server_chl, client_chl;
    zx_status_t status = zx::channel::create(0, &server_chl, &client_chl);
    EXPECT_OK(status);

    Controller controller(nullptr);
    ClientProxy clientproxy(&controller, false, std::move(server_chl));
    fbl::AutoLock lock(controller.mtx());
    clientproxy.EnableVsync(true);

    status = clientproxy.OnDisplayVsync(0, 0, nullptr, 0);
    while (status != ZX_ERR_NO_MEMORY) {
        status = clientproxy.OnDisplayVsync(0, 0, nullptr, 0);
    }
    EXPECT_TRUE(status == ZX_ERR_NO_MEMORY);

    for (int i = 0; i < 5000; i++) {
        clientproxy.OnDisplayVsync(0, 0, nullptr, 0);
    }

    clientproxy.CloseTest();
}
#endif

} // namespace display
