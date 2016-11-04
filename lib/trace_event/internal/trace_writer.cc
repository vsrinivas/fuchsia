// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace_event/internal/allocator.h"
#include "apps/tracing/lib/trace_event/internal/categories_matcher.h"
#include "apps/tracing/lib/trace_event/internal/table.h"
#include "apps/tracing/lib/trace_event/internal/trace_writer.h"
#include "lib/ftl/logging.h"
#include "magenta/syscalls.h"
#include "magenta/syscalls/object.h"

namespace tracing {
namespace internal {
namespace {

// A hacky way of ensuring a unique id per thread.
thread_local char tid;

inline uint64_t GetProcessKoid() {
  mx_info_handle_basic_t info;
  auto ret = mx_object_get_info(mx_process_self(), MX_INFO_HANDLE_BASIC,
                                sizeof(info.rec), &info, sizeof(info));
  return ret == sizeof(info) ? info.rec.koid : 0;
}

inline uint64_t GetThreadKoid() {
  // TODO(tvoss): Fix once an API for querying the current thread id
  // is available.
  return reinterpret_cast<uintptr_t>(&tid);
}

const uint64_t g_process_koid = GetProcessKoid();
thread_local const uint64_t g_thread_koid = GetThreadKoid();
Allocator g_allocator;
bool g_is_tracing_started = false;
CategoriesMatcher g_categories_matcher;
Table<uintptr_t, 0, 4096> g_string_table;
Table<uint64_t, 0, 256> g_thread_object_table;

inline uint64_t GetNanosecondTimestamp() {
  return mx_current_time();
}

}  // namespace

Payload Payload::New(size_t size) {
  return Payload(static_cast<uint64_t*>(g_allocator.Allocate(size)));
}

ThreadRef RegisterCurrentThread() {
  uint16_t index = 0;

  if (g_thread_object_table.Register(g_thread_koid, &index))
    WriteThreadRecord(index, g_process_koid, g_thread_koid);

  return ThreadRef{index, g_process_koid, g_thread_koid};
}

StringRef RegisterString(const char* string) {
  uint16_t index = 0;

  if (g_string_table.Register(reinterpret_cast<uintptr_t>(string), &index))
    WriteStringRecord(index, string);

  return index == 0 ? StringRef::MakeInlined(string, strlen(string))
                    : StringRef::MakeIndexed(index);
}

void WriteInitializationRecord(uint64_t ticks_per_second) {
  static const uint64_t kRecordSize =
      sizeof(RecordHeader) + sizeof(ticks_per_second);

  FTL_DCHECK(g_allocator);

  if (Payload payload = Payload::New(kRecordSize)) {
    payload
        .Write(InitializationRecordFields::Type::Make(
                   ToUnderlyingType(RecordType::kInitialization)) |
               InitializationRecordFields::RecordSize::Make(kRecordSize >> 3))
        .Write(ticks_per_second);
  }
}

void WriteStringRecord(uint16_t index, const char* string) {
  FTL_DCHECK(g_allocator);

  if (index == 0) {
    FTL_LOG(WARNING) << "Registering a string to index 0 is forbidden";
    return;
  }

  auto length = strlen(string);
  auto size = sizeof(RecordHeader) + Pad(length);

  if (Payload payload = Payload::New(size)) {
    payload
        .Write(StringRecordFields::Type::Make(
                   ToUnderlyingType(RecordType::kString)) |
               StringRecordFields::RecordSize::Make(size >> 3) |
               StringRecordFields::StringIndex::Make(index) |
               StringRecordFields::StringLength::Make(length))
        .WriteBytes(string, length);
  }
}

void WriteThreadRecord(uint16_t index,
                       uint64_t process_koid,
                       uint64_t thread_koid) {
  FTL_DCHECK(g_allocator);

  if (index == 0) {
    FTL_LOG(WARNING) << "Registering a thread to index 0 is forbidden";
    return;
  }

  auto size = sizeof(RecordHeader) + 2 * sizeof(uint64_t);

  if (Payload payload = Payload::New(size)) {
    payload
        .Write(ThreadRecordFields::Type::Make(
                   ToUnderlyingType(RecordType::kThread)) |
               ThreadRecordFields::RecordSize::Make(size >> 3) |
               ThreadRecordFields::ThreadIndex::Make(index))
        .Write(process_koid)
        .Write(thread_koid);
  }
}

Payload WriteEventRecord(TraceEventType event_type,
                         const char* category,
                         const char* name,
                         size_t argument_count,
                         uint64_t payload_size) {
  FTL_DCHECK(g_allocator);

  auto category_ref = RegisterString(category);
  auto name_ref = RegisterString(name);
  auto thread_ref = RegisterCurrentThread();

  auto size = sizeof(RecordHeader) + sizeof(uint64_t) + thread_ref.Size() +
              category_ref.Size() + name_ref.Size() + payload_size;

  if (Payload payload = Payload::New(size)) {
    return payload
        .Write(
            EventRecordFields::Type::Make(
                ToUnderlyingType(RecordType::kEvent)) |
            EventRecordFields::RecordSize::Make(size >> 3) |
            EventRecordFields::EventType::Make(ToUnderlyingType(event_type)) |
            EventRecordFields::ArgumentCount::Make(argument_count) |
            EventRecordFields::ThreadRef::Make(thread_ref.index) |
            EventRecordFields::CategoryStringRef::Make(category_ref.encoded) |
            EventRecordFields::NameStringRef::Make(name_ref.encoded))
        .Write(GetNanosecondTimestamp())
        .WriteValue(thread_ref)
        .WriteValue(category_ref)
        .WriteValue(name_ref);
  }

  return Payload(nullptr);
}

void StartTracing(void* buffer,
                  size_t buffer_size,
                  const std::vector<std::string>& categories) {
  g_is_tracing_started = true;
  g_allocator.Initialize(buffer, buffer_size);
  g_string_table.Reset();
  g_thread_object_table.Reset();
  g_categories_matcher.SetEnabledCategories(categories);
}

bool IsTracingEnabled(const char* categories) {
  return g_is_tracing_started &&
         g_categories_matcher.IsAnyCategoryEnabled(categories);
}

void StopTracing() {
  g_is_tracing_started = false;
  // TODO: Prevent race conditions.
  g_allocator.Reset();
  g_categories_matcher.Reset();
}

}  // namespace internal
}  // namespace tracing
