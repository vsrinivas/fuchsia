// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_BOOT_CPUID_H_
#define ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_BOOT_CPUID_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/special-sections/special-sections.h>

// Easy access to CPUID results collected for the boot CPU at boot time.
//
// NOTE: This header is available in contexts other than x86 Fuchsia code,
// but the functions declared here are only defined for x86 Fuchsia code.
// For example, parameterized templates might use BootCpuidIo as a default
// template argument but also be usable in non-Fuchsia unit test code when
// a different template argument is supplied.
//
// See <lib/arch/x86/cpuid.h> for the definitions of various types representing
// particular CPUID data.  For any type T among those types, using BootCpuid<T>
// in a program automatically causes the InitializeBootCpuid() function called
// from assembly at early startup to pre-populate the data that BootCpuid<T>
// will return.  These are "free" inline calls that just extract the bits using
// `hwreg`-style accessors from pre-populated hidden global variables.  e.g.
// ```
// bool have_avx = arch::BootCpuid<arch::CpuidFeatureFlagsC>().avx();
// ```
// This will boil down into `... = (_hidden_global_[2] & constant_mask) != 0;`.
//
// The InitializeBootCpuid() call can be made early on from assembly code to
// populate the data.  That function requires only the basic machine stack for
// its call and return, and doesn't need the full C++ ABI to be available yet.

#include <cpuid.h>

#include <type_traits>

namespace arch {

namespace internal {

// These are referenced from assembly code and so they need unmangled names
// that are tolerable to use from assembly.
extern "C" CpuidIo gBootCpuid0, gBootCpuidHyp0, gBootCpuidExt0, gBootCpuidFeature, gBootCpuidExtf;

}  // namespace internal

// A "CPUID I/O provider", BootCpuidIo's methods are expected to be
// instantiated by "CPUID value types", defined in <lib/arch/x86/cpuid.h>.
//
// `BootCpuidIo<T>{}.Get()` returns a `const arch::CpuidIo*` that can be used
// with the `hwreg` objects from `T::Get()`. InitializeBootCpuid() fills in
// the data for all the instantiations linked in.
//
// This template can be used as a parameter for template functions, e.g.
// `arch::GetVendor(BootCpuidIo{})`.
class BootCpuidIo {
 public:
  // Most often just Get<Type> is used instead to reach a particular (sub)leaf.
  // Multiple different CpuidValue types reach the same (sub)leaf, usually one
  // type for each of the four registers.
  template <typename CpuidValue>
  const CpuidIo* Get() const {
    return GetLeaf<CpuidValue::kLeaf, CpuidValue::kSubleaf>();
  }

  // Convenience accessor for the common case.
  template <typename CpuidValue>
  auto Read() const {
    return CpuidValue::Get().ReadFrom(Get<CpuidValue>());
  }

 private:
  // One of these is emitted in the BootCpuidLeaf special RODATA section for
  // each instantiation of GetLeaf<Leaf, Subleaf>.  InitializeBootCpuid scans
  // the section and fills in the corresponding CpuidIo items in data sections.
  struct BootCpuidLeaf {
    uint32_t leaf, subleaf;  // CPUID instruction arguments.

#if defined(__ELF__)

    // This is an offset from the start of this BootCpuidLeaf object to the
    // CpuidIo variable holding the corresponding data to be filled in by
    // InitializeBootCpuid.
    int32_t offset;

#elif defined(_WIN32)

    // This is just a direct pointer, dynamically relocated before startup.
    CpuidIo* data;

#endif  // _WIN32
  };

  // These ensure assumptions made by the InitializeBootCpuid assembly code.
#if defined(__ELF__)
  static_assert(alignof(BootCpuidLeaf) == alignof(uint32_t));
  static_assert(sizeof(BootCpuidLeaf) == sizeof(uint32_t[3]));
#elif defined(_WIN32)
  static_assert(alignof(BootCpuidLeaf) == alignof(CpuidIo*));
  static_assert(sizeof(BootCpuidLeaf) == sizeof(uint32_t[2]) + sizeof(void*));
#endif
  static_assert(alignof(CpuidIo) == alignof(uint32_t));
  static_assert(sizeof(CpuidIo) == sizeof(uint32_t[4]));
  static_assert(std::is_same_v<decltype(CpuidIo{}.values_), uint32_t[4]>);

  // This gets instantiated for each GetLeaf<Leaf, Subleaf> instantiation.
  // Explicit specializations can instead yield a constexpr CpuidIo& that
  // aliases a named CpuidIo object usable from assembly code.
  template <uint32_t Leaf, uint32_t Subleaf = 0>
  static inline CpuidIo gCpuidIo = {};

#if defined(__ELF__)

  // This really just returns &gCpuidIo<Leaf, Subleaf> and can be inlined away
  // as that.  But it also emits the BootCpuidLeaf metadata on the side.
  template <uint32_t Leaf, uint32_t Subleaf = 0>
  [[gnu::const]] const CpuidIo* GetLeaf() const {
    // There is no way to form a C++ constinit initializer (aka C initializer)
    // value that has a PIC-friendly offset like this.  But it can be done in
    // assembly easily enough.  Since this function may be inlined in multiple
    // places, it needs to ensure only one BootCpuidLeaf entry is emitted for
    // the leaf/subleaf pair.  This is accomplished by placing the fragment of
    // the BootCpuidLeaf section into a COMDAT group keyed by the symbol for
    // gCpuidIo<Leaf, Subleaf>.  Since that is separately COMDAT'd there will
    // be exactly one data slot, and exactly one metadata slot pointing to it.
    // As this can be inlined multiple times in the same translation unit, it
    // explicitly avoids emitting the same COMDAT section data twice since
    // that would just collect into a single longer BootCpuidLeaf section
    // repeating the same entry multiple times.  The SHF_GNU_RETAIN (R) flag
    // is important here to prevent linker GC removal of the section, as
    // nothing else refers to it.
    __asm__ volatile(
        R"""(
        .ifndef BootCpuidLeaf.%c[data]
          .pushsection BootCpuidLeaf, "aGR", %%progbits, %c[data], comdat
          .type BootCpuidLeaf.%c[data], %%object
          BootCpuidLeaf.%c[data]:
            .int %c[leaf], %c[subleaf], %c[data] - BootCpuidLeaf.%c[data]
          .size BootCpuidLeaf.%c[data], 12
          .popsection
        .endif
        )"""
        :
        : [leaf] "i"(Leaf), [subleaf] "i"(Subleaf), [data] "i"(&gCpuidIo<Leaf, Subleaf>));
    return &gCpuidIo<Leaf, Subleaf>;
  }

#elif defined(_WIN32)

  // We don't know how to do the same trick for other formats.  PE-COFF (for
  // UEFI) has equivalent COMDAT-like features but there is no assembler
  // syntax like .pushsection, only .section, so it can't really be used
  // inside inline __asm__.  However, it also does dynamic relocation before
  // startup so PIC-friendliness isn't really required.

  template <uint32_t Leaf, uint32_t Subleaf = 0>
  [[gnu::const]] const CpuidIo* GetLeaf() const {
    // Even though it's always just &gCpuidIo<Leaf, Subleaf>, do the runtime
    // indirection as a means of ensuring kBootCpuidLeaf stays live at link
    // time, since there is no [[gnu::retain]] for PE-COFF,
    return kBootCpuidLeaf<Leaf, Subleaf>.data;
  }

  // Directly emit the metadata into the special section, complete with
  // dynamically-relocated pointer to the data.
  template <uint32_t Leaf, uint32_t Subleaf>
  SPECIAL_SECTION("BootCpuidLeaf", BootCpuidLeaf)
  static inline const BootCpuidLeaf kBootCpuidLeaf = {
      .leaf = Leaf, .subleaf = Subleaf, .data = &gCpuidIo<Leaf, Subleaf>};

#endif  // _WIN32
};

// Call this once early in startup, before any uses of arch::BootCpuIdIo.  It
// initializes all the CpuidIo values in link map by using the CPUID
// instruction.  See below for implementation details.
extern "C" void InitializeBootCpuid();

// Convenient accessor for BootCpuidIo data, e.g.
// ```
// bool have_avx = arch::BootCpuid<arch::CpuidFeatureFlagsC>().avx();
// ```
template <typename CpuidValue>
inline auto BootCpuid() {
  return CpuidValue::Get().ReadFrom(BootCpuidIo{}.Get<CpuidValue>());
}

// Whether the leaf assosiated with a CPUID value type is supported, according
// to BootCpuidIo.
template <typename CpuidValue>
inline bool BootCpuidSupports() {
  return CpuidSupports<CpuidValue>(BootCpuidIo{});
}

// Explicit specializations for types used from assembly make it possible
// to give them unmangled (extern "C") names that are usable from assembly.

template <>
inline constexpr CpuidIo& BootCpuidIo::gCpuidIo<CpuidFeatureFlagsC::kLeaf> =
    internal::gBootCpuidFeature;

template <>
inline constexpr CpuidIo& BootCpuidIo::gCpuidIo<CpuidExtendedFeatureFlagsB::kLeaf> =
    internal::gBootCpuidExtf;

// Those two above were defined with assembly-friendly names, but have
// standard metadata to get them initialized by InitializeBootCpuid only if
// they're linked in.  These three are always specially initialized by
// InitializeBootCpuid and don't need any metadata.

template <>
inline const CpuidIo* BootCpuidIo::GetLeaf<CpuidMaximumLeaf::kLeaf>() const {
  return &internal::gBootCpuid0;
}

template <>
inline const CpuidIo* BootCpuidIo::GetLeaf<CpuidMaximumHypervisorLeaf::kLeaf>() const {
  return &internal::gBootCpuidHyp0;
}

template <>
inline const CpuidIo* BootCpuidIo::GetLeaf<CpuidMaximumExtendedLeaf::kLeaf>() const {
  return &internal::gBootCpuidExt0;
}

template <>
inline constexpr CpuidIo& BootCpuidIo::gCpuidIo<CpuidMaximumLeaf::kLeaf> = internal::gBootCpuid0;

template <>
inline constexpr CpuidIo& BootCpuidIo::gCpuidIo<CpuidMaximumHypervisorLeaf::kLeaf> =
    internal::gBootCpuidHyp0;

template <>
inline constexpr CpuidIo& BootCpuidIo::gCpuidIo<CpuidMaximumExtendedLeaf::kLeaf> =
    internal::gBootCpuidExt0;

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_X86_BOOT_CPUID_H_
