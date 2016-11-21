// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/trace_engine.h"

#include <utility>

#include "lib/ftl/logging.h"
#include "magenta/syscalls.h"

namespace tracing {
namespace internal {
namespace {

inline uint64_t GetNanosecondTimestamp() {
  return mx_time_get(MX_CLOCK_MONOTONIC);
}

inline uint64_t MakeRecordHeader(RecordType type, size_t size) {
  return RecordFields::Type::Make(ToUnderlyingType(type)) |
         RecordFields::RecordSize::Make(size >> 3);
}

}  // namespace

TraceEngine::TraceEngine(ftl::RefPtr<mtl::SharedVmo> shared_vmo,
                         std::vector<std::string> enabled_categories)
    : shared_vmo_(std::move(shared_vmo)),
      string_table_(std::move(enabled_categories)),
      buffer_start_(reinterpret_cast<uintptr_t>(shared_vmo_->Map())),
      buffer_end_(buffer_start_ + shared_vmo_->vmo_size()),
      buffer_current_(buffer_start_) {}

TraceEngine::~TraceEngine() {}

std::unique_ptr<TraceEngine> TraceEngine::Create(
    mx::vmo vmo,
    std::vector<std::string> enabled_categories) {
  auto shared_vmo = ftl::MakeRefCounted<mtl::SharedVmo>(
      std::move(vmo), MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
  if (!shared_vmo->Map()) {
    FTL_LOG(ERROR) << "Failed to create trace engine: vmo could not be mapped";
    return nullptr;
  }
  return std::unique_ptr<TraceEngine>(
      new TraceEngine(std::move(shared_vmo), std::move(enabled_categories)));
}

TraceEngine::StringRef TraceEngine::RegisterString(const char* string) {
  return string_table_.RegisterString(this, string);
}

bool TraceEngine::IsCategoryEnabled(const char* category) const {
  return string_table_.IsCategoryEnabled(category);
}

bool TraceEngine::PrepareCategory(const char* category,
                                  StringRef* out_category_ref) {
  return string_table_.PrepareCategory(this, category, out_category_ref);
}

TraceEngine::ThreadRef TraceEngine::RegisterCurrentThread() {
  return thread_table_.RegisterCurrentThread(this);
}

void TraceEngine::WriteInitializationRecord(uint64_t ticks_per_second) {
  const size_t record_size = sizeof(RecordHeader) + WordsToBytes(1);
  Payload payload = AllocateRecord(record_size);
  if (!payload)
    return;

  payload.Write(MakeRecordHeader(RecordType::kInitialization, record_size))
      .Write(ticks_per_second);
}

void TraceEngine::WriteStringRecord(StringIndex index, const char* string) {
  FTL_DCHECK(index != StringRefFields::kInvalidIndex);

  const size_t length = strlen(string);
  const size_t record_size = sizeof(RecordHeader) + Pad(length);
  Payload payload = AllocateRecord(record_size);
  if (!payload)
    return;

  payload
      .Write(MakeRecordHeader(RecordType::kString, record_size) |
             StringRecordFields::StringIndex::Make(index) |
             StringRecordFields::StringLength::Make(length))
      .WriteBytes(string, length);
}

void TraceEngine::WriteThreadRecord(ThreadIndex index,
                                    uint64_t process_koid,
                                    uint64_t thread_koid) {
  FTL_DCHECK(index != ThreadRefFields::kInline);

  const size_t record_size = sizeof(RecordHeader) + WordsToBytes(2);
  Payload payload = AllocateRecord(record_size);
  if (!payload)
    return;

  payload
      .Write(MakeRecordHeader(RecordType::kThread, record_size) |
             ThreadRecordFields::ThreadIndex::Make(index))
      .Write(process_koid)
      .Write(thread_koid);
}

TraceEngine::Payload TraceEngine::WriteEventRecord(
    EventType type,
    const StringRef& category_ref,
    const char* name,
    size_t argument_count,
    size_t payload_size) {
  const StringRef name_ref = string_table_.RegisterString(this, name);
  const ThreadRef thread_ref = thread_table_.RegisterCurrentThread(this);
  const size_t record_size = sizeof(RecordHeader) + WordsToBytes(1) +
                             thread_ref.Size() + category_ref.Size() +
                             name_ref.Size() + payload_size;
  Payload payload = AllocateRecord(record_size);
  if (!payload)
    return payload;

  payload
      .Write(MakeRecordHeader(RecordType::kEvent, record_size) |
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
  return payload;
}

TraceEngine::Payload TraceEngine::AllocateRecord(size_t num_bytes) {
  uintptr_t record_start = buffer_current_.fetch_add(num_bytes);
  if (record_start + num_bytes > buffer_end_)
    return Payload(nullptr);
  return Payload(reinterpret_cast<uint64_t*>(record_start));
}

}  // namespace internal
}  // namespace tracing
