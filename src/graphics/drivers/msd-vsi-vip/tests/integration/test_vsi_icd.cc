// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"

TEST(TestIcd, IcdList) {
  magma::TestDeviceBase test_device(MAGMA_VENDOR_ID_VSI);

  auto rsp =
      fidl::WireCall<fuchsia_gpu_magma::IcdLoaderDevice>(test_device.channel())->GetIcdList();
  EXPECT_TRUE(rsp.ok());
  EXPECT_EQ(rsp.value().icd_list.count(), 2u);

  const auto& icd_item = rsp.value().icd_list[0];

  EXPECT_TRUE(icd_item.has_flags());
  const auto& flags = icd_item.flags();
  EXPECT_TRUE(flags & fuchsia_gpu_magma::wire::IcdFlags::kSupportsOpencl);

  std::string res_string(icd_item.component_url().get());
  EXPECT_EQ(res_string.length(), icd_item.component_url().size());
  EXPECT_EQ(0u, res_string.find("fuchsia-pkg://fuchsia.com/libopencl_vsi_vip_"));
  EXPECT_THAT(res_string, testing::EndsWith("_test#meta/opencl.cm"));
}
