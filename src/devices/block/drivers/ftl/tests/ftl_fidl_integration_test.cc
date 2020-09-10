// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>

#include <string_view>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "ftl_test_observer.h"

namespace {

namespace block_fidl = ::llcpp::fuchsia::hardware::block;

TEST(FtlFidlTest, GetVmoReturnsVmoWithWearCount) {
  std::string path_to_device(kTestDevice);
  size_t length = path_to_device.rfind("/block");
  ASSERT_GT(length, 0);

  fbl::unique_fd ftl_service_fd(open(path_to_device.substr(0, length).c_str(), O_RDWR));
  ASSERT_TRUE(ftl_service_fd);
  fdio_cpp::UnownedFdioCaller caller(ftl_service_fd.get());
  auto r = block_fidl::Ftl::Call::GetVmo(caller.channel());
  ASSERT_OK(r.status());
  ASSERT_FALSE(r->result.is_err());
  zx::vmo inspect_vmo(std::move(r->result.mutable_response().vmo));
  ASSERT_TRUE(inspect_vmo.is_valid());
  auto hierarchy = inspect::ReadFromVmo(inspect_vmo).take_value();
  auto* wear_count_prop = hierarchy.node().get_property<inspect::UintPropertyValue>("wear_count");
  ASSERT_NOT_NULL(wear_count_prop);
}

}  // namespace
