// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/ktrace.h>
#include <lib/ktrace/ktrace_internal.h>

#include <ktl/forward.h>

// Fwd declaration of the global singleton KTraceState
extern internal::KTraceState KTRACE_STATE;

namespace ktrace_thunks {

bool tag_enabled(uint32_t tag) { return KTRACE_STATE.tag_enabled(tag); }

template <typename... Args>
void write_record(uint32_t effective_tag, uint64_t explicit_ts, Args... args) {
  if (unlikely(KTRACE_STATE.tag_enabled(effective_tag))) {
    KTRACE_STATE.WriteRecord(effective_tag, explicit_ts, ktl::forward<Args>(args)...);
  }
}

void write_record_tiny(uint32_t tag, uint32_t arg) {
  if (unlikely(KTRACE_STATE.tag_enabled(tag))) {
    KTRACE_STATE.WriteRecordTiny(tag, arg);
  }
}

void write_name_etc(uint32_t tag, uint32_t id, uint32_t arg, const char* name, bool always) {
  KTRACE_STATE.WriteNameEtc(tag, id, arg, name, always);
}

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

void fxt_string_record(uint16_t index, const char* string, size_t string_length) {
  auto writer = KTRACE_STATE.make_fxt_writer(TAG_PROBE_NAME);
  (void)fxt::WriteStringRecord(&writer, index, string, string_length);
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

template void write_record(uint32_t effective_tag, uint64_t explicit_ts);
template void write_record(uint32_t effective_tag, uint64_t explicit_ts, uint32_t a);
template void write_record(uint32_t effective_tag, uint64_t explicit_ts, uint32_t a, uint32_t b);
template void write_record(uint32_t effective_tag, uint64_t explicit_ts, uint32_t a, uint32_t b,
                           uint32_t c);
template void write_record(uint32_t effective_tag, uint64_t explicit_ts, uint32_t a, uint32_t b,
                           uint32_t c, uint32_t d);
template void write_record(uint32_t effective_tag, uint64_t explicit_ts, uint64_t a);
template void write_record(uint32_t effective_tag, uint64_t explicit_ts, uint64_t a, uint64_t b);

template void fxt_kernel_object(uint32_t tag, bool always, zx_koid_t koid, zx_obj_type_t obj_type,
                                const fxt::StringRef<fxt::RefType::kInline>& name_arg);
template void fxt_kernel_object(uint32_t tag, bool always, zx_koid_t koid, zx_obj_type_t obj_type,
                                const fxt::StringRef<fxt::RefType::kInline>& name_arg,
                                const fxt::Argument<fxt::ArgumentType::kKoid, fxt::RefType::kId>&);

template void fxt_duration_begin(uint32_t tag, uint64_t timestamp,
                                 const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                                 const fxt::StringRef<fxt::RefType::kId>& category_ref,
                                 const fxt::StringRef<fxt::RefType::kId>& name_ref);
template void fxt_duration_end(uint32_t tag, uint64_t timestamp,
                               const fxt::ThreadRef<fxt::RefType::kInline>& thread_ref,
                               const fxt::StringRef<fxt::RefType::kId>& category_ref,
                               const fxt::StringRef<fxt::RefType::kId>& name_ref);

}  // namespace ktrace_thunks
