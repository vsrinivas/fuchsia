// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"
#include "tools/fidlcat/interception_tests/test_library.h"

namespace fidlcat {

extern std::vector<uint8_t> hello_world;

std::unique_ptr<SystemCallTest> ZxChannelWrite(int64_t result, std::string_view result_name,
                                               zx_handle_t handle, uint32_t options,
                                               const uint8_t* bytes, uint32_t num_bytes,
                                               const zx_handle_t* handles, uint32_t num_handles);

#define HANDLE_INFO_TEST_CONTENT(errno, expected)                                            \
  auto loader = GetTestLibraryLoader();                                                      \
  PerformDisplayTest("$plt(zx_channel_write)",                                               \
                     ZxChannelWrite(errno, #errno, kHandle, 0, hello_world.data(),           \
                                    hello_world.size(), nullptr, 0),                         \
                     expected, loader);                                                      \
  ASSERT_EQ(last_decoder_dispatcher_->inference().GetLinkedKoid(kHandleKoid), kHandle2Koid); \
  ASSERT_EQ(last_decoder_dispatcher_->inference().GetLinkedKoid(kHandle2Koid), kHandleKoid)

#define HANDLE_INFO_TEST(name, errno, expected)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { HANDLE_INFO_TEST_CONTENT(errno, expected); } \
  TEST_F(InterceptionWorkflowTestArm, name) { HANDLE_INFO_TEST_CONTENT(errno, expected); }

HANDLE_INFO_TEST(HandleInfo, ZX_OK,
                 "\n"
                 "\x1B[32m0.000000\x1B[0m "
                 "test_3141 \x1B[31m3141\x1B[0m:\x1B[31m8764\x1B[0m zx_channel_write("
                 "handle: \x1B[32mhandle\x1B[0m = \x1B[31mcefa1db0\x1B[0m, "
                 "options: \x1B[32muint32\x1B[0m = \x1B[34m0\x1B[0m)\n"
                 "  \x1B[45m\x1B[37msent request\x1B[0m "
                 "\x1B[32mfidl.examples.echo/Echo.EchoString\x1B[0m = { "
                 "value: \x1B[32mstring\x1B[0m = \x1B[31m\"hello world\"\x1B[0m }\n"
                 "\x1B[32m0.000000\x1B[0m "
                 "  -> \x1B[32mZX_OK\x1B[0m\n")

}  // namespace fidlcat
