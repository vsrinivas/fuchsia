// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hwinfo/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <zxtest/zxtest.h>

namespace {

class HwinfoTest : public zxtest::Test {};

// Currently only testing the `cpu_architecture` field because it is the only field we can
// programmatically determine what the expected value should be.

TEST_F(HwinfoTest, BoardInfoTest) {
  fuchsia::hwinfo::BoardInfo info;
  fuchsia::hwinfo::BoardSyncPtr ptr;
  auto context = sys::ComponentContext::Create();

  EXPECT_EQ(ZX_OK, context->svc()->Connect(ptr.NewRequest()));
  EXPECT_EQ(ZX_OK, ptr->GetInfo(&info));

#if defined(__x86_64__)
  EXPECT_EQ(fuchsia::hwinfo::Architecture::X64, info.cpu_architecture());
#elif defined(__aarch64__)
  EXPECT_EQ(fuchsia::hwinfo::Architecture::ARM64, info.cpu_architecture());
#else
  // Unsupported and shouldn't happen. Fail out.
  EXPECT_TRUE(false);
#endif

  // TODO(78784): Expand to check `name` and `revision`.
}

TEST_F(HwinfoTest, ProductInfoTest) {
  fuchsia::hwinfo::ProductInfo info;
  fuchsia::hwinfo::ProductSyncPtr ptr;
  auto context = sys::ComponentContext::Create();

  EXPECT_EQ(ZX_OK, context->svc()->Connect(ptr.NewRequest()));
  EXPECT_EQ(ZX_OK, ptr->GetInfo(&info));

  // TODO(78784): Expand to check `sku`, `language`, `regulatory_domain`, `locale_list`, `name`,
  // `model`, `manufacturer`, and `build_date`.
}

TEST_F(HwinfoTest, DeviceInfoTest) {
  fuchsia::hwinfo::DeviceInfo info;
  fuchsia::hwinfo::DeviceSyncPtr ptr;
  auto context = sys::ComponentContext::Create();

  EXPECT_EQ(ZX_OK, context->svc()->Connect(ptr.NewRequest()));
  EXPECT_EQ(ZX_OK, ptr->GetInfo(&info));

  // TODO(78784): Expand to check `serial_number`.
}

}  // namespace
