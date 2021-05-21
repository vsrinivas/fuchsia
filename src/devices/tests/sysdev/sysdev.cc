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
};

zx_status_t Sysdev::Create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                           zx_handle_t items_svc_handle) {
  zx::channel items_svc(items_svc_handle);
  auto sysdev = std::make_unique<Sysdev>(parent);

  // Check if we were sent configuration data
  zx::vmo payload;
  uint32_t payload_len = 0;
  if (items_svc.is_valid()) {
    zx_status_t status = fuchsia_boot_ItemsGet(items_svc.get(), ZBI_TYPE_DRV_BOARD_PRIVATE, 0,
                                               payload.reset_and_get_address(), &payload_len);
    if (status != ZX_OK) {
      return status;
    }
  }

  uintptr_t payload_addr = 0;
  if (payload_len > 0) {
    zx_status_t status =
        zx::vmar::root_self()->map(ZX_VM_PERM_READ, 0, payload, 0, payload_len, &payload_addr);
    if (status != ZX_OK) {
      return status;
    }
  }
  payload.reset();

  zx_status_t status = sysdev->DdkAdd(ddk::DeviceAddArgs("sys").set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status != ZX_OK) {
    return status;
  }

  // If we were sent configuration data, check to see if we were told to
  // create a composite.  If so, we will create a composite out of
  // "well-known" devices that the test may create.  These are children with
  // the PLATFORM_DEV properties
  // (PDEV_VID_TEST, PDEV_PID_LIBDRIVER_TEST, PDEV_DID_TEST_CHILD_1) and
  // (PDEV_VID_TEST, PDEV_PID_LIBDRIVER_TEST, PDEV_DID_TEST_CHILD_2).
  // The resulting composite will have PLATFORM_DEV properties
  // (PDEV_VID_TEST, PDEV_PID_LIBDRIVER_TEST, PDEV_DID_TEST_COMPOSITE).
  if (payload_len >= sizeof(bool)) {
    auto should_create_composite = reinterpret_cast<const bool*>(payload_addr);
    if (*should_create_composite) {
      status = sysdev->MakeComposite();
      ZX_ASSERT(status == ZX_OK);
    }
  }

  // Now owned by devmgr.
  __UNUSED auto ptr = sysdev.release();

  return ZX_OK;
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
      {countof(fragment1_match), fragment1_match},
  };
  const device_fragment_part_t fragment2[] = {
      {countof(fragment2_match), fragment2_match},
  };
  const device_fragment_t fragments[] = {
      {"fragment-1", countof(fragment1), fragment1},
      {"fragment-2", countof(fragment2), fragment2},
  };

  const zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TEST},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_LIBDRIVER_TEST},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TEST_COMPOSITE},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = countof(props),
      .fragments = fragments,
      .fragments_count = countof(fragments),
      .coresident_device_index = UINT32_MAX,
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
