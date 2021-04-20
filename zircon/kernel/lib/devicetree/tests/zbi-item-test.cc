// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/devicetree/devicetree.h>

#include <zxtest/zxtest.h>

#include "zbi.h"

// The following test case is only meant to be run on boards that are booted
// with a shim that provides a ZBI_TYPE_DEVICETREE ZBI item: our QEMU arm64
// board is such an example.
TEST(DevicetreeTest, SystemDevicetree) {
  if (auto item = DevicetreeItem::Get(); item.is_error()) {
    ASSERT_OK(item.error_value(), "failed to get ZBI item");
  } else {
    ASSERT_GT(item->size, 0);

    auto fdt = std::make_unique<uint8_t[]>(item->size);
    ASSERT_OK(item->vmo.read(fdt.get(), 0, item->size));

    devicetree::Devicetree dt({fdt.get(), item->size});
    int node_count = 0;
    dt.Walk([&node_count](const auto& path, auto props) {
      ++node_count;
      return true;
    });
    EXPECT_GT(node_count, 0);
  }
}
