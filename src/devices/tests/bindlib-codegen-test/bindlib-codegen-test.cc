// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.test/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fuchsia/driver/development/cpp/fidl.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>

#include <bind/bindlib/codegen/testlib/cpp/bind.h>
#include <bind/bindlibparent/codegen/testlib/cpp/bind.h>
#include <bind/fuchsia/cpp/bind.h>
#include <gtest/gtest.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

#include "lib/ddk/binding_priv.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace lib = bind_bindlib_codegen_testlib;
namespace parent = bind_bindlibparent_codegen_testlib;

const std::string kChildDevicePath = "sys/test/parent";

class BindLibToFidlCodeGenTest : public testing::Test {
 protected:
  void SetUp() override {
    // Wait for the child device to bind and appear. The child device should bind with its string
    // properties.
    fbl::unique_fd string_bind_fd;
    zx_status_t status =
        device_watcher::RecursiveWaitForFile("/dev/sys/test/parent/child", &string_bind_fd);
    ASSERT_EQ(ZX_OK, status);

    // Connect to the DriverDevelopment service.
    auto context = sys::ComponentContext::Create();
    context->svc()->Connect(driver_dev_.NewRequest());
  }

  fuchsia::driver::development::DriverDevelopmentSyncPtr driver_dev_;
};

TEST_F(BindLibToFidlCodeGenTest, DeviceProperties) {
  fuchsia::driver::development::DeviceInfoIteratorSyncPtr iterator;
  ASSERT_EQ(ZX_OK, driver_dev_->GetDeviceInfo({kChildDevicePath}, iterator.NewRequest(),
                                              /* exact_match= */ true));

  std::vector<fuchsia::driver::development::DeviceInfo> devices;
  ASSERT_EQ(iterator->GetNext(&devices), ZX_OK);

  auto& str_props = devices[0].property_list().str_props;
  ASSERT_EQ(static_cast<size_t>(9), str_props.size());

  ASSERT_EQ(bind_fuchsia::PROTOCOL, str_props[0].key);
  ASSERT_TRUE(str_props[0].value.is_int_value());
  ASSERT_EQ(3u, str_props[0].value.int_value());

  ASSERT_EQ(bind_fuchsia::PCI_VID, str_props[1].key);
  ASSERT_TRUE(str_props[1].value.is_int_value());
  ASSERT_EQ(lib::BIND_PCI_VID_PIE, str_props[1].value.int_value());

  ASSERT_EQ(bind_fuchsia::PCI_DID, str_props[2].key);
  ASSERT_TRUE(str_props[2].value.is_int_value());
  ASSERT_EQ(1234u, str_props[2].value.int_value());

  ASSERT_EQ("bindlib.codegen.testlib.kinglet", str_props[3].key);
  ASSERT_EQ(lib::KINGLET, str_props[3].key);
  ASSERT_TRUE(str_props[3].value.is_str_value());
  ASSERT_EQ("firecrest", str_props[3].value.str_value());

  ASSERT_EQ("bindlib.codegen.testlib.Moon", str_props[4].key);
  ASSERT_EQ(lib::MOON, str_props[4].key);
  ASSERT_TRUE(str_props[4].value.is_enum_value());
  ASSERT_EQ("bindlib.codegen.testlib.Moon.Half", str_props[4].value.enum_value());
  ASSERT_EQ(lib::MOON_HALF, str_props[4].value.enum_value());

  ASSERT_EQ("bindlib.codegen.testlib.bobolink", str_props[5].key);
  ASSERT_EQ(lib::BOBOLINK, str_props[5].key);
  ASSERT_TRUE(str_props[5].value.is_int_value());
  ASSERT_EQ(static_cast<uint32_t>(10), str_props[5].value.int_value());

  ASSERT_EQ("bindlib.codegen.testlib.flag", str_props[6].key);
  ASSERT_EQ(lib::FLAG, str_props[6].key);
  ASSERT_TRUE(str_props[6].value.is_bool_value());
  ASSERT_TRUE(str_props[6].value.bool_value());
  ASSERT_EQ(lib::FLAG_ENABLE, str_props[6].value.bool_value());

  ASSERT_EQ("bindlibparent.codegen.testlib.Pizza", str_props[7].key);
  ASSERT_EQ(parent::PIZZA, str_props[7].key);
  ASSERT_TRUE(str_props[7].value.is_str_value());
  ASSERT_EQ("pepperoni pizza", str_props[7].value.str_value());
  ASSERT_EQ(parent::PIZZA_PEPPERONI, str_props[7].value.str_value());

  ASSERT_EQ("bindlibparent.codegen.testlib.Grit", str_props[8].key);
  ASSERT_EQ(parent::GRIT, str_props[8].key);
  ASSERT_TRUE(str_props[8].value.is_int_value());
  ASSERT_EQ(static_cast<uint32_t>(100), str_props[8].value.int_value());
  ASSERT_EQ(parent::GRIT_COARSE, str_props[8].value.int_value());
}
