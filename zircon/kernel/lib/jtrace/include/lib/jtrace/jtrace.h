// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_JTRACE_INCLUDE_LIB_JTRACE_JTRACE_H_
#define ZIRCON_KERNEL_LIB_JTRACE_INCLUDE_LIB_JTRACE_JTRACE_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>
#include <kernel/cpu.h>
#include <kernel/jtrace_config.h>
#include <kernel/persistent_ram.h>
#include <ktl/type_traits.h>
#include <ktl/unique_ptr.h>

namespace jtrace {

enum class TraceBufferType { Current = 0, Recovered };

// All arguments provided to trace entries are either 32 or 64 bits, and will be
// rendered as just hex when the trace needs to be dumped.  Define a few helper
// structs which will allow users to pass _any_ data type to a trace entry,
// provided that it will fit in the storage.

namespace internal {
struct Field32 {
  template <typename T>
  constexpr Field32(T _val) : val(static_cast<uint32_t>(_val)) {
    static_assert(sizeof(T) <= 4, "Trace field must be 32 bits or less in size");
    static_assert(!ktl::is_floating_point_v<T>, "Floating point types may not be used.");
  }
  const uint32_t val;
};

struct Field64 {
  template <typename T>
  constexpr Field64(T _val) : val(static_cast<uint64_t>(_val)) {
    static_assert(sizeof(T) <= 8, "Trace field must be 64 bits or less in size");
    static_assert(!ktl::is_floating_point_v<T>, "Floating point types may not be used.");
  }

  template <typename T>
  constexpr Field64(T* _val) : val(reinterpret_cast<uint64_t>(_val)) {}

  template <typename T>
  constexpr Field64(const ktl::unique_ptr<T>& _val) : val(reinterpret_cast<uint64_t>(_val.get())) {}

  template <typename T>
  constexpr Field64(const fbl::RefPtr<T>& _val) : val(reinterpret_cast<uint64_t>(_val.get())) {}

  const uint64_t val;
};

// The definition of a small structure used to hold constexpr file/function/line
// info when tracing. Allowing the compiler to generate these structures in the
// RO data section of the program, then storing pointers to the whole package
// ends up saving 12 bytes of storage overall.
struct FileFuncLineInfo {
  const char* file;
  const char* func;
  int32_t line;
};

}  // namespace internal

// Definition for large and small trace entries.
//
// TODO(johngro): Change the string literal and FileFuncLineInfo pointers
// contained in these structures so that they are offsets from the base of the
// kernel image, instead of being absolute pointers.
//
// In theory, this might same some storage (if a 32 bit offset can be used
// instead of a 64 bit pointer), but it may also become a more hard requirement
// for persistent traces once kernel images start to be loaded in different
// locations on every boot because of ASLR.
template <UseLargeEntries>
struct Entry;

template <>
struct Entry<UseLargeEntries::No> {
  Entry() = default;
  Entry(const char* _tag, const ::jtrace::internal::FileFuncLineInfo* _ffl_info,
        internal::Field32 _a = 0)
      : tag(_tag), a(_a.val) {}

  zx_ticks_t ts_ticks;  //  0 + 8 == 8 bytes
  const char* tag;      //  8 + 8 == 16 bytes
  cpu_num_t cpu_id;     // 16 + 4 == 20 bytes
  uint32_t a;           // 24 + 4 == 24 bytes
};

template <>
struct Entry<UseLargeEntries::Yes> {
  Entry() = default;
  Entry(const char* _tag, const ::jtrace::internal::FileFuncLineInfo* _ffl_info,
        internal::Field32 _a = 0, internal::Field32 _b = 0, internal::Field32 _c = 0,
        internal::Field32 _d = 0, internal::Field64 _e = 0, internal::Field64 _f = 0)
      : tag(_tag),
        ffl_info(_ffl_info),
        e(_e.val),
        f(_f.val),
        a(_a.val),
        b(_b.val),
        c(_c.val),
        d(_d.val) {}

  zx_ticks_t ts_ticks;                                  //  0 +  8 == 8 bytes
  const char* tag{nullptr};                             //  8 +  8 == 16 bytes
  const internal::FileFuncLineInfo* ffl_info{nullptr};  // 16 +  8 == 24 bytes
  zx_koid_t tid;                                        // 24 +  8 == 32 bytes
  uint64_t e, f;                                        // 32 + 16 == 48 bytes
  uint32_t a, b, c, d;                                  // 48 + 16 == 64 bytes
  cpu_num_t cpu_id;                                     // 64 +  4 == 68 bytes
  // implicit padding to 8 bytes alignment brings the structure to 72 bytes
  // total.
};

}  // namespace jtrace

#if JTRACE_TARGET_BUFFER_SIZE > 0
void jtrace_init();
void jtrace_set_location(void* ptr, size_t len);
void jtrace_invalidate();
void jtrace_log(jtrace::Entry<kJTraceUseLargeEntries>& e);
void jtrace_dump(jtrace::TraceBufferType which);
#else
inline void jtrace_init() {}
inline void jtrace_set_location(void* ptr, size_t len) {}
inline void jtrace_invalidate() {}
inline void jtrace_log(jtrace::Entry<kJTraceUseLargeEntries>& e) {}
inline void jtrace_dump(jtrace::TraceBufferType which) {}
#endif

#define JTRACE(tag, ...)                                                        \
  do {                                                                          \
    static constexpr ::jtrace::internal::FileFuncLineInfo ffl_info = {          \
        .file = __FILE__, .func = __FUNCTION__, .line = __LINE__};              \
    jtrace::Entry<kJTraceUseLargeEntries> entry{tag, &ffl_info, ##__VA_ARGS__}; \
    jtrace_log(entry);                                                          \
  } while (false)

#endif  // ZIRCON_KERNEL_LIB_JTRACE_INCLUDE_LIB_JTRACE_JTRACE_H_
