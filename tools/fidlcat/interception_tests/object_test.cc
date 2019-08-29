// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

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
  PerformDisplayTest("zx_object_wait_one@plt", std::move(value), expected);

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
                             "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                             "signals:\x1B[32msignals\x1B[0m: \x1B[34m"
                             "__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m, "
                             "deadline:\x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
                             "  -> \x1B[32mZX_OK\x1B[0m ("
                             "observed:\x1B[32msignals\x1B[0m: "
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
  PerformOneThreadDisplayTest("zx_object_wait_many@plt", std::move(value), expected);             \
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
    "deadline:\x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
    "    items:\x1B[32mzx_wait_item_t\x1B[0m[]: \x1B[31mnullptr\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "      items:\x1B[32mzx_wait_item_t\x1B[0m[]: \x1B[31mnullptr\x1B[0m\n");

OBJECT_WAIT_MANY_DISPLAY_TEST(
    ZxObjectWaitManyZero, ZX_OK, 0, false,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "zx_object_wait_many("
    "deadline:\x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
    "    items:\x1B[32mzx_wait_item_t\x1B[0m[]: \x1B[31mnullptr\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "      items:\x1B[32mzx_wait_item_t\x1B[0m[]: \x1B[31mnullptr\x1B[0m\n");

OBJECT_WAIT_MANY_DISPLAY_TEST(ZxObjectWaitMany, ZX_OK, 3, false,
                              "\n"
                              "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                              "zx_object_wait_many("
                              "deadline:\x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
                              "    items:\x1B[32mzx_wait_item_t\x1B[0m[]:  {\n"
                              "      {\n"
                              "        handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m\n"
                              "        waitfor:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "        pending:\x1B[32msignals\x1B[0m: \x1B[34m0\x1B[0m\n"
                              "      },\n"
                              "      {\n"
                              "        handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db1\x1B[0m\n"
                              "        waitfor:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "        pending:\x1B[32msignals\x1B[0m: \x1B[34m0\x1B[0m\n"
                              "      },\n"
                              "      {\n"
                              "        handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db2\x1B[0m\n"
                              "        waitfor:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "        pending:\x1B[32msignals\x1B[0m: \x1B[34m0\x1B[0m\n"
                              "      }\n"
                              "    }\n"
                              "  -> \x1B[32mZX_OK\x1B[0m\n"
                              "      items:\x1B[32mzx_wait_item_t\x1B[0m[]:  {\n"
                              "        {\n"
                              "          handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m\n"
                              "          waitfor:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "          pending:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE\x1B[0m\n"
                              "        },\n"
                              "        {\n"
                              "          handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db1\x1B[0m\n"
                              "          waitfor:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "          pending:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE\x1B[0m\n"
                              "        },\n"
                              "        {\n"
                              "          handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db2\x1B[0m\n"
                              "          waitfor:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "          pending:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_WRITABLE\x1B[0m\n"
                              "        }\n"
                              "      }\n");

OBJECT_WAIT_MANY_DISPLAY_TEST(ZxObjectWaitManyCanceled, ZX_ERR_CANCELED, 1, true,
                              "\n"
                              "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
                              "zx_object_wait_many("
                              "deadline:\x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m)\n"
                              "    items:\x1B[32mzx_wait_item_t\x1B[0m[]:  {\n"
                              "      {\n"
                              "        handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m\n"
                              "        waitfor:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "        pending:\x1B[32msignals\x1B[0m: \x1B[34m0\x1B[0m\n"
                              "      }\n"
                              "    }\n"
                              "  -> \x1B[31mZX_ERR_CANCELED\x1B[0m\n"
                              "      items:\x1B[32mzx_wait_item_t\x1B[0m[]:  {\n"
                              "        {\n"
                              "          handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m\n"
                              "          waitfor:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m\n"
                              "          pending:\x1B[32msignals\x1B[0m: "
                              "\x1B[34m__ZX_OBJECT_HANDLE_CLOSED\x1B[0m\n"
                              "        }\n"
                              "      }\n");

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
  PerformDisplayTest("zx_object_wait_async@plt", std::move(value), expected);

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
                               "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                               "port:\x1B[32mhandle\x1B[0m: \x1B[31mdf0b2ec1\x1B[0m, "
                               "key:\x1B[32muint64\x1B[0m: \x1B[34m1234\x1B[0m, "
                               "signals:\x1B[32msignals\x1B[0m:"
                               " \x1B[34m__ZX_OBJECT_READABLE | __ZX_OBJECT_PEER_CLOSED\x1B[0m, "
                               "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
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
  PerformDisplayTest("zx_object_signal@plt", std::move(value), expected);

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
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "clear_mask:\x1B[32msignals\x1B[0m: \x1B[34mZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_3\x1B[0m, "
    "set_mask:\x1B[32msignals\x1B[0m: \x1B[34mZX_USER_SIGNAL_5 | ZX_USER_SIGNAL_7\x1B[0m)\n"
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
  PerformDisplayTest("zx_object_signal_peer@plt", std::move(value), expected);

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
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "clear_mask:\x1B[32msignals\x1B[0m: \x1B[34mZX_USER_SIGNAL_0 | ZX_USER_SIGNAL_3\x1B[0m, "
    "set_mask:\x1B[32msignals\x1B[0m: \x1B[34mZX_USER_SIGNAL_5 | ZX_USER_SIGNAL_7\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

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
  PerformDisplayTest("zx_object_get_child@plt", std::move(value), expected);

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
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "koid:\x1B[32muint64\x1B[0m: \x1B[34m4252\x1B[0m, "
    "rights:\x1B[32mzx_rights_t\x1B[0m: \x1B[34mZX_RIGHT_SAME_RIGHTS\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out:\x1B[32mhandle\x1B[0m: \x1B[31mbde90caf\x1B[0m)\n");

}  // namespace fidlcat
