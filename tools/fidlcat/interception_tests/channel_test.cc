// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// zx_channel_create tests.

std::unique_ptr<SystemCallTest> ZxChannelCreate(int64_t result, std::string_view result_name,
                                                uint32_t options, zx_handle_t* out0,
                                                zx_handle_t* out1) {
  auto value = std::make_unique<SystemCallTest>("zx_channel_create", result, result_name);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(out0));
  value->AddInput(reinterpret_cast<uint64_t>(out1));
  return value;
}

#define CREATE_DISPLAY_TEST_CONTENT(errno, expected)                                           \
  zx_handle_t out0 = 0x12345678;                                                               \
  zx_handle_t out1 = 0x87654321;                                                               \
  PerformDisplayTest("zx_channel_create@plt", ZxChannelCreate(errno, #errno, 0, &out0, &out1), \
                     expected);

#define CREATE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { CREATE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { CREATE_DISPLAY_TEST_CONTENT(errno, expected); }

CREATE_DISPLAY_TEST(
    ZxChannelCreate, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_create("
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m (out0:\x1B[32mhandle\x1B[0m: \x1B[31m12345678\x1B[0m, "
    "out1:\x1B[32mhandle\x1B[0m: \x1B[31m87654321\x1B[0m)\n");

#define CREATE_INTERLEAVED_DISPLAY_TEST_CONTENT(errno, expected) \
  zx_handle_t out0 = 0x12345678;                                 \
  zx_handle_t out1 = 0x87654321;                                 \
  PerformInterleavedDisplayTest("zx_channel_create@plt",         \
                                ZxChannelCreate(errno, #errno, 0, &out0, &out1), expected);

#define CREATE_INTERLEAVED_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    CREATE_INTERLEAVED_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    CREATE_INTERLEAVED_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

CREATE_INTERLEAVED_DISPLAY_TEST(
    ZxChannelCreateInterleaved, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_create("
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "\n"
    "test_2718 \x1B[31m2718\x1B[0m:\x1B[31m8765\x1B[0m zx_channel_create("
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m   -> \x1B[32mZX_OK\x1B[0m ("
    "out0:\x1B[32mhandle\x1B[0m: \x1B[31m12345678\x1B[0m, "
    "out1:\x1B[32mhandle\x1B[0m: \x1B[31m87654321\x1B[0m)\n"
    "\n"
    "test_2718 \x1B[31m2718\x1B[0m:\x1B[31m8765\x1B[0m   -> \x1B[32mZX_OK\x1B[0m ("
    "out0:\x1B[32mhandle\x1B[0m: \x1B[31m12345678\x1B[0m, "
    "out1:\x1B[32mhandle\x1B[0m: \x1B[31m87654321\x1B[0m)\n");

// zx_channel_write_tests.

std::unique_ptr<SystemCallTest> ZxChannelWrite(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, uint32_t options,
                                               const uint8_t* bytes, uint32_t num_bytes,
                                               const zx_handle_t* handles, uint32_t num_handles) {
  auto value = std::make_unique<SystemCallTest>("zx_channel_write", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(bytes));
  value->AddInput(num_bytes);
  value->AddInput(reinterpret_cast<uint64_t>(handles));
  value->AddInput(num_handles);
  return value;
}

#define WRITE_CHECK_TEST_CONTENT(errno)                                                          \
  data().set_check_bytes();                                                                      \
  data().set_check_handles();                                                                    \
  PerformCheckTest("zx_channel_write@plt",                                                       \
                   ZxChannelWrite(errno, #errno, kHandle, 0, data().bytes(), data().num_bytes(), \
                                  data().handles(), data().num_handles()),                       \
                   nullptr)

#define WRITE_CHECK_TEST(name, errno)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { WRITE_CHECK_TEST_CONTENT(errno); } \
  TEST_F(InterceptionWorkflowTestArm, name) { WRITE_CHECK_TEST_CONTENT(errno); }

WRITE_CHECK_TEST(ZxChannelWriteCheck, ZX_OK);

#define WRITE_DISPLAY_TEST_CONTENT(errno, expected)                                                \
  data().set_check_bytes();                                                                        \
  data().set_check_handles();                                                                      \
  PerformDisplayTest("zx_channel_write@plt",                                                       \
                     ZxChannelWrite(errno, #errno, kHandle, 0, data().bytes(), data().num_bytes(), \
                                    data().handles(), data().num_handles()),                       \
                     expected)

#define WRITE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { WRITE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { WRITE_DISPLAY_TEST_CONTENT(errno, expected); }

WRITE_DISPLAY_TEST(ZxChannelWrite, ZX_OK,
                   "\n"
                   "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write("
                   "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                   "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                   ""
                   "  \x1B[31mCan't decode message: num_bytes=16 num_handles=2 "
                   "ordinal=77e4cceb00000000\x1B[0m\n"
                   "    data=\n"
                   "      0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
                   ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n"
                   "    handles=\n"
                   "      0000: 01234567, 89abcdef\n"
                   "  -> \x1B[32mZX_OK\x1B[0m\n");

WRITE_DISPLAY_TEST(ZxChannelWritePeerClosed, ZX_ERR_PEER_CLOSED,
                   "\n"
                   "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write("
                   "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                   "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
                   ""
                   "  \x1B[31mCan't decode message: num_bytes=16 num_handles=2 "
                   "ordinal=77e4cceb00000000\x1B[0m\n"
                   "    data=\n"
                   "      0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
                   ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n"
                   "    handles=\n"
                   "      0000: 01234567, 89abcdef\n"
                   "  -> \x1B[31mZX_ERR_PEER_CLOSED\x1B[0m\n");

#define LARGE_WRITE_DISPLAY_TEST_CONTENT(errno, expected)                                       \
  PerformDisplayTest(                                                                           \
      "zx_channel_write@plt",                                                                   \
      ZxChannelWrite(errno, #errno, kHandle, 0, data().large_bytes(), data().num_large_bytes(), \
                     data().handles(), data().num_handles()),                                   \
      expected)

#define LARGE_WRITE_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { LARGE_WRITE_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { LARGE_WRITE_DISPLAY_TEST_CONTENT(errno, expected); }

LARGE_WRITE_DISPLAY_TEST(
    ZxChannelWriteLarge, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    ""
    "  \x1B[31mCan't decode message: num_bytes=100 num_handles=2 ordinal=e1c4a99079645140\x1B[0m\n"
    "    data=\n"
    "      0000: \x1B[31m00, 01, 04, 09\x1B[0m, 10, 19, 24, 31\x1B[31m, "
    "40, 51, 64, 79\x1B[0m, 90, a9, c4, e1, \n"
    "      0010: \x1B[31m00, 21, 44, 69\x1B[0m, 90, b9, e4, 11\x1B[31m, "
    "40, 71, a4, d9\x1B[0m, 10, 49, 84, c1, \n"
    "      0020: \x1B[31m00, 41, 84, c9\x1B[0m, 10, 59, a4, f1\x1B[31m, "
    "40, 91, e4, 39\x1B[0m, 90, e9, 44, a1, \n"
    "      0030: \x1B[31m00, 61, c4, 29\x1B[0m, 90, f9, 64, d1\x1B[31m, "
    "40, b1, 24, 99\x1B[0m, 10, 89, 04, 81, \n"
    "      0040: \x1B[31m00, 81, 04, 89\x1B[0m, 10, 99, 24, b1\x1B[31m, "
    "40, d1, 64, f9\x1B[0m, 90, 29, c4, 61, \n"
    "      0050: \x1B[31m00, a1, 44, e9\x1B[0m, 90, 39, e4, 91\x1B[31m, "
    "40, f1, a4, 59\x1B[0m, 10, c9, 84, 41, \n"
    "      0060: \x1B[31m00, c1, 84, 49\x1B[0m\n"
    "    handles=\n"
    "      0000: 01234567, 89abcdef\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

#define WRITE_ABORTED_TEST_CONTENT(errno, expected)                                                \
  PerformAbortedTest("zx_channel_write@plt",                                                       \
                     ZxChannelWrite(errno, #errno, kHandle, 0, data().bytes(), data().num_bytes(), \
                                    data().handles(), data().num_handles()),                       \
                     expected)

#define WRITE_ABORTED_TEST(name, errno, expected)    \
  TEST_F(InterceptionWorkflowTestX64Aborted, name) { \
    WRITE_ABORTED_TEST_CONTENT(errno, expected);     \
  }                                                  \
  TEST_F(InterceptionWorkflowTestArmAborted, name) { WRITE_ABORTED_TEST_CONTENT(errno, expected); }

WRITE_ABORTED_TEST(ZxChannelWriteAborted, ZX_OK,
                   "\x1B[32m\nStop monitoring process with koid \x1B[31m3141\x1B[0m\n");

// zx_channel_read tests.

std::unique_ptr<SystemCallTest> ZxChannelRead(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, uint32_t options,
                                              const uint8_t* bytes, const zx_handle_t* handles,
                                              uint32_t num_bytes, uint32_t num_handles,
                                              uint32_t* actual_bytes, uint32_t* actual_handles) {
  auto value = std::make_unique<SystemCallTest>("zx_channel_read", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(bytes));
  value->AddInput(reinterpret_cast<uint64_t>(handles));
  value->AddInput(num_bytes);
  value->AddInput(num_handles);
  value->AddInput(reinterpret_cast<uint64_t>(actual_bytes));
  value->AddInput(reinterpret_cast<uint64_t>(actual_handles));
  return value;
}

#define READ_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected)                  \
  if (check_bytes) {                                                                            \
    data().set_check_bytes();                                                                   \
  }                                                                                             \
  if (check_handles) {                                                                          \
    data().set_check_handles();                                                                 \
  }                                                                                             \
  uint32_t actual_bytes = data().num_bytes();                                                   \
  uint32_t actual_handles = data().num_handles();                                               \
  PerformDisplayTest("zx_channel_read@plt",                                                     \
                     ZxChannelRead(errno, #errno, kHandle, 0, data().bytes(), data().handles(), \
                                   100, 64, (check_bytes) ? &actual_bytes : nullptr,            \
                                   (check_handles) ? &actual_handles : nullptr),                \
                     expected);

#define READ_DISPLAY_TEST(name, errno, check_bytes, check_handles, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                \
    READ_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);  \
  }                                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                                \
    READ_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);  \
  }

READ_DISPLAY_TEST(ZxChannelRead, ZX_OK, true, true,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read("
                  "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                  "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                  "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                  "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                  "  -> \x1B[32mZX_OK\x1B[0m\n"
                  "    \x1B[31mCan't decode message: num_bytes=16 num_handles=2 "
                  "ordinal=77e4cceb00000000\x1B[0m\n"
                  "      data=\n"
                  "        0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
                  ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n"
                  "      handles=\n"
                  "        0000: 01234567, 89abcdef\n");

READ_DISPLAY_TEST(ZxChannelReadShouldWait, ZX_ERR_SHOULD_WAIT, true, true,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read("
                  "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                  "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                  "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                  "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                  "  -> \x1B[31mZX_ERR_SHOULD_WAIT\x1B[0m\n");

READ_DISPLAY_TEST(ZxChannelReadTooSmall, ZX_ERR_BUFFER_TOO_SMALL, true, true,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read("
                  "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                  "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                  "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                  "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                  "  -> \x1B[31mZX_ERR_BUFFER_TOO_SMALL\x1B[0m ("
                  "actual_bytes:\x1B[32muint32\x1B[0m: \x1B[34m16\x1B[0m, "
                  "actual_handles:\x1B[32muint32\x1B[0m: \x1B[34m2\x1B[0m)\n");

READ_DISPLAY_TEST(ZxChannelReadNoBytes, ZX_OK, false, true,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read("
                  "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                  "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                  "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                  "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                  "  -> \x1B[32mZX_OK\x1B[0m\n"
                  "    \x1B[31mCan't decode message: num_bytes=0 num_handles=2\x1B[0m\n"
                  "      data=\x1B[0m\n"
                  "      handles=\n"
                  "        0000: 01234567, 89abcdef\n");

READ_DISPLAY_TEST(ZxChannelReadNoHandles, ZX_OK, true, false,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read("
                  "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                  "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                  "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                  "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                  "  -> \x1B[32mZX_OK\x1B[0m\n"
                  "    \x1B[31mCan't decode message: num_bytes=16 num_handles=0 "
                  "ordinal=77e4cceb00000000\x1B[0m\n"
                  "      data=\n"
                  "        0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
                  ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n");

// zx_channel_read_etc tests.
std::unique_ptr<SystemCallTest> ZxChannelReadEtc(int64_t result, std::string_view result_name,
                                                 zx_handle_t handle, uint32_t options,
                                                 const uint8_t* bytes,
                                                 const zx_handle_info_t* handles,
                                                 uint32_t num_bytes, uint32_t num_handles,
                                                 uint32_t* actual_bytes, uint32_t* actual_handles) {
  auto value = std::make_unique<SystemCallTest>("zx_channel_read_etc", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(bytes));
  value->AddInput(reinterpret_cast<uint64_t>(handles));
  value->AddInput(num_bytes);
  value->AddInput(num_handles);
  value->AddInput(reinterpret_cast<uint64_t>(actual_bytes));
  value->AddInput(reinterpret_cast<uint64_t>(actual_handles));
  return value;
}

#define READ_ETC_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected)                \
  if (check_bytes) {                                                                              \
    data().set_check_bytes();                                                                     \
  }                                                                                               \
  if (check_handles) {                                                                            \
    data().set_check_handles();                                                                   \
  }                                                                                               \
  uint32_t actual_bytes = data().num_bytes();                                                     \
  uint32_t actual_handles = data().num_handle_infos();                                            \
  PerformDisplayTest(                                                                             \
      "zx_channel_read_etc@plt",                                                                  \
      ZxChannelReadEtc(errno, #errno, kHandle, 0, data().bytes(), data().handle_infos(), 100, 64, \
                       (check_bytes) ? &actual_bytes : nullptr,                                   \
                       (check_handles) ? &actual_handles : nullptr),                              \
      expected);

#define READ_ETC_DISPLAY_TEST(name, errno, check_bytes, check_handles, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                    \
    READ_ETC_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);  \
  }                                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                                    \
    READ_ETC_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);  \
  }

READ_ETC_DISPLAY_TEST(ZxChannelReadEtc, ZX_OK, true, true,
                      "\n"
                      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read_etc("
                      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                      "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                      "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                      "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                      "  -> \x1B[32mZX_OK\x1B[0m\n"
                      "    \x1B[31mCan't decode message: num_bytes=16 num_handles=2 "
                      "ordinal=77e4cceb00000000\x1B[0m\n"
                      "      data=\n"
                      "        0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
                      ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n"
                      "      handles=\n"
                      "        0000: 01234567, 89abcdef\n");

READ_ETC_DISPLAY_TEST(ZxChannelReadEtcShouldWait, ZX_ERR_SHOULD_WAIT, true, true,
                      "\n"
                      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read_etc("
                      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                      "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                      "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                      "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                      "  -> \x1B[31mZX_ERR_SHOULD_WAIT\x1B[0m\n");

READ_ETC_DISPLAY_TEST(ZxChannelReadEtcTooSmall, ZX_ERR_BUFFER_TOO_SMALL, true, true,
                      "\n"
                      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read_etc("
                      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                      "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                      "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                      "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                      ""
                      "  -> \x1B[31mZX_ERR_BUFFER_TOO_SMALL\x1B[0m ("
                      "actual_bytes:\x1B[32muint32\x1B[0m: \x1B[34m16\x1B[0m, "
                      "actual_handles:\x1B[32muint32\x1B[0m: \x1B[34m2\x1B[0m)\n");

READ_ETC_DISPLAY_TEST(ZxChannelReadEtcNoBytes, ZX_OK, false, true,
                      "\n"
                      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read_etc("
                      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                      "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                      "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                      "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                      "  -> \x1B[32mZX_OK\x1B[0m\n"
                      "    \x1B[31mCan't decode message: num_bytes=0 num_handles=2\x1B[0m\n"
                      "      data=\x1B[0m\n"
                      "      handles=\n"
                      "        0000: 01234567, 89abcdef\n");

READ_ETC_DISPLAY_TEST(ZxChannelReadEtcNoHandles, ZX_OK, true, false,
                      "\n"
                      "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_read_etc("
                      "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                      "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                      "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                      "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                      "  -> \x1B[32mZX_OK\x1B[0m\n"
                      "    \x1B[31mCan't decode message: num_bytes=16 num_handles=0 "
                      "ordinal=77e4cceb00000000\x1B[0m\n"
                      "      data=\n"
                      "        0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
                      ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n");

// zx_channel_call tests.

std::unique_ptr<SystemCallTest> ZxChannelCall(int64_t result, std::string_view result_name,
                                              zx_handle_t handle, uint32_t options,
                                              zx_time_t deadline,
                                              const zx_channel_call_args_t* args,
                                              uint32_t* actual_bytes, uint32_t* actual_handles) {
  auto value = std::make_unique<SystemCallTest>("zx_channel_call", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(deadline);
  value->AddInput(reinterpret_cast<uint64_t>(args));
  value->AddInput(reinterpret_cast<uint64_t>(actual_bytes));
  value->AddInput(reinterpret_cast<uint64_t>(actual_handles));
  return value;
}

#define CALL_CHECK_TEST_CONTENT(errno)                                                \
  data().set_check_bytes();                                                           \
  data().set_check_handles();                                                         \
  zx_channel_call_args_t args;                                                        \
  args.wr_bytes = data().bytes();                                                     \
  args.wr_handles = data().handles();                                                 \
  args.rd_bytes = data().bytes();                                                     \
  args.rd_handles = data().handles();                                                 \
  args.wr_num_bytes = data().num_bytes();                                             \
  args.wr_num_handles = data().num_handles();                                         \
  args.rd_num_bytes = 100;                                                            \
  args.rd_num_handles = 64;                                                           \
  uint32_t actual_bytes = data().num_bytes();                                         \
  uint32_t actual_handles = data().num_handles();                                     \
  zx_channel_call_args_t args2;                                                       \
  args2.wr_bytes = data().bytes2();                                                   \
  args2.wr_handles = data().handles2();                                               \
  args2.rd_bytes = data().bytes2();                                                   \
  args2.rd_handles = data().handles2();                                               \
  args2.wr_num_bytes = data().num_bytes2();                                           \
  args2.wr_num_handles = data().num_handles2();                                       \
  args2.rd_num_bytes = 100;                                                           \
  args2.rd_num_handles = 64;                                                          \
  uint32_t actual_bytes2 = data().num_bytes2();                                       \
  uint32_t actual_handles2 = data().num_handles2();                                   \
  PerformCheckTest("zx_channel_call@plt",                                             \
                   ZxChannelCall(errno, #errno, kHandle, 0, ZX_TIME_INFINITE, &args,  \
                                 &actual_bytes, &actual_handles),                     \
                   ZxChannelCall(errno, #errno, kHandle, 0, ZX_TIME_INFINITE, &args2, \
                                 &actual_bytes2, &actual_handles2));

#define CALL_CHECK_TEST(name, errno)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { CALL_CHECK_TEST_CONTENT(errno); } \
  TEST_F(InterceptionWorkflowTestArm, name) { CALL_CHECK_TEST_CONTENT(errno); }

CALL_CHECK_TEST(ZxChannelCallCheck, ZX_OK);

#define CALL_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected)         \
  if (check_bytes) {                                                                   \
    data().set_check_bytes();                                                          \
  }                                                                                    \
  if (check_handles) {                                                                 \
    data().set_check_handles();                                                        \
  }                                                                                    \
  zx_channel_call_args_t args;                                                         \
  args.wr_bytes = data().bytes();                                                      \
  args.wr_handles = data().handles();                                                  \
  args.rd_bytes = data().bytes();                                                      \
  args.rd_handles = data().handles();                                                  \
  args.wr_num_bytes = data().num_bytes();                                              \
  args.wr_num_handles = data().num_handles();                                          \
  args.rd_num_bytes = 100;                                                             \
  args.rd_num_handles = 64;                                                            \
  uint32_t actual_bytes = data().num_bytes();                                          \
  uint32_t actual_handles = data().num_handles();                                      \
  PerformDisplayTest("zx_channel_call@plt",                                            \
                     ZxChannelCall(errno, #errno, kHandle, 0, ZX_TIME_INFINITE, &args, \
                                   (check_bytes) ? &actual_bytes : nullptr,            \
                                   (check_handles) ? &actual_handles : nullptr),       \
                     expected);

#define CALL_DISPLAY_TEST(name, errno, check_bytes, check_handles, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                \
    CALL_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);  \
  }                                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                                \
    CALL_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);  \
  }

CALL_DISPLAY_TEST(ZxChannelCall, ZX_OK, true, true,
                  "\n"
                  "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_call("
                  "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
                  "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
                  "deadline:\x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m, "
                  "rd_num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
                  "rd_num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
                  "  \x1B[31mCan't decode message: num_bytes=16 num_handles=2 "
                  "ordinal=77e4cceb00000000\x1B[0m\n"
                  "    data=\n"
                  "      0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
                  ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n"
                  "    handles=\n"
                  "      0000: 01234567, 89abcdef\n"
                  "  -> \x1B[32mZX_OK\x1B[0m\n"
                  "    \x1B[31mCan't decode message: num_bytes=16 num_handles=2 "
                  "ordinal=77e4cceb00000000\x1B[0m\n"
                  "      data=\n"
                  "        0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
                  ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n"
                  "      handles=\n"
                  "        0000: 01234567, 89abcdef\n");

#define CALL_DISPLAY_TEST_WITH_PROCESS_INFO(name, errno, check_bytes, check_handles, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                                  \
    set_with_process_info();                                                                   \
    CALL_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);                    \
  }                                                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                                                  \
    set_with_process_info();                                                                   \
    CALL_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);                    \
  }

CALL_DISPLAY_TEST_WITH_PROCESS_INFO(
    ZxChannelCallWithProcessInfo, ZX_OK, true, true,
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m \n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_call("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "deadline:\x1B[32mtime\x1B[0m: \x1B[34mZX_TIME_INFINITE\x1B[0m, "
    "rd_num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
    "rd_num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "  \x1B[31mCan't decode message: num_bytes=16 num_handles=2 "
    "ordinal=77e4cceb00000000\x1B[0m\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "    data=\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "      0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
    ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "    handles=\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "      0000: 01234567, 89abcdef\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "    \x1B[31mCan't decode message: num_bytes=16 num_handles=2 "
    "ordinal=77e4cceb00000000\x1B[0m\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "      data=\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "        0000: \x1B[31maa, aa, aa, aa\x1B[0m, 00, 00, 00, 01\x1B[31m"
    ", 00, 00, 00, 00\x1B[0m, eb, cc, e4, 77\x1B[0m\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "      handles=\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m "
    "        0000: 01234567, 89abcdef\n");

}  // namespace fidlcat
