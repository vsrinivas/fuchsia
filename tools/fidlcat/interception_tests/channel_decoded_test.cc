// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"
#include "tools/fidlcat/interception_tests/test_library.h"

namespace fidlcat {

std::vector<uint8_t> hello_world = {
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x91, 0x5b, 0xf2, 0x9e, 0x82, 0xe5, 0xc1, 0x28,
    0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00};

std::vector<uint8_t> on_pong = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01,
                                0x33, 0xd6, 0x9d, 0x96, 0x83, 0x30, 0x8e, 0x0f};

std::vector<uint8_t> echo_handle = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                                    0x2b, 0x22, 0x92, 0x39, 0x6f, 0x70, 0xb8, 0x7d,
                                    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00};

// zx_channel_write_tests.

std::unique_ptr<SystemCallTest> ZxChannelWrite(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, uint32_t options,
                                               const uint8_t* bytes, uint32_t num_bytes,
                                               const zx_handle_t* handles, uint32_t num_handles);

#define WRITE_DISPLAY_TEST_CONTENT(errno, dump_messages, expected)                 \
  set_dump_messages(dump_messages);                                                \
  auto loader = GetTestLibraryLoader();                                            \
  PerformDisplayTest("$plt(zx_channel_write)",                                     \
                     ZxChannelWrite(errno, #errno, kHandle, 0, hello_world.data(), \
                                    hello_world.size(), nullptr, 0),               \
                     expected, loader)

#define WRITE_DISPLAY_TEST(name, errno, dump_messages, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                    \
    WRITE_DISPLAY_TEST_CONTENT(errno, dump_messages, expected);  \
  }                                                              \
  TEST_F(InterceptionWorkflowTestArm, name) {                    \
    WRITE_DISPLAY_TEST_CONTENT(errno, dump_messages, expected);  \
  }

WRITE_DISPLAY_TEST(
    ZxChannelWriteDecoded, ZX_OK, false,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  \x1B[45m\x1B[37msent request\x1B[0m \x1B[32mfidl.examples.echo/Echo.EchoString\x1B[0m = { "
    "value: \x1B[32mstring\x1B[0m = \x1B[31m\"hello world\"\x1B[0m }\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

WRITE_DISPLAY_TEST(
    ZxChannelWriteDecodedDumped, ZX_OK, true,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  \x1B[45m\x1B[37msent request\x1B[0m \x1B[32mfidl.examples.echo/Echo.EchoString\x1B[0m = { "
    "value: \x1B[32mstring\x1B[0m = \x1B[31m\"hello world\"\x1B[0m }\n"
    "  Message: num_bytes=48 num_handles=0 txid=1 "
    "ordinal=28c1e5829ef25b91(fidl.examples.echo/Echo.EchoString)\x1B[0m\n"
    "    data=\n"
    "      0000: \x1B[31m01, 00, 00, 00\x1B[0m, 01, 00, 00, 01\x1B[31m, "
    "91, 5b, f2, 9e\x1B[0m, 82, e5, c1, 28, \n"
    "      0010: \x1B[31m0b, 00, 00, 00\x1B[0m, 00, 00, 00, 00\x1B[31m, "
    "ff, ff, ff, ff\x1B[0m, ff, ff, ff, ff, \n"
    "      0020: \x1B[31m68, 65, 6c, 6c\x1B[0m, 6f, 20, 77, 6f\x1B[31m, "
    "72, 6c, 64, 00\x1B[0m, 00, 00, 00, 00\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_channel_write_etc_tests.

std::unique_ptr<SystemCallTest> ZxChannelWriteEtc(int64_t result, std::string_view result_name,
                                                  zx_handle_t handle, uint32_t options,
                                                  const uint8_t* bytes, uint32_t num_bytes,
                                                  const zx_handle_disposition_t* handles,
                                                  uint32_t num_handles) {
  auto value = std::make_unique<SystemCallTest>("zx_channel_write_etc", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(reinterpret_cast<uint64_t>(bytes));
  value->AddInput(num_bytes);
  value->AddInput(reinterpret_cast<uint64_t>(handles));
  value->AddInput(num_handles);
  return value;
}

#define WRITE_ETC_DISPLAY_TEST_CONTENT(errno, expected)                               \
  auto loader = GetTestLibraryLoader();                                               \
  zx_handle_disposition_t handle = {.operation = ZX_HANDLE_OP_DUPLICATE,              \
                                    .handle = kHandleOut,                             \
                                    .type = ZX_OBJ_TYPE_CHANNEL,                      \
                                    .rights = ZX_RIGHT_SAME_RIGHTS,                   \
                                    .result = ZX_OK};                                 \
  PerformDisplayTest("$plt(zx_channel_write_etc)",                                    \
                     ZxChannelWriteEtc(errno, #errno, kHandle, 0, echo_handle.data(), \
                                       echo_handle.size(), &handle, 1),               \
                     expected, loader)

#define WRITE_ETC_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { WRITE_ETC_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { WRITE_ETC_DISPLAY_TEST_CONTENT(errno, expected); }

WRITE_ETC_DISPLAY_TEST(
    ZxChannelWriteEtcDecoded, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write_etc("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  \x1B[45m\x1B[37msent request\x1B[0m \x1B[32mfidl.examples.echo/Echo.EchoHandle\x1B[0m = { "
    "handle: \x1B[32mhandle\x1B[0m = Duplicate(\x1B[31mChannel:bde90caf\x1B[0m, "
    "\x1B[34mZX_RIGHT_SAME_RIGHTS\x1B[0m) }\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

// zx_channel_call_etc_tests.

std::unique_ptr<SystemCallTest> ZxChannelCallEtc(int64_t result, std::string_view result_name,
                                                 zx_handle_t handle, uint32_t options,
                                                 zx_time_t deadline,
                                                 const zx_channel_call_etc_args_t* args,
                                                 uint32_t* actual_bytes, uint32_t* actual_handles) {
  auto value = std::make_unique<SystemCallTest>("zx_channel_call_etc", result, result_name);
  value->AddInput(handle);
  value->AddInput(options);
  value->AddInput(deadline);
  value->AddInput(reinterpret_cast<uint64_t>(args));
  value->AddInput(reinterpret_cast<uint64_t>(actual_bytes));
  value->AddInput(reinterpret_cast<uint64_t>(actual_handles));
  return value;
}

#define CALL_ETC_DISPLAY_TEST_CONTENT(errno, expected)                                    \
  auto loader = GetTestLibraryLoader();                                                   \
  zx_handle_disposition_t outgoing_handle = {.operation = ZX_HANDLE_OP_DUPLICATE,         \
                                             .handle = kHandleOut,                        \
                                             .type = ZX_OBJ_TYPE_CHANNEL,                 \
                                             .rights = ZX_RIGHT_TRANSFER,                 \
                                             .result = ZX_OK};                            \
  zx_handle_info_t incoming_handle = {                                                    \
      .handle = kHandleOut2, .type = ZX_OBJ_TYPE_CHANNEL, .rights = ZX_RIGHT_EXECUTE};    \
  zx_channel_call_etc_args_t args;                                                        \
  args.wr_bytes = echo_handle.data();                                                     \
  args.wr_handles = &outgoing_handle;                                                     \
  args.rd_bytes = echo_handle.data();                                                     \
  args.rd_handles = &incoming_handle;                                                     \
  args.wr_num_bytes = echo_handle.size();                                                 \
  args.wr_num_handles = 1;                                                                \
  args.rd_num_bytes = 1000;                                                               \
  args.rd_num_handles = 64;                                                               \
  uint32_t actual_bytes = echo_handle.size();                                             \
  uint32_t actual_handles = 1;                                                            \
  PerformDisplayTest("$plt(zx_channel_call_etc)",                                         \
                     ZxChannelCallEtc(errno, #errno, kHandle, 0, ZX_TIME_INFINITE, &args, \
                                      &actual_bytes, &actual_handles),                    \
                     expected, loader);

#define CALL_ETC_DISPLAY_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { CALL_ETC_DISPLAY_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { CALL_ETC_DISPLAY_TEST_CONTENT(errno, expected); }

CALL_ETC_DISPLAY_TEST(
    ZxChannelCallEtcDecoded, ZX_OK,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_call_etc("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m, "
    "deadline: \x1B[32mzx.time\x1B[0m = \x1B[34mZX_TIME_INFINITE\x1B[0m, "
    "rd_num_bytes: \x1B[32muint32\x1B[0m = \x1B[34m1000\x1B[0m, "
    "rd_num_handles: \x1B[32muint32\x1B[0m = \x1B[34m64\x1B[0m)\n"
    "  \x1B[45m\x1B[37msent request\x1B[0m \x1B[32mfidl.examples.echo/Echo.EchoHandle\x1B[0m = { "
    "handle: \x1B[32mhandle\x1B[0m = Duplicate(\x1B[31mChannel:bde90caf\x1B[0m,"
    " \x1B[34mZX_RIGHT_TRANSFER\x1B[0m) }\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "    \x1B[45m\x1B[37mreceived response\x1B[0m "
    "\x1B[32mfidl.examples.echo/Echo.EchoHandle\x1B[0m = { "
    "handle: \x1B[32mhandle\x1B[0m = "
    "\x1B[31mChannel:bde90222\x1B[0m(\x1B[34mZX_RIGHT_EXECUTE\x1B[0m) }\n");

// Event tests.

#define WRITE_EVENT_TEST_CONTENT(errno, dump_messages, expected)                             \
  set_dump_messages(dump_messages);                                                          \
  auto loader = GetTestLibraryLoader();                                                      \
  PerformDisplayTest(                                                                        \
      "$plt(zx_channel_write)",                                                              \
      ZxChannelWrite(errno, #errno, kHandle, 0, on_pong.data(), on_pong.size(), nullptr, 0), \
      expected, loader)

#define WRITE_EVENT_TEST(name, errno, dump_messages, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                  \
    WRITE_EVENT_TEST_CONTENT(errno, dump_messages, expected);  \
  }                                                            \
  TEST_F(InterceptionWorkflowTestArm, name) {                  \
    WRITE_EVENT_TEST_CONTENT(errno, dump_messages, expected);  \
  }

WRITE_EVENT_TEST(
    EventWriteDecoded, ZX_OK, false,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write("
    "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
    "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
    "  \x1B[45m\x1B[37msent event\x1B[0m \x1B[32mfidl.examples.echo/Echo.OnPong\x1B[0m = {}\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
