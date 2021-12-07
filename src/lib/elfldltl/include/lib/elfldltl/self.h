// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_SELF_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_SELF_H_

#include <lib/stdcompat/span.h>

#include <climits>
#include <cstdint>
#include <variant>

#include "layout.h"
#include "memory.h"

namespace elfldltl {

// The elfldltl::Self calls provide introspection for a program to inspect its
// own ELF headers.  Note these always refer to the containing ELF module's
// static link image; i.e. calls made inside a shared library (or a static
// library linked into it) refer to the shared library's runtime image, while
// calls inside the main executable (or a static library linked into it) refer
// to the main executable's runtime image.  The elfldltl::Self class itself is
// always an empty object, just used for scoping and template parameterization.
// All methods are static.

class SelfBase {
 public:
  // Compute the load bias: the difference between the runtime load address of
  // the first PT_LOAD segment and its page-truncated p_vaddr.  (That aligned
  // p_vaddr is usually zero, but not always.)
  static uintptr_t LoadBias() {
#ifndef __PIC__
    // When compiled for fixed-address use, _DYNAMIC et al might not be
    // defined.  But it's a compile-time assumption throughout the generated
    // code that the load bias is zero, so we can assume it here too.
    return 0;
#elif defined(__i386__) || defined(__x86_64__)
    // On x86, the _GLOBAL_OFFSET_TABLE_ symbol is magic in the assembler.  It
    // always gets a special relocation type that tells the linker to follow
    // the traditional protocol where _GLOBAL_OFFSET_TABLE_[0] contains the
    // unrelocated link-time address of _DYNAMIC.  The difference between the
    // runtime and link-time addresses of that symbol is the load bias.
    return reinterpret_cast<uintptr_t>(kDynamic) - kGot[0];
#else
    // The _GLOBAL_OFFSET_TABLE_ trick doesn't work reliably on other machines
    // any more, though past linkers held to this invariant on all machines.
    // However, x86 is the only machine where Fuchsia has any historical cases
    // using fixed load addresses with nonzero link-time base addresses.  On
    // other machines, in practice the link-time base address is always zero so
    // the runtime base address is the load bias.
    //
    // **NOTE**: This assumption breaks prelink cases, which were historically
    // common on Linux but are no longer in common use and have never been used
    // on Fuchsia.  A more robust approach here would be to synthesize our own
    // link-time constant datum containing the link-time address of some known
    // thing (such as itself).  That can be done in a linker script, but that's
    // the only means that won't generate a dynamic relocation for that datum.
    // Unfortunately, it can't be done in an input linker script, only in a
    // SECTIONS command in a -T script, which is difficult to arrange for in
    // the build system.
    return reinterpret_cast<uintptr_t>(kBase);
#endif
  }

  // This returns a memory-access object for referring to the program's own ELF
  // metadata directly in memory.  See <lib/elfldltl/memory.h> about the API.
  static DirectMemory Memory(std::byte* start, std::byte* end) {
    size_t image_size = static_cast<size_t>(end - start);
    uintptr_t image_vaddr = reinterpret_cast<uintptr_t>(start) - LoadBias();
    return DirectMemory({start, image_size}, image_vaddr);
  }

  // This version can be used in normal ELF objects with standard layout.  The
  // explicit image bounds must be passed for e.g. kernels with special layout.
  static DirectMemory Memory() { return Memory(kImage, kImageEnd); }

 protected:
  // These are defined implicitly by the linker.  _DYNAMIC should be defined in
  // any -pie or -shared link, while __ehdr_start is defined only in standard
  // layouts (i.e. not in non-ELF raw kernel images via custom linker scripts).
  [[gnu::visibility("hidden")]] static std::byte kImage[] __asm__("__ehdr_start");
  [[gnu::visibility("hidden")]] static std::byte kImageEnd[] __asm__("_end");
  [[gnu::visibility("hidden")]] static const std::byte kDynamic[] __asm__("_DYNAMIC");
  [[gnu::visibility("hidden")]] static const uintptr_t kGot[] __asm__("_GLOBAL_OFFSET_TABLE_");

  // This is defined in //src/lib/elfldltl/self-base.ld, which is included as
  // an input linker script in the link when the library is used, or with a
  // special-case definition in a linker script for a non-ELF layout.
  [[gnu::visibility("hidden")]] static const std::byte kBase[] __asm__("elfldltl.kBase");
};

template <ElfClass Class = ElfClass::kNative>
class Self : public SelfBase {
 public:
  using Elf = ::elfldltl::Elf<Class>;

  // Access the calling ELF module's own ELF file header.  Using this in a
  // program with a nonstandard layout without visible ELF headers will cause
  // a link-time failure.
  static const typename Elf::Ehdr& Ehdr() {
    return *reinterpret_cast<const typename Elf::Ehdr*>(kImage);
  }

  // Dynamically check if the calling ELF module's file header matches this
  // instantiation's ElfClass.  See Ehdr() above about link-time constraints.
  static bool Match() { return Ehdr().elfclass == Class; }

  // Dynamically check if the calling ELF module's file header passes basic
  // format checks for this instantiation's ElfClass and native byte order.
  // See Ehdr() above about link-time constraints.
  static bool Valid() { return Ehdr().Valid(); }

  // Examine the calling ELF module's file header to find its own program
  // headers.  See Ehdr() above about link-time constraints.
  static cpp20::span<const typename Elf::Phdr> Phdrs() {
    const auto phoff = Ehdr().phoff;
    size_t phnum;
    if (Ehdr().phnum == Elf::Ehdr::kPnXnum) [[unlikely]] {
      // This is the marker that the count might exceed 16 bits.  In that case,
      // it's instead stored in the special stub section header at index 0.
      // This is the only time the section header table is used at runtime,
      // and there are still no actual sections (index 0 is always a stub).
      const auto shoff = Ehdr().shoff;
      auto shdr0 = reinterpret_cast<const typename Elf::Shdr*>(kImage + shoff);
      phnum = shdr0->info;
    } else {
      phnum = Ehdr().phnum;
    }
    return {reinterpret_cast<const typename Elf::Phdr*>(kImage + phoff), phnum};
  }

  // Get the calling ELF module's own dynamic section.  This works in any
  // program linked to have a dynamic section, even if the ELF headers are not
  // preserved at runtime.  Note that the returned span's size is only an
  // extreme upper bound on the actual dynamic section that can be accessed.
  // It must always be examined linearly from the front and not examined past
  // the elfldltl::ElfDynTag::kNull terminator entry.
  static cpp20::span<const typename Elf::Dyn> Dynamic() {
    // This is pedantically speaking undefined behavior since that array
    // doesn't go that far.  But we have no way to determine its size without
    // looking at memory (either scanning it for the null terminator, or
    // examining phdrs for PT_DYNAMIC's p_filesz--if phdrs are even available).
    // In practice things are fine as long as access past the end of the array
    // is not actually attempted through the span.  We cap the span at the
    // bounds of the overall module image anyway (though a mapped image can
    // have whole-page holes so even this provides no guarantee that access
    // through the span cannot fault).
    auto first = reinterpret_cast<const typename Elf::Dyn*>(kDynamic);
    auto last = reinterpret_cast<const typename Elf::Dyn*>(kImageEnd);
    return {first, static_cast<size_t>(last - first)};
  }
};

// Explicit instantiations are required to convince the compiler that there are
// definitions for the static member variables, which are really acting as
// scoped extern "C" declarations.
extern template class Self<ElfClass::k32>;
extern template class Self<ElfClass::k64>;

// Determine which ELF class is used in this program's own ELF header, in
// case a 64-bit program was converted to ELFCLASS32 at link time.
// Use it like:
// ```
// auto rv = elfldltl::VisitSelf([](auto&& self) { return self.Method(...); });
// ```
template <typename T>
inline decltype(auto) VisitSelf(T&& visitor) {
  if constexpr (ElfClass::kNative == ElfClass::k64) {
    if (Self<ElfClass::k64>::Match()) {
      return std::forward<T>(visitor)(Self<ElfClass::k64>());
    }
  }
  return std::forward<T>(visitor)(Self<ElfClass::k32>());
}

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_SELF_H_
