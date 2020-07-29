// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "proxy_iostate.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "driver_host_context.h"
#include "zx_device.h"

namespace {

TEST(ProxyIostateTestCase, Creation) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);
  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  zx::channel proxy_local, proxy_remote;
  ASSERT_OK(zx::channel::create(0, &proxy_local, &proxy_remote));

  {
    fbl::AutoLock guard(&dev->proxy_ios_lock);
    ASSERT_NULL(dev->proxy_ios);
  }
  ASSERT_OK(ProxyIostate::Create(dev, std::move(proxy_remote), ctx.loop().dispatcher()));
  {
    fbl::AutoLock guard(&dev->proxy_ios_lock);
    ASSERT_NOT_NULL(dev->proxy_ios);
  }

  ctx.ProxyIosDestroy(dev);
  ASSERT_OK(ctx.loop().RunUntilIdle());
  dev->vnode.reset();
}

// This test reproduces the bug from ZX-4060, in which we would double-free the
// ProxyIostate due to a cancellation being queued after a channel close event
// gets queued, but before the channel close is processed.  If the bug is
// present, and we're running with ASAN, this will crash 100% of the time.
TEST(ProxyIostateTestCase, ChannelCloseThenCancel) {
  DriverHostContext ctx(&kAsyncLoopConfigNoAttachToCurrentThread);
  fbl::RefPtr<zx_driver> drv;
  ASSERT_OK(zx_driver::Create("test", ctx.inspect().drivers(), &drv));

  fbl::RefPtr<zx_device> dev;
  ASSERT_OK(zx_device::Create(&ctx, "test", drv.get(), &dev));

  zx::channel proxy_local, proxy_remote;
  ASSERT_OK(zx::channel::create(0, &proxy_local, &proxy_remote));

  ASSERT_OK(ProxyIostate::Create(dev, std::move(proxy_remote), ctx.loop().dispatcher()));
  ASSERT_OK(ctx.loop().RunUntilIdle());

  proxy_local.reset();

  {
    fbl::AutoLock guard(&dev->proxy_ios_lock);
    dev->proxy_ios->CancelLocked(ctx.loop().dispatcher());
    ASSERT_NULL(dev->proxy_ios);
  }

  ASSERT_OK(ctx.loop().RunUntilIdle());
  dev->vnode.reset();
}

}  // namespace
