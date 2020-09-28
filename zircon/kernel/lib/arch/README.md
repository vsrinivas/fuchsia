# lib/arch -- common API for low-level machine access

This library provides clean, ergonomic, and consistent APIs for
low-level and machine-dependent code across a wide variety of low-level
environments.  It provides some machine-independent APIs abstracting
some simple machine dependencies, but mostly provides APIs that are
inherently machine-dependent.

## Compatible build environments

`lib/arch` code is intended to be compatible with a wide variety of low-level
environments.  Not every API is usable in every context.  But everywhere
possible, every API is written to be compatible with all these different
contexts.  For example, due to the [Multiboot] and [EFI] environments, nothing
uses `long int` types and instead everything carefully uses `uint64_t`,
`uintptr_t`, etc. to distinguish specific bit widths from pointer and type
sizes (pointers are 32 bits in Multiboot, `long int` is 32 bits in EFI though
pointers are 64 bits).

### Privileged environments

#### Zircon kernel proper

The main Zircon kernel is about the richest and most forgiving of the
environments where `lib/arch` code runs.  Yet it has more constraints than
userspace code.

* Floating-point and vector types and features can't be used.
  * Assembly code can't use the vector registers at all, e.g. in `memcpy`.

#### Zircon `kernel.phys` environment

This is another kernel-like environment that is even more constrained.
It's used in some "bare metal"-like contexts that work with the kernel:
 * [physboot][phys]
 * boot shims

These places have many more things they can't do:

* All code and data initializers must be **purely position-independent**:
  * no dynamic construction of static/global variables, aka
    [C++20 `constinit` rules](https://en.cppreference.com/w/cpp/language/constinit)
  * **no address constants in static initializers**, aka `.data.rel.ro`
    * no static/global initializer can use `&something` or `function_ptr`
    * no static/global initializer can use `"string literal"` for `const char*`
    * no vtables, i.e. **no C++ virtual functions**
  * In general, zero-initialization or non-pointer POD `const` or data is best.
* All code will be compiled with
  [UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
  and no (detected) undefined behavior will be tolerated. (Low-level
  code is riddled with things that are technically undefined behavior
  and unavoidably so, but such cases need to be well-understood,
  well-commented, and use well-known workarounds to silence UBsan when
  necessary.)
* Most code always runs single-threaded on the sole (i.e. boot) CPU,
  often with interrupts disabled.  It's not necessarily possible or
  meaningful to block (other than busy-wait or hardware `mwait`-style
  features), let alone spawn threads or the like.
* MMU and/or caches may be disabled, constraining what instructions are
  available.
* ARM64 requires strictly aligned memory access.

#### Multiboot

[Multiboot] is a legacy boot method used by some pre-EFI x86 boot loaders such
as GRUB, and by QEMU's support for directly booting x86 kernels without a boot
loader.

This is a `kernel.phys`-like environment, but it's actually x86-32 (i686),
i.e. ILP32!

#### EFI

The [EFI] environment is used to build the Gigaboot boot loader, or other tests
and tools that run in EFI.  This uses the Windows object file format (PE-COFF),
and so code is compiled by what's essentially a Windows toolchain (e.g. `long
int` is 32 bits while pointers are 64 bits).  It's the same Clang C++ front
end, but the calling convention, object file, and linking details are like
Windows.

### User environments

`lib/arch` is primarily geared towards the needs of privileged (kernel) code.
But applicable pieces can also be used in userspace, e.g. [`<lib/arch/asm.h>`].

## Library dependencies

`lib/arch` code is largely standalone leaf functions without its own
dependencies.  But some other header and library dependencies are both
acceptable and encouraged in `lib/arch`.

Users are not permitted to directly depend on libraries strictly under
`lib/arch`, as they are implementation details; only `lib/arch` itself may be
depended on.

### C library

`lib/arch` should be entirely compatible with a full-fledged standard C
library.  But it should depend only on the most minimal "bare metal"
subset of library APIs.  In privileged environments, `lib/arch` uses the
[Zircon kernel libc](../libc).  This provides only these standard APIs:

 * basic `<ctype.h>` functions
 * basic `<string.h>` functions
 * just `abort`, `strtol`, `strtotul` from `<stdlib.h>`
 * nothing but the `printf` family from `<stdio.h>`
   * the kernel implementation supports only basic format strings
   * `snprintf` and `vsnprintf`
   * `printf` and `vprintf`, `fprintf` and `vfprintf`
   * `FILE*`, `stdout`, and `stderr` but only those two (which might
     actually be the same one) exist and no others can be created
   * `printf` / `fprintf` output to `stdout`/`stderr` is probably not
     thread-safe or interrupt-safe, may busy-wait slowly for large
     output strings, etc.

### Assertions

Prefer `static_assert` whenever possible.  All runtime assertions should
use [`<zircon/assert.h>`](../../../system/public/zircon/assert.h).  This
is available (with various different implementations of `__zx_panic`) in
all the supported environments.

### `ktl` and `std`

The [`ktl`](../ktl) subset of standard C++ library functionality can be used
freely in `lib/arch` and [phys] code.  For an API that makes sense in
userspace, the standard headers and `std::` names can be used directly instead
of the `ktl::` wrappers--but care must be taken to stick to the subset that are
exported with `ktl::` wrappers, as only those are approved for use in kernel
code.

### Kernel-compatible libraries

Other libraries can be used as long as they are compatible with the kernel's
constraints.  This is a non-exhaustive list:

* [`zxc`](../../../system/ulib/zxc)
* [`fbl`](../../../system/ulib/fbl)
* [`hwreg`]
* [`pretty`](../../../system/ulib/pretty)

## API and coding style principles

`lib/arch` interfaces should be well-isolated, well-documented, clean APIs.

Hardware bit layouts are expressed using [`hwreg`] types, **never** with ad hoc
`#define` or direct use of constants.

### Header file & source code layout

All header files are under `<lib/arch/...>`.  Some headers provide
machine-independent APIs and some provide machine-specific APIs and some
provide a mix of the two.  In general a common header file name is used for
the same API topic across machines even if the actual APIs are partly or
wholly machine-specific.  This reduces the number of places where the code
using the library needs `#if` tests for each machine.  For example,
`<lib/arch/intrin.h>` provides many machine-specific APIs as well as a few
machine-independent APIs but there aren't separate `<lib/arch/arm64/intrin.h>`
and `<lib/arch/x86/intrin.h>` names to `#include`.

There is a source subdirectory for each machine, named by the kernel's name for
the CPU (`$zircon_cpu` in GN, i.e. `arm64` or not `x86`).  Each is actually a
`library()` target of its own, but users of `lib/arch` don't know the
sub-library exists, they just use `deps = [ "$zx/kernel/lib/arch" ]`.  The
machine-specific subdirectory provides `include/lib/arch/...` files for the
machine-dependent header files with machine-independent file names.

These subdirectories should be reserved for the things that really can't be
compiled on a different machine, such as code using inline `asm` or
intrinsics.  Declarations that are simply _about_ only one machine should go
into the main `include/lib/arch/...` directory, possibly under a
machine-specific subdirectory there.  Things like `hwreg` declarations for
system registers are topically particular to one machine architecture, but the
declarations themselves can and should be portable to any machine and indeed
to any operating system.  That makes it possible to use these headers in
`hwreg::AsmHeader` generator programs, which are compiled on the build host.
Such declarations may also be useful in unit test code that can sometimes be
built and tested on a different machine and/or operating system.

There is also a `host` subdirectory akin to the machine subdirectories.  This
is used in lieu of a particular machine when compiling for host environments.
This makes it possible to write code using the machine-independent lib/arch API
that can be built on host for purposes lock mock testing.

### C++

Only C++ 17 with modern style is supported.
There is no provision for C or for C++ 14.

All `lib/arch` C++ APIs use the `arch` namespace.

All public APIs are documented with [clang-doc]-style `///` comments before the
declaration.

### Assembly

Assembly code is minimized, preferring to use compiler intrinsics or inline
`__asm__` in C++ code whenever that's possible.  Standalone assembly code is in
`.S` files with straightforward style using two-space indentation and C++-style
`//` comments, and uses [`<lib/arch/asm.h>`] macros for symbol definitions.

Header files that are compatible with assembly use `#ifdef __ASSEMBLER__` to
separate assembly-only and C++-only declarations.  **All** header files are
compatible with C++ even if they have nothing outside `#ifdef __ASSEMBLER__`.

#### Assembly macros

Macros for assembly code have an assembly API flavor and are defined as GAS
assembly macros using `.macro`, _not_ as C macros using `#define`.  Assembly
macro APIs are documented using `///` comments before the `.macro` declaration.

Public macros that do not generate instructions have names starting with `.`,
such as `.function` and `.object` in [`<lib/arch/asm.h>`].  Macros that
generate instructions have instruction-like names with no particular prefix.

Internal macros not used outside a header file have names starting with `_`
(and thus `_.` for non-instruction-generating macros) and do not get `///`
comments.

#### Constants for assembly

Isolated trivial integer constants used in both C++ and assembly can be defined
in header files using `#define`.  However, most constants should be defined in
C++ using `constexpr` (often via [`hwreg`] types).  When assembly code needs to
use those values, create a generated header file using the
[`hwreg::AsmHeader`](../../../system/ulib/hwreg/include/hwreg/asm.h) API and
the [`hwreg_asm_header()`](../../../system/ulib/hwreg/hwreg_asm_header.gni) GN
template.

## Testing

**TODO(mcgrathr)** _Describe testing methodology._
 * host/user if possible: zxtest
 * kernel/lib/unittest if possible
 * phys unittest: kernel/lib/unittest with no auto-run decls

[clang-doc]: https://clang.llvm.org/extra/clang-doc.html
[EFI]: https://en.wikipedia.org/wiki/Unified_Extensible_Firmware_Interface
[`hwreg`]: ../../../system/ulib/hwreg
[Multiboot]: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
[phys]: ../../phys
[`<lib/arch/asm.h>`]: include/lib/arch/asm.h
