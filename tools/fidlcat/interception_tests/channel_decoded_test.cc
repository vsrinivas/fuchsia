// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"
#include "test_library.h"

namespace fidlcat {

std::vector<uint8_t> hello_world = {
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x91, 0x5b, 0xf2, 0x9e, 0x82, 0xe5, 0xc1, 0x28,
    0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x77, 0x6f, 0x72, 0x6c, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00};

// zx_channel_write_tests.

std::unique_ptr<SystemCallTest> ZxChannelWrite(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, uint32_t options,
                                               const uint8_t* bytes, uint32_t num_bytes,
                                               const zx_handle_t* handles, uint32_t num_handles);

#define WRITE_DISPLAY_TEST_CONTENT(errno, dump_messages, expected)                 \
  set_dump_messages(dump_messages);                                                \
  auto loader = GetTestLibraryLoader();                                            \
  PerformDisplayTest("zx_channel_write@plt",                                       \
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
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  \x1B[45m\x1B[37msent request\x1B[0m \x1B[32mfidl.examples.echo/Echo.EchoString\x1B[0m = {\n"
    "    value: \x1B[32mstring\x1B[0m = \x1B[31m\"hello world\"\x1B[0m\n"
    "  }\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

WRITE_DISPLAY_TEST(
    ZxChannelWriteDecodedDumped, ZX_OK, true,
    "\n"
    "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31mcefa1db0\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  \x1B[45m\x1B[37msent request\x1B[0m \x1B[32mfidl.examples.echo/Echo.EchoString\x1B[0m = {\n"
    "    value: \x1B[32mstring\x1B[0m = \x1B[31m\"hello world\"\x1B[0m\n"
    "  }\n"
    "  Message: num_bytes=48 num_handles=0 "
    "ordinal=28c1e5829ef25b91(fidl.examples.echo/Echo.EchoString)\x1B[0m\n"
    "    data=\n"
    "      0000: \x1B[31m01, 00, 00, 00\x1B[0m, 01, 00, 00, 01\x1B[31m, "
    "91, 5b, f2, 9e\x1B[0m, 82, e5, c1, 28, \n"
    "      0010: \x1B[31m0b, 00, 00, 00\x1B[0m, 00, 00, 00, 00\x1B[31m, "
    "ff, ff, ff, ff\x1B[0m, ff, ff, ff, ff, \n"
    "      0020: \x1B[31m68, 65, 6c, 6c\x1B[0m, 6f, 20, 77, 6f\x1B[31m, "
    "72, 6c, 64, 00\x1B[0m, 00, 00, 00, 00\x1B[0m\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n");

}  // namespace fidlcat
