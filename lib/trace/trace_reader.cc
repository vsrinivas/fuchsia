// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>

#include <iostream>

#include "apps/tracing/lib/trace/trace_reader.h"

namespace tracing {
namespace reader {

TraceInput::TraceInput() = default;
TraceInput::~TraceInput() = default;

MemoryTraceInput::MemoryTraceInput(void* memory, size_t size)
    : chunk_(reinterpret_cast<uint64_t*>(memory), size / sizeof(uint64_t)) {}

bool MemoryTraceInput::ReadChunk(size_t num_words, Chunk* out) {
  return chunk_.ReadChunk(num_words, out);
}

Chunk::Chunk() : current_(nullptr), end_(nullptr) {}

Chunk::Chunk(const uint64_t* begin, size_t size)
    : current_(begin), end_(current_ + size) {}

bool Chunk::Read(uint64_t* value) {
  if (current_ < end_) {
    *value = *current_++;
    return true;
  }
  return false;
}

bool Chunk::ReadChunk(size_t num_words, Chunk* out) {
  if (current_ + num_words > end_)
    return false;

  *out = Chunk(current_, num_words);
  current_ += num_words;
  return true;
}

bool Chunk::ReadString(size_t length, ftl::StringView* out) {
  auto num_words = internal::Pad(length) / sizeof(uint64_t);
  if (current_ + num_words > end_)
    return false;

  *out = ftl::StringView(reinterpret_cast<const char*>(current_), length);
  current_ += num_words;
  return true;
}

TraceContext::TraceContext(const TraceErrorHandler& error_handler)
    : error_handler_(error_handler) {}

void TraceContext::OnError(const std::string& error) const {
  error_handler_(error);
}

ftl::StringView TraceContext::DecodeStringRef(uint16_t string_ref,
                                              Chunk& chunk) const {
  if (string_ref == internal::StringRefFields::kEmpty)
    return ftl::StringView();

  if (!(string_ref & internal::StringRefFields::kInlineFlag)) {
    auto it = string_table_.find(string_ref);
    return it != string_table_.end() ? it->second : ftl::StringView();
  }

  auto length = string_ref & internal::StringRefFields::kLengthMask;
  ftl::StringView string;
  if (!chunk.ReadString(length, &string))
    OnError("Failed to decode string ref");
  return string;
}

void TraceContext::RegisterStringRef(uint16_t index,
                                     const ftl::StringView& string) {
  FTL_DCHECK(index != internal::StringRefFields::kInvalidIndex &&
             index <= internal::StringRefFields::kMaxIndex);
  string_table_[index] = string;
}

Thread TraceContext::DecodeThreadRef(uint16_t thread_ref, Chunk& chunk) const {
  if (thread_ref == internal::ThreadRefFields::kInline) {
    Thread result{0, 0};
    if (chunk.Read(&result.process_koid) && chunk.Read(&result.thread_koid))
      return result;
  } else {
    auto it = thread_table_.find(thread_ref);
    if (it != thread_table_.end())
      return it->second;
  }

  return Thread{0, 0};
}

void TraceContext::RegisterThreadRef(uint16_t index, const Thread& thread) {
  FTL_DCHECK(index != internal::ThreadRefFields::kInline &&
             index <= internal::ThreadRefFields::kMaxIndex);
  thread_table_[index] = thread;
}

ArgumentValue& ArgumentValue::Destroy() {
  using string = std::string;
  if (type_ == ArgumentType::kString)
    string_.~string();

  return *this;
}

ArgumentValue& ArgumentValue::Copy(const ArgumentValue& other) {
  type_ = other.type_;
  switch (type_) {
    case ArgumentType::kInt32:
      int32_ = other.int32_;
      break;
    case ArgumentType::kInt64:
      int64_ = other.int64_;
      break;
    case ArgumentType::kDouble:
      double_ = other.double_;
      break;
    case ArgumentType::kString:
      new (&string_) std::string(other.string_);
      break;
    case ArgumentType::kPointer:
      uint64_ = other.uint64_;
      break;
    case ArgumentType::kKernelObjectId:
      uint64_ = other.uint64_;
      break;
    default:
      break;
  }

  return *this;
}

bool ArgumentReader::ForEachArgument(TraceContext& context,
                                     size_t argument_count,
                                     Chunk& chunk,
                                     const Visitor& visitor) {
  Chunk payload;
  internal::ArgumentHeader header = 0;
  while (argument_count > 0) {
    if (!chunk.Read(&header)) {
      context.OnError("Failed to read argument header");
      return false;
    }

    auto type = internal::ArgumentFields::Type::Get<ArgumentType>(header);
    auto size = internal::ArgumentFields::ArgumentSize::Get<uint16_t>(header);
    auto name_ref = internal::ArgumentFields::NameRef::Get<uint16_t>(header);

    if (size == 0)
      return false;

    if (!chunk.ReadChunk(size - 1, &payload))
      return false;

    bool handled = false;

    switch (type) {
      case ArgumentType::kNull:
        handled = HandleNullArgument(context, header,
                                     context.DecodeStringRef(name_ref, payload),
                                     payload, visitor);
        break;
      case ArgumentType::kInt32:
        handled = HandleInt32Argument(
            context, header, context.DecodeStringRef(name_ref, payload),
            payload, visitor);
        break;
      case ArgumentType::kInt64:
        handled = HandleInt64Argument(
            context, header, context.DecodeStringRef(name_ref, payload),
            payload, visitor);
        break;
      case ArgumentType::kDouble:
        handled = HandleDoubleArgument(
            context, header, context.DecodeStringRef(name_ref, payload),
            payload, visitor);
        break;
      case ArgumentType::kString:
        handled = HandleStringArgument(
            context, header, context.DecodeStringRef(name_ref, payload),
            payload, visitor);
        break;
      case ArgumentType::kPointer:
        handled = HandlePointerArgument(
            context, header, context.DecodeStringRef(name_ref, payload),
            payload, visitor);
        break;
      case ArgumentType::kKernelObjectId:
        handled = HandleKernelObjectIdArgument(
            context, header, context.DecodeStringRef(name_ref, payload),
            payload, visitor);
        break;
      default:
        context.OnError("Encountered an unknown argument type: " +
                        std::to_string(static_cast<uint32_t>(type)));
        break;
    }

    argument_count--;

    if (!handled)
      return false;
  }

  return true;
}

bool ArgumentReader::HandleNullArgument(TraceContext& context,
                                        internal::ArgumentHeader header,
                                        const ftl::StringView& name,
                                        Chunk&,
                                        const Visitor& visitor) {
  visitor(Argument{name.ToString(), ArgumentValue::MakeNull()});
  return true;
}

bool ArgumentReader::HandleInt32Argument(TraceContext& context,
                                         internal::ArgumentHeader header,
                                         const ftl::StringView& name,
                                         Chunk&,
                                         const Visitor& visitor) {
  visitor(Argument{
      name.ToString(),
      ArgumentValue::MakeInt32(
          internal::Int32ArgumentFields::Value::Get<int32_t>(header))});
  return true;
}

bool ArgumentReader::HandleInt64Argument(TraceContext& context,
                                         internal::ArgumentHeader header,
                                         const ftl::StringView& name,
                                         Chunk& chunk,
                                         const Visitor& visitor) {
  uint64_t value = 0;
  if (chunk.Read(&value)) {
    visitor(Argument{name.ToString(),
                     ArgumentValue::MakeInt64(static_cast<int64_t>(value))});
    return true;
  }
  return false;
}

bool ArgumentReader::HandleDoubleArgument(TraceContext& context,
                                          internal::ArgumentHeader header,
                                          const ftl::StringView& name,
                                          Chunk& chunk,
                                          const Visitor& visitor) {
  double value = 0;
  if (chunk.Read(reinterpret_cast<uint64_t*>(&value))) {
    visitor(Argument{name.ToString(),
                     ArgumentValue::MakeDouble(static_cast<double>(value))});
    return true;
  }
  return false;
}

bool ArgumentReader::HandleStringArgument(TraceContext& context,
                                          internal::ArgumentHeader header,
                                          const ftl::StringView& name,
                                          Chunk& chunk,
                                          const Visitor& visitor) {
  auto index = internal::StringArgumentFields::Index::Get<uint16_t>(header);
  auto value = context.DecodeStringRef(index, chunk);
  visitor(
      Argument{name.ToString(), ArgumentValue::MakeString(value.ToString())});
  return true;
}

bool ArgumentReader::HandlePointerArgument(TraceContext& context,
                                           internal::ArgumentHeader header,
                                           const ftl::StringView& name,
                                           Chunk& chunk,
                                           const Visitor& visitor) {
  uint64_t value = 0;
  if (chunk.Read(&value)) {
    visitor(Argument{name.ToString(), ArgumentValue::MakePointer(
                                          static_cast<uintptr_t>(value))});
    return true;
  }
  return false;
}

bool ArgumentReader::HandleKernelObjectIdArgument(
    TraceContext& context,
    internal::ArgumentHeader header,
    const ftl::StringView& name,
    Chunk& chunk,
    const Visitor& visitor) {
  uint64_t value = 0;
  if (chunk.Read(&value)) {
    visitor(
        Argument{name.ToString(), ArgumentValue::MakeKernelObjectId(value)});
    return true;
  }
  return false;
}

Record& Record::Destroy() {
  switch (type_) {
    case RecordType::kInitialization:
      break;
    case RecordType::kString:
      string_record_.~StringRecord();
      break;
    case RecordType::kThread:
      thread_record_.~ThreadRecord();
      break;
    case RecordType::kEvent:
      event_record_.~EventRecord();
      break;
    default:
      break;
  }

  return *this;
}

Record& Record::Copy(const Record& other) {
  type_ = other.type_;
  switch (type_) {
    case RecordType::kInitialization:
      initialization_record_ = other.initialization_record_;
      break;
    case RecordType::kString:
      new (&string_record_) StringRecord(other.string_record_);
      break;
    case RecordType::kThread:
      new (&thread_record_) ThreadRecord(other.thread_record_);
      break;
    case RecordType::kEvent:
      new (&event_record_) EventRecord(other.event_record_);
      break;
    default:
      break;
  }

  return *this;
}

TraceReader::TraceReader(const TraceErrorHandler& error_handler,
                         const RecordVisitor& visitor)
    : trace_context_(error_handler), visitor_(visitor) {}

void TraceReader::HandleInitializationRecord(internal::RecordHeader header,
                                             Chunk& chunk) {
  InitializationRecord record{0};
  if (chunk.Read(&record.ticks_per_second))
    visitor_(Record(record));
}

void TraceReader::HandleStringRecord(internal::RecordHeader record_header,
                                     Chunk& chunk) {
  auto id =
      internal::StringRecordFields::StringIndex::Get<uint16_t>(record_header);
  auto length =
      internal::StringRecordFields::StringLength::Get<uint16_t>(record_header);

  if (id == internal::StringRefFields::kInvalidIndex) {
    trace_context_.OnError("Cannot associate string with reserved id 0");
    return;
  }

  ftl::StringView string;
  if (chunk.ReadString(length, &string)) {
    trace_context_.RegisterStringRef(id, string);
    visitor_(Record(StringRecord{id, string.ToString()}));
  }
}

void TraceReader::HandleThreadRecord(internal::RecordHeader record_header,
                                     Chunk& chunk) {
  auto index =
      internal::ThreadRecordFields::ThreadIndex::Get<uint8_t>(record_header);

  if (index == internal::ThreadRefFields::kInline) {
    trace_context_.OnError("Cannot associate thread with reserved id 0");
    return;
  }

  Thread thread{0, 0};
  if (chunk.Read(&thread.process_koid) && chunk.Read(&thread.thread_koid)) {
    if (thread) {
      trace_context_.RegisterThreadRef(index, thread);
      visitor_(Record(ThreadRecord{index, thread}));
    }
  }
}

void TraceReader::HandleEventRecord(internal::RecordHeader record_header,
                                    Chunk& chunk) {
  auto event_type = internal::EventRecordFields::EventType::Get<TraceEventType>(
      record_header);
  auto argument_count =
      internal::EventRecordFields::ArgumentCount::Get<uint8_t>(record_header);
  auto thread_ref =
      internal::EventRecordFields::ThreadRef::Get<uint8_t>(record_header);
  auto category_ref =
      internal::EventRecordFields::CategoryStringRef::Get<uint16_t>(
          record_header);
  auto name_ref =
      internal::EventRecordFields::NameStringRef::Get<uint16_t>(record_header);

  uint64_t timestamp = 0;
  if (!chunk.Read(&timestamp)) {
    trace_context_.OnError("Failed to read timestamp");
    return;
  }

  auto thread = trace_context_.DecodeThreadRef(thread_ref, chunk);
  if (!thread) {
    trace_context_.OnError("Failed to resolve thread ref");
    return;
  }

  auto category = trace_context_.DecodeStringRef(category_ref, chunk);
  if (category.empty()) {
    trace_context_.OnError("Failed to resolve string ref for category");
    return;
  }

  auto name = trace_context_.DecodeStringRef(name_ref, chunk);
  if (name.empty()) {
    trace_context_.OnError("Failed to resolve string ref for name");
    return;
  }

  std::vector<Argument> arguments;
  ArgumentReader argument_reader;
  if (!argument_reader.ForEachArgument(
          trace_context_, argument_count, chunk,
          [&arguments](const Argument& arg) { arguments.push_back(arg); })) {
    trace_context_.OnError("Failed to visit arguments");
    return;
  }

  switch (event_type) {
    case TraceEventType::kAsyncStart: {
      uint64_t id = 0;
      if (chunk.Read(&id))
        visitor_(Record(EventRecord{event_type, timestamp, thread,
                                    name.ToString(), category.ToString(),
                                    arguments, EventData(AsyncBegin{id})}));
      break;
    }
    case TraceEventType::kAsyncInstant: {
      uint64_t id = 0;
      if (chunk.Read(&id))
        visitor_(Record(EventRecord{event_type, timestamp, thread,
                                    name.ToString(), category.ToString(),
                                    arguments, EventData(AsyncInstant{id})}));
      break;
    }
    case TraceEventType::kAsyncEnd: {
      uint64_t id = 0;
      if (chunk.Read(&id))
        visitor_(Record(EventRecord{event_type, timestamp, thread,
                                    name.ToString(), category.ToString(),
                                    arguments, EventData(AsyncEnd{id})}));
      break;
    }
    default:
      break;
  }
}

void TraceReader::ForEachRecord(TraceInput& input) {
  Chunk chunk;
  internal::RecordHeader record_header = 0;
  while (true) {
    if (!input.ReadChunk(1, &chunk))
      return;

    if (!chunk.Read(&record_header))
      return;

    auto record_type =
        internal::RecordFields::Type::Get<RecordType>(record_header);
    auto record_size =
        internal::RecordFields::RecordSize::Get<uint16_t>(record_header);

    if (record_size == 0)
      return;

    if (!input.ReadChunk(record_size - 1, &chunk))
      return;

    switch (record_type) {
      case RecordType::kMetadata:
        break;
      case RecordType::kInitialization:
        HandleInitializationRecord(record_header, chunk);
        break;
      case RecordType::kString:
        HandleStringRecord(record_header, chunk);
        break;
      case RecordType::kThread:
        HandleThreadRecord(record_header, chunk);
        break;
      case RecordType::kEvent:
        HandleEventRecord(record_header, chunk);
        break;
    }
  }
}

}  // namespace reader
}  // namespace tracing
