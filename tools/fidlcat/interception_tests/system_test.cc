// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/features.h>
#include <zircon/syscalls/system.h>

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"

namespace fidlcat {

// zx_system_get_dcache_line_size tests.

std::unique_ptr<SystemCallTest> ZxSystemGetDcacheLineSize(int64_t result,
                                                          std::string_view result_name) {
  auto value =
      std::make_unique<SystemCallTest>("zx_system_get_dcache_line_size", result, result_name);
  return value;
}

#define SYSTEM_GET_DCACHE_LINE_SIZE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_system_get_dcache_line_size)",               \
                     ZxSystemGetDcacheLineSize(result, #result), expected);

#define SYSTEM_GET_DCACHE_LINE_SIZE_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                            \
    SYSTEM_GET_DCACHE_LINE_SIZE_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) {                            \
    SYSTEM_GET_DCACHE_LINE_SIZE_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_GET_DCACHE_LINE_SIZE_DISPLAY_TEST(
    ZxSystemGetDcacheLineSize, 64,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_system_get_dcache_line_size()\n"
    "  -> \x1B[34m64\x1B[0m\n");

// zx_system_get_num_cpus tests.

std::unique_ptr<SystemCallTest> ZxSystemGetNumCpus(int64_t result, std::string_view result_name) {
  auto value = std::make_unique<SystemCallTest>("zx_system_get_num_cpus", result, result_name);
  return value;
}

#define SYSTEM_GET_NUM_CPUS_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_system_get_num_cpus)", ZxSystemGetNumCpus(result, #result), expected);

#define SYSTEM_GET_NUM_CPUS_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    SYSTEM_GET_NUM_CPUS_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    SYSTEM_GET_NUM_CPUS_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_GET_NUM_CPUS_DISPLAY_TEST(
    ZxSystemGetNumCpus, 8,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_system_get_num_cpus()\n"
    "  -> \x1B[34m8\x1B[0m\n");

// zx_system_get_version tests.

std::unique_ptr<SystemCallTest> ZxSystemGetVersion(int64_t result, std::string_view result_name,
                                                   const char* version, size_t version_size) {
  auto value = std::make_unique<SystemCallTest>("zx_system_get_version", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(version));
  value->AddInput(version_size);
  return value;
}

#define SYSTEM_GET_VERSION_DISPLAY_TEST_CONTENT(result, expected)                              \
  std::string version("git-8a07d52603404521038d8866b297f99de36f9162");                         \
  PerformDisplayTest("$plt(zx_system_get_version)",                                            \
                     ZxSystemGetVersion(result, #result, version.c_str(), version.size() + 1), \
                     expected);

#define SYSTEM_GET_VERSION_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    SYSTEM_GET_VERSION_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    SYSTEM_GET_VERSION_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_GET_VERSION_DISPLAY_TEST(
    ZxSystemGetVersion, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_system_get_version()\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "version: \x1B[32mstring\x1B[0m = "
    "\x1B[31m\"git-8a07d52603404521038d8866b297f99de36f9162\"\x1B[0m)\n");

// zx_system_get_physmem tests.

std::unique_ptr<SystemCallTest> ZxSystemGetPhysmem(int64_t result, std::string_view result_name) {
  auto value = std::make_unique<SystemCallTest>("zx_system_get_physmem", result, result_name);
  return value;
}

#define SYSTEM_GET_PHYSMEM_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_system_get_physmem)", ZxSystemGetPhysmem(result, #result), expected);

#define SYSTEM_GET_PHYSMEM_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    SYSTEM_GET_PHYSMEM_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    SYSTEM_GET_PHYSMEM_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_GET_PHYSMEM_DISPLAY_TEST(
    ZxSystemGetPhysmem, 536870912,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_system_get_physmem()\n"
    "  -> \x1B[34m536870912\x1B[0m\n");

// zx_system_get_event tests.

std::unique_ptr<SystemCallTest> ZxSystemGetEvent(int64_t result, std::string_view result_name,
                                                 zx_handle_t root_job, uint32_t kind,
                                                 zx_handle_t* event) {
  auto value = std::make_unique<SystemCallTest>("zx_system_get_event", result, result_name);
  value->AddInput(root_job);
  value->AddInput(kind);
  value->AddInput(reinterpret_cast<uint64_t>(event));
  return value;
}

#define SYSTEM_GET_EVENT_DISPLAY_TEST_CONTENT(result, expected)                          \
  zx_handle_t event = kHandleOut;                                                        \
  PerformDisplayTest(                                                                    \
      "$plt(zx_system_get_event)",                                                       \
      ZxSystemGetEvent(result, #result, kHandle, ZX_SYSTEM_EVENT_OUT_OF_MEMORY, &event), \
      expected);

#define SYSTEM_GET_EVENT_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                 \
    SYSTEM_GET_EVENT_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                           \
  TEST_F(InterceptionWorkflowTestArm, name) {                 \
    SYSTEM_GET_EVENT_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_GET_EVENT_DISPLAY_TEST(
    ZxSystemGetEvent, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_system_get_event("
    "root_job: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "kind: \x1B[32mzx_system_event_type_t\x1B[0m = \x1B[34mZX_SYSTEM_EVENT_OUT_OF_MEMORY\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (event: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n");

// zx_system_get_features tests.

std::unique_ptr<SystemCallTest> ZxSystemGetFeatures(int64_t result, std::string_view result_name,
                                                    uint32_t kind, uint32_t* features) {
  auto value = std::make_unique<SystemCallTest>("zx_system_get_features", result, result_name);
  value->AddInput(kind);
  value->AddInput(reinterpret_cast<uint64_t>(features));
  return value;
}

#define SYSTEM_GET_FEATURES_DISPLAY_TEST_CONTENT(result, expected)                          \
  uint32_t features = 8;                                                                    \
  PerformDisplayTest(                                                                       \
      "$plt(zx_system_get_features)",                                                       \
      ZxSystemGetFeatures(result, #result, ZX_FEATURE_KIND_HW_BREAKPOINT_COUNT, &features), \
      expected);

#define SYSTEM_GET_FEATURES_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    SYSTEM_GET_FEATURES_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    SYSTEM_GET_FEATURES_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_GET_FEATURES_DISPLAY_TEST(
    ZxSystemGetFeatures, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_system_get_features("
    "kind: \x1B[32mzx_feature_kind_t\x1B[0m = \x1B[31mZX_FEATURE_KIND_HW_BREAKPOINT_COUNT\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (features: \x1B[32muint32\x1B[0m = \x1B[34m8\x1B[0m)\n");

// zx_system_mexec tests.

std::unique_ptr<SystemCallTest> ZxSystemMexec(int64_t result, std::string_view result_name,
                                              zx_handle_t resource, zx_handle_t kernel_vmo,
                                              zx_handle_t bootimage_vmo) {
  auto value = std::make_unique<SystemCallTest>("zx_system_mexec", result, result_name);
  value->AddInput(resource);
  value->AddInput(kernel_vmo);
  value->AddInput(bootimage_vmo);
  return value;
}

#define SYSTEM_MEXEC_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_system_mexec)",               \
                     ZxSystemMexec(result, #result, kHandle, kHandle2, kHandle3), expected);

#define SYSTEM_MEXEC_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    SYSTEM_MEXEC_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { SYSTEM_MEXEC_DISPLAY_TEST_CONTENT(result, expected); }

SYSTEM_MEXEC_DISPLAY_TEST(ZxSystemMexec, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_system_mexec("
                          "resource: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                          "kernel_vmo: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1222\x1B[0m, "
                          "bootimage_vmo: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1333\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_system_mexec_payload_get tests.

std::unique_ptr<SystemCallTest> ZxSystemMexecPayloadGet(int64_t result,
                                                        std::string_view result_name,
                                                        zx_handle_t resource, const uint8_t* buffer,
                                                        size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_system_mexec_payload_get", result, result_name);
  value->AddInput(resource);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define SYSTEM_MEXEC_PAYLOAD_GET_DISPLAY_TEST_CONTENT(result, expected)           \
  std::vector<uint8_t> buffer = {0x10, 0x01, 0x20, 0x02, 0x30, 0x03, 0x40, 0x04}; \
  PerformDisplayTest(                                                             \
      "$plt(zx_system_mexec_payload_get)",                                        \
      ZxSystemMexecPayloadGet(result, #result, kHandle, buffer.data(), buffer.size()), expected);

#define SYSTEM_MEXEC_PAYLOAD_GET_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                         \
    SYSTEM_MEXEC_PAYLOAD_GET_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                                   \
  TEST_F(InterceptionWorkflowTestArm, name) {                         \
    SYSTEM_MEXEC_PAYLOAD_GET_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_MEXEC_PAYLOAD_GET_DISPLAY_TEST(
    ZxSystemMexecPayloadGet, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_system_mexec_payload_get("
    "resource: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "buffer_size: \x1B[32msize\x1B[0m = \x1B[34m8\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    buffer: \x1B[32mvector<uint8>\x1B[0m = [ "
    "\x1B[34m10\x1B[0m, \x1B[34m01\x1B[0m, \x1B[34m20\x1B[0m, \x1B[34m02\x1B[0m, "
    "\x1B[34m30\x1B[0m, \x1B[34m03\x1B[0m, \x1B[34m40\x1B[0m, \x1B[34m04\x1B[0m ]\n");

// zx_system_powerctl tests.

std::unique_ptr<SystemCallTest> ZxSystemPowerctl(int64_t result, std::string_view result_name,
                                                 zx_handle_t resource, uint32_t cmd,
                                                 const zx_system_powerctl_arg_t* arg) {
  auto value = std::make_unique<SystemCallTest>("zx_system_powerctl", result, result_name);
  value->AddInput(resource);
  value->AddInput(cmd);
  value->AddInput(reinterpret_cast<uint64_t>(arg));
  return value;
}

#define SYSTEM_POWERCTL_DISPLAY_TEST_CONTENT(result, expected)                                 \
  PerformDisplayTest(                                                                          \
      "$plt(zx_system_powerctl)",                                                              \
      ZxSystemPowerctl(result, #result, kHandle, ZX_SYSTEM_POWERCTL_ENABLE_ALL_CPUS, nullptr), \
      expected);

#define SYSTEM_POWERCTL_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    SYSTEM_POWERCTL_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    SYSTEM_POWERCTL_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_POWERCTL_DISPLAY_TEST(ZxSystemPowerctl, ZX_OK,
                             "\n"
                             "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                             "zx_system_powerctl("
                             "resource: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                             "cmd: \x1B[32mzx_system_powerctl_t\x1B[0m = "
                             "\x1B[34mZX_SYSTEM_POWERCTL_ENABLE_ALL_CPUS\x1B[0m)\n"
                             "  -> \x1B[32mZX_OK\x1B[0m\n");

#define SYSTEM_POWERCTL_ACPI_DISPLAY_TEST_CONTENT(result, expected)                            \
  zx_system_powerctl_arg_t arg = {                                                             \
      .acpi_transition_s_state = {.target_s_state = 1, .sleep_type_a = 2, .sleep_type_b = 3}}; \
  PerformDisplayTest("$plt(zx_system_powerctl)",                                               \
                     ZxSystemPowerctl(result, #result, kHandle,                                \
                                      ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE, &arg),       \
                     expected);

#define SYSTEM_POWERCTL_ACPI_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                     \
    SYSTEM_POWERCTL_ACPI_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                               \
  TEST_F(InterceptionWorkflowTestArm, name) {                     \
    SYSTEM_POWERCTL_ACPI_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_POWERCTL_ACPI_DISPLAY_TEST(ZxSystemPowerctlAcpi, ZX_OK,
                                  "\n"
                                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                  "zx_system_powerctl("
                                  "resource: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                                  "cmd: \x1B[32mzx_system_powerctl_t\x1B[0m = "
                                  "\x1B[34mZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE\x1B[0m)\n"
                                  "  arg: \x1B[32mzx_system_powerctl_arg_t\x1B[0m = {\n"
                                  "    target_s_state: \x1B[32muint8\x1B[0m = \x1B[34m1\x1B[0m\n"
                                  "    sleep_type_a: \x1B[32muint8\x1B[0m = \x1B[34m2\x1B[0m\n"
                                  "    sleep_type_b: \x1B[32muint8\x1B[0m = \x1B[34m3\x1B[0m\n"
                                  "  }\n"
                                  "  -> \x1B[32mZX_OK\x1B[0m\n");

#define SYSTEM_POWERCTL_PL1_DISPLAY_TEST_CONTENT(result, expected)                          \
  zx_system_powerctl_arg_t arg = {                                                          \
      .x86_power_limit = {                                                                  \
          .power_limit = 200, .time_window = 300, .clamp = false, .enable = true}};         \
  PerformDisplayTest(                                                                       \
      "$plt(zx_system_powerctl)",                                                           \
      ZxSystemPowerctl(result, #result, kHandle, ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1, &arg), \
      expected);

#define SYSTEM_POWERCTL_PL1_DISPLAY_TEST(name, result, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    SYSTEM_POWERCTL_PL1_DISPLAY_TEST_CONTENT(result, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    SYSTEM_POWERCTL_PL1_DISPLAY_TEST_CONTENT(result, expected);  \
  }

SYSTEM_POWERCTL_PL1_DISPLAY_TEST(ZxSystemPowerctlPl1, ZX_OK,
                                 "\n"
                                 "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                                 "zx_system_powerctl("
                                 "resource: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                                 "cmd: \x1B[32mzx_system_powerctl_t\x1B[0m = "
                                 "\x1B[34mZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1\x1B[0m)\n"
                                 "  arg: \x1B[32mzx_system_powerctl_arg_t\x1B[0m = {\n"
                                 "    power_limit: \x1B[32muint32\x1B[0m = \x1B[34m200\x1B[0m\n"
                                 "    time_window: \x1B[32muint32\x1B[0m = \x1B[34m300\x1B[0m\n"
                                 "    clamp: \x1B[32muint8\x1B[0m = \x1B[34m0\x1B[0m\n"
                                 "    enable: \x1B[32muint8\x1B[0m = \x1B[34m1\x1B[0m\n"
                                 "  }\n"
                                 "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
