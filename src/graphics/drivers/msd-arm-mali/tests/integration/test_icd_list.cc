// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/zx/channel.h>

#include <shared_mutex>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_util/macros.h"
#include "src/graphics/drivers/msd-arm-mali/include/magma_vendor_queries.h"

namespace {

TEST(Mali, IcdList) {
  magma::TestDeviceBase test_device(MAGMA_VENDOR_ID_MALI);
  auto rsp =
      fidl::WireCall<fuchsia_gpu_magma::IcdLoaderDevice>(test_device.channel())->GetIcdList();
  EXPECT_TRUE(rsp.ok());
  EXPECT_EQ(rsp.value().icd_list.count(), 3u);
  {
    auto& icd_item = rsp.value().icd_list[0];
    EXPECT_TRUE(icd_item.has_flags());
    EXPECT_TRUE(icd_item.flags() & fuchsia_gpu_magma::wire::IcdFlags::kSupportsVulkan);
    std::string res_string(icd_item.component_url().get());
    EXPECT_EQ(res_string.length(), icd_item.component_url().size());
    EXPECT_EQ(0u, res_string.find("fuchsia-pkg://mali.fuchsia.com/libvulkan_arm_mali_"));
    EXPECT_THAT(res_string, testing::EndsWith("_test#meta/vulkan.cm"));
  }
  {
    auto& icd_item = rsp.value().icd_list[1];
    std::string res_string(icd_item.component_url().get());
    EXPECT_EQ(0u, res_string.find("fuchsia-pkg://fuchsia.com/libvulkan_arm_mali_"));
    EXPECT_THAT(res_string, testing::EndsWith("_test#meta/vulkan.cm"));
  }
}

}  // namespace
