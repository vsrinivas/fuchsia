// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_LIB_KTRACE_H_
#define ZIRCON_KERNEL_INCLUDE_LIB_KTRACE_H_

#include <lib/fxt/serializer.h>
#include <lib/ktrace/string_ref.h>
#include <lib/user_copy/user_ptr.h>
#include <lib/zircon-internal/ktrace.h>
#include <platform.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <kernel/thread.h>
#include <ktl/atomic.h>

namespace ktrace_thunks {

bool tag_enabled(uint32_t tag);
ssize_t read_user(user_out_ptr<void> ptr, uint32_t off, size_t len);

template <fxt::RefType name_type, fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_kernel_object(uint32_t tag, bool always, zx_koid_t koid, zx_obj_type_t obj_type,
                       const fxt::StringRef<name_type>& name_arg,
                       const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_instant(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                 const fxt::StringRef<category_type>& category_ref,
                 const fxt::StringRef<name_type>& name_ref,
                 const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_duration_begin(uint32_t tag, uint64_t timestamp,
                        const fxt::ThreadRef<thread_type>& thread_ref,
                        const fxt::StringRef<category_type>& category_ref,
                        const fxt::StringRef<name_type>& name_ref,
                        const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_duration_end(uint32_t tag, uint64_t timestamp,
                      const fxt::ThreadRef<thread_type>& thread_ref,
                      const fxt::StringRef<category_type>& category_ref,
                      const fxt::StringRef<name_type>& name_ref,
                      const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_duration_complete(uint32_t tag, uint64_t start_time,
                           const fxt::ThreadRef<thread_type>& thread_ref,
                           const fxt::StringRef<category_type>& category_ref,
                           const fxt::StringRef<name_type>& name_ref, uint64_t end_time,
                           const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_counter(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                 const fxt::StringRef<category_type>& category_ref,
                 const fxt::StringRef<name_type>& name_ref, uint64_t counter_id,
                 const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_flow_begin(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                    const fxt::StringRef<category_type>& category_ref,
                    const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                    const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_flow_step(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                   const fxt::StringRef<category_type>& category_ref,
                   const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                   const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_flow_end(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                  const fxt::StringRef<category_type>& category_ref,
                  const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                  const fxt::Argument<arg_types, ref_types>&... args);

template <fxt::RefType outgoing_type, fxt::RefType incoming_type>
void fxt_context_switch(uint32_t tag, uint64_t timestamp, uint8_t cpu_number,
                        zx_thread_state_t outgoing_thread_state,
                        const fxt::ThreadRef<outgoing_type>& outgoing_thread,
                        const fxt::ThreadRef<incoming_type>& incoming_thread,
                        uint8_t outgoing_thread_priority, uint8_t incoming_thread_priority);

void fxt_string_record(uint16_t index, const char* string, size_t string_length);

}  // namespace ktrace_thunks

// TODO(fxbug.dev/112751)
constexpr zx_koid_t kKernelPseudoKoidBase = 0x00000000'70000000u;
constexpr zx_koid_t kKernelPseudoCpuBase = kKernelPseudoKoidBase + 0x00000000'01000000u;
constexpr zx_koid_t kNoProcess = 0u;

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

// Indicate that the current time should be recorded when writing a trace record.
//
// Used for ktrace calls which accept a custom timestamp as a parameter.
inline constexpr uint64_t kRecordCurrentTimestamp = 0xffffffff'ffffffff;

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

inline StringRef* GetCategoryForGroup(uint32_t group) {
  switch (group) {
    case KTRACE_GRP_META:
      return "kernel:meta"_stringref;
    case KTRACE_GRP_LIFECYCLE:
      return "kernel:lifecycle"_stringref;
    case KTRACE_GRP_SCHEDULER:
      return "kernel:sched"_stringref;
    case KTRACE_GRP_TASKS:
      return "kernel:tasks"_stringref;
    case KTRACE_GRP_IPC:
      return "kernel:ipc"_stringref;
    case KTRACE_GRP_IRQ:
      return "kernel:irq"_stringref;
    case KTRACE_GRP_SYSCALL:
      return "kernel:syscall"_stringref;
    case KTRACE_GRP_PROBE:
      return "kernel:probe"_stringref;
    case KTRACE_GRP_ARCH:
      return "kernel:arch"_stringref;
    case KTRACE_GRP_VM:
      return "kernel:vm"_stringref;
    default:
      return "unknown"_stringref;
  }
}

// Check if tracing is enabled for the given tag.
inline bool ktrace_tag_enabled(uint32_t tag) { return ktrace_thunks::tag_enabled(tag); }

template <bool enabled>
inline void ktrace_probe(TraceEnabled<enabled>, TraceContext context, StringRef* string_ref) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_PROBE_16(string_ref->GetId());
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_instant(tag, current_ticks(), thread_ref,
                             fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
                             fxt::StringRef{string_ref->GetFxtId()});
}

template <bool enabled>
inline void ktrace_probe(TraceEnabled<enabled>, TraceContext context, StringRef* string_ref,
                         uint32_t a, uint32_t b) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_PROBE_24(string_ref->GetId());
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_instant(tag, current_ticks(), thread_ref,
                             fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
                             fxt::StringRef{string_ref->GetFxtId()},
                             fxt::Argument{fxt::StringRef{"arg0"_stringref->GetFxtId()}, a},
                             fxt::Argument{fxt::StringRef{"arg1"_stringref->GetFxtId()}, b});
}

template <bool enabled>
inline void ktrace_probe(TraceEnabled<enabled>, TraceContext context, StringRef* string_ref,
                         uint64_t a) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_PROBE_24(string_ref->GetId());
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_instant(tag, current_ticks(), thread_ref,
                             fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
                             fxt::StringRef{string_ref->GetFxtId()},
                             fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
                                 fxt::StringRef{"arg0"_stringref->GetFxtId()}, a});
}

template <bool enabled>
inline void ktrace_probe(TraceEnabled<enabled>, TraceContext context, StringRef* string_ref,
                         uint64_t a, uint64_t b) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_PROBE_32(string_ref->GetId());
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_instant(tag, current_ticks(), thread_ref,
                             fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
                             fxt::StringRef{string_ref->GetFxtId()},
                             fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
                                 fxt::StringRef{"arg0"_stringref->GetFxtId()}, a},
                             fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
                                 fxt::StringRef{"arg1"_stringref->GetFxtId()}, b});
}

template <bool enabled>
inline void ktrace_begin_duration(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                                  StringRef* string_ref) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_BEGIN_DURATION_16(string_ref->GetId(), group);
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_duration_begin(
      tag, current_ticks(), thread_ref,
      fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
      fxt::StringRef{string_ref->GetFxtId()});
}

template <bool enabled>
inline void ktrace_end_duration(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                                StringRef* string_ref) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_END_DURATION_16(string_ref->GetId(), group);
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_duration_end(
      tag, current_ticks(), thread_ref,
      fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
      fxt::StringRef{string_ref->GetFxtId()});
}

template <bool enabled>
inline void ktrace_begin_duration(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                                  StringRef* string_ref, uint64_t a, uint64_t b) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_BEGIN_DURATION_32(string_ref->GetId(), group);
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_duration_begin(
      tag, current_ticks(), thread_ref,
      fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
      fxt::StringRef{string_ref->GetFxtId()},
      fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
          fxt::StringRef{"arg0"_stringref->GetFxtId()}, a},
      fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
          fxt::StringRef{"arg1"_stringref->GetFxtId()}, b});
}

template <bool enabled>
inline void ktrace_end_duration(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                                StringRef* string_ref, uint64_t a, uint64_t b) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_END_DURATION_32(string_ref->GetId(), group);
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_duration_end(
      tag, current_ticks(), thread_ref,
      fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
      fxt::StringRef{string_ref->GetFxtId()},
      fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
          fxt::StringRef{"arg0"_stringref->GetFxtId()}, a},
      fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
          fxt::StringRef{"arg1"_stringref->GetFxtId()}, b});
}

template <bool enabled>
inline void ktrace_flow_begin(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                              StringRef* string_ref, uint64_t flow_id, uint64_t a = 0) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_FLOW_BEGIN(string_ref->GetId(), group);
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_flow_begin(tag, current_ticks(), thread_ref,
                                fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
                                fxt::StringRef{string_ref->GetFxtId()}, flow_id,
                                fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
                                    fxt::StringRef{"arg0"_stringref->GetFxtId()}, a});
}

template <bool enabled>
inline void ktrace_flow_end(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                            StringRef* string_ref, uint64_t flow_id, uint64_t a = 0) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_FLOW_END(string_ref->GetId(), group);
  auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_flow_end(tag, current_ticks(), thread_ref,
                              fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
                              fxt::StringRef{string_ref->GetFxtId()}, flow_id,
                              fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
                                  fxt::StringRef{"arg0"_stringref->GetFxtId()}, a});
}

template <bool enabled>
inline void ktrace_flow_step(TraceEnabled<enabled>, TraceContext context, uint32_t group,
                             StringRef* string_ref, uint64_t flow_id, uint64_t a = 0) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = TAG_FLOW_STEP(string_ref->GetId(), group);
  const auto thread_ref =
      context == TraceContext::Thread
          ? fxt::ThreadRef{Thread::Current::Get()->pid(), Thread::Current::Get()->tid()}
          : fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()};
  ktrace_thunks::fxt_flow_step(tag, current_ticks(), thread_ref,
                               fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
                               fxt::StringRef{string_ref->GetFxtId()}, flow_id,
                               fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>{
                                   fxt::StringRef{"arg0"_stringref->GetFxtId()}, a});
}

template <bool enabled>
inline void ktrace_counter(TraceEnabled<enabled>, uint32_t group, StringRef* string_ref,
                           int64_t value, uint64_t counter_id = 0) {
  if constexpr (!enabled) {
    return;
  }
  const uint32_t tag = KTRACE_TAG_FLAGS(TAG_COUNTER(string_ref->GetId(), group), KTRACE_FLAGS_CPU);
  ktrace_thunks::fxt_counter(tag, current_ticks(),
                             fxt::ThreadRef{kNoProcess, kKernelPseudoCpuBase + arch_curr_cpu_num()},
                             fxt::StringRef{GetCategoryForGroup(KTRACE_GROUP(tag))->GetFxtId()},
                             fxt::StringRef{string_ref->GetFxtId()}, counter_id,
                             fxt::Argument{fxt::StringRef{"arg0"_stringref->GetFxtId()}, value});
}

inline ssize_t ktrace_read_user(user_out_ptr<void> ptr, uint32_t off, size_t len) {
  return ktrace_thunks::read_user(ptr, off, len);
}

template <fxt::RefType name_type, fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_kernel_object(uint32_t tag, bool always, zx_koid_t koid, zx_obj_type_t obj_type,
                              const fxt::StringRef<name_type>& name_arg,
                              const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_kernel_object(tag, always, koid, obj_type, name_arg, args...);
}

template <fxt::RefType outgoing_type, fxt::RefType incoming_type>
inline void fxt_context_switch(uint32_t tag, uint64_t timestamp, uint8_t cpu_num,
                               zx_thread_state_t outgoing_thread_state,
                               const fxt::ThreadRef<outgoing_type>& outgoing_thread,
                               const fxt::ThreadRef<incoming_type>& incoming_thread,
                               uint8_t outgoing_priority, uint8_t incoming_priority) {
  ktrace_thunks::fxt_context_switch(tag, timestamp, cpu_num, outgoing_thread_state, outgoing_thread,
                                    incoming_thread, outgoing_priority, incoming_priority);
}

inline void fxt_string_record(uint16_t index, const char* string, size_t string_length) {
  ktrace_thunks::fxt_string_record(index, string, string_length);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_instant(uint32_t tag, uint64_t timestamp,
                        const fxt::ThreadRef<thread_type>& thread_ref,
                        const fxt::StringRef<category_type>& category_ref,
                        const fxt::StringRef<name_type>& name_ref,
                        const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_instant(tag, timestamp, thread_ref, category_ref, name_ref, args...);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_duration_begin(uint32_t tag, uint64_t timestamp,
                               const fxt::ThreadRef<thread_type>& thread_ref,
                               const fxt::StringRef<category_type>& category_ref,
                               const fxt::StringRef<name_type>& name_ref,
                               const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_duration_begin(tag, timestamp, thread_ref, category_ref, name_ref, args...);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_duration_end(uint32_t tag, uint64_t timestamp,
                             const fxt::ThreadRef<thread_type>& thread_ref,
                             const fxt::StringRef<category_type>& category_ref,
                             const fxt::StringRef<name_type>& name_ref,
                             const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_duration_end(tag, timestamp, thread_ref, category_ref, name_ref, args...);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_duration_complete(uint32_t tag, uint64_t start_time,
                                  const fxt::ThreadRef<thread_type>& thread_ref,
                                  const fxt::StringRef<category_type>& category_ref,
                                  const fxt::StringRef<name_type>& name_ref, uint64_t end_time,
                                  const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_duration_complete(tag, start_time, thread_ref, category_ref, name_ref,
                                       end_time, args...);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_counter(uint32_t tag, uint64_t timestamp,
                        const fxt::ThreadRef<thread_type>& thread_ref,
                        const fxt::StringRef<category_type>& category_ref,
                        const fxt::StringRef<name_type>& name_ref, uint64_t counter_id,
                        const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_counter(tag, timestamp, thread_ref, category_ref, name_ref, counter_id,
                             args...);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_flow_begin(uint32_t tag, uint64_t timestamp,
                           const fxt::ThreadRef<thread_type>& thread_ref,
                           const fxt::StringRef<category_type>& category_ref,
                           const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                           const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_flow_begin(tag, timestamp, thread_ref, category_ref, name_ref, flow_id,
                                args...);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_flow_step(uint32_t tag, uint64_t timestamp,
                          const fxt::ThreadRef<thread_type>& thread_ref,
                          const fxt::StringRef<category_type>& category_ref,
                          const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                          const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_flow_step(tag, timestamp, thread_ref, category_ref, name_ref, flow_id,
                               args...);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
inline void fxt_flow_end(uint32_t tag, uint64_t timestamp,
                         const fxt::ThreadRef<thread_type>& thread_ref,
                         const fxt::StringRef<category_type>& category_ref,
                         const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                         const fxt::Argument<arg_types, ref_types>&... args) {
  ktrace_thunks::fxt_flow_end(tag, timestamp, thread_ref, category_ref, name_ref, flow_id, args...);
}

zx_status_t ktrace_control(uint32_t action, uint32_t options, void* ptr);

void ktrace_report_live_threads();
void ktrace_report_live_processes();

// RAII type that emits begin/end duration events covering the lifetime of the
// instance for use in tracing scopes.
// TODO(eieio): Add option to combine begin/end traces as a single complete
// event for better trace buffer efficiency.
template <typename Enabled, uint16_t group, TraceContext = TraceContext::Thread>
class TraceDuration;

template <bool enabled, uint16_t group, TraceContext context>
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
