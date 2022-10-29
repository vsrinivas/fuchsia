// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_STRING_REF_H_
#define ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_STRING_REF_H_

#include <lib/special-sections/special-sections.h>
#include <zircon/types.h>

#include <ktl/atomic.h>

// Represents an internalized string that may be referenced in traces by id to
// improve the efficiency of labels and other strings. This type does not define
// constructors or a destructor and contains trivially constructible members so
// that it may be aggregate-initialized to avoid static initializers and guards.
struct StringRef {
  static constexpr int kInvalidId = -1;

  const char* string{nullptr};
  ktl::atomic<int> id{kInvalidId};
  StringRef* next{nullptr};

  // Returns the numeric id for this string ref. If this is the first runtime
  // encounter with this string ref a new id is generated and the string ref
  // is added to the global linked list.
  int GetId() {
    const int ref_id = id.load(ktl::memory_order_relaxed);
    return ref_id == kInvalidId ? Register(this) : ref_id;
  }

  // TEMPORARY(fxbug.dev/98176): Returns the numeric id for this string ref for
  // use in FXT records. Since ktrace_provider also allocates string records,
  // use the high half of the index space to try to avoid collisions.
  uint16_t GetFxtId() { return static_cast<uint16_t>(GetId() | 0x4000); }

  // Returns the head of the global string ref linked list.
  static StringRef* head() { return head_.load(ktl::memory_order_acquire); }

  // Pre-registers all StringRef instances on supported compilers.
  //
  // Clang correctly implements section attributes on static template members in ELF targets,
  // resulting in every StringRef instance from instantiations of the _stringref literal operator
  // being placed in the "string_refs_table" section. However, GCC ignores section attributes on
  // COMDAT symbols as of this writing, resulting in an empty section when compiled with GCC.
  // TODO(fxbug.dev/27083): Cleanup this comment when GCC supports section attributes on COMDAT.
  static void PreRegister();

 private:
  // TODO(fxbug.dev/33293): Replace runtime lock-free linked list with comdat linker
  // sections once the toolchain supports it.
  static int Register(StringRef* string_ref);

  static ktl::atomic<int> id_counter_;
  static ktl::atomic<StringRef*> head_;
};

// String literal template operator that generates a unique StringRef instance
// for the given string literal. Every invocation for a given string literal
// value returns the same StringRef instance, such that the set of StringRef
// instances behaves as an internalized string table.
//
// This implementation uses the N3599 extension supported by Clang and GCC.
// C++20 ratified a slightly different syntax that is simple to switch to, once
// available, without affecting call sites.
// TODO(fxbug.dev/33284): Update to C++20 syntax when available.
//
// References:
//     http://open-std.org/JTC1/SC22/WG21/docs/papers/2013/n3599.html
//     http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0424r2.pdf
//
// Example:
//     ktrace_probe("probe_name"_stringref, ...);
//
template <typename T, T... chars>
inline StringRef* operator""_stringref() {
  static const char storage[] = {chars..., '\0'};
  static StringRef string_ref SPECIAL_SECTION("__trace_string_refs_table", StringRef){storage};
  return &string_ref;
}

#endif  // ZIRCON_KERNEL_LIB_KTRACE_INCLUDE_LIB_KTRACE_STRING_REF_H_
