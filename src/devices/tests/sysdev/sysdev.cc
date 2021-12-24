// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/sysdev/sysdev.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/tests/sysdev/sysdev-bind.h"

namespace {

class Sysdev;
using SysdevType = ddk::Device<Sysdev>;

class Sysdev : public SysdevType {
 public:
  explicit Sysdev(zx_device_t* device) : SysdevType(device) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                            zx_handle_t items_svc_handle);

  // Device protocol implementation.
  void DdkRelease() {
    // sysdev should never have its release called.
    ZX_ASSERT_MSG(false, "Sysdev::DdkRelease() invoked!\n");
  }

  zx_status_t MakeComposite();
  zx_status_t AddTestParent();
};

class TestParent;
using TestParentType = ddk::Device<TestParent>;

class TestParent : public TestParentType {
 public:
  explicit TestParent(zx_device_t* device) : TestParentType(device) {}

  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease() {
    // test-parent should never have its release called.
    ZX_ASSERT_MSG(false, "TestParent::DdkRelease() invoked!\n");
  }
};

zx_status_t TestParent::Create(zx_device_t* parent) {
  auto test_parent = std::make_unique<TestParent>(parent);
  zx_status_t status = test_parent->DdkAdd(ddk::DeviceAddArgs("test")
                                               .set_proto_id(ZX_PROTOCOL_TEST_PARENT)
                                               .set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE));
  if (status != ZX_OK) {
    return status;
  }

  // Now owned by the driver framework.
  __UNUSED auto ptr = test_parent.release();

  return ZX_OK;
}

zx_status_t Sysdev::Create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                           zx_handle_t items_svc_handle) {
  zx::channel items_svc(items_svc_handle);
  auto sysdev = std::make_unique<Sysdev>(parent);

  zx_status_t status = sysdev->DdkAdd(ddk::DeviceAddArgs("sys").set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status != ZX_OK) {
    return status;
  }

  // Create a composite out of "well-known" devices that the libdriver-integration-test may create.
  // These are children with
  // the PLATFORM_DEV properties
  // (PDEV_VID_TEST, PDEV_PID_LIBDRIVER_TEST, PDEV_DID_TEST_CHILD_1) and
  // (PDEV_VID_TEST, PDEV_PID_LIBDRIVER_TEST, PDEV_DID_TEST_CHILD_2).
  // The resulting composite will have PLATFORM_DEV properties
  // (PDEV_VID_TEST, PDEV_PID_LIBDRIVER_TEST, PDEV_DID_TEST_COMPOSITE).
  status = sysdev->MakeComposite();
  ZX_ASSERT(status == ZX_OK);

  status = TestParent::Create(sysdev->zxdev());

  // Now owned by devmgr.
  __UNUSED auto ptr = sysdev.release();

  return status;
}

zx_status_t Sysdev::MakeComposite() {
  // Composite binding rules for the well-known composite that
  // libdriver-integration-test uses.
  const zx_bind_inst_t fragment1_match[] = {
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_LIBDRIVER_TEST),
      BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_CHILD_1),
  };
  const zx_bind_inst_t fragment2_match[] = {
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
      BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_LIBDRIVER_TEST),
      BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_CHILD_2),
  };
  const device_fragment_part_t fragment1[] = {
      {std::size(fragment1_match), fragment1_match},
  };
  const device_fragment_part_t fragment2[] = {
      {std::size(fragment2_match), fragment2_match},
  };
  const device_fragment_t fragments[] = {
      {"fragment-1", std::size(fragment1), fragment1},
      {"fragment-2", std::size(fragment2), fragment2},
  };

  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TEST},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_LIBDRIVER_TEST},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TEST_COMPOSITE},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = fragments,
      .fragments_count = std::size(fragments),
      .primary_fragment = "fragment-1",
      .spawn_colocated = false,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  return device_add_composite(zxdev(), "composite", &comp_desc);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.create = Sysdev::Create;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(test_sysdev, driver_ops, "zircon", "0.1");
