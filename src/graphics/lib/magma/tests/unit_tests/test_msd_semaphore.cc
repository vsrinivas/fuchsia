// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "msd.h"
#include "platform_semaphore.h"

TEST(MsdSemaphore, ImportAndDestroy) {
  auto semaphore = magma::PlatformSemaphore::Create();
  ASSERT_NE(semaphore, nullptr);

  uint32_t duplicate_handle;
  ASSERT_TRUE(semaphore->duplicate_handle(&duplicate_handle));

  msd_semaphore_t* abi_sem = nullptr;
  EXPECT_EQ(MAGMA_STATUS_OK, msd_semaphore_import(duplicate_handle, &abi_sem));

  ASSERT_NE(abi_sem, nullptr);

  msd_semaphore_release(abi_sem);
}
