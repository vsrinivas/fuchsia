// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_SYSREG_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_SYSREG_H_

#include <lib/arch/hwreg.h>
#include <lib/arch/intrin.h>
#include <zircon/assert.h>

#include <cstdint>
#include <type_traits>

namespace arch {

// Each system register is identified by an unique "register tag" type.  This
// serves as a template argument for specializations for the hardware access.
// It also defines a static method Get() that returns an hwreg::RegisterAddr
// derived object representing the register.  The register tag type can be the
// hwreg::RegisterBase derived type defining the register's layout itself, if
// there is only one register with that layout.  Or it can be an empty struct
// type with only the Get() method calling a layout type's Get() method, if
// multiple distinct system registers use the same layout.
//
// Various <lib/arch/*/*.h> headers define particular register types.  They
// include this header and then use the ARCH_*_SYSREG macro to associate each
// system register tag type they define with its assembly register name.

// Ideally this would be a template parameterized by a constexpr string.  But
// the intrinsics and __asm__ syntax require actual string literals and don't
// accept equivalent constant expressions.  So macros are the only option.

// arch::SysReg() is an object used to access system registers.  It's always an
// empty object, so it can be constructed afresh for each use.  Other classes
// can be defined for testing with the same object API.
class SysReg {
 public:
  // This returns an hwreg "IO provider" pointer that can be passed to
  // ReadFrom() and WriteTo() methods to access the register identified
  // by RegisterTag.
  template <class RegisterTag>
  auto* Io() {
    return &kIo<RegisterTag>;
  }

  // This is a shorthand for reading the register into an hwreg::RegisterAddr
  // object as returned by RegisterTag::Get().
  template <class RegisterTag>
  auto Read() {
    return RegisterTag::Get().ReadFrom(Io<RegisterTag>());
  }

  // This is a shorthand for doing Read(), some modifications, and then writing
  // back.  The argument is called with a mutable reference to the object as
  // returned by Read<RegisterTag>().  That object is also returned so it can
  // be used for additional fluent calls.
  template <class RegisterTag, typename T>
  auto Modify(T&& mutate) {
    auto reg = Read<RegisterTag>();
    std::forward<T>(mutate)(reg);
    return reg.WriteTo(Io<RegisterTag>());
  }

  // This is a shorthand for writing a register value from an existing
  // hwreg::RegisterAddr object.  It returns the object (or reference) passed
  // after writing it back to the system register so it can be used for
  // additional fluent calls.
  template <class RegisterTag, typename T>
  decltype(auto) Write(T&& reg) {
    // Note this uses a separate return statement rather than returning what
    // WriteTo returns, which is a reference.  It returns the argument as
    // passed in whatever value category that was.
    reg.WriteTo(Io<RegisterTag>());
    return reg;
  }

  // Write<RegisterTag> can also be called with a plain integer value to
  // clobber the register with that value like RegisterTag::Get().FromValue()
  // before writing it.  It returns the hwreg::RegisterAddr object by value.
  template <class RegisterTag>
  auto Write(typename RegisterTag::ValueType value) {
    auto reg = RegisterTag::Get().FromValue(value);
    reg.WriteTo(Io<RegisterTag>());
    return reg;
  }

 private:
  // Each tag type gets a distinct IoProvider type that uses WriteRegister and
  // ReadRegister specializations, an empty object just used for its type.
  template <class RegisterTag>
  struct IoProvider {
    using ValueType = typename RegisterTag::ValueType;
    static_assert(sizeof(ValueType) == sizeof(uint64_t) || sizeof(ValueType) == sizeof(uint32_t));

    template <typename IntType>
    void Write(IntType value, uint32_t offset = 0) const {
      static_assert(sizeof(IntType) == sizeof(ValueType));
      ZX_DEBUG_ASSERT(offset == 0);
      WriteRegister<RegisterTag>(value);
    }

    template <typename IntType>
    IntType Read(uint32_t offset = 0) const {
      static_assert(sizeof(IntType) == sizeof(ValueType));
      ZX_DEBUG_ASSERT(offset == 0);
      return ReadRegister<RegisterTag>();
    }
  };

  // There is just one canonical instance of each IoProvider type, since there
  // is no actual object.
  template <class RegisterTag>
  static constexpr IoProvider<RegisterTag> kIo{};

  // The ARCH_*_SYSREG macros define specializations of these that do each
  // specific hardware register access.

  template <class RegisterTag>
  static void WriteRegister(typename RegisterTag::ValueType value) {
    static_assert(!std::is_same_v<RegisterTag, RegisterTag>,
                  "missing ARCH_*_SYSREG() for arch::SysReg()::Io<...>() use");
  }

  template <class RegisterTag>
  static typename RegisterTag::ValueType ReadRegister() {
    static_assert(!std::is_same_v<RegisterTag, RegisterTag>,
                  "missing ARCH_*_SYSREG() for arch::SysReg()::Io<...>() use");
    return {};
  }
};

// <lib/arch/arm64/*.h> headers use this to declare AArch64 system registers.
// RegisterTag is an unique C++ type that has a static Get() method.
// RegisterName is a string literal of the assembly name for the register.
//
// When compiling for Aarch64, this makes arch::SysReg()::Io<RegisterTag>()
// and arch::SysReg()::Read<RegisterTag>() available.  It does nothing at all
// when compiling for other machines.
//
// For example:
//   ARCH_ARM64_SYSREG(ArmCurrentEl, "CurrentEL");
//   ARCH_ARM64_SYSREG(ArmEsrEl0, "esr_el0");
//
#ifdef __aarch64__
#define ARCH_ARM64_SYSREG(RegisterTag, RegisterName)                                      \
  template <>                                                                             \
  inline void SysReg::WriteRegister<RegisterTag>(typename RegisterTag::ValueType value) { \
    if constexpr (sizeof(typename RegisterTag::ValueType) == sizeof(uint64_t)) {          \
      __arm_wsr64(RegisterName, value);                                                   \
    } else {                                                                              \
      __arm_wsr(RegisterName, value);                                                     \
    }                                                                                     \
  }                                                                                       \
  template <>                                                                             \
  inline typename RegisterTag::ValueType SysReg::ReadRegister<RegisterTag>() {            \
    if constexpr (sizeof(typename RegisterTag::ValueType) == sizeof(uint64_t)) {          \
      return __arm_rsr64(RegisterName);                                                   \
    } else {                                                                              \
      return __arm_rsr(RegisterName);                                                     \
    }                                                                                     \
  }                                                                                       \
  class SysReg
// The extra incomplete redeclaration just consumes a semicolon in the macro.
#else
// These are not callable and will cause a compilation failure if used.
#define ARCH_ARM64_SYSREG(RegisterTag, RegisterName) class SysReg
#endif

// <lib/arch/x86/*.h> headers use this to declare x86 system registers.
// RegisterTag is an unique C++ type that has a static Get() method.
// RegisterName is a string literal of the assembly name for the register
// without the leading '%'.
//
// When compiling for x86, this makes arch::SysReg()::Io<RegisterTag>() and
// arch::SysReg()::Read<RegisterTag>() available.  It does nothing at all when
// compiling for other machines.
//
// For example:
//   ARCH_ARM64_SYSREG(X86Cr0, "cr0");
//   ARCH_ARM64_SYSREG(X86Db0, "db0");
//
#if defined(__x86_64__) || defined(__i386__)
#define ARCH_X86_SYSREG(RegisterTag, RegisterName)                                        \
  template <>                                                                             \
  inline void SysReg::WriteRegister<RegisterTag>(typename RegisterTag::ValueType value) { \
    __asm__ volatile("mov %0, %%" RegisterName : : "r"(static_cast<uintptr_t>(value)));   \
  }                                                                                       \
  template <>                                                                             \
  typename RegisterTag::ValueType inline SysReg::ReadRegister<RegisterTag>() {            \
    uintptr_t value;                                                                      \
    __asm__ volatile("mov %%" RegisterName ", %0" : "=r"(value));                         \
    return static_cast<RegisterTag::ValueType>(value);                                    \
  }                                                                                       \
  class SysReg
#else
// These are not callable and will cause a compilation failure if used.
#define ARCH_X86_SYSREG(RegisterTag, RegisterName) class SysReg
#endif

// arch::SysRegBase provides a shorthand for defining register types.  The
// standard boilerplate of `struct T : public hwreg::RegisterBase<T, uint64_t>`
// is replaced with `struct T : public arch::SysRegBase<T>`.  This provides the
// Get() method automatically.  It also provides Read, Modify, and Write
// methods that can be called directly in code that isn't trying to be mockable
// with a testing object other than arch::SysReg().
//
// Multiple registers with the same layout type are handled by defining the
// layout type with arch::SysRegDerivedBase<LT> and then defining separate
// register tag types using `struct T : public arch::SysRegDerived<T, LT> {};`.

template <class RegisterType, typename IntType = uint64_t>
class SysRegDerivedBase : public hwreg::RegisterBase<RegisterType, IntType, EnablePrinter> {
 public:
  using SelfType = RegisterType;
  using ValueType = IntType;

  static auto Get() { return hwreg::RegisterAddr<RegisterType>(0); }
};

template <class RegisterTag, class RegisterType>
struct SysRegDerived : public RegisterType {
  static auto Read() { return SysReg().Read<RegisterTag>(); }

  template <typename T>
  static auto Modify(T&& mutate) {
    return SysReg().Modify<RegisterTag>(std::forward<T>(mutate));
  }

  template <typename T>
  static auto Write(T&& arg) {
    return SysReg().Write<RegisterTag>(std::forward<T>(arg));
  }

  void Write() { SysReg().Write<RegisterTag>(*this); }
};

template <class RegisterTag, typename IntType = uint64_t>
using SysRegBase = SysRegDerived<RegisterTag, SysRegDerivedBase<RegisterTag, IntType>>;

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_SYSREG_H_
