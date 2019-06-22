// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdarg>
#include <cstdint>
#include <elf.h>
#include <tuple>
#include <type_traits>
#include <utility>
#include <zircon/syscalls.h>

// This is the base class of hermetic compute engines.
//
// Engine is the callable derived class being defined.  It will be
// default-constructed and then immediately called as int64_t(Args...).
// Then the process will exit with the returned exit status code.
//
// TODO(mcgrathr): Make it possible to use the handle for something.
// That's not really useful without resolving vDSO symbols, which is not soon.
//
template <typename Engine, typename... Args>
class HermeticComputeEngine {
public:
    using type = int64_t(Args...);

private:
    // Just instantiating the template defines this function, which always has
    // the extern "C" linkage name called from engine-start.S.  This function
    // is responsible for unwrapping the arguments (however many uintptr_t
    // arguments the caller passed), creating and calling the Engine object.
    [[noreturn, gnu::used]] static void EngineMain(zx_handle_t handle,
                                                   const Elf64_Ehdr* vdso,
                                                   ...) __asm__("EngineMain");
};

template <typename Engine, typename... Args>
void HermeticComputeEngine<Engine, Args...>::EngineMain(zx_handle_t handle,
                                                        const Elf64_Ehdr* vdso,
                                                        ...) {
    static_assert(std::is_invocable_r<int64_t, Engine, Args...>::value);

#ifndef __clang__
    // GCC doesn't respect the __asm__("...") linkage name in a template
    // instantiation.  These shenanigans amount to defining an alias with
    // the proper name, but don't require deducing the mangled symbol name.
# ifdef __x86_64__
#  define HermeticComputeEngine_tailcall_asm "jmp %c0"
#  define HermeticComputeEngine_tailcall_constraint "i"
# elif defined(__aarch64__)
#  define HermeticComputeEngine_tailcall_asm "b %0"
#  define HermeticComputeEngine_tailcall_constraint "S"
# else
#  error "what architecture?"
# endif
    __asm__(".pushsection .text.EngineMain.trampoline,\"ax\",%%progbits\n"
            ".globl EngineMain\n"
            ".hidden EngineMain\n"
            "EngineMain:\n"
            ".cfi_startproc\n"
            HermeticComputeEngine_tailcall_asm "\n"
            ".cfi_endproc\n"
            ".size EngineMain,.-EngineMain\n"
            ".popsection" ::
            HermeticComputeEngine_tailcall_constraint(&EngineMain));
# undef HermeticComputeEngine_tailcall_asm
# undef HermeticComputeEngine_tailcall_constraint
#endif

    // Function calls don't guarantee the order of evaluation, so just putting
    // va_arg(args, uintptr_t)... into a call isn't kosher.  However, list
    // initialization does guarantee the order of evaluation.  Hopefully the
    // compiler will actually optimize out the copy into the array.
    va_list ap;
    va_start(ap, vdso);
    std::tuple<Args...> args = { static_cast<Args>(va_arg(ap, uintptr_t))... };
    va_end(ap);

    // The engine's constructor runs just before the call and its destructor
    // runs just after (before exit).
    int64_t result = std::apply(Engine(), args);

    const auto process_exit = reinterpret_cast<decltype(zx_process_exit)*>(
        reinterpret_cast<uintptr_t>(vdso) + vdso->e_entry);
    process_exit(result);
    __builtin_trap();
}
