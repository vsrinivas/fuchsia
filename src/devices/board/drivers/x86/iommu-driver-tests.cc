// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/driver-unit-test/utils.h>

#include <zxtest/zxtest.h>

#include "x86.h"

namespace x86 {

TEST(IommuTest, BasicTest) {
  // Initialize enough ACPI to allow us to construct an IommuManager
  std::unique_ptr<X86> dev;
  ASSERT_OK(X86::Create(nullptr, driver_unit_test::GetParent(), &dev));
  ASSERT_OK(dev->EarlyAcpiInit());
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_resource(get_root_resource());
  // Create and destroy an IommuManager. Pass a flag to force use of the hardware iommu to ensure
  // kernel iommu objects are actually created (and then destroyed).
  IommuManager manager([](fx_log_severity_t severity, const char* file, int line, const char* msg,
                          va_list args) { zxlogvf_etc(severity, nullptr, file, line, msg, args); });
  zx_status_t status = manager.Init(std::move(root_resource), true /* force_hardware_iommu */);
  // It could be that this system has no hardware iommus and so we tolerate ZX_ERR_NOT_FOUND.
  EXPECT_TRUE(status == ZX_OK || status == ZX_ERR_NOT_FOUND);
  // Let the IommuManager get destroyed and free the kernel resources, turning back off any iommus.
}

}  // namespace x86
