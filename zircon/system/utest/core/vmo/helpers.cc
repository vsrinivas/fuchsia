// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helpers.h"

#include <lib/boot-options/boot-options.h>
#include <lib/maybe-standalone-test/maybe-standalone.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

namespace vmo_test {

zx::result<PhysVmo> GetTestPhysVmo(size_t size) {
  // We cannot create any physical VMOs without the root resource.
  zx::unowned_resource root_resource = maybe_standalone::GetRootResource();
  if (!root_resource->is_valid()) {
    return zx::error_result(ZX_ERR_NOT_SUPPORTED);
  }

  // Fetch the address of the test reserved RAM region.  Even with the root
  // resource, we cannot use zx_vmo_create_physical to create a VMO which
  // points to RAM unless someone passed a kernel command line argument telling
  // the kernel to reserve a chunk of RAM for this purpose.
  //
  // If a chunk of RAM was reserved, the kernel will publish its size and
  // physical location in the boot options.  If we have access to the root
  // resource, it is because we are running in the core-tests.zbi.  The boot
  // options command line arguments should be available to us as a VMO.
  //
  // This is an all-or-nothing thing.  If we have the root resource, then we
  // should also have some RAM reserved for running these tests.  If we have
  // the root resource, but _don't_ have any reserved RAM, it should be
  // considered a test error.

  RamReservation ram;
  const BootOptions* boot_options = maybe_standalone::GetBootOptions();
  EXPECT_TRUE(boot_options->test_ram_reserve);
  ram = *boot_options->test_ram_reserve;
  EXPECT_TRUE(ram.paddr.has_value());
  if (!ram.paddr) {
    return zx::error_result(ZX_ERR_NO_RESOURCES);
  }

  PhysVmo ret = {.addr = *ram.paddr, .size = ram.size};
  if (size > 0) {
    if (size > ret.size) {
      return zx::error_result(ZX_ERR_INVALID_ARGS);
    }
    ret.size = size;
  }

  // Go ahead and create the VMO itself.
  zx_status_t res = zx::vmo::create_physical(*root_resource, ret.addr, ret.size, &ret.vmo);
  EXPECT_OK(res);
  if (res != ZX_OK) {
    return zx::error_result(res);
  }

  return zx::ok(std::move(ret));
}

zx::bti CreateNamedBti(const zx::iommu& fake_iommu, uint32_t options, uint64_t bti_id,
                       const char* name) {
  zx::bti ret;
  EXPECT_OK(zx::bti::create(fake_iommu, options, bti_id, &ret));

  if (ret.is_valid()) {
    EXPECT_OK(ret.set_property(ZX_PROP_NAME, name, strlen(name)));
  }

  return ret;
}

}  // namespace vmo_test
