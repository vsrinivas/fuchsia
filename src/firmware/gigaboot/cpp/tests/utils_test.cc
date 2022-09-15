// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "utils.h"

#include <efi/types.h>
#include <gtest/gtest.h>

#include "mock_boot_service.h"

namespace gigaboot {
namespace {

TEST(GigabootTest, PrintTpm2Capability) {
  MockStubService stub_service;
  Device image_device({"path", "image"});  // dont care
  Tcg2Device tcg2_device;
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&tcg2_device);

  ASSERT_EQ(PrintTpm2Capability(), EFI_SUCCESS);
}

TEST(GigabootTest, PrintTpm2CapabilityTpm2NotSupported) {
  MockStubService stub_service;
  Device image_device({"path", "image"});  // dont care
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);
  stub_service.AddDevice(&image_device);
  ASSERT_NE(PrintTpm2Capability(), EFI_SUCCESS);
}

}  // namespace
}  // namespace gigaboot
