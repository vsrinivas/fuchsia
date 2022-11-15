// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/ktrace.h>
#include <lib/ktrace/ktrace_internal.h>

#include <ktl/forward.h>

#include "lib/fxt/serializer.h"

// Fwd declaration of the global singleton KTraceState
extern internal::KTraceState KTRACE_STATE;

namespace ktrace_thunks {

bool tag_enabled(uint32_t tag) { return KTRACE_STATE.tag_enabled(tag); }

ssize_t read_user(user_out_ptr<void> ptr, uint32_t off, size_t len) {
  return KTRACE_STATE.ReadUser(ptr, off, len);
}

template <fxt::RefType name_type, fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_kernel_object(uint32_t tag, bool always, zx_koid_t koid, zx_obj_type_t obj_type,
                       const fxt::StringRef<name_type>& name_arg,
                       const fxt::Argument<arg_types, ref_types>&... args) {
  if (always || unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteKernelObjectRecord(&writer, koid, obj_type, name_arg, args...);
  }
}

template <fxt::RefType outgoing_type, fxt::RefType incoming_type>
void fxt_context_switch(uint32_t tag, uint64_t timestamp, uint8_t cpu_number,
                        zx_thread_state_t outgoing_thread_state,
                        const fxt::ThreadRef<outgoing_type>& outgoing_thread,
                        const fxt::ThreadRef<incoming_type>& incoming_thread,
                        uint8_t outgoing_thread_priority, uint8_t incoming_thread_priority) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteContextSwitchRecord(&writer, timestamp, cpu_number, outgoing_thread_state,
                                        outgoing_thread, incoming_thread, outgoing_thread_priority,
                                        incoming_thread_priority);
  }
}

void fxt_string_record(uint16_t index, const char* string, size_t string_length) {
  auto writer = KTRACE_STATE.make_fxt_writer(TAG_PROBE_NAME);
  (void)fxt::WriteStringRecord(&writer, index, string, string_length);
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_instant(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                 const fxt::StringRef<category_type>& category_ref,
                 const fxt::StringRef<name_type>& name_ref,
                 const fxt::Argument<arg_types, ref_types>&... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteInstantEventRecord(&writer, timestamp, thread_ref, category_ref, name_ref,
                                       args...);
  }
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_duration_begin(uint32_t tag, uint64_t timestamp,
                        const fxt::ThreadRef<thread_type>& thread_ref,
                        const fxt::StringRef<category_type>& category_ref,
                        const fxt::StringRef<name_type>& name_ref,
                        const fxt::Argument<arg_types, ref_types>&... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteDurationBeginEventRecord(&writer, timestamp, thread_ref, category_ref, name_ref,
                                             args...);
  }
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_duration_end(uint32_t tag, uint64_t timestamp,
                      const fxt::ThreadRef<thread_type>& thread_ref,
                      const fxt::StringRef<category_type>& category_ref,
                      const fxt::StringRef<name_type>& name_ref,
                      const fxt::Argument<arg_types, ref_types>&... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteDurationEndEventRecord(&writer, timestamp, thread_ref, category_ref, name_ref,
                                           args...);
  }
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_duration_complete(uint32_t tag, uint64_t start,
                           const fxt::ThreadRef<thread_type>& thread_ref,
                           const fxt::StringRef<category_type>& category_ref,
                           const fxt::StringRef<name_type>& name_ref, uint64_t end,
                           const fxt::Argument<arg_types, ref_types>&... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteDurationCompleteEventRecord(&writer, start, thread_ref, category_ref, name_ref,
                                                end, args...);
  }
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_counter(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                 const fxt::StringRef<category_type>& category_ref,
                 const fxt::StringRef<name_type>& name_ref, uint64_t counter_id,
                 const fxt::Argument<arg_types, ref_types>&... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteCounterEventRecord(&writer, timestamp, thread_ref, category_ref, name_ref,
                                       counter_id, args...);
  }
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_flow_begin(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                    const fxt::StringRef<category_type>& category_ref,
                    const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                    const fxt::Argument<arg_types, ref_types>&... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteFlowBeginEventRecord(&writer, timestamp, thread_ref, category_ref, name_ref,
                                         flow_id, args...);
  }
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_flow_step(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                   const fxt::StringRef<category_type>& category_ref,
                   const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                   const fxt::Argument<arg_types, ref_types>&... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteFlowStepEventRecord(&writer, timestamp, thread_ref, category_ref, name_ref,
                                        flow_id, args...);
  }
}

template <fxt::RefType thread_type, fxt::RefType category_type, fxt::RefType name_type,
          fxt::ArgumentType... arg_types, fxt::RefType... ref_types>
void fxt_flow_end(uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<thread_type>& thread_ref,
                  const fxt::StringRef<category_type>& category_ref,
                  const fxt::StringRef<name_type>& name_ref, uint64_t flow_id,
                  const fxt::Argument<arg_types, ref_types>&... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    auto writer = KTRACE_STATE.make_fxt_writer(tag);
    (void)fxt::WriteFlowEndEventRecord(&writer, timestamp, thread_ref, category_ref, name_ref,
                                       flow_id, args...);
  }
}

template void fxt_kernel_object(uint32_t tag, bool always, zx_koid_t koid, zx_obj_type_t obj_type,
                                const fxt::StringRef<fxt::RefType::kInline>& name_arg);
template void fxt_kernel_object(uint32_t tag, bool always, zx_koid_t koid, zx_obj_type_t obj_type,
                                const fxt::StringRef<fxt::RefType::kId>& name_arg);
template void fxt_kernel_object(uint32_t tag, bool always, zx_koid_t koid, zx_obj_type_t obj_type,
                                const fxt::StringRef<fxt::RefType::kInline>& name_arg,
                                const fxt::Argument<fxt::ArgumentType::kKoid, fxt::RefType::kId>&);

template void fxt_instant(uint32_t tag, uint64_t timestamp,
                          const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                          const fxt::StringRef<fxt::RefType::kId>& category_ref,
                          const fxt::StringRef<fxt::RefType::kId>& name_ref);
template void fxt_instant(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg);
template void fxt_instant(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg2);
template void fxt_instant(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg2);
template void fxt_duration_begin(uint32_t tag, uint64_t timestamp,
                                 const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                                 const fxt::StringRef<fxt::RefType::kId>& category_ref,
                                 const fxt::StringRef<fxt::RefType::kId>& name_ref);
template void fxt_duration_begin(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg);
template void fxt_duration_begin(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg);
template void fxt_duration_begin(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg2);
template void fxt_duration_begin(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg2);
template void fxt_duration_begin(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg2);

template void fxt_duration_end(uint32_t tag, uint64_t timestamp,
                               const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                               const fxt::StringRef<fxt::RefType::kId>& category_ref,
                               const fxt::StringRef<fxt::RefType::kId>& name_ref);
template void fxt_duration_end(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg);
template void fxt_duration_end(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg);
template void fxt_duration_end(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kString, fxt::RefType::kId, fxt::RefType::kId>& arg2);
template void fxt_duration_end(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg2);
template void fxt_duration_end(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg2);
template void fxt_duration_complete(uint32_t tag, uint64_t start_time,
                                    const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                                    const fxt::StringRef<fxt::RefType::kId>& category_ref,
                                    const fxt::StringRef<fxt::RefType::kId>& name_ref,
                                    uint64_t end_time);
template void fxt_duration_complete(
    uint32_t tag, uint64_t start_time, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t end_time,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg2);
template void fxt_duration_complete(
    uint32_t tag, uint64_t start_time, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t end_time,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg1);
template void fxt_duration_complete(
    uint32_t tag, uint64_t start_time, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t end_time,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg1,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg2,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg3,
    const fxt::Argument<fxt::ArgumentType::kUint32, fxt::RefType::kId, fxt::RefType::kId>& arg4);

template void fxt_counter(uint32_t tag, uint64_t timestamp,
                          const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                          const fxt::StringRef<fxt::RefType::kId>& category_ref,
                          const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t counter_id);
template void fxt_counter(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t counter_id,
    const fxt::Argument<fxt::ArgumentType::kInt64, fxt::RefType::kId, fxt::RefType::kId>& arg4);
template void fxt_counter(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t counter_id,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg);

template void fxt_flow_begin(uint32_t tag, uint64_t timestamp,
                             const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                             const fxt::StringRef<fxt::RefType::kId>& category_ref,
                             const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t flow_id);
template void fxt_flow_begin(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t flow_id,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg);
template void fxt_flow_step(uint32_t tag, uint64_t timestamp,
                            const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                            const fxt::StringRef<fxt::RefType::kId>& category_ref,
                            const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t flow_id);
template void fxt_flow_step(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t flow_id,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg);
template void fxt_flow_end(uint32_t tag, uint64_t timestamp,
                           const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                           const fxt::StringRef<fxt::RefType::kId>& category_ref,
                           const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t flow_id);
template void fxt_flow_end(
    uint32_t tag, uint64_t timestamp, const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
    const fxt::StringRef<fxt::RefType::kId>& category_ref,
    const fxt::StringRef<fxt::RefType::kId>& name_ref, uint64_t flow_id,
    const fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId, fxt::RefType::kId>& arg);

template void fxt_context_switch(uint32_t tag, uint64_t timestamp, uint8_t cpu_number,
                                 zx_thread_state_t outgoing_thread_state,
                                 const fxt::ThreadRef<fxt::RefType::kInline>& outgoing_thread,
                                 const fxt::ThreadRef<fxt::RefType::kInline>& incoming_thread,
                                 uint8_t outgoing_thread_priority,
                                 uint8_t incoming_thread_priority);
}  // namespace ktrace_thunks
