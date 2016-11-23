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

constexpr char kProcessArgKey[] = "process";

inline uint64_t GetNanosecondTimestamp() {
  return mx_time_get(MX_CLOCK_MONOTONIC);
}

inline uint64_t MakeRecordHeader(RecordType type, size_t size) {
  return RecordFields::Type::Make(ToUnderlyingType(type)) |
         RecordFields::RecordSize::Make(size >> 3);
}

}  // namespace

TraceEngine::TraceEngine(ftl::RefPtr<mtl::SharedVmo> buffer,
                         mx::eventpair fence,
                         std::vector<std::string> enabled_categories)
    : generation_(g_next_generation.fetch_add(1u, std::memory_order_relaxed)),
      buffer_(std::move(buffer)),
      buffer_start_(reinterpret_cast<uintptr_t>(buffer_->Map())),
      buffer_end_(buffer_start_ + buffer_->vmo_size()),
      buffer_current_(buffer_start_),
      fence_(std::move(fence)),
      enabled_categories_(std::move(enabled_categories)),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()),
      weak_ptr_factory_(this) {
  FTL_DCHECK(task_runner_);

  for (const auto& category : enabled_categories_) {
    enabled_category_set_.emplace(category);
  }

  if (!g_process_koid)
    g_process_koid = mtl::GetCurrentProcessKoid();
}

TraceEngine::~TraceEngine() {
  FTL_DCHECK(!fence_handler_key_);
}

std::unique_ptr<TraceEngine> TraceEngine::Create(
    mx::vmo buffer,
    mx::eventpair fence,
    std::vector<std::string> enabled_categories) {
  FTL_DCHECK(buffer);
  FTL_DCHECK(fence);
  FTL_DCHECK(mtl::MessageLoop::GetCurrent());

  auto buffer_shared_vmo = ftl::MakeRefCounted<mtl::SharedVmo>(
      std::move(buffer), MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
  if (!buffer_shared_vmo->Map()) {
    FTL_LOG(ERROR) << "Could not map trace buffer.";
    return nullptr;
  }
  return std::unique_ptr<TraceEngine>(
      new TraceEngine(std::move(buffer_shared_vmo), std::move(fence),
                      std::move(enabled_categories)));
}

void TraceEngine::StartTracing(TraceFinishedCallback finished_callback) {
  FTL_DCHECK(finished_callback);
  FTL_DCHECK(!finished_callback_);
  FTL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  FTL_VLOG(1) << "Started tracing...";
  finished_callback_ = std::move(finished_callback);

  WriteKernelObjectRecordBase(g_process_koid, MX_OBJ_TYPE_PROCESS,
                              mtl::GetCurrentProcessName().c_str(), 0u, 0u);

  fence_handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
      this, fence_.get(), MX_EPAIR_CLOSED);
}

void TraceEngine::StopTracing() {
  StopTracing(TraceDisposition::kFinishedNormally, false);
}

bool TraceEngine::IsCategoryEnabled(const char* category) const {
  return enabled_category_set_.empty() ||
         enabled_category_set_.count(ftl::StringView(category)) > 0;
}

TraceEngine::StringRef TraceEngine::RegisterString(const char* constant,
                                                   bool check_category) {
  if (!constant || !*constant)
    return StringRef::MakeEmpty();

  LocalState& state = g_local_state;

  if (state.string_generation < generation_) {
    state.string_generation = generation_;
    state.string_table.clear();
  }

  if (state.string_generation == generation_) {
    auto it = state.string_table.find(constant);
    if (it == state.string_table.end()) {
      std::tie(it, std::ignore) = state.string_table.emplace(constant, 0u);
    }

    if (check_category && (!(it->second & kCategoryEnabledFlag))) {
      if (it->second & kCategoryDisabledFlag) {
        return StringRef::MakeEmpty();
      }
      if (!IsCategoryEnabled(constant)) {
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
        WriteStringRecord(string_index, constant);
        it->second |= string_index | kIndexedFlag;
        return StringRef::MakeIndexed(string_index);
      }
      next_string_index_.store(StringRefFields::kMaxIndex + 1u,
                               std::memory_order_relaxed);
      it->second |= kInlinedFlag;
    }
  } else if (check_category && !IsCategoryEnabled(constant)) {
    return StringRef::MakeEmpty();
  }

  return StringRef::MakeInlinedOrEmpty(constant, strlen(constant));
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

    ::tracing::writer::KoidArgument process_arg(
        RegisterString(kProcessArgKey, false), g_process_koid);
    Payload payload = WriteKernelObjectRecordBase(
        state.thread_koid, MX_OBJ_TYPE_THREAD,
        mtl::GetCurrentThreadName().c_str(), 1u, process_arg.Size());
    if (payload)
      payload.WriteValue(process_arg);

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

void TraceEngine::WriteStringRecord(StringIndex index, const char* value) {
  FTL_DCHECK(index != StringRefFields::kInvalidIndex);

  const size_t length = strlen(value);
  const size_t record_size = sizeof(RecordHeader) + Pad(length);
  Payload payload = AllocateRecord(record_size);
  if (!payload)
    return;

  payload
      .Write(MakeRecordHeader(RecordType::kString, record_size) |
             StringRecordFields::StringIndex::Make(index) |
             StringRecordFields::StringLength::Make(length))
      .WriteBytes(value, length);
}

void TraceEngine::WriteThreadRecord(ThreadIndex index,
                                    mx_koid_t process_koid,
                                    mx_koid_t thread_koid) {
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

TraceEngine::Payload TraceEngine::WriteEventRecordBase(
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

TraceEngine::Payload TraceEngine::WriteKernelObjectRecordBase(
    mx_handle_t handle,
    size_t argument_count,
    size_t payload_size) {
  mx_info_handle_basic_t info;
  mx_status_t status = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info,
                                          sizeof(info), nullptr, nullptr);
  if (status != NO_ERROR)
    return Payload(nullptr);

  return WriteKernelObjectRecordBase(
      info.koid, static_cast<mx_obj_type_t>(info.type),
      mtl::GetObjectName(handle).c_str(), argument_count, payload_size);
}

TraceEngine::Payload TraceEngine::WriteKernelObjectRecordBase(
    mx_koid_t koid,
    mx_obj_type_t object_type,
    const char* name,
    size_t argument_count,
    size_t payload_size) {
  const StringRef name_ref = StringRef::MakeInlinedOrEmpty(name, strlen(name));
  const size_t record_size =
      sizeof(RecordHeader) + WordsToBytes(1) + name_ref.Size() + payload_size;
  Payload payload = AllocateRecord(record_size);
  if (!payload)
    return payload;

  payload
      .Write(MakeRecordHeader(RecordType::kKernelObject, record_size) |
             KernelObjectRecordFields::ObjectType::Make(
                 ToUnderlyingType(object_type)) |
             KernelObjectRecordFields::NameStringRef::Make(
                 name_ref.encoded_value()) |
             KernelObjectRecordFields::ArgumentCount::Make(argument_count))
      .Write(koid)
      .WriteValue(name_ref);
  return payload;
}

TraceEngine::Payload TraceEngine::AllocateRecord(size_t num_bytes) {
  uintptr_t record_start =
      buffer_current_.fetch_add(num_bytes, std::memory_order_relaxed);
  if (record_start + num_bytes <= buffer_end_)
    return Payload(reinterpret_cast<uint64_t*>(record_start));

  StopTracing(TraceDisposition::kBufferExhausted, false);
  return Payload(nullptr);
}

void TraceEngine::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  FTL_DCHECK(pending & MX_EPAIR_CLOSED);

  StopTracing(TraceDisposition::kConnectionLost, true);
}

void TraceEngine::OnHandleError(mx_handle_t handle, mx_status_t error) {
  FTL_DCHECK(error == ERR_BAD_STATE);

  StopTracing(TraceDisposition::kConnectionLost, true);
}

void TraceEngine::StopTracing(TraceDisposition disposition, bool immediate) {
  // Prevent wrapping.
  buffer_current_.store(buffer_end_, std::memory_order_relaxed);

  // We can't close the fence until all trace writers have released
  // their references to the trace engine since they may still have
  // partially written records.  So we set a flag while we await our doom.
  if (state_.exchange(State::kAwaitingFinish) == State::kAwaitingFinish)
    return;

  // Finish cleanup on message loop.
  if (immediate) {
    StopTracingOnMessageLoop(disposition);
  } else {
    task_runner_->PostTask(
        [ weak = weak_ptr_factory_.GetWeakPtr(), disposition ] {
          if (weak)
            weak->StopTracingOnMessageLoop(disposition);
        });
  }
}

void TraceEngine::StopTracingOnMessageLoop(TraceDisposition disposition) {
  FTL_DCHECK(task_runner_->RunsTasksOnCurrentThread());

  mtl::MessageLoop::GetCurrent()->RemoveHandler(fence_handler_key_);
  fence_handler_key_ = 0u;

  switch (disposition) {
    case TraceDisposition::kFinishedNormally:
      FTL_VLOG(1) << "Trace finished normally";
      break;
    case TraceDisposition::kConnectionLost:
      FTL_LOG(WARNING) << "Trace aborted: connection lost";
      break;
    case TraceDisposition::kBufferExhausted:
      FTL_LOG(WARNING) << "Trace aborted: buffer exhausted";
      break;
  }

  finished_callback_(disposition);
}

}  // namespace internal
}  // namespace tracing
