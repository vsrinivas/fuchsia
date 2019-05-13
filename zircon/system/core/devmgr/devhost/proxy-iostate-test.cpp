// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <zxtest/zxtest.h>
#include "proxy-iostate.h"
#include "zx-device.h"

TEST(ProxyIostateTestCase, Creation) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    fbl::RefPtr<zx_device> dev;
    ASSERT_OK(zx_device::Create(&dev));

    zx::channel proxy_local, proxy_remote;
    ASSERT_OK(zx::channel::create(0, &proxy_local, &proxy_remote));

    ASSERT_OK(devmgr::ProxyIostate::Create(dev, std::move(proxy_remote), loop.dispatcher()));

    ASSERT_OK(loop.RunUntilIdle());
}


int main(int argc, char** argv) {
    return RUN_ALL_TESTS(argc, argv);
}

