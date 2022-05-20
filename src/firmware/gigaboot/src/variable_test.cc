// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "variable.h"

#include <string.h>

#include <efi/runtime-services.h>
#include <efi/types.h>
#include <gtest/gtest.h>

namespace gigaboot {

constexpr char16_t kTestVariableName[] = u"test";

EFIAPI efi_status FakeSetVariable(char16_t* name, efi_guid* guid, uint32_t flags, size_t length,
                                  const void* data) {
  EXPECT_EQ(0, memcmp(kTestVariableName, name, sizeof(kTestVariableName)));
  EXPECT_EQ(0, memcmp(&kGigabootVendorGuid, guid, sizeof(kGigabootVendorGuid)));
  EXPECT_EQ(uint32_t{EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS}, flags);
  EXPECT_EQ(sizeof(bool), length);
  EXPECT_EQ(true, *static_cast<const bool*>(data));
  return EFI_SUCCESS;
}

EFIAPI efi_status FakeGetVariable(char16_t* name, efi_guid* guid, uint32_t* flags, size_t* length,
                                  void* data) {
  EXPECT_EQ(0, memcmp(kTestVariableName, name, sizeof(kTestVariableName)));
  EXPECT_EQ(0, memcmp(&kGigabootVendorGuid, guid, sizeof(kGigabootVendorGuid)));
  EXPECT_EQ(sizeof(bool), *length);
  *static_cast<bool*>(data) = true;
  return EFI_SUCCESS;
}

TEST(VariableTest, SetBool) {
  efi_runtime_services services{
      .SetVariable = FakeSetVariable,
  };

  set_bool(&services, const_cast<char16_t*>(kTestVariableName), true);
}

TEST(VariableTest, GetBool) {
  efi_runtime_services services{
      .GetVariable = FakeGetVariable,
  };

  bool value = false;
  get_bool(&services, const_cast<char16_t*>(kTestVariableName), &value);
  ASSERT_TRUE(value);
}

}  // namespace gigaboot
