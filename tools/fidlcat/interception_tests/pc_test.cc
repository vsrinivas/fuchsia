// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_pc_firmware_tables tests.

std::unique_ptr<SystemCallTest> ZxPcFirmwareTables(int64_t result, std::string_view result_name,
                                                   zx_handle_t handle, zx_paddr_t* acpi_rsdp,
                                                   zx_paddr_t* smbios) {
  auto value = std::make_unique<SystemCallTest>("zx_pc_firmware_tables", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(acpi_rsdp));
  value->AddInput(reinterpret_cast<uint64_t>(smbios));
  return value;
}

#define PC_FIRMWARE_TABLES_DISPLAY_TEST_CONTENT(result, expected) \
  zx_paddr_t acpi_rsdp = 0x12340000;                              \
  zx_paddr_t smbios = 0x12350000;                                 \
  PerformDisplayTest("$plt(zx_pc_firmware_tables)",               \
                     ZxPcFirmwareTables(result, #result, kHandle, &acpi_rsdp, &smbios), expected);

#define PC_FIRMWARE_TABLES_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    PC_FIRMWARE_TABLES_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    PC_FIRMWARE_TABLES_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

PC_FIRMWARE_TABLES_DISPLAY_TEST(
    ZxPcFirmwareTables, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_pc_firmware_tables(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "acpi_rsdp:\x1B[32mzx_paddr_t\x1B[0m: \x1B[34m0000000012340000\x1B[0m, "
    "smbios:\x1B[32mzx_paddr_t\x1B[0m: \x1B[34m0000000012350000\x1B[0m)\n");

}  // namespace fidlcat
