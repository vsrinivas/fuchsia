// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>
#include <zircon/syscalls/clock.h>

#include <zxtest/zxtest.h>

namespace {

TEST(KernelClocksTestCase, Create) {
  zx::clock the_clock;

  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, zx::clock::create(0, nullptr, &the_clock));
}

TEST(KernelClocksTestCase, Read) {
  zx::clock the_clock;
  zx_time_t now;

  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, the_clock.read(&now));
}

TEST(KernelClocksTestCase, GetDetails) {
  zx::clock the_clock;
  zx_clock_details_v1_t details;

  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, the_clock.get_details(&details));
}

TEST(KernelClocksTestCase, Update) {
  zx::clock the_clock;
  zx::clock::update_args args;
  args.set_value(zx::time(0));

  ASSERT_STATUS(ZX_ERR_NOT_SUPPORTED, the_clock.update(args));
}

}  // namespace
