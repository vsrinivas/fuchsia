// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linux-static-pie.h"

#include <array>
#include <string_view>

#include "lss.h"

namespace {

using namespace std::literals;

void WriteString(int fd, std::string_view str) { sys_write(fd, str.data(), str.size()); }

[[noreturn]] void Exit(int status) {
  while (true) {
    sys_exit_group(status);
  }
}

[[noreturn]] bool Panic(std::string_view str) {
  WriteString(2, str);
  Exit(127);
}

// This just returns *ptr, but it prevents the compiler from doing dataflow
// analysis and realizing that the return value is just *ptr.  This makes sure
// that compiler can't do things like constant-fold the value because it knows
// ptr is the address of a constexpr object.
int* Launder(int* const* ptr) {
  __asm__("" : "=r"(ptr) : "0"(ptr));
  return *ptr;
}

// This is big enough to ensure the RELRO segment will span multiple pages.
constexpr size_t kBig = 128 * 1024 / sizeof(int*);
constexpr size_t kMiddle = kBig / 2;

// constexpr ensures this will be linker-initialized in the RELRO segment.
// It's accesseed at runtime via "laundered" address so the reads can't be
// constant-folded.
constexpr std::array<int*, kBig> kMuchRelro = []() {
  // This is a zero-initialization, mutation, and copy in constexpr context
  // just to achieve the effect of a designated array element initializer.
  std::array<int*, kBig> big{};
  big[kMiddle] = &gSyscallErrno;
  return big;
}();

}  // namespace

// The traditional Unix/Linux entry point protocol is not compatible with the
// C/C++ ABI: instead the argc, argv, and envp words are directly on the stack.
__asm__(
    R"""(
    .pushsection .text._start
    .globl _start
    .type _start, %function
    _start:
      .cfi_startproc
    )"""
#if defined(__aarch64__)
    R"""(
      mov x0, sp
      bl StaticPieSetup
      bl TestMain
    )"""
#elif defined(__x86_64__)
    R"""(
      mov %rsp, %rdi
      and $-16, %rsp
      call StaticPieSetup
      call TestMain
    )"""
#else
#error "what machine?"
#endif
    R"""(
      .cfi_endproc
    .size _start, . - _start
    .popsection
    )""");

extern "C" [[noreturn]] void TestMain() {
  static int* data_address = &gSyscallErrno;
  static int* const relro_address = &gSyscallErrno;

  // Since gSyscallErrno has internal linkage, the references here will use
  // pure PC-relative address materialization.

  int* from_data = Launder(&data_address);
  if (from_data != &gSyscallErrno) {
    Panic("address in data not relocated properly"sv);
  }

  int* from_relro = Launder(&relro_address);
  if (from_relro != &gSyscallErrno) {
    Panic("address in RELRO not relocated properly"sv);
  }

  from_relro = Launder(&kMuchRelro[kMiddle]);
  if (from_relro != &gSyscallErrno) {
    Panic("second address in RELRO not relocated properly"sv);
  }

  WriteString(1, "Hello, world!\n"sv);
  Exit(0);
}
