// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.test/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fuchsia/driver/development/cpp/fidl.h>
#include <lib/ddk/driver.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/component_context.h>

#include <bind/bindlib/to/fidl/testlib/cpp/fidl.h>
#include <bind/bindlibparent/to/fidl/testlib/cpp/fidl.h>
#include <gtest/gtest.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

#include "lib/ddk/binding_priv.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace bindlib = bind::bindlib::to::fidl::testlib;
namespace bindlibparent = bind::bindlibparent::to::fidl::testlib;

const std::string kDriverBaseUrl = "fuchsia-boot:///#driver";
const std::string kStringBindDriverLibPath = kDriverBaseUrl + "/bindlib-to-fidl-codegen-child.so";
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
  ASSERT_EQ(ZX_OK, driver_dev_->GetDeviceInfo({kChildDevicePath}, iterator.NewRequest()));

  std::vector<fuchsia::driver::development::DeviceInfo> devices;
  ASSERT_EQ(iterator->GetNext(&devices), ZX_OK);

  constexpr zx_device_prop_t kExpectedProps[] = {
      {BIND_PROTOCOL, 0, 3},
      {BIND_PCI_VID, 0, bindlib::BIND_PCI_VID_PIE},
      {BIND_PCI_DID, 0, 1234},
  };

  ASSERT_EQ(devices.size(), 1u);
  auto props = devices[0].property_list().props;
  ASSERT_EQ(props.size(), std::size(kExpectedProps));
  for (size_t i = 0; i < props.size(); i++) {
    ASSERT_EQ(props[i].id, kExpectedProps[i].id);
    ASSERT_EQ(props[i].reserved, kExpectedProps[i].reserved);
    ASSERT_EQ(props[i].value, kExpectedProps[i].value);
  }

  auto& str_props = devices[0].property_list().str_props;
  ASSERT_EQ(static_cast<size_t>(6), str_props.size());

  ASSERT_STREQ("bindlib.to.fidl.testlib.kinglet", str_props[0].key.data());
  ASSERT_STREQ(bindlib::KINGLET, str_props[0].key.data());
  ASSERT_TRUE(str_props[0].value.is_str_value());
  ASSERT_STREQ("firecrest", str_props[0].value.str_value().data());

  ASSERT_STREQ("bindlib.to.fidl.testlib.Moon", str_props[1].key.data());
  ASSERT_STREQ(bindlib::MOON, str_props[1].key.data());
  ASSERT_TRUE(str_props[1].value.is_enum_value());
  ASSERT_STREQ("bindlib.to.fidl.testlib.Moon.Half", str_props[1].value.enum_value().data());
  ASSERT_STREQ(bindlib::MOON_HALF, str_props[1].value.enum_value().data());

  ASSERT_STREQ("bindlib.to.fidl.testlib.bobolink", str_props[2].key.data());
  ASSERT_STREQ(bindlib::BOBOLINK, str_props[2].key.data());
  ASSERT_TRUE(str_props[2].value.is_int_value());
  ASSERT_EQ(static_cast<uint32_t>(10), str_props[2].value.int_value());

  ASSERT_STREQ("bindlib.to.fidl.testlib.flag", str_props[3].key.data());
  ASSERT_STREQ(bindlib::FLAG, str_props[3].key.data());
  ASSERT_TRUE(str_props[3].value.is_bool_value());
  ASSERT_TRUE(str_props[3].value.bool_value());
  ASSERT_EQ(bindlib::FLAG_ENABLE, str_props[3].value.bool_value());

  ASSERT_STREQ("bindlibparent.to.fidl.testlib.Pizza", str_props[4].key.data());
  ASSERT_STREQ(bindlibparent::PIZZA, str_props[4].key.data());
  ASSERT_TRUE(str_props[4].value.is_str_value());
  ASSERT_STREQ("pepperoni pizza", str_props[4].value.str_value().data());
  ASSERT_STREQ(bindlibparent::PIZZA_PEPPERONI, str_props[4].value.str_value().data());

  ASSERT_STREQ("bindlibparent.to.fidl.testlib.Grit", str_props[5].key.data());
  ASSERT_STREQ(bindlibparent::GRIT, str_props[5].key.data());
  ASSERT_TRUE(str_props[5].value.is_int_value());
  ASSERT_EQ(static_cast<uint32_t>(100), str_props[5].value.int_value());
  ASSERT_EQ(bindlibparent::GRIT_COARSE, str_props[5].value.int_value());
}
