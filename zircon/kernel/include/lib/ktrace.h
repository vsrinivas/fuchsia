// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_LIB_KTRACE_H_
#define ZIRCON_KERNEL_INCLUDE_LIB_KTRACE_H_

#include <err.h>
#include <lib/ktrace/string_ref.h>
#include <lib/zircon-internal/ktrace.h>
#include <platform.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Specifies whether the trace applies to the current thread or cpu.
enum class TraceContext {
  Thread,
  Cpu,
  // TODO(eieio): Support process?
};

// Argument type that specifies whether a trace function is enabled or disabled.
template <bool enabled>
struct TraceEnabled {};

// Type that specifies whether tracing is enabled or disabled for the local
// compilation unit.
template <bool enabled>
constexpr auto LocalTrace = TraceEnabled<enabled>{};

// Constants that specify unconditional enabled or disabled tracing.
constexpr auto TraceAlways = TraceEnabled<true>{};
constexpr auto TraceNever = TraceEnabled<false>{};

static inline uint64_t ktrace_timestamp() { return current_ticks(); }

// Utility macro to convert string literals passed to local tracing macros into
// StringRef literals.
//
// Example:
//
// #define LOCAL_KTRACE_ENABLE 0
//
// #define LOCAL_KTRACE(string, args...)
//     ktrace_probe(LocalTrace<LOCAL_KTRACE_ENABLE>, TraceContext::Cpu,
//                  KTRACE_STRING_REF(string), ##args)
//
#define KTRACE_STRING_REF_CAT(a, b) a##b
#define KTRACE_STRING_REF(string) KTRACE_STRING_REF_CAT(string, _stringref)

// Allocates a new trace record in the trace buffer. Returns a pointer to the
// start of the record or nullptr if tracing is disabled or the end of the
// buffer is reached.
void* ktrace_open(uint32_t tag, uint64_t ts = ktrace_timestamp());

// Emits a tiny trace record.
void ktrace_tiny(uint32_t tag, uint32_t arg);

// Emits a new trace record in the given context. Compiles to no-op if |enabled|
// is false.
template <bool enabled>
inline void ktrace(TraceEnabled<enabled>, TraceContext context, uint32_t tag, uint32_t a,
                   uint32_t b, uint32_t c, uint32_t d, uint64_t explicit_ts = ktrace_timestamp()) {
  if constexpr (enabled) {
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);
    if (uint32_t* data = static_cast<uint32_t*>(ktrace_open(effective_tag, explicit_ts))) {
      data[0] = a;
      data[1] = b;
      data[2] = c;
      data[3] = d;
    }
  } else {
    (void)context;
    (void)tag;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
  }
}

// Backwards-compatible API for existing users of unconditional thread-context
// traces.
static inline void ktrace(uint32_t tag, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                          uint64_t explicit_ts = ktrace_timestamp()) {
  ktrace(TraceAlways, TraceContext::Thread, tag, a, b, c, d, explicit_ts);
}

// Backwards-compatible API for existing users of unconditional thread-context
// pointer traces.
static inline void ktrace_ptr(uint32_t tag, const void* ptr, uint32_t c, uint32_t d) {
  const uintptr_t raw_value = reinterpret_cast<uintptr_t>(ptr);
  const uint32_t ptr_high = static_cast<uint32_t>(raw_value >> 32);
  const uint32_t ptr_low = static_cast<uint32_t>(raw_value);
  ktrace(tag, ptr_high, ptr_low, c, d);
}

template <bool enabled>
inline void ktrace_probe(TraceEnabled<enabled>, TraceContext context, StringRef* string_ref) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_PROBE_16(string_ref->GetId());
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    ktrace_open(effective_tag);
  } else {
    (void)context;
    (void)string_ref;
  }
}

template <bool enabled>
inline void ktrace_probe(TraceEnabled<enabled>, TraceContext context, StringRef* string_ref,
                         uint32_t a, uint32_t b) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_PROBE_24(string_ref->GetId());
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    void* const payload = ktrace_open(effective_tag);
    uint32_t* const args = static_cast<uint32_t*>(payload);
    if (args) {
      args[0] = a;
      args[1] = b;
    }
  } else {
    (void)context;
    (void)string_ref;
    (void)a;
    (void)b;
  }
}

template <bool enabled>
inline void ktrace_probe(TraceEnabled<enabled>, TraceContext context, StringRef* string_ref,
                         uint64_t a) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_PROBE_24(string_ref->GetId());
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    void* const payload = ktrace_open(effective_tag);
    uint64_t* const args = static_cast<uint64_t*>(payload);
    if (args) {
      args[0] = a;
    }
  } else {
    (void)context;
    (void)string_ref;
    (void)a;
  }
}

template <bool enabled>
inline void ktrace_probe(TraceEnabled<enabled>, TraceContext context, StringRef* string_ref,
                         uint64_t a, uint64_t b) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_PROBE_32(string_ref->GetId());
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    void* const payload = ktrace_open(effective_tag);
    uint64_t* const args = static_cast<uint64_t*>(payload);
    if (args) {
      args[0] = a;
      args[1] = b;
    }
  } else {
    (void)context;
    (void)string_ref;
    (void)a;
    (void)b;
  }
}

template <bool enabled>
inline void ktrace_begin_duration(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                                  StringRef* string_ref) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_BEGIN_DURATION_16(string_ref->GetId(), group);
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    ktrace_open(effective_tag);
  } else {
    (void)context;
    (void)group;
    (void)string_ref;
  }
}

template <bool enabled>
inline void ktrace_end_duration(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                                StringRef* string_ref) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_END_DURATION_16(string_ref->GetId(), group);
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    ktrace_open(effective_tag);
  } else {
    (void)context;
    (void)group;
    (void)string_ref;
  }
}

template <bool enabled>
inline void ktrace_begin_duration(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                                  StringRef* string_ref, uint64_t a, uint64_t b) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_BEGIN_DURATION_32(string_ref->GetId(), group);
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    void* const payload = ktrace_open(effective_tag);
    uint64_t* const args = static_cast<uint64_t*>(payload);
    if (args) {
      args[0] = a;
      args[1] = b;
    }
  } else {
    (void)context;
    (void)group;
    (void)string_ref;
    (void)a;
    (void)b;
  }
}

template <bool enabled>
inline void ktrace_end_duration(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                                StringRef* string_ref, uint64_t a, uint64_t b) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_END_DURATION_32(string_ref->GetId(), group);
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    void* const payload = ktrace_open(effective_tag);
    uint64_t* const args = static_cast<uint64_t*>(payload);
    if (args) {
      args[0] = a;
      args[1] = b;
    }
  } else {
    (void)context;
    (void)group;
    (void)string_ref;
    (void)a;
    (void)b;
  }
}

template <bool enabled>
inline void ktrace_flow_begin(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                              StringRef* string_ref, uint64_t flow_id) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_FLOW_BEGIN(string_ref->GetId(), group);
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    void* const payload = ktrace_open(effective_tag);
    uint64_t* const args = static_cast<uint64_t*>(payload);
    if (args) {
      args[0] = flow_id;
    }
  } else {
    (void)context;
    (void)group;
    (void)string_ref;
    (void)flow_id;
  }
}

template <bool enabled>
inline void ktrace_flow_end(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                            StringRef* string_ref, uint64_t flow_id) {
  if constexpr (enabled) {
    const uint32_t tag = TAG_FLOW_END(string_ref->GetId(), group);
    const uint32_t effective_tag =
        KTRACE_TAG_FLAGS(tag, context == TraceContext::Thread ? 0 : KTRACE_FLAGS_CPU);

    void* const payload = ktrace_open(effective_tag);
    uint64_t* const args = static_cast<uint64_t*>(payload);
    if (args) {
      args[0] = flow_id;
    }
  } else {
    (void)context;
    (void)group;
    (void)string_ref;
    (void)flow_id;
  }
}

void ktrace_name_etc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always);

static inline void ktrace_name(uint32_t tag, uint32_t id, uint32_t arg, const char* name) {
  ktrace_name_etc(tag, id, arg, name, false);
}

ssize_t ktrace_read_user(void* ptr, uint32_t off, size_t len);
zx_status_t ktrace_control(uint32_t action, uint32_t options, void* ptr);

#define KTRACE_DEFAULT_BUFSIZE 32  // MB
#define KTRACE_DEFAULT_GRPMASK 0xFFF

void ktrace_report_live_threads(void);
void ktrace_report_live_processes(void);

// RAII type that emits begin/end duration events covering the lifetime of the
// instance for use in tracing scopes.
// TODO(eieio): Add option to combine begin/end traces as a single complete
// event for better trace buffer efficiency.
template <typename Enabled, uint8_t group, TraceContext = TraceContext::Thread>
class TraceDuration;

template <bool enabled, uint8_t group, TraceContext context>
class TraceDuration<TraceEnabled<enabled>, group, context> {
 public:
  explicit TraceDuration(StringRef* string_ref) : string_ref_{string_ref} {
    ktrace_begin_duration(TraceEnabled<enabled>{}, context, group, string_ref_);
  }
  TraceDuration(StringRef* string_ref, uint64_t a, uint64_t b) : string_ref_{string_ref} {
    ktrace_begin_duration(TraceEnabled<enabled>{}, context, group, string_ref_, a, b);
  }

  ~TraceDuration() { End(); }

  TraceDuration(const TraceDuration&) = delete;
  TraceDuration& operator=(const TraceDuration&) = delete;
  TraceDuration(TraceDuration&&) = delete;
  TraceDuration& operator=(TraceDuration&&) = delete;

  // Emits the end trace early, before this instance destructs.
  void End() {
    if (string_ref_) {
      ktrace_end_duration(TraceEnabled<enabled>{}, context, group, string_ref_);
      string_ref_ = nullptr;
    }
  }
  // Similar to the overload above, taking the given arguments for the end
  // event.
  void End(uint64_t a, uint64_t b) {
    if (string_ref_) {
      ktrace_end_duration(TraceEnabled<enabled>{}, context, group, string_ref_, a, b);
      string_ref_ = nullptr;
    }
  }

  // Returns a callable to complete this duration trace. This is useful to
  // delegate closing the duration to a callee. The lifetime of the
  // TraceDuration instance must not end before the completer is invoked.
  auto Completer() {
    return [this]() { End(); };
  }

 private:
  StringRef* string_ref_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_LIB_KTRACE_H_
