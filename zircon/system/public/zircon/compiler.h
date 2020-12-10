// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides macros for unifying differences in C/C++ compiler
// extensions, and providing backwards-compatibility for older C++ standards.
//
// May be included by C, C++, and assembly files.

#ifndef SYSROOT_ZIRCON_COMPILER_H_
#define SYSROOT_ZIRCON_COMPILER_H_

// Ensure we are using a known compiler.
#if !defined(__GNUC__) && !defined(__clang__)
#error "Unrecognized compiler!"
#endif

// Feature checking macros.
//
// If feature checking macros are not provided by the compiler, we assume that the
// checked features are unavailable.

// C++11 attribute checking.
#ifndef __has_cpp_attribute
#define __has_cpp_attribute(x) 0
#endif

// clang feature checking.
#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if !defined(__ASSEMBLER__)

// C++ header guards.
#ifdef __cplusplus
#define __BEGIN_CDECLS extern "C" {
#define __END_CDECLS }
#else
#define __BEGIN_CDECLS
#define __END_CDECLS
#endif

//
// Function and data attributes.
//

// Function inlining directives.
#define __NO_INLINE __attribute__((__noinline__))
#define __ALWAYS_INLINE __attribute__((__always_inline__))

// Avoid issuing a warning if the given variable/function is unused.
#define __UNUSED __attribute__((__unused__))

// Pack the given structure or class, omitting padding between fields.
#define __PACKED __attribute__((packed))

// Place the variable in thread-local storage.
#define __THREAD __thread

// Align the variable or type to at least `x` bytes. `x` must be a power of two.
#define __ALIGNED(x) __attribute__((aligned(x)))

// Declare the given function will never return. (such as `exit`, `abort`, etc).
#define __NO_RETURN __attribute__((__noreturn__))

// Warn if the result of this function is ignored by the caller.
#define __WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))

// Warn if a constructed objected is not used by the caller.
//
// Generates a warning on code like this:
//
//   {
//     AutoLock(&mutex);  // error: the AutoLock goes out of scope immediately.
//     locked_variable = 42;
//   }
//
// C++17 defines [[nodiscard]] to do this, but some older but supported
// versions of GCC support [[nodiscard]] on functions but not constructors.
// Having a separate __WARN_UNUSED_CONSTRUCTOR allows us to hide the attribute
// only on constructors on old versions of GCC.
#if defined(__cplusplus)
#if __cplusplus >= 201703L && (defined(__clang__) || (defined(__GNUC__) && (__GNUC__ >= 10)))
#define __WARN_UNUSED_CONSTRUCTOR [[nodiscard]]
#else
#define __WARN_UNUSED_CONSTRUCTOR
#endif
#endif

// Mark a function or variable as deprecated, warning about any callers.
#if !defined(__DEPRECATE)
#define __DEPRECATE __attribute__((__deprecated__))
#endif

// Mark a function as having no visible side-effects: It may read memory, but
// will not modify it.
//
// Multiple calls to the function may be optimized away by the compiler.
#define __PURE __attribute__((__pure__))

// Mark a function has having an output determined solely on its input
// parameters (but not memory).
//
// Multiple calls to the function may be optimized away by the compiler, even
// if memory is modified between calls.
#define __CONST __attribute__((__const__))

// The given function is malloc-like, returning a pointer to new, unused
// memory.
//
// The compiler can assume that the returned pointer does not alias any other
// pointer, which may help the compiler optimize the program.
#define __MALLOC __attribute__((__malloc__))

// Indicate that the given function takes a printf/scanf-style format string.
//
// "__fmt" is the argument number of the format string, indexed from 1.
// "__varargs" is the argument number of the variable args "..." argument
// counting from 1.
//
// If applied to a class method, the implicit "this" parameter counts as the
// first argument.
#define __PRINTFLIKE(__fmt, __varargs) __attribute__((__format__(__printf__, __fmt, __varargs)))
#define __SCANFLIKE(__fmt, __varargs) __attribute__((__format__(__scanf__, __fmt, __varargs)))

// Indicate that the `n`th argument to a function is non-null.
//
// The compiler will emit warnings if it can prove an argument is null, and
// may optimise assuming that the values are non-null.
#define __NONNULL(n) __attribute__((__nonnull__ n))

// The given function is a "leaf", and won't call further functions.
//
// Leaf functions must only return directly, and not call back into the
// current compilation unit (either via direct calls, or function pointers).
//
// May help the compiler optimize calls to the function in some cases.
#if !defined(__clang__)
#define __LEAF_FN __attribute__((__leaf__))
#else
#define __LEAF_FN
#endif

// Mark the given function or variable as `constexpr`.
//
// Used in code included by both C and C++. Code that is pure C++ should use
// `constexpr` directly.
#ifdef __cplusplus
#define __CONSTEXPR constexpr
#else
#define __CONSTEXPR
#endif

// Optimize the given function using custom flags.
//
// For example,
//
//  __OPTIMIZE("O3") int myfunction() { ... }
//
// will cause GCC to optimize the function at the O3 level, independent
// of what the compiler optimization flags are.
#if !defined(__clang__)
#define __OPTIMIZE(x) __attribute__((__optimize__(x)))
#else
#define __OPTIMIZE(x)
#endif

// Indicate the given function should not use LLVM's stack hardening features,
// but instead put all local variables on the standard stack.
//
// c.f. https://clang.llvm.org/docs/SafeStack.html
#if defined(__clang__)
#define __NO_SAFESTACK __attribute__((__no_sanitize__("safe-stack", "shadow-call-stack")))
#else
#define __NO_SAFESTACK
#endif

// The given C++ class or struct need not have a unique address when part of
// a larger struct or class, but can be safely collapsed into a zero-byte
// object by the compiler.
#if __has_cpp_attribute(no_unique_address)
#define __NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define __NO_UNIQUE_ADDRESS
#endif

// Indicate that the given function should be treated by the Clang static
// analyzer as if it doesn't return.
//
// A workaround to help static analyzer identify assertion failures
#if defined(__clang__)
#define __ANALYZER_CREATE_SINK __attribute__((analyzer_noreturn))
#else
#define __ANALYZER_CREATE_SINK
#endif

// Mark the given function as externally visible, and shouldn't be optimized
// away by link-time optimizations or whole-program optimizations.
#if !defined(__clang__)
#define __EXTERNALLY_VISIBLE __attribute__((__externally_visible__))
#else
#define __EXTERNALLY_VISIBLE
#endif

// Declare that this function declaration should be emitted as an alias for
// another function.
#define __ALIAS(x) __attribute__((__alias__(x)))

// Place the given global into a particular linker section.
#define __SECTION(x) __attribute__((__section__(x)))

// The given function or global should be given a weak symbol, or a weak
// alias to another symbol.
#define __WEAK __attribute__((__weak__))
#define __WEAK_ALIAS(x) __attribute__((__weak__, __alias__(x)))

// The given static variable should still be emitted by the compiler, even if it
// appears unused to the compiler.
//
// Rarely needed. Not to be confused with "__UNUSED", which avoids the
// compiler warning if a variable appears unused.
#define __ALWAYS_EMIT __attribute__((__used__))

// Declare this object's ELF symbol visibility.
#define __EXPORT __attribute__((__visibility__("default")))
#define __LOCAL __attribute__((__visibility__("hidden")))

//
// Builtin functions.
//

// Provide a hint to the compiler that the given expression is likely/unlikely
// to be true.
//
//   if (unlikely(status != ZX_OK)) {
//     error(...);
//   }
//
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Return the program counter of the calling function.
#define __GET_CALLER(x) __builtin_return_address(0)

// Return the address of the current stack frame.
#define __GET_FRAME(x) __builtin_frame_address(0)

// Return true if the given expression is a known compile-time constant.
#define __ISCONSTANT(x) __builtin_constant_p(x)

// Assume this branch of code cannot be reached.
#define __UNREACHABLE __builtin_unreachable()

// Get the offset of `field` from the beginning of the struct or class `type`.
#define __offsetof(type, field) __builtin_offsetof(type, field)

// Return the number of elements in the given C-style array.
//
// TODO: add type check
#if !defined(countof)
#define countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

// Perform an arithmetic operation, returning "true" if the operation overflowed.
//
// Equivalent to: { *result = a + b; return _overflow_occurred; }
#define add_overflow(a, b, result) __builtin_add_overflow(a, b, result)
#define sub_overflow(a, b, result) __builtin_sub_overflow(a, b, result)
#define mul_overflow(a, b, result) __builtin_mul_overflow(a, b, result)

// Indicate the given case of a switch statement is intended to fall through
// to the next case, and avoid generating a compiler warning.
//
//   switch (n) {
//     case 0:
//     case 1:
//       handle_zero_and_one_case();
//       __FALLTHROUGH;
//     default:
//       handle_all_cases();
//       break;
//   }
//
#if defined(__cplusplus) && __cplusplus >= 201703L
#define __FALLTHROUGH [[fallthrough]]
#elif defined(__cplusplus) && defined(__clang__)
#define __FALLTHROUGH [[clang::fallthrough]]
// The GNU style attribute is supported by Clang for C code, but __GNUC__ for
// clang right now is 4.
#elif __GNUC__ >= 7 || (!defined(__cplusplus) && defined(__clang__))
#define __FALLTHROUGH __attribute__((__fallthrough__))
#else
#define __FALLTHROUGH \
  do {                \
  } while (0)
#endif

// Locking annotations.
//
// The following annotations allow compile-time checking that annotated data
// is only accessed while holding the correct locks.
//
// The annotations are only supported by Clang, and frequently used with C++
// standard library types in userspace, so only enable in Clang when we know
// that the C++ standard library types are annotated or if we're in kernel
// code.
#if defined(__clang__) && (defined(_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS) || defined(_KERNEL))
#define __THREAD_ANNOTATION(x) __attribute__((x))
#else
#define __THREAD_ANNOTATION(x)
#endif
#define __TA_CAPABILITY(x) __THREAD_ANNOTATION(__capability__(x))
#define __TA_GUARDED(x) __THREAD_ANNOTATION(__guarded_by__(x))
#define __TA_ACQUIRE(...) __THREAD_ANNOTATION(__acquire_capability__(__VA_ARGS__))
#define __TA_ACQUIRE_SHARED(...) __THREAD_ANNOTATION(__acquire_shared_capability__(__VA_ARGS__))
#define __TA_TRY_ACQUIRE(...) __THREAD_ANNOTATION(__try_acquire_capability__(__VA_ARGS__))
#define __TA_ACQUIRED_BEFORE(...) __THREAD_ANNOTATION(__acquired_before__(__VA_ARGS__))
#define __TA_ACQUIRED_AFTER(...) __THREAD_ANNOTATION(__acquired_after__(__VA_ARGS__))
#define __TA_RELEASE(...) __THREAD_ANNOTATION(__release_capability__(__VA_ARGS__))
#define __TA_RELEASE_SHARED(...) __THREAD_ANNOTATION(__release_shared_capability__(__VA_ARGS__))
#define __TA_REQUIRES(...) __THREAD_ANNOTATION(__requires_capability__(__VA_ARGS__))
#define __TA_REQUIRES_SHARED(...) __THREAD_ANNOTATION(__requires_shared_capability__(__VA_ARGS__))
#define __TA_EXCLUDES(...) __THREAD_ANNOTATION(__locks_excluded__(__VA_ARGS__))
#define __TA_ASSERT(...) __THREAD_ANNOTATION(__assert_capability__(__VA_ARGS__))
#define __TA_ASSERT_SHARED(...) __THREAD_ANNOTATION(__assert_shared_capability__(__VA_ARGS__))
#define __TA_RETURN_CAPABILITY(x) __THREAD_ANNOTATION(__lock_returned__(x))
#define __TA_SCOPED_CAPABILITY __THREAD_ANNOTATION(__scoped_lockable__)
#define __TA_NO_THREAD_SAFETY_ANALYSIS __THREAD_ANNOTATION(__no_thread_safety_analysis__)

// Experimental lifetime analysis annotations.
#ifndef __OWNER
#ifdef __clang__
#define __OWNER(x) [[gsl::Owner(x)]]
#define __POINTER(x) [[gsl::Pointer(x)]]
#else
#define __OWNER(x)
#define __POINTER(x)
#endif
#endif

#endif // !defined(__ASSEMBLER__)

#endif  // SYSROOT_ZIRCON_COMPILER_H_
