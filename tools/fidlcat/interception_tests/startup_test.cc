// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "interception_workflow_test.h"

namespace fidlcat {

// processargs_extract_handles tests.
// This function is the first to be intercepted. All the handles are defined.

std::unique_ptr<SystemCallTest> ProcessargsExtractHandles(
    uint32_t nhandles, zx_handle_t handles[], uint32_t handle_info[], zx_handle_t* process_self,
    zx_handle_t* job_default, zx_handle_t* vmar_root_self, zx_handle_t* thread_self) {
  auto value = std::make_unique<SystemCallTest>("processargs_extract_handles", 0, "");
  value->AddInput(nhandles);
  value->AddInput(reinterpret_cast<uint64_t>(handles));
  value->AddInput(reinterpret_cast<uint64_t>(handle_info));
  value->AddInput(reinterpret_cast<uint64_t>(process_self));
  value->AddInput(reinterpret_cast<uint64_t>(job_default));
  value->AddInput(reinterpret_cast<uint64_t>(vmar_root_self));
  value->AddInput(reinterpret_cast<uint64_t>(thread_self));
  return value;
}

#define PROCESSARGS_EXTRACT_HANDLES_TEST()                                                         \
  constexpr uint32_t nhandles = 12;                                                                \
  zx_handle_t handles[nhandles] = {0x1e925427, 0xa45248f3, 0x18c254a3, 0x39b2565b,                 \
                                   0x21c2485b, 0x37324bbb, 0x3b8255ab, 0x07b24b13,                 \
                                   0x3dc2489b, 0x3a32566f, 0x38a2565f, 0x1842488f};                \
  uint32_t handle_info[nhandles] = {0x00000001, 0x00000004, 0x00000002, 0x00000020,                \
                                    0x00010020, 0x00000003, 0x0000003b, 0x00000030,                \
                                    0x00010030, 0x00020030, 0x00000011, 0x00000013};               \
  zx_handle_t process_self;                                                                        \
  zx_handle_t job_default;                                                                         \
  zx_handle_t vmar_root_self;                                                                      \
  zx_handle_t thread_self;                                                                         \
  ProcessController controller(this, session(), loop());                                           \
  PerformFunctionTest(&controller, "processargs_extract_handles",                                  \
                      ProcessargsExtractHandles(nhandles, handles, handle_info, &process_self,     \
                                                &job_default, &vmar_root_self, &thread_self));     \
  SyscallDecoderDispatcher* dispatcher = controller.workflow().syscall_decoder_dispatcher();       \
  ASSERT_EQ(dispatcher->inference().handles().size(), nhandles);                                   \
  const HandleDescription* description = dispatcher->inference().GetHandleDescription(handles[0]); \
  ASSERT_NE(description, nullptr);                                                                 \
  ASSERT_EQ(description->type(), "proc-self");                                                     \
  description = dispatcher->inference().GetHandleDescription(handles[8]);                          \
  ASSERT_NE(description, nullptr);                                                                 \
  ASSERT_EQ(description->type(), "fd");                                                            \
  ASSERT_EQ(description->fd(), 1);

#define PROCESSARGS_EXTRACT_HANDLES_DISPLAY_TEST(name)                              \
  TEST_F(InterceptionWorkflowTestX64, name) { PROCESSARGS_EXTRACT_HANDLES_TEST(); } \
  TEST_F(InterceptionWorkflowTestArm, name) { PROCESSARGS_EXTRACT_HANDLES_TEST(); }

PROCESSARGS_EXTRACT_HANDLES_DISPLAY_TEST(ProcessargsExtractHandles);

// libc_extensions_init tests.
// This is the second intercepted function. Some handles have already been used by
// processargs_extract_handles and have been reset (null values).

std::unique_ptr<SystemCallTest> LibcExtensionsInit(uint32_t nhandles, zx_handle_t handles[],
                                                   uint32_t handle_info[], uint32_t name_count,
                                                   const char* names[]) {
  auto value = std::make_unique<SystemCallTest>("libc_extensions_init", 0, "");
  value->AddInput(nhandles);
  value->AddInput(reinterpret_cast<uint64_t>(handles));
  value->AddInput(reinterpret_cast<uint64_t>(handle_info));
  value->AddInput(name_count);
  value->AddInput(reinterpret_cast<uint64_t>(names));
  return value;
}

#define LIBC_EXTENSIONS_INIT_TEST()                                                                \
  constexpr uint32_t kMaxNameSize = 80;                                                            \
  constexpr uint32_t nhandles = 12;                                                                \
  constexpr uint32_t defined_handles = 8;                                                          \
  zx_handle_t handles[nhandles] = {0x00000000, 0x00000000, 0x00000000, 0x39b2565b,                 \
                                   0x21c2485b, 0x00000000, 0x3b8255ab, 0x07b24b13,                 \
                                   0x3dc2489b, 0x3a32566f, 0x38a2565f, 0x1842488f};                \
  uint32_t handle_info[nhandles] = {0x00000000, 0x00000000, 0x00000000, 0x00000020,                \
                                    0x00010020, 0x00000000, 0x0000003b, 0x00000030,                \
                                    0x00010030, 0x00020030, 0x00000011, 0x00000013};               \
  constexpr uint32_t name_count = 2;                                                               \
  char pkg[kMaxNameSize] = "/pkg";                                                                 \
  char svc[kMaxNameSize] = "/svc";                                                                 \
  const char* names[name_count] = {pkg, svc};                                                      \
  ProcessController controller(this, session(), loop());                                           \
  PerformFunctionTest(&controller, "__libc_extensions_init",                                       \
                      LibcExtensionsInit(nhandles, handles, handle_info, name_count, names));      \
  SyscallDecoderDispatcher* dispatcher = controller.workflow().syscall_decoder_dispatcher();       \
  ASSERT_EQ(dispatcher->inference().handles().size(), defined_handles);                            \
  const HandleDescription* description = dispatcher->inference().GetHandleDescription(handles[3]); \
  ASSERT_NE(description, nullptr);                                                                 \
  ASSERT_EQ(description->type(), "dir");                                                           \
  ASSERT_EQ(description->path(), "/pkg");

#define LIBC_EXTENSIONS_INIT_DISPLAY_TEST(name)                              \
  TEST_F(InterceptionWorkflowTestX64, name) { LIBC_EXTENSIONS_INIT_TEST(); } \
  TEST_F(InterceptionWorkflowTestArm, name) { LIBC_EXTENSIONS_INIT_TEST(); }

LIBC_EXTENSIONS_INIT_DISPLAY_TEST(LibcExtensionsInit);

}  // namespace fidlcat
