// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"

namespace fidlcat {

// zx_object_wait_one tests.

std::unique_ptr<SystemCallTest> ZxObjectWaitOne(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, zx_signals_t signals,
                                                zx_time_t deadline, zx_signals_t* observed) {
  auto value = std::make_unique<SystemCallTest>("zx_object_wait_one", result, result_name);
  value->AddInput(handle);
  value->AddInput(signals);
  value->AddInput(deadline);
  value->AddInput(reinterpret_cast<uint64_t>(observed));
  return value;
}

#define OBJECT_WAIT_ONE_DISPLAY_TEST_CONTENT(result, expected)                                  \
  zx_signals_t observed = __ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE;                          \
  auto value =                                                                                  \
      ZxObjectWaitOne(result, #result, kHandle, __ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED, \
                      ZX_TIME_INFINITE, &observed);                                             \
  PerformDisplayTest("$plt(zx_object_wait_one)", std::move(value), expected);

#define OBJECT_WAIT_ONE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {               \
    OBJECT_WAIT_ONE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {               \
    OBJECT_WAIT_ONE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_WAIT_ONE_DISPLAY_TEST(ZxObjectWaitOne, ZX_OK,
                             "\n"
                             "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                             "zx_object_wait_one("
                             "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                             "signals: \x1B[32msignals\x1B[0m = \x1B[34m"
                             "__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m, "
                             "deadline: \x1B[32mzx.time\x1B[0m = \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
                             "  -> \x1B[32mZX_OK\x1B[0m ("
                             "observed: \x1B[32msignals\x1B[0m = "
                             "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE\x1B[0m)\n");

// zx_object_wait_many tests.

std::unique_ptr<SystemCallTest> ZxObjectWaitMany(int64_t result, std::string_view result_name,
                                                 zx_wait_item_t* items, size_t count,
                                                 zx_time_t deadline) {
  auto value = std::make_unique<SystemCallTest>("zx_object_wait_many", result, result_name);
  value->AddInput(reinterpret_cast<uint64_t>(items));
  value->AddInput(count);
  value->AddInput(deadline);
  return value;
}

#define OBJECT_WAIT_MANY_DISPLAY_TEST_CONTENT(result, item_count, canceled, expected)             \
  size_t count;                                                                                   \
  zx_wait_item* items;                                                                            \
  if ((item_count) == -1) {                                                                       \
    count = 0;                                                                                    \
    items = nullptr;                                                                              \
  } else {                                                                                        \
    count = (item_count);                                                                         \
    items = new zx_wait_item_t[count];                                                            \
    for (size_t i = 0; i < count; ++i) {                                                          \
      items[i].handle = kHandle + i;                                                              \
      items[i].waitfor = __ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED;                          \
      items[i].pending = 0;                                                                       \
    }                                                                                             \
  }                                                                                               \
  auto value = ZxObjectWaitMany(result, #result, items, count, ZX_TIME_INFINITE);                 \
  update_data_ = [items, count]() {                                                               \
    for (size_t i = 0; i < count; ++i) {                                                          \
      items[i].pending =                                                                          \
          (canceled) ? __ZX_OBJECT_HANDLE_CLOSED : (__ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE); \
    }                                                                                             \
  };                                                                                              \
  PerformOneThreadDisplayTest("$plt(zx_object_wait_many)", std::move(value), expected);           \
  delete[] items;

#define OBJECT_WAIT_MANY_DISPLAY_TEST(name, errno, item_count, canceled, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                      \
    OBJECT_WAIT_MANY_DISPLAY_TEST_CONTENT(errno, item_count, canceled, expected);  \
  }                                                                                \
  TEST_F(InterceptionWorkflowTestArm, name) {                                      \
    OBJECT_WAIT_MANY_DISPLAY_TEST_CONTENT(errno, item_count, canceled, expected);  \
  }

OBJECT_WAIT_MANY_DISPLAY_TEST(
    ZxObjectWaitManyNull, ZX_OK, -1, false,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_wait_many("
    "deadline: \x1B[32mtime\x1B[0m = \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
    "  items: vector<\x1B[32mzx_wait_item_t\x1B[0m> = \x1B[31mnullptr\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    items: vector<\x1B[32mzx_wait_item_t\x1B[0m> = \x1B[31mnullptr\x1B[0m\n");

OBJECT_WAIT_MANY_DISPLAY_TEST(
    ZxObjectWaitManyZero, ZX_OK, 0, false,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_wait_many("
    "deadline: \x1B[32mtime\x1B[0m = \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
    "  items: vector<\x1B[32mzx_wait_item_t\x1B[0m> = \x1B[31mnullptr\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    items: vector<\x1B[32mzx_wait_item_t\x1B[0m> = \x1B[31mnullptr\x1B[0m\n");

OBJECT_WAIT_MANY_DISPLAY_TEST(ZxObjectWaitMany, ZX_OK, 3, false,
                              "\n"
                              "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                              "zx_object_wait_many("
                              "deadline: \x1B[32mtime\x1B[0m = \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
                              "  items: vector<\x1B[32mzx_wait_item_t\x1B[0m> =  [\n"
                              "    {\n"
                              "      handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m\n"
                              "      waitfor: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "      pending: \x1B[32msignals\x1B[0m = \x1B[34m0\x1B[0m\n"
                              "    },\n"
                              "    {\n"
                              "      handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db1\x1B[0m\n"
                              "      waitfor: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "      pending: \x1B[32msignals\x1B[0m = \x1B[34m0\x1B[0m\n"
                              "    },\n"
                              "    {\n"
                              "      handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db2\x1B[0m\n"
                              "      waitfor: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "      pending: \x1B[32msignals\x1B[0m = \x1B[34m0\x1B[0m\n"
                              "    }\n"
                              "  ]\n"
                              "  -> \x1B[32mZX_OK\x1B[0m\n"
                              "    items: vector<\x1B[32mzx_wait_item_t\x1B[0m> =  [\n"
                              "      {\n"
                              "        handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m\n"
                              "        waitfor: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "        pending: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE\x1B[0m\n"
                              "      },\n"
                              "      {\n"
                              "        handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db1\x1B[0m\n"
                              "        waitfor: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "        pending: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE\x1B[0m\n"
                              "      },\n"
                              "      {\n"
                              "        handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db2\x1B[0m\n"
                              "        waitfor: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "        pending: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE\x1B[0m\n"
                              "      }\n"
                              "    ]\n");

OBJECT_WAIT_MANY_DISPLAY_TEST(ZxObjectWaitManyCanceled, ZX_ERR_CANCELED, 1, true,
                              "\n"
                              "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                              "zx_object_wait_many("
                              "deadline: \x1B[32mtime\x1B[0m = \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
                              "  items: vector<\x1B[32mzx_wait_item_t\x1B[0m> =  [\n"
                              "    {\n"
                              "      handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m\n"
                              "      waitfor: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "      pending: \x1B[32msignals\x1B[0m = \x1B[34m0\x1B[0m\n"
                              "    }\n"
                              "  ]\n"
                              "  -> \x1B[31mZX_ERR_CANCELED\x1B[0m\n"
                              "    items: vector<\x1B[32mzx_wait_item_t\x1B[0m> =  [\n"
                              "      {\n"
                              "        handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m\n"
                              "        waitfor: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "        pending: \x1B[32msignals\x1B[0m = "
                              "\x1B[34m__ZX_OBJECT_HANDLE_CLOSED\x1B[0m\n"
                              "      }\n"
                              "    ]\n");

// zx_object_wait_async tests.

std::unique_ptr<SystemCallTest> ZxObjectWaitAsync(int64_t result, std::string_view result_name,
                                                  zx_handle_t handle, zx_handle_t port,
                                                  uint64_t key, zx_signals_t signals,
                                                  uint32_t options) {
  auto value = std::make_unique<SystemCallTest>("zx_object_wait_async", result, result_name);
  value->AddInput(handle);
  value->AddInput(port);
  value->AddInput(key);
  value->AddInput(signals);
  value->AddInput(options);
  return value;
}

#define OBJECT_WAIT_ASYNC_DISPLAY_TEST_CONTENT(result, expected)                     \
  auto value = ZxObjectWaitAsync(result, #result, kHandle, kPort, kKey,              \
                                 __ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED, 0); \
  PerformDisplayTest("$plt(zx_object_wait_async)", std::move(value), expected);

#define OBJECT_WAIT_ASYNC_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                 \
    OBJECT_WAIT_ASYNC_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                           \
  TEST_F(InterceptionWorkflowTestArm, name) {                 \
    OBJECT_WAIT_ASYNC_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_WAIT_ASYNC_DISPLAY_TEST(ZxObjectWaitAsync, ZX_OK,
                               "\n"
                               "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                               "zx_object_wait_async("
                               "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                               "port: \x1B[32mhandle\x1B[0m = \x1B[31mdf0b2ec1\x1B[0m, "
                               "key: \x1B[32muint64\x1B[0m = \x1B[34m1234\x1B[0m, "
                               "signals: \x1B[32msignals\x1B[0m = "
                               "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m, "
                               "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
                               "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_object_signal tests.

std::unique_ptr<SystemCallTest> ZxObjectSignal(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, zx_signals_t clear_mask,
                                               zx_signals_t set_mask) {
  auto value = std::make_unique<SystemCallTest>("zx_object_signal", result, result_name);
  value->AddInput(handle);
  value->AddInput(clear_mask);
  value->AddInput(set_mask);
  return value;
}

#define OBJECT_SIGNAL_DISPLAY_TEST_CONTENT(result, expected)                                 \
  auto value = ZxObjectSignal(result, #result, kHandle, ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_3, \
                              ZX_USER_SIGNAL_5 | ZX_USER_SIGNAL_7);                          \
  PerformDisplayTest("$plt(zx_object_signal)", std::move(value), expected);

#define OBJECT_SIGNAL_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {             \
    OBJECT_SIGNAL_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) { OBJECT_SIGNAL_DISPLAY_TEST_CONTENT(errno, expected); }

OBJECT_SIGNAL_DISPLAY_TEST(
    ZxObjectSignal, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_signal("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "clear_mask: \x1B[32msignals\x1B[0m = \x1B[34mZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_3\x1B[0m, "
    "set_mask: \x1B[32msignals\x1B[0m = \x1B[34mZX_USER_SIGNAL_5 | ZX_USER_SIGNAL_7\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_object_signal_peer tests.

std::unique_ptr<SystemCallTest> ZxObjectSignalPeer(int64_t result, std::string_view result_name,
                                                   zx_handle_t handle, zx_signals_t clear_mask,
                                                   zx_signals_t set_mask) {
  auto value = std::make_unique<SystemCallTest>("zx_object_signal_peer", result, result_name);
  value->AddInput(handle);
  value->AddInput(clear_mask);
  value->AddInput(set_mask);
  return value;
}

#define OBJECT_SIGNAL_PEER_DISPLAY_TEST_CONTENT(result, expected)                                \
  auto value = ZxObjectSignalPeer(result, #result, kHandle, ZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_3, \
                                  ZX_USER_SIGNAL_5 | ZX_USER_SIGNAL_7);                          \
  PerformDisplayTest("$plt(zx_object_signal_peer)", std::move(value), expected);

#define OBJECT_SIGNAL_PEER_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    OBJECT_SIGNAL_PEER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    OBJECT_SIGNAL_PEER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SIGNAL_PEER_DISPLAY_TEST(
    ZxObjectSignalPeer, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_signal_peer("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "clear_mask: \x1B[32msignals\x1B[0m = \x1B[34mZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_3\x1B[0m, "
    "set_mask: \x1B[32msignals\x1B[0m = \x1B[34mZX_USER_SIGNAL_5 | ZX_USER_SIGNAL_7\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_object_get_property tests.

std::unique_ptr<SystemCallTest> ZxObjectGetProperty(int64_t result, std::string_view result_name,
                                                    zx_handle_t handle, uint32_t property,
                                                    void* value, size_t value_size) {
  auto syscall = std::make_unique<SystemCallTest>("zx_object_get_property", result, result_name);
  syscall->AddInput(handle);
  syscall->AddInput(property);
  syscall->AddInput(reinterpret_cast<uint64_t>(value));
  syscall->AddInput(value_size);
  return syscall;
}

#define OBJECT_GET_PROPERTY_NAME_DISPLAY_TEST_CONTENT(result, expected)                          \
  std::vector<char> buffer(ZX_MAX_NAME_LEN);                                                     \
  std::string data = "My_name";                                                                  \
  memcpy(buffer.data(), data.c_str(), data.size() + 1);                                          \
  PerformDisplayTest(                                                                            \
      "$plt(zx_object_get_property)",                                                            \
      ZxObjectGetProperty(result, #result, kHandle, ZX_PROP_NAME, buffer.data(), buffer.size()), \
      expected);

#define OBJECT_GET_PROPERTY_NAME_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                        \
    OBJECT_GET_PROPERTY_NAME_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                  \
  TEST_F(InterceptionWorkflowTestArm, name) {                        \
    OBJECT_GET_PROPERTY_NAME_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_PROPERTY_NAME_DISPLAY_TEST(
    ZxObjectGetPropertyName, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_NAME\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (value: \x1B[32mstring\x1B[0m = \x1B[31m\"My_name\"\x1B[0m)\n");

#define OBJECT_GET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST_CONTENT(result, expected)          \
  uintptr_t value = 0x45678;                                                                   \
  PerformDisplayTest("$plt(zx_object_get_property)",                                           \
                     ZxObjectGetProperty(result, #result, kHandle, ZX_PROP_PROCESS_DEBUG_ADDR, \
                                         &value, sizeof(value)),                               \
                     expected);

#define OBJECT_GET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                      \
    OBJECT_GET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                                \
  TEST_F(InterceptionWorkflowTestArm, name) {                                      \
    OBJECT_GET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST(
    ZxObjectGetPropertyProcessDebugAddr, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_PROCESS_DEBUG_ADDR\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (value: \x1B[32mzx.vaddr\x1B[0m = "
    "\x1B[34m0000000000045678\x1B[0m)\n");

#define OBJECT_GET_PROPERTY_PROCESS_VDSO_BASE_ADDRESS_DISPLAY_TEST_CONTENT(result, expected)   \
  uintptr_t value = 0x45678;                                                                   \
  PerformDisplayTest(                                                                          \
      "$plt(zx_object_get_property)",                                                          \
      ZxObjectGetProperty(result, #result, kHandle, ZX_PROP_PROCESS_VDSO_BASE_ADDRESS, &value, \
                          sizeof(value)),                                                      \
      expected);

#define OBJECT_GET_PROPERTY_PROCESS_VDSO_BASE_ADDRESS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                             \
    OBJECT_GET_PROPERTY_PROCESS_VDSO_BASE_ADDRESS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) {                                             \
    OBJECT_GET_PROPERTY_PROCESS_VDSO_BASE_ADDRESS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_PROPERTY_PROCESS_VDSO_BASE_ADDRESS_DISPLAY_TEST(
    ZxObjectGetPropertyProcessVdsoBaseAddress, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_PROCESS_VDSO_BASE_ADDRESS\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (value: \x1B[32mzx.vaddr\x1B[0m = "
    "\x1B[34m0000000000045678\x1B[0m)\n");

#define OBJECT_GET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST_CONTENT(result, expected)          \
  size_t value = 1000;                                                                          \
  PerformDisplayTest("$plt(zx_object_get_property)",                                            \
                     ZxObjectGetProperty(result, #result, kHandle, ZX_PROP_SOCKET_RX_THRESHOLD, \
                                         &value, sizeof(value)),                                \
                     expected);

#define OBJECT_GET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                       \
    OBJECT_GET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                                 \
  TEST_F(InterceptionWorkflowTestArm, name) {                                       \
    OBJECT_GET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST(
    ZxObjectGetPropertySocketRxThreshold, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_SOCKET_RX_THRESHOLD\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (value: \x1B[32msize\x1B[0m = \x1B[34m1000\x1B[0m)\n");

#define OBJECT_GET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST_CONTENT(result, expected)          \
  size_t value = 1000;                                                                          \
  PerformDisplayTest("$plt(zx_object_get_property)",                                            \
                     ZxObjectGetProperty(result, #result, kHandle, ZX_PROP_SOCKET_TX_THRESHOLD, \
                                         &value, sizeof(value)),                                \
                     expected);

#define OBJECT_GET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                       \
    OBJECT_GET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                                 \
  TEST_F(InterceptionWorkflowTestArm, name) {                                       \
    OBJECT_GET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST(
    ZxObjectGetPropertySocketTxThreshold, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_SOCKET_TX_THRESHOLD\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (value: \x1B[32msize\x1B[0m = \x1B[34m1000\x1B[0m)\n");

#define OBJECT_GET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST_CONTENT(result, expected)          \
  uint32_t value = ZX_EXCEPTION_STATE_HANDLED;                                              \
  PerformDisplayTest("$plt(zx_object_get_property)",                                        \
                     ZxObjectGetProperty(result, #result, kHandle, ZX_PROP_EXCEPTION_STATE, \
                                         &value, sizeof(value)),                            \
                     expected);

#define OBJECT_GET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                   \
    OBJECT_GET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                                   \
    OBJECT_GET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST(
    ZxObjectGetPropertyExceptionState, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_EXCEPTION_STATE\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "value: \x1B[32mzx.exception_state\x1B[0m = \x1B[34mZX_EXCEPTION_STATE_HANDLED\x1B[0m)\n");

// zx_object_set_property tests.

std::unique_ptr<SystemCallTest> ZxObjectSetProperty(int64_t result, std::string_view result_name,
                                                    zx_handle_t handle, uint32_t property,
                                                    const void* value, size_t value_size) {
  auto syscall = std::make_unique<SystemCallTest>("zx_object_set_property", result, result_name);
  syscall->AddInput(handle);
  syscall->AddInput(property);
  syscall->AddInput(reinterpret_cast<uint64_t>(value));
  syscall->AddInput(value_size);
  return syscall;
}

#define OBJECT_SET_PROPERTY_NAME_DISPLAY_TEST_CONTENT(result, expected)                          \
  std::vector<char> buffer(ZX_MAX_NAME_LEN);                                                     \
  std::string data = "My_name";                                                                  \
  memcpy(buffer.data(), data.c_str(), data.size() + 1);                                          \
  PerformDisplayTest(                                                                            \
      "$plt(zx_object_set_property)",                                                            \
      ZxObjectSetProperty(result, #result, kHandle, ZX_PROP_NAME, buffer.data(), buffer.size()), \
      expected);

#define OBJECT_SET_PROPERTY_NAME_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                        \
    OBJECT_SET_PROPERTY_NAME_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                  \
  TEST_F(InterceptionWorkflowTestArm, name) {                        \
    OBJECT_SET_PROPERTY_NAME_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROPERTY_NAME_DISPLAY_TEST(
    ZxObjectSetPropertyName, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_NAME\x1B[0m, "
    "value: \x1B[32mstring\x1B[0m = \x1B[31m\"My_name\"\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define OBJECT_SET_PROPERTY_REGISTER_FS_DISPLAY_TEST_CONTENT(result, expected)                   \
  uintptr_t value = 0x45678;                                                                     \
  PerformDisplayTest(                                                                            \
      "$plt(zx_object_set_property)",                                                            \
      ZxObjectSetProperty(result, #result, kHandle, ZX_PROP_REGISTER_FS, &value, sizeof(value)), \
      expected);

#define OBJECT_SET_PROPERTY_REGISTER_FS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                               \
    OBJECT_SET_PROPERTY_REGISTER_FS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {                               \
    OBJECT_SET_PROPERTY_REGISTER_FS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROPERTY_REGISTER_FS_DISPLAY_TEST(
    ZxObjectSetPropertyRegisterFs, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_REGISTER_FS\x1B[0m, "
    "value: \x1B[32mzx.vaddr\x1B[0m = \x1B[34m0000000000045678\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define OBJECT_SET_PROPERTY_REGISTER_GS_DISPLAY_TEST_CONTENT(result, expected)                   \
  uintptr_t value = 0x45678;                                                                     \
  PerformDisplayTest(                                                                            \
      "$plt(zx_object_set_property)",                                                            \
      ZxObjectSetProperty(result, #result, kHandle, ZX_PROP_REGISTER_GS, &value, sizeof(value)), \
      expected);

#define OBJECT_SET_PROPERTY_REGISTER_GS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                               \
    OBJECT_SET_PROPERTY_REGISTER_GS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {                               \
    OBJECT_SET_PROPERTY_REGISTER_GS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROPERTY_REGISTER_GS_DISPLAY_TEST(
    ZxObjectSetPropertyRegisterGs, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_REGISTER_GS\x1B[0m, "
    "value: \x1B[32mzx.vaddr\x1B[0m = \x1B[34m0000000000045678\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define OBJECT_SET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST_CONTENT(result, expected)          \
  uintptr_t value = 0x45678;                                                                   \
  PerformDisplayTest("$plt(zx_object_set_property)",                                           \
                     ZxObjectSetProperty(result, #result, kHandle, ZX_PROP_PROCESS_DEBUG_ADDR, \
                                         &value, sizeof(value)),                               \
                     expected);

#define OBJECT_SET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                      \
    OBJECT_SET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                                \
  TEST_F(InterceptionWorkflowTestArm, name) {                                      \
    OBJECT_SET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROPERTY_PROCESS_DEBUG_ADDR_DISPLAY_TEST(
    ZxObjectSetPropertyProcessDebugAddr, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_PROCESS_DEBUG_ADDR\x1B[0m, "
    "value: \x1B[32mzx.vaddr\x1B[0m = \x1B[34m0000000000045678\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define OBJECT_SET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST_CONTENT(result, expected)          \
  size_t value = 1000;                                                                          \
  PerformDisplayTest("$plt(zx_object_set_property)",                                            \
                     ZxObjectSetProperty(result, #result, kHandle, ZX_PROP_SOCKET_RX_THRESHOLD, \
                                         &value, sizeof(value)),                                \
                     expected);

#define OBJECT_SET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                       \
    OBJECT_SET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                                 \
  TEST_F(InterceptionWorkflowTestArm, name) {                                       \
    OBJECT_SET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROPERTY_SOCKET_RX_THRESHOLD_DISPLAY_TEST(
    ZxObjectSetPropertySocketRxThreshold, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_SOCKET_RX_THRESHOLD\x1B[0m, "
    "value: \x1B[32msize\x1B[0m = \x1B[34m1000\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define OBJECT_SET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST_CONTENT(result, expected)          \
  size_t value = 1000;                                                                          \
  PerformDisplayTest("$plt(zx_object_set_property)",                                            \
                     ZxObjectSetProperty(result, #result, kHandle, ZX_PROP_SOCKET_TX_THRESHOLD, \
                                         &value, sizeof(value)),                                \
                     expected);

#define OBJECT_SET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                       \
    OBJECT_SET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                                 \
  TEST_F(InterceptionWorkflowTestArm, name) {                                       \
    OBJECT_SET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROPERTY_SOCKET_TX_THRESHOLD_DISPLAY_TEST(
    ZxObjectSetPropertySocketTxThreshold, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_SOCKET_TX_THRESHOLD\x1B[0m, "
    "value: \x1B[32msize\x1B[0m = \x1B[34m1000\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define OBJECT_SET_PROPERTY_JOB_KILL_ON_OOM_DISPLAY_TEST_CONTENT(result, expected)          \
  size_t value = 1;                                                                         \
  PerformDisplayTest("$plt(zx_object_set_property)",                                        \
                     ZxObjectSetProperty(result, #result, kHandle, ZX_PROP_JOB_KILL_ON_OOM, \
                                         &value, sizeof(value)),                            \
                     expected);

#define OBJECT_SET_PROPERTY_JOB_KILL_ON_OOM_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                   \
    OBJECT_SET_PROPERTY_JOB_KILL_ON_OOM_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                                   \
    OBJECT_SET_PROPERTY_JOB_KILL_ON_OOM_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROPERTY_JOB_KILL_ON_OOM_DISPLAY_TEST(
    ZxObjectSetPropertyJobKillOnOom, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_JOB_KILL_ON_OOM\x1B[0m, "
    "value: \x1B[32msize\x1B[0m = \x1B[34m1\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define OBJECT_SET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST_CONTENT(result, expected)          \
  uint32_t value = ZX_EXCEPTION_STATE_HANDLED;                                              \
  PerformDisplayTest("$plt(zx_object_set_property)",                                        \
                     ZxObjectSetProperty(result, #result, kHandle, ZX_PROP_EXCEPTION_STATE, \
                                         &value, sizeof(value)),                            \
                     expected);

#define OBJECT_SET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                   \
    OBJECT_SET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                                   \
    OBJECT_SET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROPERTY_EXCEPTION_STATE_DISPLAY_TEST(
    ZxObjectSetPropertyExceptionState, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_property("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "property: \x1B[32mzx.prop_type\x1B[0m = \x1B[34mZX_PROP_EXCEPTION_STATE\x1B[0m, "
    "value: \x1B[32mzx.exception_state\x1B[0m = \x1B[34mZX_EXCEPTION_STATE_HANDLED\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_object_get_info tests.

std::unique_ptr<SystemCallTest> ZxObjectGetInfo(int64_t result, std::string_view result_name,
                                                zx_handle_t handle, uint32_t topic, void* buffer,
                                                size_t buffer_size, size_t* actual, size_t* avail) {
  auto value = std::make_unique<SystemCallTest>("zx_object_get_info", result, result_name);
  value->AddInput(handle);
  value->AddInput(topic);
  value->AddInput(reinterpret_cast<uint64_t>(buffer));
  value->AddInput(buffer_size);
  value->AddInput(reinterpret_cast<uint64_t>(actual));
  value->AddInput(reinterpret_cast<uint64_t>(avail));
  return value;
}

#define OBJECT_GET_INFO_HANDLE_VALID_DISPLAY_TEST_CONTENT(result, expected)                \
  auto value = ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_HANDLE_VALID, nullptr, 0, \
                               nullptr, nullptr);                                          \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_HANDLE_VALID_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                            \
    OBJECT_GET_INFO_HANDLE_VALID_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) {                            \
    OBJECT_GET_INFO_HANDLE_VALID_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_HANDLE_VALID_DISPLAY_TEST(
    ZxObjectGetInfoHandleValidOk, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_HANDLE_VALID\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

OBJECT_GET_INFO_HANDLE_VALID_DISPLAY_TEST(
    ZxObjectGetInfoHandleValidBad, ZX_ERR_BAD_HANDLE,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_HANDLE_VALID\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[31mZX_ERR_BAD_HANDLE\x1B[0m\n");

#define OBJECT_GET_INFO_HANDLE_BASIC_DISPLAY_TEST_CONTENT(result, expected)                   \
  zx_info_handle_basic_t buffer;                                                              \
  buffer.koid = kKoid;                                                                        \
  buffer.rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | \
                  ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT;                                           \
  buffer.type = ZX_OBJ_TYPE_LOG;                                                              \
  buffer.related_koid = 0;                                                                    \
  auto value =                                                                                \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_HANDLE_BASIC,                         \
                      reinterpret_cast<void*>(&buffer), sizeof(buffer), nullptr, nullptr);    \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_HANDLE_BASIC_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                            \
    OBJECT_GET_INFO_HANDLE_BASIC_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) {                            \
    OBJECT_GET_INFO_HANDLE_BASIC_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_HANDLE_BASIC_DISPLAY_TEST(
    ZxObjectGetInfoHandleBasic, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_HANDLE_BASIC\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m32\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_handle_basic_t\x1B[0m = {\n"
    "      koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m4252\x1B[0m\n"
    "      rights: \x1B[32mzx_rights_t\x1B[0m = \x1B[34mZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | "
    "ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT\x1B[0m\n"
    "      type: \x1B[32mzx_obj_type_t\x1B[0m = \x1B[34mZX_OBJ_TYPE_LOG\x1B[0m\n"
    "      related_koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m0\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_HANDLE_COUNT_DISPLAY_TEST_CONTENT(result, expected)                \
  zx_info_handle_count_t buffer;                                                           \
  buffer.handle_count = 2;                                                                 \
  auto value =                                                                             \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_HANDLE_COUNT,                      \
                      reinterpret_cast<void*>(&buffer), sizeof(buffer), nullptr, nullptr); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_HANDLE_COUNT_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                            \
    OBJECT_GET_INFO_HANDLE_COUNT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) {                            \
    OBJECT_GET_INFO_HANDLE_COUNT_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_HANDLE_COUNT_DISPLAY_TEST(
    ZxObjectGetInfoHandleCount, ZX_OK,
    "\ntest_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_HANDLE_COUNT\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m4\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_handle_count_t\x1B[0m = {\n"
    "      handle_count: \x1B[32muint32\x1B[0m = \x1B[34m2\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_PROCESS_HANDLE_STATS_DISPLAY_TEST_CONTENT(result, expected)        \
  zx_info_process_handle_stats_t buffer;                                                   \
  memset(&buffer, 0, sizeof(buffer));                                                      \
  buffer.handle_count[ZX_OBJ_TYPE_THREAD] = 3;                                             \
  buffer.handle_count[ZX_OBJ_TYPE_SOCKET] = 2;                                             \
  buffer.handle_count[ZX_OBJ_TYPE_TIMER] = 1;                                              \
  auto value =                                                                             \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_PROCESS_HANDLE_STATS,              \
                      reinterpret_cast<void*>(&buffer), sizeof(buffer), nullptr, nullptr); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_PROCESS_HANDLE_STATS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                    \
    OBJECT_GET_INFO_PROCESS_HANDLE_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                                    \
    OBJECT_GET_INFO_PROCESS_HANDLE_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_PROCESS_HANDLE_STATS_DISPLAY_TEST(
    ZxObjectGetInfoProcessHandleStats, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_PROCESS_HANDLE_STATS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m256\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_process_handle_stats_t\x1B[0m = {\n"
    "      handle_count: vector<\x1B[32muint32\x1B[0m> = [ "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m3\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m2\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m1\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, "
    "\x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m, \x1B[34m0\x1B[0m ]\n"
    "    }\n");

#define OBJECT_GET_INFO_JOB_DISPLAY_TEST_CONTENT(result, expected)                             \
  zx_info_job_t buffer;                                                                        \
  buffer.return_code = -1;                                                                     \
  buffer.exited = true;                                                                        \
  buffer.kill_on_oom = false;                                                                  \
  buffer.debugger_attached = false;                                                            \
  auto value =                                                                                 \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_JOB, reinterpret_cast<void*>(&buffer), \
                      sizeof(buffer), nullptr, nullptr);                                       \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_JOB_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    OBJECT_GET_INFO_JOB_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    OBJECT_GET_INFO_JOB_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_JOB_DISPLAY_TEST(
    ZxObjectGetInfoJob, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_JOB\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m16\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_job_t\x1B[0m = {\n"
    "      return_code: \x1B[32mint64\x1B[0m = \x1B[34m-1\x1B[0m\n"
    "      exited: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m\n"
    "      kill_on_oom: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m\n"
    "      debugger_attached: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_PROCESS_DISPLAY_TEST_CONTENT(result, expected)                             \
  zx_info_process_t buffer;                                                                        \
  buffer.return_code = -1;                                                                         \
  buffer.started = true;                                                                           \
  buffer.exited = true;                                                                            \
  buffer.debugger_attached = false;                                                                \
  auto value =                                                                                     \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_PROCESS, reinterpret_cast<void*>(&buffer), \
                      sizeof(buffer), nullptr, nullptr);                                           \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_PROCESS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                       \
    OBJECT_GET_INFO_PROCESS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                 \
  TEST_F(InterceptionWorkflowTestArm, name) {                       \
    OBJECT_GET_INFO_PROCESS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_PROCESS_DISPLAY_TEST(
    ZxObjectGetInfoProcess, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_PROCESS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m16\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_process_t\x1B[0m = {\n"
    "      return_code: \x1B[32mint64\x1B[0m = \x1B[34m-1\x1B[0m\n"
    "      started: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m\n"
    "      exited: \x1B[32mbool\x1B[0m = \x1B[34mtrue\x1B[0m\n"
    "      debugger_attached: \x1B[32mbool\x1B[0m = \x1B[34mfalse\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_PROCESS_THREADS_DISPLAY_TEST_CONTENT(result, expected)      \
  constexpr zx_koid_t kThread1 = 1111;                                              \
  constexpr zx_koid_t kThread2 = 2222;                                              \
  constexpr zx_koid_t kThread3 = 3333;                                              \
  std::vector<zx_koid_t> buffer = {kThread1, kThread2, kThread3};                   \
  size_t actual = buffer.size();                                                    \
  size_t avail = buffer.size() + 2;                                                 \
  auto value = ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_PROCESS_THREADS,   \
                               reinterpret_cast<void*>(buffer.data()),              \
                               buffer.size() * sizeof(zx_koid_t), &actual, &avail); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_PROCESS_THREADS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                               \
    OBJECT_GET_INFO_PROCESS_THREADS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                         \
  TEST_F(InterceptionWorkflowTestArm, name) {                               \
    OBJECT_GET_INFO_PROCESS_THREADS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_PROCESS_THREADS_DISPLAY_TEST(
    ZxObjectGetInfoProcessThreads, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_PROCESS_THREADS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m24\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (actual: \x1B[32msize_t\x1B[0m = "
    "\x1B[34m3\x1B[0m/\x1B[34m5\x1B[0m)\n"
    "    info: \x1B[32mzx_koid_t\x1B[0m = "
    "\x1B[31m1111\x1B[0m, \x1B[31m2222\x1B[0m, \x1B[31m3333\x1B[0m\n");

#define OBJECT_GET_INFO_THREAD_DISPLAY_TEST_CONTENT(result, expected)                             \
  zx_info_thread_t buffer;                                                                        \
  buffer.state = ZX_THREAD_STATE_BLOCKED_EXCEPTION;                                               \
  buffer.wait_exception_channel_type = ZX_EXCEPTION_CHANNEL_TYPE_THREAD;                          \
  memset(&buffer.cpu_affinity_mask, 0, sizeof(buffer.cpu_affinity_mask));                         \
  buffer.cpu_affinity_mask.mask[0] = 0xe;                                                         \
  auto value =                                                                                    \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_THREAD, reinterpret_cast<void*>(&buffer), \
                      sizeof(buffer), nullptr, nullptr);                                          \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_THREAD_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                      \
    OBJECT_GET_INFO_THREAD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                \
  TEST_F(InterceptionWorkflowTestArm, name) {                      \
    OBJECT_GET_INFO_THREAD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_THREAD_DISPLAY_TEST(
    ZxObjectGetInfoThread, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_THREAD\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m72\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_thread_t\x1B[0m = {\n"
    "      state: \x1B[32mzx_info_thread_t::state\x1B[0m = "
    "\x1B[34mZX_THREAD_STATE_BLOCKED_EXCEPTION\x1B[0m\n"
    "      "
    "wait_exception_channel_type: \x1B[32mzx_info_thread_t::wait_exception_channel_type\x1B[0m = "
    "\x1B[34mZX_EXCEPTION_CHANNEL_TYPE_THREAD\x1B[0m\n"
    "      cpu_affinity_mask: \x1B[32mzx_cpu_set_t\x1B[0m = {\n"
    "        mask: vector<\x1B[32muint64\x1B[0m> = [ "
    "\x1B[34m000000000000000e\x1B[0m, \x1B[34m0000000000000000\x1B[0m, "
    "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000000000000\x1B[0m, "
    "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000000000000\x1B[0m, "
    "\x1B[34m0000000000000000\x1B[0m, \x1B[34m0000000000000000\x1B[0m ]\n"
    "      }\n"
    "    }\n");

#define OBJECT_GET_INFO_THREAD_STATS_DISPLAY_TEST_CONTENT(result, expected)                \
  zx_info_thread_stats_t buffer;                                                           \
  buffer.total_runtime = ZX_SEC(3664) + 1234;                                              \
  buffer.last_scheduled_cpu = 1;                                                           \
  auto value =                                                                             \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_THREAD_STATS,                      \
                      reinterpret_cast<void*>(&buffer), sizeof(buffer), nullptr, nullptr); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_THREAD_STATS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                            \
    OBJECT_GET_INFO_THREAD_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) {                            \
    OBJECT_GET_INFO_THREAD_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_THREAD_STATS_DISPLAY_TEST(
    ZxObjectGetInfoThreadStats, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_THREAD_STATS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m16\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_thread_stats_t\x1B[0m = {\n"
    "      total_runtime: \x1B[32mduration\x1B[0m = "
    "\x1B[34m1 hours, 1 minutes, 4 seconds and 1234 nano seconds\x1B[0m\n"
    "      last_scheduled_cpu: \x1B[32muint32\x1B[0m = \x1B[34m1\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_CPU_STATS_DISPLAY_TEST_CONTENT(result, expected)                   \
  constexpr zx_duration_t kIdleTime = ZX_SEC(50) + 567;                                    \
  constexpr uint64_t kReschedules = 321;                                                   \
  constexpr uint64_t kContextSwitches = 130;                                               \
  constexpr uint64_t kIrqPreempts = 10;                                                    \
  constexpr uint64_t kPreempts = 20;                                                       \
  constexpr uint64_t kYields = 5;                                                          \
  constexpr uint64_t kInts = 3;                                                            \
  constexpr uint64_t kTimerInts = 1;                                                       \
  constexpr uint64_t kTimers = 1;                                                          \
  constexpr uint64_t kSyscalls = 15;                                                       \
  constexpr uint64_t kRecheduleIpis = 2;                                                   \
  constexpr uint64_t kGenericIpis = 1;                                                     \
  zx_info_cpu_stats_t buffer;                                                              \
  buffer.cpu_number = 1;                                                                   \
  buffer.flags = 0;                                                                        \
  buffer.idle_time = kIdleTime;                                                            \
  buffer.reschedules = kReschedules;                                                       \
  buffer.context_switches = kContextSwitches;                                              \
  buffer.irq_preempts = kIrqPreempts;                                                      \
  buffer.preempts = kPreempts;                                                             \
  buffer.yields = kYields;                                                                 \
  buffer.ints = kInts;                                                                     \
  buffer.timer_ints = kTimerInts;                                                          \
  buffer.timers = kTimers;                                                                 \
  buffer.syscalls = kSyscalls;                                                             \
  buffer.reschedule_ipis = kRecheduleIpis;                                                 \
  buffer.generic_ipis = kGenericIpis;                                                      \
  auto value =                                                                             \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_CPU_STATS,                         \
                      reinterpret_cast<void*>(&buffer), sizeof(buffer), nullptr, nullptr); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_CPU_STATS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                         \
    OBJECT_GET_INFO_CPU_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                   \
  TEST_F(InterceptionWorkflowTestArm, name) {                         \
    OBJECT_GET_INFO_CPU_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_CPU_STATS_DISPLAY_TEST(
    ZxObjectGetInfoCpuStats, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_CPU_STATS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m120\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_cpu_stats_t\x1B[0m = {\n"
    "      cpu_number: \x1B[32muint32\x1B[0m = \x1B[34m1\x1B[0m\n"
    "      flags: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m\n"
    "      idle_time: \x1B[32mduration\x1B[0m = \x1B[34m50 seconds and 567 nano seconds\x1B[0m\n"
    "      reschedules: \x1B[32muint64\x1B[0m = \x1B[34m321\x1B[0m\n"
    "      context_switches: \x1B[32muint64\x1B[0m = \x1B[34m130\x1B[0m\n"
    "      irq_preempts: \x1B[32muint64\x1B[0m = \x1B[34m10\x1B[0m\n"
    "      preempts: \x1B[32muint64\x1B[0m = \x1B[34m20\x1B[0m\n"
    "      yields: \x1B[32muint64\x1B[0m = \x1B[34m5\x1B[0m\n"
    "      ints: \x1B[32muint64\x1B[0m = \x1B[34m3\x1B[0m\n"
    "      timer_ints: \x1B[32muint64\x1B[0m = \x1B[34m1\x1B[0m\n"
    "      timers: \x1B[32muint64\x1B[0m = \x1B[34m1\x1B[0m\n"
    "      syscalls: \x1B[32muint64\x1B[0m = \x1B[34m15\x1B[0m\n"
    "      reschedule_ipis: \x1B[32muint64\x1B[0m = \x1B[34m2\x1B[0m\n"
    "      generic_ipis: \x1B[32muint64\x1B[0m = \x1B[34m1\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_VMAR_DISPLAY_TEST_CONTENT(result, expected)                             \
  constexpr uintptr_t kBase = 0x124680aceUL;                                                    \
  constexpr size_t kLen = 4096;                                                                 \
  zx_info_vmar_t buffer;                                                                        \
  buffer.base = kBase;                                                                          \
  buffer.len = kLen;                                                                            \
  auto value =                                                                                  \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_VMAR, reinterpret_cast<void*>(&buffer), \
                      sizeof(buffer), nullptr, nullptr);                                        \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_VMAR_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    OBJECT_GET_INFO_VMAR_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    OBJECT_GET_INFO_VMAR_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_VMAR_DISPLAY_TEST(
    ZxObjectGetInfoVmar, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_VMAR\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m16\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_vmar_t\x1B[0m = {\n"
    "      base: \x1B[32muintptr_t\x1B[0m = \x1B[34m0000000124680ace\x1B[0m\n"
    "      len: \x1B[32msize_t\x1B[0m = \x1B[34m4096\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_VMO_DISPLAY_TEST_CONTENT(result, expected)                             \
  constexpr size_t kSizeBytes = 4000;                                                          \
  constexpr size_t kCommittedBytes = 4096;                                                     \
  zx_info_vmo_t buffer{                                                                        \
      .koid = kKoid,                                                                           \
      .name = "my_vmo",                                                                        \
      .size_bytes = kSizeBytes,                                                                \
      .parent_koid = 0,                                                                        \
      .num_children = 2,                                                                       \
      .num_mappings = 1,                                                                       \
      .share_count = 3,                                                                        \
      .flags = ZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_RESIZABLE | ZX_INFO_VMO_CONTIGUOUS,        \
      .committed_bytes = kCommittedBytes,                                                      \
      .handle_rights = 0,                                                                      \
      .cache_policy = ZX_CACHE_POLICY_CACHED};                                                 \
  auto value =                                                                                 \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_VMO, reinterpret_cast<void*>(&buffer), \
                      sizeof(buffer), nullptr, nullptr);                                       \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_VMO_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    OBJECT_GET_INFO_VMO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    OBJECT_GET_INFO_VMO_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_VMO_DISPLAY_TEST(
    ZxObjectGetInfoVmo, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_VMO\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m120\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_vmo_t\x1B[0m = {\n"
    "      koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m4252\x1B[0m\n"
    "      name: vector<\x1B[32mchar\x1B[0m> = \x1B[31m\"my_vmo\"\x1B[0m\n"
    "      size_bytes: \x1B[32muint64\x1B[0m = \x1B[34m4000\x1B[0m\n"
    "      parent_koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m0\x1B[0m\n"
    "      num_children: \x1B[32msize_t\x1B[0m = \x1B[34m2\x1B[0m\n"
    "      num_mappings: \x1B[32msize_t\x1B[0m = \x1B[34m1\x1B[0m\n"
    "      share_count: \x1B[32msize_t\x1B[0m = \x1B[34m3\x1B[0m\n"
    "      flags: \x1B[32mzx_info_vmo_type_t\x1B[0m = "
    "\x1B[34mZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_RESIZABLE | ZX_INFO_VMO_CONTIGUOUS\x1B[0m\n"
    "      committed_bytes: \x1B[32muint64\x1B[0m = \x1B[34m4096\x1B[0m\n"
    "      handle_rights: \x1B[32mzx_rights_t\x1B[0m = \x1B[34mZX_RIGHT_NONE\x1B[0m\n"
    "      cache_policy: \x1B[32mzx_cache_policy_t\x1B[0m = \x1B[34mZX_CACHE_POLICY_CACHED\x1B[0m\n"
    "      metadata_bytes: \x1B[32muint64\x1B[0m = \x1B[34m0\x1B[0m\n"
    "      committed_change_events: \x1B[32muint64\x1B[0m = \x1B[34m0\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_SOCKET_DISPLAY_TEST_CONTENT(result, expected)                             \
  constexpr size_t kRxBufMax = 8192;                                                              \
  constexpr size_t kRxBufSize = 4096;                                                             \
  constexpr size_t kRxBufAvailable = 4002;                                                        \
  constexpr size_t kTxBufMax = 8192;                                                              \
  constexpr size_t kTxBufSize = 4096;                                                             \
  zx_info_socket_t buffer;                                                                        \
  buffer.options = 0;                                                                             \
  buffer.rx_buf_max = kRxBufMax;                                                                  \
  buffer.rx_buf_size = kRxBufSize;                                                                \
  buffer.rx_buf_available = kRxBufAvailable;                                                      \
  buffer.tx_buf_max = kTxBufMax;                                                                  \
  buffer.tx_buf_size = kTxBufSize;                                                                \
  auto value =                                                                                    \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_SOCKET, reinterpret_cast<void*>(&buffer), \
                      sizeof(buffer), nullptr, nullptr);                                          \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_SOCKET_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                      \
    OBJECT_GET_INFO_SOCKET_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                \
  TEST_F(InterceptionWorkflowTestArm, name) {                      \
    OBJECT_GET_INFO_SOCKET_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_SOCKET_DISPLAY_TEST(
    ZxObjectGetInfoSocket, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_SOCKET\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m48\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_socket_t\x1B[0m = {\n"
    "      options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m\n"
    "      rx_buf_max: \x1B[32msize_t\x1B[0m = \x1B[34m8192\x1B[0m\n"
    "      rx_buf_size: \x1B[32msize_t\x1B[0m = \x1B[34m4096\x1B[0m\n"
    "      rx_buf_available: \x1B[32msize_t\x1B[0m = \x1B[34m4002\x1B[0m\n"
    "      tx_buf_max: \x1B[32msize_t\x1B[0m = \x1B[34m8192\x1B[0m\n"
    "      tx_buf_size: \x1B[32msize_t\x1B[0m = \x1B[34m4096\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_TIMER_DISPLAY_TEST_CONTENT(result, expected)                             \
  constexpr int kDeadline = 1000;                                                                \
  constexpr zx_duration_t kSlack = 100;                                                          \
  zx_info_timer_t buffer;                                                                        \
  buffer.options = 0;                                                                            \
  buffer.deadline = ZX_SEC(kDeadline);                                                           \
  buffer.slack = kSlack;                                                                         \
  auto value =                                                                                   \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_TIMER, reinterpret_cast<void*>(&buffer), \
                      sizeof(buffer), nullptr, nullptr);                                         \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_TIMER_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                     \
    OBJECT_GET_INFO_TIMER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                               \
  TEST_F(InterceptionWorkflowTestArm, name) {                     \
    OBJECT_GET_INFO_TIMER_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_TIMER_DISPLAY_TEST(
    ZxObjectGetInfoTimer, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mtopic=25\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m24\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_timer_t\x1B[0m = {\n"
    "      options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m\n"
    "      deadline: \x1B[32mzx_time_t\x1B[0m = \x1B[34m16 minutes, 40 seconds\x1B[0m\n"
    "      slack: \x1B[32mduration\x1B[0m = \x1B[34m100 nano seconds\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_JOB_CHILDREN_DISPLAY_TEST_CONTENT(result, expected)         \
  constexpr zx_koid_t kThread1 = 1111;                                              \
  constexpr zx_koid_t kThread2 = 2222;                                              \
  constexpr zx_koid_t kThread3 = 3333;                                              \
  std::vector<zx_koid_t> buffer = {kThread1, kThread2, kThread3};                   \
  size_t actual = buffer.size();                                                    \
  size_t avail = buffer.size() + 2;                                                 \
  auto value = ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_JOB_CHILDREN,      \
                               reinterpret_cast<void*>(buffer.data()),              \
                               buffer.size() * sizeof(zx_koid_t), &actual, &avail); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_JOB_CHILDREN_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                            \
    OBJECT_GET_INFO_JOB_CHILDREN_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) {                            \
    OBJECT_GET_INFO_JOB_CHILDREN_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_JOB_CHILDREN_DISPLAY_TEST(
    ZxObjectGetInfoJobChildren, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_JOB_CHILDREN\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m24\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (actual: \x1B[32msize_t\x1B[0m = "
    "\x1B[34m3\x1B[0m/\x1B[34m5\x1B[0m)\n"
    "    info: \x1B[32mzx_koid_t\x1B[0m = "
    "\x1B[31m1111\x1B[0m, \x1B[31m2222\x1B[0m, \x1B[31m3333\x1B[0m\n");

#define OBJECT_GET_INFO_JOB_PROCESSES_DISPLAY_TEST_CONTENT(result, expected)        \
  constexpr zx_koid_t kThread1 = 1111;                                              \
  constexpr zx_koid_t kThread2 = 2222;                                              \
  constexpr zx_koid_t kThread3 = 3333;                                              \
  std::vector<zx_koid_t> buffer = {kThread1, kThread2, kThread3};                   \
  size_t actual = buffer.size();                                                    \
  size_t avail = buffer.size() + 2;                                                 \
  auto value = ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_JOB_PROCESSES,     \
                               reinterpret_cast<void*>(buffer.data()),              \
                               buffer.size() * sizeof(zx_koid_t), &actual, &avail); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_JOB_PROCESSES_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                             \
    OBJECT_GET_INFO_JOB_PROCESSES_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                       \
  TEST_F(InterceptionWorkflowTestArm, name) {                             \
    OBJECT_GET_INFO_JOB_PROCESSES_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_JOB_PROCESSES_DISPLAY_TEST(
    ZxObjectGetInfoJobProcesses, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_JOB_PROCESSES\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m24\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (actual: \x1B[32msize_t\x1B[0m = "
    "\x1B[34m3\x1B[0m/\x1B[34m5\x1B[0m)\n"
    "    info: \x1B[32mzx_koid_t\x1B[0m = "
    "\x1B[31m1111\x1B[0m, \x1B[31m2222\x1B[0m, \x1B[31m3333\x1B[0m\n");

#define OBJECT_GET_INFO_TASK_STATS_DISPLAY_TEST_CONTENT(result, expected)                  \
  constexpr size_t kMemMappedBytes = 65536;                                                \
  constexpr size_t kMemPrivateBytes = 16000;                                               \
  constexpr size_t kMemSharedBytes = 20000;                                                \
  constexpr size_t kMemScaledSharedBytes = 5000;                                           \
  zx_info_task_stats_t buffer;                                                             \
  buffer.mem_mapped_bytes = kMemMappedBytes;                                               \
  buffer.mem_private_bytes = kMemPrivateBytes;                                             \
  buffer.mem_shared_bytes = kMemSharedBytes;                                               \
  buffer.mem_scaled_shared_bytes = kMemScaledSharedBytes;                                  \
  auto value =                                                                             \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_TASK_STATS,                        \
                      reinterpret_cast<void*>(&buffer), sizeof(buffer), nullptr, nullptr); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_TASK_STATS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                          \
    OBJECT_GET_INFO_TASK_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                    \
  TEST_F(InterceptionWorkflowTestArm, name) {                          \
    OBJECT_GET_INFO_TASK_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_TASK_STATS_DISPLAY_TEST(
    ZxObjectGetInfoTaskStats, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_TASK_STATS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m32\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_task_stats_t\x1B[0m = {\n"
    "      mem_mapped_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m65536\x1B[0m\n"
    "      mem_private_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m16000\x1B[0m\n"
    "      mem_shared_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m20000\x1B[0m\n"
    "      mem_scaled_shared_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m5000\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_PROCESS_MAPS_DISPLAY_TEST_CONTENT(result, expected)                       \
  std::vector<zx_info_maps_t> buffer(3);                                                          \
  buffer[0] = {                                                                                   \
      .name = "map1", .base = 0x10000, .size = 4096, .depth = 1, .type = ZX_INFO_MAPS_TYPE_NONE}; \
  buffer[1] = {                                                                                   \
      .name = "map2",                                                                             \
      .base = 0x20000,                                                                            \
      .size = 2048,                                                                               \
      .depth = 2,                                                                                 \
      .type = ZX_INFO_MAPS_TYPE_MAPPING,                                                          \
      .u = {.mapping = {.mmu_flags = ZX_VM_ALIGN_2KB | ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE,      \
                        .vmo_koid = 5555,                                                         \
                        .vmo_offset = 2048,                                                       \
                        .committed_pages = 1}}};                                                  \
  buffer[2] = {                                                                                   \
      .name = "map3", .base = 0x40000, .size = 2048, .depth = 2, .type = ZX_INFO_MAPS_TYPE_VMAR}; \
  size_t actual = buffer.size();                                                                  \
  size_t avail = 10;                                                                              \
  auto value = ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_PROCESS_MAPS,                    \
                               reinterpret_cast<void*>(buffer.data()),                            \
                               buffer.size() * sizeof(zx_info_maps_t), &actual, &avail);          \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_PROCESS_MAPS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                            \
    OBJECT_GET_INFO_PROCESS_MAPS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) {                            \
    OBJECT_GET_INFO_PROCESS_MAPS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_PROCESS_MAPS_DISPLAY_TEST(
    ZxObjectGetInfoProcessMaps, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_PROCESS_MAPS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m288\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "actual: \x1B[32msize_t\x1B[0m = \x1B[34m3\x1B[0m/\x1B[34m10\x1B[0m)\n"
    "    info: vector<\x1B[32mzx_info_maps_t\x1B[0m> =  [\n"
    "      {\n"
    "        name: vector<\x1B[32mchar\x1B[0m> = \x1B[31m\"map1\"\x1B[0m\n"
    "        base: \x1B[32mzx_vaddr_t\x1B[0m = \x1B[34m0000000000010000\x1B[0m\n"
    "        size: \x1B[32msize_t\x1B[0m = \x1B[34m4096\x1B[0m\n"
    "        depth: \x1B[32msize_t\x1B[0m = \x1B[34m1\x1B[0m\n"
    "        type: \x1B[32mzx_info_maps_type_t\x1B[0m = \x1B[31mZX_INFO_MAPS_TYPE_NONE\x1B[0m\n"
    "      },\n"
    "      {\n"
    "        name: vector<\x1B[32mchar\x1B[0m> = \x1B[31m\"map2\"\x1B[0m\n"
    "        base: \x1B[32mzx_vaddr_t\x1B[0m = \x1B[34m0000000000020000\x1B[0m\n"
    "        size: \x1B[32msize_t\x1B[0m = \x1B[34m2048\x1B[0m\n"
    "        depth: \x1B[32msize_t\x1B[0m = \x1B[34m2\x1B[0m\n"
    "        type: \x1B[32mzx_info_maps_type_t\x1B[0m = \x1B[31mZX_INFO_MAPS_TYPE_MAPPING\x1B[0m\n"
    "        mapping: \x1B[32mzx_info_maps_mapping_t\x1B[0m = {\n"
    "          mmu_flags: \x1B[32mzx_vm_option_t\x1B[0m = "
    "\x1B[31mZX_VM_ALIGN_2KB | ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE\x1B[0m\n"
    "          vmo_koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m5555\x1B[0m\n"
    "          vmo_offset: \x1B[32muint64\x1B[0m = \x1B[34m2048\x1B[0m\n"
    "          committed_pages: \x1B[32msize_t\x1B[0m = \x1B[34m1\x1B[0m\n"
    "        }\n"
    "      },\n"
    "      {\n"
    "        name: vector<\x1B[32mchar\x1B[0m> = \x1B[31m\"map3\"\x1B[0m\n"
    "        base: \x1B[32mzx_vaddr_t\x1B[0m = \x1B[34m0000000000040000\x1B[0m\n"
    "        size: \x1B[32msize_t\x1B[0m = \x1B[34m2048\x1B[0m\n"
    "        depth: \x1B[32msize_t\x1B[0m = \x1B[34m2\x1B[0m\n"
    "        type: \x1B[32mzx_info_maps_type_t\x1B[0m = \x1B[31mZX_INFO_MAPS_TYPE_VMAR\x1B[0m\n"
    "      }\n"
    "    ]\n");

#define OBJECT_GET_INFO_PROCESS_VMOS_DISPLAY_TEST_CONTENT(result, expected)                      \
  std::vector<zx_info_vmo_t> buffer(2);                                                          \
  buffer[0] = {.koid = kKoid,                                                                    \
               .name = "my_vmo1",                                                                \
               .size_bytes = 4000,                                                               \
               .parent_koid = 0,                                                                 \
               .num_children = 2,                                                                \
               .num_mappings = 1,                                                                \
               .share_count = 3,                                                                 \
               .flags = ZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_RESIZABLE | ZX_INFO_VMO_CONTIGUOUS, \
               .committed_bytes = 4096,                                                          \
               .handle_rights = 0,                                                               \
               .cache_policy = ZX_CACHE_POLICY_CACHED};                                          \
  buffer[1] = {.koid = kKoid2,                                                                   \
               .name = "my_vmo2",                                                                \
               .size_bytes = 8000,                                                               \
               .parent_koid = 0,                                                                 \
               .num_children = 2,                                                                \
               .num_mappings = 1,                                                                \
               .share_count = 3,                                                                 \
               .flags = ZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_RESIZABLE,                          \
               .committed_bytes = 4096,                                                          \
               .handle_rights = 0,                                                               \
               .cache_policy = ZX_CACHE_POLICY_CACHED};                                          \
  size_t actual = buffer.size();                                                                 \
  size_t avail = 2;                                                                              \
  auto value = ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_PROCESS_VMOS,                   \
                               reinterpret_cast<void*>(buffer.data()),                           \
                               buffer.size() * sizeof(zx_info_vmo_t), &actual, &avail);          \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_PROCESS_VMOS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                            \
    OBJECT_GET_INFO_PROCESS_VMOS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                      \
  TEST_F(InterceptionWorkflowTestArm, name) {                            \
    OBJECT_GET_INFO_PROCESS_VMOS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_PROCESS_VMOS_DISPLAY_TEST(
    ZxObjectGetInfoProcessVmos, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_PROCESS_VMOS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m240\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m ("
    "actual: \x1B[32msize_t\x1B[0m = \x1B[34m2\x1B[0m/\x1B[34m2\x1B[0m)\n"
    "    info: vector<\x1B[32mzx_info_vmo_t\x1B[0m> =  [\n"
    "      {\n"
    "        koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m4252\x1B[0m\n"
    "        name: vector<\x1B[32mchar\x1B[0m> = \x1B[31m\"my_vmo1\"\x1B[0m\n"
    "        size_bytes: \x1B[32muint64\x1B[0m = \x1B[34m4000\x1B[0m\n"
    "        parent_koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m0\x1B[0m\n"
    "        num_children: \x1B[32msize_t\x1B[0m = \x1B[34m2\x1B[0m\n"
    "        num_mappings: \x1B[32msize_t\x1B[0m = \x1B[34m1\x1B[0m\n"
    "        share_count: \x1B[32msize_t\x1B[0m = \x1B[34m3\x1B[0m\n"
    "        flags: \x1B[32mzx_info_vmo_type_t\x1B[0m = "
    "\x1B[34mZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_RESIZABLE | ZX_INFO_VMO_CONTIGUOUS\x1B[0m\n"
    "        committed_bytes: \x1B[32muint64\x1B[0m = \x1B[34m4096\x1B[0m\n"
    "        handle_rights: \x1B[32mzx_rights_t\x1B[0m = \x1B[34mZX_RIGHT_NONE\x1B[0m\n"
    "        cache_policy: \x1B[32mzx_cache_policy_t\x1B[0m = "
    "\x1B[34mZX_CACHE_POLICY_CACHED\x1B[0m\n"
    "        metadata_bytes: \x1B[32muint64\x1B[0m = \x1B[34m0\x1B[0m\n"
    "        committed_change_events: \x1B[32muint64\x1B[0m = \x1B[34m0\x1B[0m\n"
    "      },\n"
    "      {\n"
    "        koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m5242\x1B[0m\n"
    "        name: vector<\x1B[32mchar\x1B[0m> = \x1B[31m\"my_vmo2\"\x1B[0m\n"
    "        size_bytes: \x1B[32muint64\x1B[0m = \x1B[34m8000\x1B[0m\n"
    "        parent_koid: \x1B[32mzx_koid_t\x1B[0m = \x1B[31m0\x1B[0m\n"
    "        num_children: \x1B[32msize_t\x1B[0m = \x1B[34m2\x1B[0m\n"
    "        num_mappings: \x1B[32msize_t\x1B[0m = \x1B[34m1\x1B[0m\n"
    "        share_count: \x1B[32msize_t\x1B[0m = \x1B[34m3\x1B[0m\n"
    "        flags: \x1B[32mzx_info_vmo_type_t\x1B[0m = "
    "\x1B[34mZX_INFO_VMO_TYPE_PAGED | ZX_INFO_VMO_RESIZABLE\x1B[0m\n"
    "        committed_bytes: \x1B[32muint64\x1B[0m = \x1B[34m4096\x1B[0m\n"
    "        handle_rights: \x1B[32mzx_rights_t\x1B[0m = \x1B[34mZX_RIGHT_NONE\x1B[0m\n"
    "        cache_policy: \x1B[32mzx_cache_policy_t\x1B[0m = "
    "\x1B[34mZX_CACHE_POLICY_CACHED\x1B[0m\n"
    "        metadata_bytes: \x1B[32muint64\x1B[0m = \x1B[34m0\x1B[0m\n"
    "        committed_change_events: \x1B[32muint64\x1B[0m = \x1B[34m0\x1B[0m\n"
    "      }\n"
    "    ]\n");

#define OBJECT_GET_INFO_KMEM_STATS_DISPLAY_TEST_CONTENT(result, expected)                  \
  constexpr size_t kTotalBytes = 16384;                                                    \
  constexpr size_t kFreeBytes = 6334;                                                      \
  constexpr size_t kWiredBytes = 1000;                                                     \
  constexpr size_t kTotalHeapBytes = 800;                                                  \
  constexpr size_t kFreeHeapBytes = 100;                                                   \
  constexpr size_t kVmoBytes = 8000;                                                       \
  constexpr size_t kMmuOverheadBytes = 200;                                                \
  constexpr size_t kOtherBytes = 50;                                                       \
  zx_info_kmem_stats_t buffer;                                                             \
  buffer.total_bytes = kTotalBytes;                                                        \
  buffer.free_bytes = kFreeBytes;                                                          \
  buffer.wired_bytes = kWiredBytes;                                                        \
  buffer.total_heap_bytes = kTotalHeapBytes;                                               \
  buffer.free_heap_bytes = kFreeHeapBytes;                                                 \
  buffer.vmo_bytes = kVmoBytes;                                                            \
  buffer.mmu_overhead_bytes = kMmuOverheadBytes;                                           \
  buffer.other_bytes = kOtherBytes;                                                        \
  auto value =                                                                             \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_KMEM_STATS,                        \
                      reinterpret_cast<void*>(&buffer), sizeof(buffer), nullptr, nullptr); \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_KMEM_STATS_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                          \
    OBJECT_GET_INFO_KMEM_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                    \
  TEST_F(InterceptionWorkflowTestArm, name) {                          \
    OBJECT_GET_INFO_KMEM_STATS_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_KMEM_STATS_DISPLAY_TEST(
    ZxObjectGetInfoKmemStats, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_KMEM_STATS\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m72\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_kmem_stats_t\x1B[0m = {\n"
    "      total_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m16384\x1B[0m\n"
    "      free_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m6334\x1B[0m\n"
    "      wired_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m1000\x1B[0m\n"
    "      total_heap_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m800\x1B[0m\n"
    "      free_heap_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m100\x1B[0m\n"
    "      vmo_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m8000\x1B[0m\n"
    "      mmu_overhead_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m200\x1B[0m\n"
    "      other_bytes: \x1B[32msize_t\x1B[0m = \x1B[34m50\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_RESOURCE_DISPLAY_TEST_CONTENT(result, expected)                       \
  constexpr uint64_t kBase = 1000;                                                            \
  constexpr uint64_t kSize = 100;                                                             \
  zx_info_resource_t buffer{                                                                  \
      .kind = ZX_RSRC_KIND_ROOT, .flags = 0, .base = kBase, .size = kSize, .name = "my_res"}; \
  auto value =                                                                                \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_RESOURCE,                             \
                      reinterpret_cast<void*>(&buffer), sizeof(buffer), nullptr, nullptr);    \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_RESOURCE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                        \
    OBJECT_GET_INFO_RESOURCE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                                  \
  TEST_F(InterceptionWorkflowTestArm, name) {                        \
    OBJECT_GET_INFO_RESOURCE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_RESOURCE_DISPLAY_TEST(
    ZxObjectGetInfoResource, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_RESOURCE\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m56\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_resource_t\x1B[0m = {\n"
    "      kind: \x1B[32mzx_rsrc_kind_t\x1B[0m = \x1B[34mZX_RSRC_KIND_ROOT\x1B[0m\n"
    "      flags: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m\n"
    "      base: \x1B[32muint64\x1B[0m = \x1B[34m1000\x1B[0m\n"
    "      size: \x1B[32msize_t\x1B[0m = \x1B[34m100\x1B[0m\n"
    "      name: vector<\x1B[32mchar\x1B[0m> = \x1B[31m\"my_res\"\x1B[0m\n"
    "    }\n");

#define OBJECT_GET_INFO_BTI_DISPLAY_TEST_CONTENT(result, expected)                             \
  constexpr uint64_t kMinimumContiguity = 1024 * 1024;                                         \
  constexpr uint64_t kAspaceSize = 512 * 1024 * 1024;                                          \
  zx_info_bti_t buffer;                                                                        \
  buffer.minimum_contiguity = kMinimumContiguity;                                              \
  buffer.aspace_size = kAspaceSize;                                                            \
  buffer.pmo_count = 0;                                                                        \
  buffer.quarantine_count = 0;                                                                 \
  auto value =                                                                                 \
      ZxObjectGetInfo(result, #result, kHandle, ZX_INFO_BTI, reinterpret_cast<void*>(&buffer), \
                      sizeof(buffer), nullptr, nullptr);                                       \
  PerformDisplayTest("$plt(zx_object_get_info)", std::move(value), expected);

#define OBJECT_GET_INFO_BTI_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                   \
    OBJECT_GET_INFO_BTI_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                             \
  TEST_F(InterceptionWorkflowTestArm, name) {                   \
    OBJECT_GET_INFO_BTI_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_INFO_BTI_DISPLAY_TEST(
    ZxObjectGetInfoBti, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_info("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "topic: \x1B[32mzx_object_info_topic_t\x1B[0m = \x1B[34mZX_INFO_BTI\x1B[0m, "
    "buffer_size: \x1B[32msize_t\x1B[0m = \x1B[34m32\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    info: \x1B[32mzx_info_bti_t\x1B[0m = {\n"
    "      minimum_contiguity: \x1B[32muint64\x1B[0m = \x1B[34m1048576\x1B[0m\n"
    "      aspace_size: \x1B[32muint64\x1B[0m = \x1B[34m536870912\x1B[0m\n"
    "      pmo_count: \x1B[32muint64\x1B[0m = \x1B[34m0\x1B[0m\n"
    "      quarantine_count: \x1B[32muint64\x1B[0m = \x1B[34m0\x1B[0m\n"
    "    }\n");

// zx_object_get_child tests.

std::unique_ptr<SystemCallTest> ZxObjectGetChild(int64_t result, std::string_view result_name,
                                                 zx_handle_t handle, uint64_t koid,
                                                 zx_rights_t rights, zx_handle_t* out) {
  auto value = std::make_unique<SystemCallTest>("zx_object_get_child", result, result_name);
  value->AddInput(handle);
  value->AddInput(koid);
  value->AddInput(rights);
  value->AddInput(reinterpret_cast<uint64_t>(out));
  return value;
}

#define OBJECT_GET_CHILD_DISPLAY_TEST_CONTENT(result, expected)                               \
  zx_handle_t out = kHandleOut;                                                               \
  auto value = ZxObjectGetChild(result, #result, kHandle, kKoid, ZX_RIGHT_SAME_RIGHTS, &out); \
  PerformDisplayTest("$plt(zx_object_get_child)", std::move(value), expected);

#define OBJECT_GET_CHILD_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                \
    OBJECT_GET_CHILD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                \
    OBJECT_GET_CHILD_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_GET_CHILD_DISPLAY_TEST(
    ZxObjectGetChild, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_get_child("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "koid: \x1B[32muint64\x1B[0m = \x1B[34m4252\x1B[0m, "
    "rights: \x1B[32mzx.rights\x1B[0m = \x1B[34mZX_RIGHT_SAME_RIGHTS\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out: \x1B[32mhandle\x1B[0m = \x1B[31mbde90caf\x1B[0m)\n");

// zx_object_set_profile tests.

std::unique_ptr<SystemCallTest> ZxObjectSetProfile(int64_t result, std::string_view result_name,
                                                   zx_handle_t handle, zx_handle_t profile,
                                                   uint32_t options) {
  auto value = std::make_unique<SystemCallTest>("zx_object_set_profile", result, result_name);
  value->AddInput(handle);
  value->AddInput(profile);
  value->AddInput(options);
  return value;
}

#define OBJECT_SET_PROFILE_DISPLAY_TEST_CONTENT(result, expected) \
  PerformDisplayTest("$plt(zx_object_set_profile)",               \
                     ZxObjectSetProfile(result, #result, kHandle, kHandle2, 0), expected);

#define OBJECT_SET_PROFILE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    OBJECT_SET_PROFILE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    OBJECT_SET_PROFILE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

OBJECT_SET_PROFILE_DISPLAY_TEST(
    ZxObjectSetProfile, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_set_profile(handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "profile: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1222\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
