// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/standard-segments.h>
#include <lib/arch/zbi-boot.h>

namespace arch {

[[noreturn]] void ZbiBootRaw(uintptr_t entry, void* data) {
  // Make a fresh little GDT on the stack here just so we know there's a 64-bit
  // code segment to use.  The stack this function is using is usually not
  // going to be preserved, it's just arbitrary memory that the new kernel
  // might overwrite.  But it's obliged to set up its own GDT and its own page
  // tables and so on in its own load image and bss space before touching any
  // other "free" memory in the system, so this bit of stack is as good a place
  // as any for the temporary GDT.
  X86StandardSegments().Load(entry, reinterpret_cast<uintptr_t>(data));
}

}  // namespace arch
