// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/writer.h"

#include "apps/tracing/lib/trace/internal/allocator.h"
#include "apps/tracing/lib/trace/internal/categories_matcher.h"
#include "apps/tracing/lib/trace/internal/table.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/handles/object_info.h"
#include "lib/mtl/vmo/shared_vmo.h"
#include "magenta/syscalls.h"

using namespace ::tracing::internal;

namespace tracing {
namespace writer {
namespace {

// A hacky way of ensuring a unique id per thread.
thread_local char tid;

inline mx_koid_t GetProcessKoid() {
  return mtl::GetKoid(mx_process_self());
}

inline mx_koid_t GetThreadKoid() {
  // TODO(tvoss): Fix once an API for querying the current thread id
  // is available.
  return reinterpret_cast<uintptr_t>(&tid);
}

const mx_koid_t g_process_koid = GetProcessKoid();
thread_local const mx_koid_t g_thread_koid = GetThreadKoid();
Allocator g_allocator;
ftl::RefPtr<mtl::SharedVmo> g_shared_vmo;
bool g_is_tracing_started = false;
CategoriesMatcher g_categories_matcher;
Table<uintptr_t, StringIndex, 4095> g_string_table;
Table<mx_koid_t, ThreadIndex, 255> g_thread_object_table;

inline uint64_t GetNanosecondTimestamp() {
  return mx_time_get(MX_CLOCK_MONOTONIC);
}

inline Payload BeginRecord(size_t num_words) {
  return Payload(static_cast<uint64_t*>(g_allocator.Allocate(num_words)));
}

inline uint64_t MakeRecordHeader(RecordType type, size_t size) {
  return RecordFields::Type::Make(ToUnderlyingType(type)) |
         RecordFields::RecordSize::Make(size >> 3);
}

}  // namespace

StringRef RegisterString(const char* string) {
  if (!string || !*string)
    return StringRef::MakeEmpty();

  bool added;
  StringIndex index =
      g_string_table.Register(reinterpret_cast<uintptr_t>(string), &added);
  if (!index)
    return StringRef::MakeInlined(string, strlen(string));

  if (added)
    WriteStringRecord(index, string);
  return StringRef::MakeIndexed(index);
}

ThreadRef RegisterCurrentThread() {
  bool added;
  ThreadIndex index = g_thread_object_table.Register(g_thread_koid, &added);
  if (!index)
    return ThreadRef::MakeInlined(g_process_koid, g_thread_koid);

  if (added)
    WriteThreadRecord(index, g_process_koid, g_thread_koid);
  return ThreadRef::MakeIndexed(index);
}

void WriteInitializationRecord(uint64_t ticks_per_second) {
  static const size_t kRecordSize = sizeof(RecordHeader) + sizeof(uint64_t);

  if (Payload payload = BeginRecord(kRecordSize)) {
    payload.Write(MakeRecordHeader(RecordType::kInitialization, kRecordSize))
        .Write(ticks_per_second);
  }
}

void WriteStringRecord(StringIndex index, const char* string) {
  FTL_DCHECK(index != StringRefFields::kInvalidIndex);

  size_t length = strlen(string);
  size_t record_size = sizeof(RecordHeader) + Pad(length);

  if (Payload payload = BeginRecord(record_size)) {
    payload
        .Write(MakeRecordHeader(RecordType::kString, record_size) |
               StringRecordFields::StringIndex::Make(index) |
               StringRecordFields::StringLength::Make(length))
        .WriteBytes(string, length);
  }
}

void WriteThreadRecord(ThreadIndex index,
                       uint64_t process_koid,
                       uint64_t thread_koid) {
  FTL_DCHECK(index != ThreadRefFields::kInline);

  size_t record_size = sizeof(RecordHeader) + 2 * sizeof(uint64_t);

  if (Payload payload = BeginRecord(record_size)) {
    payload
        .Write(MakeRecordHeader(RecordType::kThread, record_size) |
               ThreadRecordFields::ThreadIndex::Make(index))
        .Write(process_koid)
        .Write(thread_koid);
  }
}

Payload WriteEventRecord(EventType type,
                         const char* category,
                         const char* name,
                         size_t argument_count,
                         uint64_t payload_size) {
  StringRef category_ref = RegisterString(category);
  StringRef name_ref = RegisterString(name);
  ThreadRef thread_ref = RegisterCurrentThread();

  size_t record_size = sizeof(RecordHeader) + sizeof(uint64_t) +
                       thread_ref.Size() + category_ref.Size() +
                       name_ref.Size() + payload_size;

  Payload payload = BeginRecord(record_size);
  if (payload) {
    payload
        .Write(
            MakeRecordHeader(RecordType::kEvent, record_size) |
            EventRecordFields::EventType::Make(ToUnderlyingType(type)) |
            EventRecordFields::ArgumentCount::Make(argument_count) |
            EventRecordFields::ThreadRef::Make(thread_ref.encoded_value()) |
            EventRecordFields::CategoryStringRef::Make(
                category_ref.encoded_value()) |
            EventRecordFields::NameStringRef::Make(name_ref.encoded_value()))
        .Write(GetNanosecondTimestamp())
        .WriteValue(thread_ref)
        .WriteValue(category_ref)
        .WriteValue(name_ref);
  }
  return payload;
}

void StartTracing(mx::vmo current,
                  mx::vmo next,
                  std::vector<std::string> categories) {
  FTL_VLOG(1) << "Started tracing...";
  // TODO: Use next, instead of just dropping it.
  g_shared_vmo = ftl::MakeRefCounted<mtl::SharedVmo>(
      std::move(current), MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
  g_is_tracing_started = g_shared_vmo->Map() != nullptr;

  if (!g_is_tracing_started) {
    FTL_LOG(WARNING) << "Failed to start tracing, vmo could not be mapped";
    g_shared_vmo = nullptr;
    return;
  }

  g_allocator.Initialize(g_shared_vmo->Map(), g_shared_vmo->vmo_size());
  g_string_table.Reset();
  g_thread_object_table.Reset();
  g_categories_matcher.SetEnabledCategories(std::move(categories));
}

void StopTracing() {
  FTL_VLOG(1) << "Stopped tracing...";
  g_is_tracing_started = false;
  // TODO: Prevent race conditions.
  g_allocator.Reset();
  g_shared_vmo = nullptr;
  g_categories_matcher.Reset();
}

bool IsTracingEnabledForCategory(const char* categories) {
  return g_is_tracing_started &&
         g_categories_matcher.IsAnyCategoryEnabled(categories);
}

}  // namespace writer
}  // namespace tracing
