// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/descriptor.h>
#include <stdint.h>

#include <phys/main.h>

void ArchPanicReset() {
  // The zero-limit IDT should ensure that UD2 causes a triple-fault reset.
  // But just in case first try the golden oldy i8042 reset pulse.
  constexpr arch::GdtRegister64 kEmpty{};
  constexpr uint8_t k8042 = 0x64;
  constexpr uint8_t kReset = 0xfe;
  __asm__ volatile(
      R"""(
      lidt %[idt_ptr]
      outb %[port], %[byte]
0:    ud2
      jmp 0b
      )"""
      :
      : [idt_ptr] "m"(kEmpty), [port] "a"(k8042), [byte] "i"(kReset));
  __builtin_trap();
}
