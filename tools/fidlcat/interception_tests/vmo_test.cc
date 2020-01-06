// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_vmo_create tests.

std::unique_ptr<SystemCallTest> ZxVmoCreate(int64_t result, std::string_view result_name,
                                            uint64_t size, uint32_t options, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_create", result, result_name);
  value->AddInput(size);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define VMO_CREATE_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                           \
  PerformDisplayTest("zx_vmo_create@plt",                 \
                     ZxVmoCreate(result, #result, 1024, ZX_VMO_RESIZABLE, &out), expected);

#define VMO_CREATE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { VMO_CREATE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { VMO_CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

VMO_CREATE_DISPLAY_TEST(
    ZxVmoCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_create("
    "size:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m, "
    "options:\x1B[32mzx_vmo_creation_option_t\x1B[0m: \x1B[34mZX_VMO_RESIZABLE\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_vmo_read tests.

std::unique_ptr<SystemCallTest> ZxVmoRead(int64_t result, std::string_view result_name,
                                          zx_handle_t handle, void* buffer, uint64_t offset,
                                          size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_read", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(offset);
  value->AddInput(buffer_size);
  return value;
}

#define VMO_READ_DISPLAY_TEST_CONTENT(result, expected)                                     \
  std::vector<uint8_t> buffer;                                                              \
  for (int i = 0; i < 20; ++i) {                                                            \
    buffer.emplace_back(i);                                                                 \
  }                                                                                         \
  PerformDisplayTest("zx_vmo_read@plt",                                                     \
                     ZxVmoRead(result, #result, kHandle, buffer.data(), 10, buffer.size()), \
                     expected);

#define VMO_READ_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { VMO_READ_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { VMO_READ_DISPLAY_TEST_CONTENT(errno, expected); }

VMO_READ_DISPLAY_TEST(
    ZxVmoRead, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "offset:\x1B[32muint64\x1B[0m: \x1B[34m10\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "      buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m00\x1B[0m, \x1B[34m01\x1B[0m, \x1B[34m02\x1B[0m, \x1B[34m03\x1B[0m, "
    "\x1B[34m04\x1B[0m, \x1B[34m05\x1B[0m, \x1B[34m06\x1B[0m, \x1B[34m07\x1B[0m, "
    "\x1B[34m08\x1B[0m, \x1B[34m09\x1B[0m, \x1B[34m0a\x1B[0m, \x1B[34m0b\x1B[0m, "
    "\x1B[34m0c\x1B[0m, \x1B[34m0d\x1B[0m, \x1B[34m0e\x1B[0m, \x1B[34m0f\x1B[0m, "
    "\x1B[34m10\x1B[0m, \x1B[34m11\x1B[0m, \x1B[34m12\x1B[0m, \x1B[34m13\x1B[0m\n");

// zx_vmo_write tests.

std::unique_ptr<SystemCallTest> ZxVmoWrite(int64_t result, std::string_view result_name,
                                           zx_handle_t handle, const void* buffer, uint64_t offset,
                                           size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_write", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(offset);
  value->AddInput(buffer_size);
  return value;
}

#define VMO_WRITE_DISPLAY_TEST_CONTENT(result, expected)                                     \
  std::vector<uint8_t> buffer;                                                               \
  for (int i = 0; i < 20; ++i) {                                                             \
    buffer.emplace_back(i);                                                                  \
  }                                                                                          \
  PerformDisplayTest("zx_vmo_write@plt",                                                     \
                     ZxVmoWrite(result, #result, kHandle, buffer.data(), 10, buffer.size()), \
                     expected);

#define VMO_WRITE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { VMO_WRITE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { VMO_WRITE_DISPLAY_TEST_CONTENT(errno, expected); }

VMO_WRITE_DISPLAY_TEST(
    ZxVmoWrite, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_write("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "offset:\x1B[32muint64\x1B[0m: \x1B[34m10\x1B[0m)\n"
    "    buffer:\x1B[32muint8\x1B[0m: "
    "\x1B[34m00\x1B[0m, \x1B[34m01\x1B[0m, \x1B[34m02\x1B[0m, \x1B[34m03\x1B[0m, "
    "\x1B[34m04\x1B[0m, \x1B[34m05\x1B[0m, \x1B[34m06\x1B[0m, \x1B[34m07\x1B[0m, "
    "\x1B[34m08\x1B[0m, \x1B[34m09\x1B[0m, \x1B[34m0a\x1B[0m, \x1B[34m0b\x1B[0m, "
    "\x1B[34m0c\x1B[0m, \x1B[34m0d\x1B[0m, \x1B[34m0e\x1B[0m, \x1B[34m0f\x1B[0m, "
    "\x1B[34m10\x1B[0m, \x1B[34m11\x1B[0m, \x1B[34m12\x1B[0m, \x1B[34m13\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_vmo_get_size tests.

std::unique_ptr<SystemCallTest> ZxVmoGetSize(int64_t result, std::string_view result_name,
                                             zx_handle_t handle, uint64_t* size) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_get_size", result, result_name);
  value->AddInput(handle);
  value->AddInput(reinterpret_cast<uint64_t>(size));
  return value;
}

#define VMO_GET_SIZE_DISPLAY_TEST_CONTENT(result, expected)                                \
  uint64_t size = 1024;                                                                    \
  PerformDisplayTest("zx_vmo_get_size@plt", ZxVmoGetSize(result, #result, kHandle, &size), \
                     expected);

#define VMO_GET_SIZE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    VMO_GET_SIZE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { VMO_GET_SIZE_DISPLAY_TEST_CONTENT(errno, expected); }

VMO_GET_SIZE_DISPLAY_TEST(
    ZxVmoGetSize, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_get_size(handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (size:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m)\n");

// zx_vmo_set_size tests.

std::unique_ptr<SystemCallTest> ZxVmoSetSize(int64_t result, std::string_view result_name,
                                             zx_handle_t handle, uint64_t size) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_set_size", result, result_name);
  value->AddInput(handle);
  value->AddInput(size);
  return value;
}

#define VMO_SET_SIZE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("zx_vmo_set_size@plt", ZxVmoSetSize(result, #result, kHandle, 1024), expected);

#define VMO_SET_SIZE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    VMO_SET_SIZE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { VMO_SET_SIZE_DISPLAY_TEST_CONTENT(errno, expected); }

VMO_SET_SIZE_DISPLAY_TEST(ZxVmoSetSize, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_vmo_set_size("
                          "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                          "size:\x1B[32muint64\x1B[0m: \x1B[34m1024\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_vmo_op_range tests.

std::unique_ptr<SystemCallTest> ZxVmoOpRange(int64_t result, std::string_view result_name,
                                             zx_handle_t handle, uint32_t op, uint64_t offset,
                                             uint64_t size, void* buffer, size_t buffer_size) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_op_range", result, result_name);
  value->AddInput(handle);
  value->AddInput(op);
  value->AddInput(offset);
  value->AddInput(size);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  return value;
}

#define VMO_OP_RANGE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest(                                       \
      "zx_vmo_op_range@plt",                                \
      ZxVmoOpRange(result, #result, kHandle, ZX_VMO_OP_CACHE_SYNC, 10, 20, nullptr, 0), expected);

#define VMO_OP_RANGE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {            \
    VMO_OP_RANGE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) { VMO_OP_RANGE_DISPLAY_TEST_CONTENT(errno, expected); }

VMO_OP_RANGE_DISPLAY_TEST(ZxVmoOpRange, ZX_OK,
                          "\n"
                          "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                          "zx_vmo_op_range("
                          "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                          "op:\x1B[32mzx_vmo_op_t\x1B[0m: \x1B[34mZX_VMO_OP_CACHE_SYNC\x1B[0m, "
                          "offset:\x1B[32muint64\x1B[0m: \x1B[34m10\x1B[0m, "
                          "size:\x1B[32muint64\x1B[0m: \x1B[34m20\x1B[0m)\n"
                          "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_vmo_create_child tests.

std::unique_ptr<SystemCallTest> ZxVmoCreateChild(int64_t result, std::string_view result_name,
                                                 zx_handle_t handle, uint32_t options,
                                                 uint64_t offset, uint64_t size, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_create_child", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(offset);
  value->AddInput(size);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define VMO_CREATE_CHILD_DISPLAY_TEST_CONTENT(result, expected)                             \
  zx_handle_t out = kHandleOut;                                                             \
  PerformDisplayTest(                                                                       \
      "zx_vmo_create_child@plt",                                                            \
      ZxVmoCreateChild(result, #result, kHandle, ZX_VMO_CHILD_SNAPSHOT, 10, 20, &out), \
      expected);

#define VMO_CREATE_CHILD_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    VMO_CREATE_CHILD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    VMO_CREATE_CHILD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

VMO_CREATE_CHILD_DISPLAY_TEST(
    ZxVmoCreateChild, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_create_child("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32mzx_vmo_option_t\x1B[0m: \x1B[34mZX_VMO_CHILD_SNAPSHOT\x1B[0m, "
    "offset:\x1B[32muint64\x1B[0m: \x1B[34m10\x1B[0m, "
    "size:\x1B[32muint64\x1B[0m: \x1B[34m20\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_vmo_set_cache_policy tests.

std::unique_ptr<SystemCallTest> ZxVmoSetCachePolicy(int64_t result, std::string_view result_name,
                                                    zx_handle_t handle, uint32_t cache_policy) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_set_cache_policy", result, result_name);
  value->AddInput(handle);
  value->AddInput(cache_policy);
  return value;
}

#define VMO_SET_CACHE_POLICY_DISPLAY_TEST_CONTENT(result, expected)                         \
  PerformDisplayTest("zx_vmo_set_cache_policy@plt",                                         \
                     ZxVmoSetCachePolicy(result, #result, kHandle, ZX_CACHE_POLICY_CACHED), \
                     expected);

#define VMO_SET_CACHE_POLICY_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    VMO_SET_CACHE_POLICY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    VMO_SET_CACHE_POLICY_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

VMO_SET_CACHE_POLICY_DISPLAY_TEST(
    ZxVmoSetCachePolicy, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_set_cache_policy("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "cache_policy:\x1B[32mzx_cache_policy_t\x1B[0m: \x1B[31mZX_CACHE_POLICY_CACHED\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_vmo_replace_as_executable tests.

std::unique_ptr<SystemCallTest> ZxVmoReplaceAsExecutable(int64_t result,
                                                         std::string_view result_name,
                                                         zx_handle_t handle, zx_handle_t vmex,
                                                         zx_handle_t* out) {
  auto value =
      std::make_unique<SystemCallTest>("zx_vmo_replace_as_executable", result, result_name);
  value->AddInput(handle);
  value->AddInput(vmex);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define VMO_REPLACE_AS_EXECUTABLE_DISPLAY_TEST_CONTENT(result, expected)                 \
  zx_handle_t out = kHandleOut;                                                          \
  PerformDisplayTest("zx_vmo_replace_as_executable@plt",                                 \
                     ZxVmoReplaceAsExecutable(result, #result, kHandle, kHandle2, &out), \
                     expected);

#define VMO_REPLACE_AS_EXECUTABLE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                         \
    VMO_REPLACE_AS_EXECUTABLE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                   \
  TEST_F(InterceptionWorkflowTestArm, name) {                         \
    VMO_REPLACE_AS_EXECUTABLE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

VMO_REPLACE_AS_EXECUTABLE_DISPLAY_TEST(
    ZxVmoReplaceAsExecutable, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_replace_as_executable("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "vmex:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1222\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_vmo_create_contiguous tests.

std::unique_ptr<SystemCallTest> ZxVmoCreateContiguous(int64_t result, std::string_view result_name,
                                                      zx_handle_t bti, size_t size,
                                                      uint32_t alignment_log2, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_create_contiguous", result, result_name);
  value->AddInput(bti);
  value->AddInput(size);
  value->AddInput(alignment_log2);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define VMO_CREATE_CONTIGUOUS_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                                      \
  PerformDisplayTest("zx_vmo_create_contiguous@plt",                 \
                     ZxVmoCreateContiguous(result, #result, kHandle, 20, 2, &out), expected);

#define VMO_CREATE_CONTIGUOUS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                     \
    VMO_CREATE_CONTIGUOUS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                               \
  TEST_F(InterceptionWorkflowTestArm, name) {                     \
    VMO_CREATE_CONTIGUOUS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

VMO_CREATE_CONTIGUOUS_DISPLAY_TEST(
    ZxVmoCreateContiguous, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_create_contiguous("
    "bti:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "size:\x1B[32msize_t\x1B[0m: \x1B[34m20\x1B[0m, "
    "alignment_log2:\x1B[32muint32\x1B[0m: \x1B[34m2\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

// zx_vmo_create_physical tests.

std::unique_ptr<SystemCallTest> ZxVmoCreatePhysical(int64_t result, std::string_view result_name,
                                                    zx_handle_t resource, zx_paddr_t paddr,
                                                    size_t size, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_vmo_create_physical", result, result_name);
  value->AddInput(resource);
  value->AddInput(paddr);
  value->AddInput(size);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define VMO_CREATE_PHYSICAL_DISPLAY_TEST_CONTENT(result, expected) \
  zx_handle_t out = kHandleOut;                                    \
  PerformDisplayTest("zx_vmo_create_physical@plt",                 \
                     ZxVmoCreatePhysical(result, #result, kHandle, 0x12345, 20, &out), expected);

#define VMO_CREATE_PHYSICAL_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    VMO_CREATE_PHYSICAL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    VMO_CREATE_PHYSICAL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

VMO_CREATE_PHYSICAL_DISPLAY_TEST(
    ZxVmoCreatePhysical, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_vmo_create_physical("
    "resource:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "paddr:\x1B[32mzx_paddr_t\x1B[0m: \x1B[34m0000000000012345\x1B[0m, "
    "size:\x1B[32msize_t\x1B[0m: \x1B[34m20\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

}  // namespace fidlcat
