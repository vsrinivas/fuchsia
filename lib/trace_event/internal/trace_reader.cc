// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>

#include "apps/tracing/lib/trace_event/internal/trace_reader.h"

namespace tracing {
namespace internal {
namespace {

inline size_t Pad(size_t size) {
  return size + ((8 - (size & 7)) & 7);
}

}  // namespace

InputReader::InputReader() = default;
InputReader::~InputReader() = default;

MemoryInputReader::MemoryInputReader(void* memory, size_t size)
    : current_(reinterpret_cast<uintptr_t>(memory)), end_(current_ + size) {}

bool MemoryInputReader::Read(void* destination, size_t size) {
  if (current_ + size >= end_)
    return false;

  memcpy(destination, reinterpret_cast<void*>(current_), size);
  current_ += size;

  return true;
}

ChunkInputReader::ChunkInputReader(Chunk::const_iterator begin,
                                   Chunk::const_iterator end)
    : current_(begin), end_(end) {}

ChunkInputReader::operator bool() const {
  return current_ < end_;
}

uint64_t ChunkInputReader::Get() {
  auto result = *current_++;
  return result;
}

bool ChunkInputReader::Read(size_t words, Chunk& chunk) {
  if (current_ + words > end_)
    return false;

  chunk = Chunk(current_, current_ + words);
  current_ += words;
  return true;
}

bool ChunkInputReader::Read(size_t words, uint64_t* to) {
  if (current_ + words > end_)
    return false;

  std::copy(current_, current_ + words, to);
  current_ += words;
  return true;
}

std::string StringTable::DecodeString(uint16_t index,
                                      ChunkInputReader& reader) const {
  if (index == 0)
    return std::string();

  if (index < 0x8000) {
    auto it = table_.find(index);
    return it != table_.end() ? it->second : std::string();
  }

  auto length = index & ~0x8000;
  Chunk inline_string(length);

  if (!reader.Read(Pad(length) >> 3, inline_string))
    return std::string();

  return std::string(reinterpret_cast<const char*>(&inline_string.front()),
                     length);
}

void StringTable::Register(uint16_t index, const std::string& string) {
  table_[index] = string;
}

Thread ThreadTable::DecodeThread(uint16_t index,
                                 ChunkInputReader& reader) const {
  Thread result;
  if (index == 0) {
    result.process_koid = reader.Get();
    result.thread_koid = reader.Get();
  } else {
    auto it = table_.find(index);
    if (it != table_.end())
      result = it->second;
  }

  return result;
}

void ThreadTable::Register(uint16_t index, const Thread& thread) {
  table_[index] = thread;
}

ArgumentVisitor::ArgumentVisitor() = default;
ArgumentVisitor::~ArgumentVisitor() = default;

void ArgumentPrinter::operator()(const Argument<void>& arg) {
  printf(
      "NullArgument:\n"
      "  name: %s\n",
      arg.name.c_str());
}

void ArgumentPrinter::operator()(const Argument<int32_t>& arg) {
  printf(
      "Int32Argument:\n"
      "  name: %s\n"
      "  value: %d\n",
      arg.name.c_str(), arg.value);
}

void ArgumentPrinter::operator()(const Argument<int64_t>& arg) {
  printf(
      "Int64Argument:\n"
      "  name: %s\n"
      "  value: %" PRId64 "\n",
      arg.name.c_str(), arg.value);
}

void ArgumentPrinter::operator()(const Argument<double>& arg) {
  printf(
      "DoubleArgument:\n"
      "  name: %s\n"
      "  value: %f\n",
      arg.name.c_str(), arg.value);
}

void ArgumentPrinter::operator()(const Argument<std::string>& arg) {
  printf(
      "StringArgument:\n"
      "  name: %s\n"
      "  value: %s\n",
      arg.name.c_str(), arg.value.c_str());
}

void ArgumentPrinter::operator()(const Argument<const void*>& arg) {
  printf(
      "PointerArgument:\n"
      "  name: %s\n"
      "  value: %p\n",
      arg.name.c_str(), arg.value);
}

void ArgumentPrinter::operator()(const Argument<uint64_t>& arg) {
  printf(
      "KoidArgument:\n"
      "  name: %s\n"
      "  value: %" PRIu64 "\n",
      arg.name.c_str(), arg.value);
}

ArgumentReader::ArgumentReader(const ChunkInputReader& reader,
                               const StringTable& table)
    : reader_(reader), string_table_(table) {}

void ArgumentReader::ForEachArgument(ArgumentVisitor& visitor) {
  Chunk payload;

  while (reader_) {
    ArgumentHeader header = reader_.Get();

    auto type = ArgumentFields::Type::Get<ArgumentType>(header);
    auto size = ArgumentFields::ArgumentSize::Get<uint16_t>(header);
    auto name_ref = ArgumentFields::NameRef::Get<uint16_t>(header);
    if (size == 0 || !reader_.Read(size - 1, payload))
      return;

    ChunkInputReader payload_reader(payload.begin(), payload.end());

    switch (type) {
      case ArgumentType::kInt32:
        HandleInt32Argument(
            header, string_table_.DecodeString(name_ref, payload_reader),
            payload_reader, visitor);
        break;
      case ArgumentType::kInt64:
        HandleInt64Argument(
            header, string_table_.DecodeString(name_ref, payload_reader),
            payload_reader, visitor);
        break;
      case ArgumentType::kDouble:
        HandleDoubleArgument(
            header, string_table_.DecodeString(name_ref, payload_reader),
            payload_reader, visitor);
        break;
      case ArgumentType::kString:
        HandleStringArgument(
            header, string_table_.DecodeString(name_ref, payload_reader),
            payload_reader, visitor);
        break;
      case ArgumentType::kPointer:
        HandlePointerArgument(
            header, string_table_.DecodeString(name_ref, payload_reader),
            payload_reader, visitor);
        break;
      case ArgumentType::kKernelObjectId:
        HandleKernelObjectIdArgument(
            header, string_table_.DecodeString(name_ref, payload_reader),
            payload_reader, visitor);
        break;
      default:
        FTL_LOG(WARNING) << "Encountered an unknown argument type: "
                         << static_cast<uint32_t>(type);
        break;
    }
  }
}

void ArgumentReader::HandleNullArgument(ArgumentHeader header,
                                        const std::string& name,
                                        ChunkInputReader&,
                                        ArgumentVisitor& visitor) {
  visitor(Argument<void>{name});
}

void ArgumentReader::HandleInt32Argument(ArgumentHeader header,
                                         const std::string& name,
                                         ChunkInputReader&,
                                         ArgumentVisitor& visitor) {
  visitor(Argument<int32_t>{name,
                            Int32ArgumentFields::Value::Get<int32_t>(header)});
}

void ArgumentReader::HandleInt64Argument(ArgumentHeader header,
                                         const std::string& name,
                                         ChunkInputReader& reader,
                                         ArgumentVisitor& visitor) {
  visitor(Argument<int64_t>{name, static_cast<int64_t>(reader.Get())});
}

void ArgumentReader::HandleDoubleArgument(ArgumentHeader header,
                                          const std::string& name,
                                          ChunkInputReader& reader,
                                          ArgumentVisitor& visitor) {
  double value = 0.;
  if (!reader.Read(1, reinterpret_cast<uint64_t*>(&value)))
    return;

  visitor(Argument<double>{name, value});
}

void ArgumentReader::HandleStringArgument(ArgumentHeader header,
                                          const std::string& name,
                                          ChunkInputReader& reader,
                                          ArgumentVisitor& visitor) {
  auto index = StringArgumentFields::Index::Get<uint16_t>(header);
  auto value = string_table_.DecodeString(index, reader);
  visitor(Argument<std::string>{name, value});
}

void ArgumentReader::HandlePointerArgument(ArgumentHeader header,
                                           const std::string& name,
                                           ChunkInputReader& reader,
                                           ArgumentVisitor& visitor) {
  visitor(
      Argument<const void*>{name, reinterpret_cast<const void*>(reader.Get())});
}

void ArgumentReader::HandleKernelObjectIdArgument(ArgumentHeader header,
                                                  const std::string& name,
                                                  ChunkInputReader& reader,
                                                  ArgumentVisitor& visitor) {
  visitor(Argument<uint64_t>{name, reader.Get()});
}

EventVisitor::EventVisitor() = default;
EventVisitor::~EventVisitor() = default;

void EventPrinter::operator()(const DurationBegin&) {
  printf("DurationBegin\n");
}

void EventPrinter::operator()(const DurationEnd&) {
  printf("DurationEnd\n");
}

void EventPrinter::operator()(const AsyncBegin& event) {
  printf(
      "AsyncBegin:\n"
      "  id: %" PRIu64 "\n",
      event.id);
}

void EventPrinter::operator()(const AsyncInstant& event) {
  printf(
      "AsyncInstant:\n"
      "  id: %" PRIu64 "\n",
      event.id);
}

void EventPrinter::operator()(const AsyncEnd& event) {
  printf(
      "AsyncEnd:\n"
      "  id: %" PRIu64 "\n",
      event.id);
}

void EventRecord::Visit(EventVisitor& visitor) const {
  switch (event_type) {
    case TraceEventType::kDurationBegin:
      visitor(DurationBegin{});
      break;
    case TraceEventType::kDurationEnd:
      visitor(DurationEnd{});
      break;
    case TraceEventType::kAsyncStart:
      visitor(AsyncBegin{payload[0]});
      break;
    case TraceEventType::kAsyncInstant:
      visitor(AsyncInstant{payload[0]});
      break;
    case TraceEventType::kAsyncEnd:
      visitor(AsyncEnd{payload[0]});
      break;
    default:
      FTL_LOG(WARNING) << "Found unknown event type: "
                       << static_cast<uint32_t>(event_type);
      break;
  }
}

void EventRecord::Visit(const StringTable& string_table,
                        ArgumentVisitor& visitor) const {
  Chunk::const_iterator it = payload.begin();
  Chunk::const_iterator end = payload.end();

  switch (event_type) {
    case TraceEventType::kAsyncStart:
    case TraceEventType::kAsyncInstant:
    case TraceEventType::kAsyncEnd:
      it++;
      break;
    default:
      break;
  }

  ArgumentReader reader(ChunkInputReader(it, end), string_table);
  reader.ForEachArgument(visitor);
}

RecordVisitor::RecordVisitor() = default;
RecordVisitor::~RecordVisitor() = default;

void RecordPrinter::operator()(const InitializationRecord& record) {
  printf(
      "Initialization record:\n"
      "  ticks/second: %" PRId64 "\n",
      record.ticks_per_second);
}

void RecordPrinter::operator()(const StringRecord& record) {
  string_table_.Register(record.index, record.string);
  printf(
      "String record:\n"
      "  %" PRIu16 " -> %s\n",
      record.index, record.string.c_str());
}

void RecordPrinter::operator()(const ThreadRecord& record) {
  printf(
      "Thread record:\n"
      "  %" PRIu16 " -> (%" PRIu64 ", %" PRIu64 ")\n",
      record.index, record.thread.process_koid, record.thread.thread_koid);
}

void RecordPrinter::operator()(const EventRecord& record) {
  printf(
      "Event record:\n"
      "  type:   %u\n"
      "  ts:     %" PRIu64
      "\n"
      "  thread: (%" PRIu64 ", %" PRIu64
      ")\n"
      "  name:   %s\n"
      "  cat:    %s\n"
      "  #args:  %" PRIu64 "\n",
      static_cast<unsigned int>(record.event_type), record.timestamp,
      record.thread.process_koid, record.thread.thread_koid,
      record.name.c_str(), record.cat.c_str(), record.argument_count);

  EventPrinter event_printer;
  record.Visit(event_printer);
  ArgumentPrinter argument_printer;
  record.Visit(string_table_, argument_printer);
}

void TraceReader::HandleInitializationRecord(RecordHeader header,
                                             const Chunk& payload,
                                             RecordVisitor& visitor) {
  visitor(InitializationRecord{payload.front()});
}

void TraceReader::HandleStringRecord(RecordHeader record_header,
                                     const Chunk& payload,
                                     RecordVisitor& visitor) {
  auto id = StringRecordFields::StringIndex::Get<uint16_t>(record_header);
  auto length = StringRecordFields::StringLength::Get<uint16_t>(record_header);

  if (id == 0) {
    FTL_LOG(WARNING) << "Cannot associate string with reserved id 0";
    return;
  }

  if (payload.size() < Pad(length) >> 3) {
    FTL_LOG(WARNING) << "Chunk has wrong size, aborting interpretation";
    return;
  }

  auto string =
      std::string(reinterpret_cast<const char*>(&payload.front()), length);
  string_table_.Register(id, string);
  visitor(StringRecord{id, string});
}

void TraceReader::HandleThreadRecord(RecordHeader record_header,
                                     const Chunk& payload,
                                     RecordVisitor& visitor) {
  if (payload.size() != 2) {
    FTL_LOG(WARNING) << "Chunk has wrong size, aborting interpretation";
    return;
  }

  auto index = ThreadRecordFields::ThreadIndex::Get<uint8_t>(record_header);

  if (index == 0) {
    FTL_LOG(WARNING) << "Cannot associate string with reserved id 0";
    return;
  }

  Thread thread{payload[0], payload[1]};
  thread_table_.Register(index, thread);
  visitor(ThreadRecord{index, thread});
}

void TraceReader::HandleEventRecord(RecordHeader record_header,
                                    const Chunk& payload,
                                    RecordVisitor& visitor) {
  if (payload.size() == 0) {
    FTL_LOG(WARNING) << "Chunk has wrong size, aborting interpretation";
    return;
  }

  auto event_type =
      EventRecordFields::EventType::Get<TraceEventType>(record_header);
  auto argument_count =
      EventRecordFields::ArgumentCount::Get<uint8_t>(record_header);
  auto thread_ref = EventRecordFields::ThreadRef::Get<uint8_t>(record_header);
  auto category_ref =
      EventRecordFields::CategoryStringRef::Get<uint16_t>(record_header);
  auto name_ref =
      EventRecordFields::NameStringRef::Get<uint16_t>(record_header);

  ChunkInputReader reader(payload.begin(), payload.end());

  auto timestamp = reader.Get();

  auto thread = thread_table_.DecodeThread(thread_ref, reader);
  if (!thread) {
    FTL_LOG(WARNING) << "Failed to resolve thread ref";
    return;
  }

  auto category = string_table_.DecodeString(category_ref, reader);
  if (category.empty()) {
    FTL_LOG(WARNING) << "Failed to resolve string ref for category";
    return;
  }

  auto name = string_table_.DecodeString(name_ref, reader);
  if (name.empty()) {
    FTL_LOG(WARNING) << "Failed to resolve string ref for name";
    return;
  }

  visitor(EventRecord{event_type, timestamp, thread, name, category,
                      argument_count, Chunk(reader.current(), payload.end())});
}

void TraceReader::VisitEachRecord(InputReader& reader, RecordVisitor& visitor) {
  RecordHeader record_header = 0;
  Chunk payload;

  while (true) {
    if (!reader.Read(&record_header, sizeof(record_header)))
      return;

    auto record_type = RecordFields::Type::Get<RecordType>(record_header);
    auto record_size = RecordFields::RecordSize::Get<uint16_t>(record_header);

    if (record_size == 0)
      return;

    payload.resize(record_size - 1);
    if (!reader.Read(payload.data(), sizeof(uint64_t) * payload.size()))
      return;

    switch (record_type) {
      case RecordType::kMetadata:
        break;
      case RecordType::kInitialization:
        HandleInitializationRecord(record_header, payload, visitor);
        break;
      case RecordType::kString:
        HandleStringRecord(record_header, payload, visitor);
        break;
      case RecordType::kThread:
        HandleThreadRecord(record_header, payload, visitor);
        break;
      case RecordType::kEvent:
        HandleEventRecord(record_header, payload, visitor);
        break;
    }
  }
}

}  // namespace internal
}  // namespace tracing
