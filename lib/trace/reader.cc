// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/reader.h"

#include "apps/tracing/lib/trace/internal/fields.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

using namespace ::tracing::internal;

namespace tracing {
namespace reader {

Chunk::Chunk() : current_(nullptr), end_(nullptr) {}

Chunk::Chunk(const uint64_t* begin, size_t num_words)
    : current_(begin), end_(current_ + num_words) {}

bool Chunk::Read(uint64_t* out_value) {
  if (current_ < end_) {
    *out_value = *current_++;
    return true;
  }
  return false;
}

bool Chunk::ReadInt64(int64_t* out_value) {
  if (current_ < end_) {
    *out_value = *reinterpret_cast<const int64_t*>(current_++);
    return true;
  }
  return false;
}

bool Chunk::ReadDouble(double* out_value) {
  if (current_ < end_) {
    *out_value = *reinterpret_cast<const double*>(current_++);
    return true;
  }
  return false;
}

bool Chunk::ReadChunk(size_t num_words, Chunk* out_chunk) {
  if (current_ + num_words > end_)
    return false;

  *out_chunk = Chunk(current_, num_words);
  current_ += num_words;
  return true;
}

bool Chunk::ReadString(size_t length, ftl::StringView* out_string) {
  auto num_words = BytesToWords(Pad(length));
  if (current_ + num_words > end_)
    return false;

  *out_string =
      ftl::StringView(reinterpret_cast<const char*>(current_), length);
  current_ += num_words;
  return true;
}

TraceContext::TraceContext(ErrorHandler error_handler)
    : error_handler_(error_handler) {
  RegisterProvider(0u, "default");
}

TraceContext::~TraceContext() {}

void TraceContext::ReportError(std::string error) const {
  error_handler_(std::move(error));
}

std::string TraceContext::GetProviderName(ProviderId id) const {
  auto it = providers_.find(id);
  if (it != providers_.end())
    return it->second->name;
  return std::string();
}

bool TraceContext::DecodeStringRef(Chunk& chunk,
                                   EncodedStringRef string_ref,
                                   std::string* out_string) const {
  if (string_ref == StringRefFields::kEmpty) {
    out_string->clear();
    return true;
  }

  if (string_ref & StringRefFields::kInlineFlag) {
    size_t length = string_ref & StringRefFields::kLengthMask;
    ftl::StringView string_view;
    if (!chunk.ReadString(length, &string_view)) {
      ReportError("Could not read inline string");
      return false;
    }
    *out_string = string_view.ToString();
    return true;
  }

  auto it = current_provider_->string_table.find(string_ref);
  if (it == current_provider_->string_table.end()) {
    ReportError("String ref not in table");
    return false;
  }
  *out_string = it->second;
  return true;
}

bool TraceContext::DecodeThreadRef(Chunk& chunk,
                                   EncodedThreadRef thread_ref,
                                   ProcessThread* out_process_thread) const {
  if (thread_ref == ThreadRefFields::kInline) {
    ProcessThread process_thread;
    if (!chunk.Read(&process_thread.process_koid) ||
        !chunk.Read(&process_thread.thread_koid)) {
      ReportError("Could not read inline process and thread");
      return false;
    }
    *out_process_thread = process_thread;
    return true;
  }

  auto it = current_provider_->thread_table.find(thread_ref);
  if (it == current_provider_->thread_table.end()) {
    ReportError("Thread ref not in table");
    return false;
  }
  *out_process_thread = it->second;
  return true;
}

void TraceContext::RegisterProvider(ProviderId id, std::string name) {
  auto provider = std::make_unique<ProviderInfo>();
  provider->id = id;
  provider->name = name;
  current_provider_ = provider.get();
  providers_.emplace(id, std::move(provider));
}

void TraceContext::RegisterString(StringIndex index, std::string string) {
  FTL_DCHECK(index != StringRefFields::kInvalidIndex &&
             index <= StringRefFields::kMaxIndex);
  current_provider_->string_table[index] = std::move(string);
}

void TraceContext::RegisterThread(ThreadIndex index,
                                  const ProcessThread& process_thread) {
  FTL_DCHECK(index != ThreadRefFields::kInline &&
             index <= ThreadRefFields::kMaxIndex);
  current_provider_->thread_table[index] = process_thread;
}

void TraceContext::SetCurrentProvider(ProviderId id) {
  auto it = providers_.find(id);
  if (it != providers_.end()) {
    current_provider_ = it->second.get();
    return;
  }
  RegisterProvider(id, "");
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
    case ArgumentType::kKoid:
      uint64_ = other.uint64_;
      break;
    default:
      break;
  }

  return *this;
}

MetadataData& MetadataData::Destroy() {
  switch (type_) {
    case MetadataType::kProviderInfo:
      provider_info_.~ProviderInfo();
      break;
    case MetadataType::kProviderSection:
      provider_section_.~ProviderSection();
      break;
    default:
      break;
  }

  return *this;
}

MetadataData& MetadataData::Copy(const MetadataData& other) {
  type_ = other.type_;
  switch (type_) {
    case MetadataType::kProviderInfo:
      new (&provider_info_) ProviderInfo(other.provider_info_);
      break;
    case MetadataType::kProviderSection:
      new (&provider_section_) ProviderSection(other.provider_section_);
      break;
    default:
      break;
  }

  return *this;
}

Record& Record::Destroy() {
  switch (type_) {
    case RecordType::kMetadata:
      metadata_.~Metadata();
      break;
    case RecordType::kInitialization:
      initialization_.~Initialization();
      break;
    case RecordType::kString:
      string_.~String();
      break;
    case RecordType::kThread:
      thread_.~Thread();
      break;
    case RecordType::kEvent:
      event_.~Event();
      break;
    default:
      break;
  }

  return *this;
}

Record& Record::Copy(const Record& other) {
  type_ = other.type_;
  switch (type_) {
    case RecordType::kMetadata:
      new (&metadata_) Metadata(other.metadata_);
      break;
    case RecordType::kInitialization:
      new (&initialization_) Initialization(other.initialization_);
      break;
    case RecordType::kString:
      new (&string_) String(other.string_);
      break;
    case RecordType::kThread:
      new (&thread_) Thread(other.thread_);
      break;
    case RecordType::kEvent:
      new (&event_) Event(other.event_);
      break;
    default:
      break;
  }

  return *this;
}

TraceReader::TraceReader(RecordConsumer record_consumer,
                         ErrorHandler error_handler)
    : record_consumer_(std::move(record_consumer)),
      context_(std::move(error_handler)) {}

bool TraceReader::ReadRecords(Chunk& chunk) {
  while (true) {
    if (!pending_header_ && !chunk.Read(&pending_header_))
      return true;  // need more data

    auto size = RecordFields::RecordSize::Get<size_t>(pending_header_);
    if (size == 0) {
      context_.ReportError("Unexpected record of size 0");
      return false;  // fatal error
    }
    FTL_DCHECK(size <= RecordFields::kMaxRecordSizeWords);

    Chunk record;
    if (!chunk.ReadChunk(size - 1, &record))
      return true;  // need more data to decode record

    auto type = RecordFields::Type::Get<RecordType>(pending_header_);
    switch (type) {
      case RecordType::kMetadata: {
        if (!ReadMetadataRecord(record, pending_header_)) {
          context_.ReportError("Failed to read metadata record");
        }
        break;
      }
      case RecordType::kInitialization: {
        if (!ReadInitializationRecord(record, pending_header_)) {
          context_.ReportError("Failed to read initialization record");
        }
        break;
      }
      case RecordType::kString: {
        if (!ReadStringRecord(record, pending_header_)) {
          context_.ReportError("Failed to read string record");
        }
        break;
      }
      case RecordType::kThread: {
        if (!ReadThreadRecord(record, pending_header_)) {
          context_.ReportError("Failed to read thread record");
        }
        break;
      }
      case RecordType::kEvent: {
        if (!ReadEventRecord(record, pending_header_)) {
          context_.ReportError("Failed to read event record");
        }
        break;
      }
      default: {
        // Ignore unknown record types for forward compatibility.
        context_.ReportError(ftl::StringPrintf(
            "Skipping record of unknown type %d", static_cast<uint32_t>(type)));
        break;
      }
    }
    pending_header_ = 0u;
  }
}

bool TraceReader::ReadMetadataRecord(Chunk& record, RecordHeader header) {
  auto type = MetadataRecordFields::MetadataType::Get<MetadataType>(header);

  switch (type) {
    case MetadataType::kProviderInfo: {
      auto id = ProviderInfoMetadataRecordFields::Id::Get<ProviderId>(header);
      auto name_length =
          ProviderInfoMetadataRecordFields::NameLength::Get<size_t>(header);
      ftl::StringView name_view;
      if (!record.ReadString(name_length, &name_view))
        return false;
      std::string name = name_view.ToString();

      context_.RegisterProvider(id, name);
      record_consumer_(Record(Record::Metadata{
          MetadataData(MetadataData::ProviderInfo{id, name})}));
      break;
    }
    case MetadataType::kProviderSection: {
      auto id =
          ProviderSectionMetadataRecordFields::Id::Get<ProviderId>(header);

      context_.SetCurrentProvider(id);
      record_consumer_(Record(
          Record::Metadata{MetadataData(MetadataData::ProviderSection{id})}));
      break;
    }
    default: {
      // Ignore unknown metadata types for forward compatibility.
      context_.ReportError(ftl::StringPrintf(
          "Skipping metadata of unknown type %d", static_cast<uint32_t>(type)));
      break;
    }
  }
  return true;
}

bool TraceReader::ReadInitializationRecord(Chunk& record, RecordHeader header) {
  uint64_t ticks_per_second;
  if (!record.Read(&ticks_per_second))
    return false;

  record_consumer_(Record(Record::Initialization{ticks_per_second}));
  return true;
}

bool TraceReader::ReadStringRecord(Chunk& record, RecordHeader header) {
  auto index = StringRecordFields::StringIndex::Get<StringIndex>(header);
  if (index == StringRefFields::kInvalidIndex) {
    context_.ReportError("Cannot associate string with reserved id 0");
    return false;
  }

  auto length = StringRecordFields::StringLength::Get<size_t>(header);
  ftl::StringView string_view;
  if (!record.ReadString(length, &string_view))
    return false;
  std::string string = string_view.ToString();

  context_.RegisterString(index, string);
  record_consumer_(Record(Record::String{index, std::move(string)}));
  return true;
}

bool TraceReader::ReadThreadRecord(Chunk& record, RecordHeader header) {
  auto index = ThreadRecordFields::ThreadIndex::Get<ThreadIndex>(header);
  if (index == ThreadRefFields::kInline) {
    context_.ReportError("Cannot associate thread with reserved id 0");
    return false;
  }

  ProcessThread process_thread;
  if (!record.Read(&process_thread.process_koid) ||
      !record.Read(&process_thread.thread_koid))
    return false;

  context_.RegisterThread(index, process_thread);
  record_consumer_(Record(Record::Thread{index, process_thread}));
  return true;
}

bool TraceReader::ReadEventRecord(Chunk& record, RecordHeader header) {
  auto type = EventRecordFields::EventType::Get<EventType>(header);
  auto argument_count = EventRecordFields::ArgumentCount::Get<size_t>(header);
  auto thread_ref = EventRecordFields::ThreadRef::Get<EncodedThreadRef>(header);
  auto category_ref =
      EventRecordFields::CategoryStringRef::Get<EncodedStringRef>(header);
  auto name_ref =
      EventRecordFields::NameStringRef::Get<EncodedStringRef>(header);

  uint64_t timestamp;
  ProcessThread process_thread;
  std::string category;
  std::string name;
  std::vector<Argument> arguments;
  if (!record.Read(&timestamp) ||
      !context_.DecodeThreadRef(record, thread_ref, &process_thread) ||
      !context_.DecodeStringRef(record, category_ref, &category) ||
      !context_.DecodeStringRef(record, name_ref, &name) ||
      !ReadArguments(record, argument_count, &arguments))
    return false;

  switch (type) {
    case EventType::kDurationBegin: {
      record_consumer_(Record(Record::Event{
          timestamp, process_thread, std::move(category), std::move(name),
          std::move(arguments), EventData(EventData::DurationBegin{})}));
      break;
    }
    case EventType::kDurationEnd: {
      record_consumer_(Record(Record::Event{
          timestamp, process_thread, std::move(category), std::move(name),
          std::move(arguments), EventData(EventData::DurationEnd{})}));
      break;
    }
    case EventType::kAsyncStart: {
      uint64_t id;
      if (!record.Read(&id))
        return false;
      record_consumer_(Record(Record::Event{
          timestamp, process_thread, std::move(category), std::move(name),
          std::move(arguments), EventData(EventData::AsyncBegin{id})}));
      break;
    }
    case EventType::kAsyncInstant: {
      uint64_t id;
      if (!record.Read(&id))
        return false;
      record_consumer_(Record(Record::Event{
          timestamp, process_thread, std::move(category), std::move(name),
          std::move(arguments), EventData(EventData::AsyncInstant{id})}));
      break;
    }
    case EventType::kAsyncEnd: {
      uint64_t id;
      if (!record.Read(&id))
        return false;
      record_consumer_(Record(Record::Event{
          timestamp, process_thread, std::move(category), std::move(name),
          std::move(arguments), EventData(EventData::AsyncBegin{id})}));
      break;
    }
    default: {
      // Ignore unknown event types for forward compatibility.
      context_.ReportError(ftl::StringPrintf(
          "Skipping event of unknown type %d", static_cast<uint32_t>(type)));
      break;
    }
  }
  return true;
}

bool TraceReader::ReadArguments(Chunk& record,
                                size_t count,
                                std::vector<Argument>* out_arguments) {
  while (count-- > 0) {
    ArgumentHeader header;
    if (!record.Read(&header)) {
      context_.ReportError("Failed to read argument header");
      return false;
    }

    auto size = ArgumentFields::ArgumentSize::Get<size_t>(header);
    Chunk arg;
    if (!size || !record.ReadChunk(size - 1, &arg)) {
      context_.ReportError("Invalid argument size");
      return false;
    }

    auto name_ref = ArgumentFields::NameRef::Get<EncodedStringRef>(header);
    std::string name;
    if (!context_.DecodeStringRef(arg, name_ref, &name)) {
      context_.ReportError("Failed to read argument name");
      return false;
    }

    auto type = ArgumentFields::Type::Get<ArgumentType>(header);
    switch (type) {
      case ArgumentType::kNull: {
        out_arguments->emplace_back(std::move(name), ArgumentValue::MakeNull());
        break;
      }
      case ArgumentType::kInt32: {
        auto value = Int32ArgumentFields::Value::Get<int32_t>(header);
        out_arguments->emplace_back(std::move(name),
                                    ArgumentValue::MakeInt32(value));
        break;
      }
      case ArgumentType::kInt64: {
        int64_t value;
        if (!arg.ReadInt64(&value)) {
          context_.ReportError("Failed to read int64 argument value");
          return false;
        }
        out_arguments->emplace_back(std::move(name),
                                    ArgumentValue::MakeInt64(value));
        break;
      }
      case ArgumentType::kDouble: {
        double value;
        if (!arg.ReadDouble(&value)) {
          context_.ReportError("Failed to read double argument value");
          return false;
        }
        out_arguments->emplace_back(std::move(name),
                                    ArgumentValue::MakeDouble(value));
        break;
      }
      case ArgumentType::kString: {
        auto string_ref =
            StringArgumentFields::Index::Get<EncodedStringRef>(header);
        std::string value;
        if (!context_.DecodeStringRef(arg, string_ref, &value)) {
          context_.ReportError("Failed to read string argument value");
          return false;
        }
        out_arguments->emplace_back(
            std::move(name), ArgumentValue::MakeString(std::move(value)));
        break;
      }
      case ArgumentType::kPointer: {
        uintptr_t value;
        if (!arg.Read(&value)) {
          context_.ReportError("Failed to read pointer argument value");
          return false;
        }
        out_arguments->emplace_back(std::move(name),
                                    ArgumentValue::MakePointer(value));
        break;
      }
      case ArgumentType::kKoid: {
        mx_koid_t value;
        if (!arg.Read(&value)) {
          context_.ReportError("Failed to read koid argument value");
          return false;
        }
        out_arguments->emplace_back(std::move(name),
                                    ArgumentValue::MakeKoid(value));
        break;
      }
      default: {
        // Ignore unknown argument types for forward compatibility.
        context_.ReportError(ftl::StringPrintf(
            "Skipping argument of unknown type %d, argument name %s",
            static_cast<uint32_t>(type), name.c_str()));
        break;
      }
    }
  }
  return true;
}

}  // namespace reader
}  // namespace tracing
