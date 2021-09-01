// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
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

namespace block_fidl = fuchsia_hardware_block;

TEST(FtlFidlTest, GetVmoReturnsVmoWithCounters) {
  std::vector<std::string> property_list = {
      "counter.max_wear",
      "counter.read",
      "counter.read_issued_page_reads",
      "counter.read_issued_page_writes",
      "counter.read_issued_block_erase",
      "counter.write",
      "counter.write_issued_page_reads",
      "counter.write_issued_page_writes",
      "counter.write_issued_block_erase",
      "counter.trim",
      "counter.trim_issued_page_reads",
      "counter.trim_issued_page_writes",
      "counter.trim_issued_block_erase",
      "counter.flush",
      "counter.flush_issued_page_reads",
      "counter.flush_issued_page_writes",
      "counter.flush_issued_block_erase",
  };

  std::string path_to_device(kTestDevice);
  size_t length = path_to_device.rfind("/block");
  ASSERT_GT(length, 0);

  fbl::unique_fd ftl_service_fd(open(path_to_device.substr(0, length).c_str(), O_RDWR));
  ASSERT_TRUE(ftl_service_fd);
  fdio_cpp::UnownedFdioCaller caller(ftl_service_fd.get());
  auto r = fidl::WireCall<block_fidl::Ftl>(caller.channel()).GetVmo();
  ASSERT_OK(r.status());
  ASSERT_FALSE(r->result.is_err());
  zx::vmo inspect_vmo(std::move(r->result.mutable_response().vmo));
  ASSERT_TRUE(inspect_vmo.is_valid());
  auto base_hierarchy = inspect::ReadFromVmo(inspect_vmo).take_value();
  auto* hierarchy_ptr = base_hierarchy.GetByPath({"ftl"});
  ASSERT_NOT_NULL(hierarchy_ptr);
  for (const auto& property_name : property_list) {
    auto* property = hierarchy_ptr->node().get_property<inspect::UintPropertyValue>(property_name);
    EXPECT_NOT_NULL(property, "Missing Inspect Property: %s", property_name.c_str());
  }
}

}  // namespace
