// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/bti.h>
#include <lib/zx/iommu.h>
#include <lib/zx/msi.h>
#include <lib/zx/resource.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/iommu.h>

#include <zxtest/zxtest.h>

namespace {

extern "C" zx_handle_t get_root_resource(void);

class RootResourceFixture : public zxtest::Test {
 public:
  void SetUp() override {
    zx_iommu_desc_dummy_t desc = {};
    root_resource_ = zx::unowned_resource(get_root_resource());
    ASSERT_OK(
        zx::iommu::create(*root_resource_, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu_));
    ASSERT_OK(zx::bti::create(iommu_, 0, 0xdeadbeef, &bti_));
  }

  bool MsiTestsSupported() {
    zx::msi msi;
    return !(zx::msi::allocate(*root_resource_, 1, &msi) == ZX_ERR_NOT_SUPPORTED);
  }

 protected:
  zx::unowned_resource root_resource_;
  zx::iommu iommu_;
  zx::bti bti_;
};

// This is not really a function, but an entry point for a thread that has
// a tiny stack and no other setup. It's not really entered with the C ABI
// as such.  Rather, it's entered with the first argument register set to
// zx_handle_t and with the SP at the very top of the allocated stack.
// It's defined in pure assembly so that there are no issues with
// compiler-generated code's assumptions about the proper ABI setup,
// instrumentation, etc.
extern "C" void ThreadEntry(uintptr_t arg1, uintptr_t arg2);
// if (zx_interrupt_wait(static_cast<zx_handle_t>(arg1), nullptr) == ZX_OK) {
//   zx_thread_exit();
// }
// ASSERT(false);
__asm__(
    ".pushsection .text.ThreadEntry,\"ax\",%progbits\n"
    ".balign 4\n"
    ".type ThreadEntry,%function\n"
    "ThreadEntry:\n"
#ifdef __aarch64__
    "  mov w20, w0\n"           // Save handle in callee-saves register.
    "  mov w0, w20\n"           // Load saved handle into argument register.
    "  mov x1, xzr\n"           // Load nullptr into argument register.
    "  bl zx_interrupt_wait\n"  // Call.
    "  cbz w0, exit\n"          // Exit if returned ZX_OK.
    "  brk #0\n"                // Else crash.
    "exit:\n"
    "  bl zx_thread_exit\n"
    "  brk #0\n"  // Crash if we didn't exit.
#elif defined(__x86_64__)
    "  mov %edi, %ebx\n"          // Save handle in callee-saves register.
    "  mov %ebx, %edi\n"          // Load saved handle into argument register.
    "  xor %edx, %edx\n"          // Load nullptr into argument register.
    "  call zx_interrupt_wait\n"  // Call.
    "  testl %eax, %eax\n"        // If returned ZX_OK...
    "  jz exit\n"                 // ...exit.
    "  ud2\n"                     // Else crash.
    "exit:\n"
    "  call zx_thread_exit\n"
    "  ud2\n"  // Crash if we didn't exit.
#else
#error "what machine?"
#endif
    ".size ThreadEntry, . - ThreadEntry\n"
    ".popsection");

}  // namespace
