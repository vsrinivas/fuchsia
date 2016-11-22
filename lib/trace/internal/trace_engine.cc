// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/trace_engine.h"

#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <tuple>
#include <unordered_map>
#include <utility>

#include "lib/ftl/logging.h"
#include "lib/mtl/handles/object_info.h"

namespace tracing {
namespace internal {
namespace {

std::atomic<uint32_t> g_next_generation{1u};
mx_koid_t g_process_koid;

using StringSpec = uint32_t;
constexpr StringSpec kStringIndexMask = 0x7fff;
constexpr StringSpec kIndexedFlag = 0x10000;
constexpr StringSpec kInlinedFlag = 0x20000;
constexpr StringSpec kCategoryEnabledFlag = 0x40000;
constexpr StringSpec kCategoryDisabledFlag = 0x80000;

struct LocalState {
  // String table state.
  // Note: Strings are hashed by pointer not by content.
  uint32_t string_generation = 0u;
  std::unordered_map<const char*, StringSpec> string_table;

  // Thread table state.
  uint32_t thread_generation = 0u;
  mx_koid_t thread_koid = MX_KOID_INVALID;
  TraceEngine::ThreadRef thread_ref =
      TraceEngine::ThreadRef::MakeInlined(MX_KOID_INVALID, MX_KOID_INVALID);
};
thread_local LocalState g_local_state;

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
    : generation_(g_next_generation.fetch_add(1u, std::memory_order_relaxed)),
      shared_vmo_(std::move(shared_vmo)),
      buffer_start_(reinterpret_cast<uintptr_t>(shared_vmo_->Map())),
      buffer_end_(buffer_start_ + shared_vmo_->vmo_size()),
      buffer_current_(buffer_start_),
      enabled_categories_(std::move(enabled_categories)) {
  for (const auto& category : enabled_categories_) {
    enabled_category_set_.emplace(category);
  }

  if (!g_process_koid)
    g_process_koid = mtl::GetCurrentProcessKoid();
}

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

bool TraceEngine::IsCategoryEnabled(const char* category) const {
  return enabled_category_set_.empty() ||
         enabled_category_set_.count(ftl::StringView(category)) > 0;
}

TraceEngine::StringRef TraceEngine::RegisterString(const char* string,
                                                   bool check_category) {
  LocalState& state = g_local_state;

  if (state.string_generation < generation_) {
    state.string_generation = generation_;
    state.string_table.clear();
  }

  if (state.string_generation == generation_) {
    auto it = state.string_table.find(string);
    if (it == state.string_table.end()) {
      std::tie(it, std::ignore) = state.string_table.emplace(string, 0u);
    }

    if (check_category && (!(it->second & kCategoryEnabledFlag))) {
      if (it->second & kCategoryDisabledFlag) {
        return StringRef::MakeEmpty();
      }
      if (!IsCategoryEnabled(string)) {
        it->second |= kCategoryDisabledFlag;
        return StringRef::MakeEmpty();
      }
      it->second |= kCategoryEnabledFlag;
    }

    if (it->second & kIndexedFlag) {
      return StringRef::MakeIndexed(it->second & kStringIndexMask);
    }

    if (!(it->second & kInlinedFlag)) {
      StringIndex string_index =
          next_string_index_.fetch_add(1u, std::memory_order_relaxed);
      if (string_index <= StringRefFields::kMaxIndex) {
        WriteStringRecord(string_index, string);
        it->second |= string_index | kIndexedFlag;
        return StringRef::MakeIndexed(string_index);
      }
      next_string_index_.store(StringRefFields::kMaxIndex + 1u,
                               std::memory_order_relaxed);
      it->second |= kInlinedFlag;
    }
  } else if (check_category && !IsCategoryEnabled(string)) {
    return StringRef::MakeEmpty();
  }

  return StringRef::MakeInlined(string, strlen(string));
}

TraceEngine::ThreadRef TraceEngine::RegisterCurrentThread() {
  LocalState& state = g_local_state;

  if (state.thread_generation == generation_) {
    return state.thread_ref;
  }

  if (state.thread_generation < generation_) {
    state.thread_generation = generation_;
    if (state.thread_koid == MX_KOID_INVALID) {
      state.thread_koid = mtl::GetCurrentThreadKoid();
    }

    ThreadIndex thread_index =
        next_thread_index_.fetch_add(1u, std::memory_order_relaxed);
    if (thread_index <= ThreadRefFields::kMaxIndex) {
      WriteThreadRecord(thread_index, g_process_koid, state.thread_koid);
      state.thread_ref = ThreadRef::MakeIndexed(thread_index);
    } else {
      next_thread_index_.store(ThreadRefFields::kMaxIndex + 1,
                               std::memory_order_relaxed);
      state.thread_ref =
          ThreadRef::MakeInlined(g_process_koid, state.thread_koid);
    }
    return state.thread_ref;
  }

  return ThreadRef::MakeInlined(g_process_koid, state.thread_koid);
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
  const StringRef name_ref = RegisterString(name, false);
  const ThreadRef thread_ref = RegisterCurrentThread();
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
