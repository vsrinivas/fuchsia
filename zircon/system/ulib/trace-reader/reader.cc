// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/trace-engine/fields.h>

#include <string_view>
#include <utility>

#include <fbl/string_printf.h>
#include <trace-reader/reader.h>

namespace trace {

TraceReader::TraceReader(RecordConsumer record_consumer, ErrorHandler error_handler)
    : record_consumer_(std::move(record_consumer)), error_handler_(std::move(error_handler)) {
  // Provider ids begin at 1. We don't have a provider yet but we want to
  // set the current provider. So set it to non-existent provider 0.
  RegisterProvider(0u, "");
}

bool TraceReader::ReadRecords(Chunk& chunk) {
  while (true) {
    if (pending_header_ == 0) {
      std::optional next = chunk.ReadUint64();
      if (!next.has_value()) {
        return true;  // need more data
      }
      pending_header_ = next.value();
    }

    auto type = RecordFields::Type::Get<RecordType>(pending_header_);

    size_t size;
    if (type != RecordType::kLargeRecord) {
      size = RecordFields::RecordSize::Get<size_t>(pending_header_);
      ZX_DEBUG_ASSERT(size <= RecordFields::kMaxRecordSizeWords);
      static_assert(RecordFields::kMaxRecordSizeBytes <=
                    TRACE_ENCODED_INLINE_LARGE_RECORD_MAX_SIZE);
    } else {
      size = LargeBlobFields::RecordSize::Get<size_t>(pending_header_);
      ZX_DEBUG_ASSERT(size <= BytesToWords(TRACE_ENCODED_INLINE_LARGE_RECORD_MAX_SIZE));
    }
    if (size == 0) {
      ReportError("Unexpected record of size 0");
      return false;  // fatal error
    }

    // TODO(fxbug.dev/23072): Here we assume that the entire blob payload can
    // fit into the read buffer.
    std::optional record_opt = chunk.ReadChunk(size - 1);
    if (!record_opt.has_value()) {
      return true;  // need more data to decode record
    }
    Chunk& record = record_opt.value();

    switch (type) {
      case RecordType::kMetadata: {
        if (!ReadMetadataRecord(record, pending_header_)) {
          ReportError("Failed to read metadata record");
        }
        break;
      }
      case RecordType::kInitialization: {
        if (!ReadInitializationRecord(record, pending_header_)) {
          ReportError("Failed to read initialization record");
        }
        break;
      }
      case RecordType::kString: {
        if (!ReadStringRecord(record, pending_header_)) {
          ReportError("Failed to read string record");
        }
        break;
      }
      case RecordType::kThread: {
        if (!ReadThreadRecord(record, pending_header_)) {
          ReportError("Failed to read thread record");
        }
        break;
      }
      case RecordType::kEvent: {
        if (!ReadEventRecord(record, pending_header_)) {
          ReportError("Failed to read event record");
        }
        break;
      }
      case RecordType::kBlob: {
        void* ptr;
        if (!ReadBlobRecord(record, pending_header_, &ptr)) {
          ReportError("Failed to read blob record");
        }
        break;
      }
      case RecordType::kKernelObject: {
        if (!ReadKernelObjectRecord(record, pending_header_)) {
          ReportError("Failed to read kernel object record");
        }
        break;
      }
      case RecordType::kContextSwitch: {
        if (!ReadContextSwitchRecord(record, pending_header_)) {
          ReportError("Failed to read context switch record");
        }
        break;
      }
      case RecordType::kLog: {
        if (!ReadLogRecord(record, pending_header_)) {
          ReportError("Failed to read log record");
        }
        break;
      }
      case RecordType::kLargeRecord: {
        if (!ReadLargeRecord(record, pending_header_)) {
          ReportError("Failed to read large record");
        }
        break;
      }
      default: {
        // Ignore unknown record types for forward compatibility.
        ReportError(
            fbl::StringPrintf("Skipping record of unknown type %d", static_cast<uint32_t>(type)));
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
      auto name_length = ProviderInfoMetadataRecordFields::NameLength::Get<size_t>(header);
      std::optional name_view = record.ReadString(name_length);
      if (!name_view.has_value()) {
        return false;
      }
      fbl::String name(name_view.value());

      RegisterProvider(id, name);
      record_consumer_(
          Record(Record::Metadata{MetadataContent(MetadataContent::ProviderInfo{id, name})}));
      break;
    }
    case MetadataType::kProviderSection: {
      auto id = ProviderSectionMetadataRecordFields::Id::Get<ProviderId>(header);

      SetCurrentProvider(id);
      record_consumer_(
          Record(Record::Metadata{MetadataContent(MetadataContent::ProviderSection{id})}));
      break;
    }
    case MetadataType::kProviderEvent: {
      auto id = ProviderEventMetadataRecordFields::Id::Get<ProviderId>(header);
      auto event = ProviderEventMetadataRecordFields::Event::Get<ProviderEventType>(header);
      switch (event) {
        case ProviderEventType::kBufferOverflow:
          record_consumer_(
              Record(Record::Metadata{MetadataContent(MetadataContent::ProviderEvent{id, event})}));
          break;
        default:
          // Ignore unknown event types for forward compatibility.
          ReportError(fbl::StringPrintf("Skipping provider event of unknown type %u",
                                        static_cast<unsigned>(event)));
          break;
      }
      break;
    }
    case MetadataType::kTraceInfo: {
      auto info_type = TraceInfoMetadataRecordFields::TraceInfoType::Get<TraceInfoType>(header);
      switch (info_type) {
        case TraceInfoType::kMagicNumber: {
          auto record_magic = MagicNumberRecordFields::Magic::Get<uint32_t>(header);
          record_consumer_(Record(Record::Metadata{MetadataContent(MetadataContent::TraceInfo{
              TraceInfoContent(TraceInfoContent::MagicNumberInfo{record_magic})})}));
          break;
        }
        default: {
          // Ignore unknown trace info types for forward compatibility.
          ReportError(fbl::StringPrintf("Skipping trace info record of unknown type %u",
                                        static_cast<unsigned>(info_type)));
          break;
        }
      }
      break;
    }
    default: {
      // Ignore unknown metadata types for forward compatibility.
      ReportError(
          fbl::StringPrintf("Skipping metadata of unknown type %d", static_cast<uint32_t>(type)));
      break;
    }
  }
  return true;
}

bool TraceReader::ReadInitializationRecord(Chunk& record, RecordHeader header) {
  std::optional ticks_per_second_opt = record.ReadUint64();
  if (!ticks_per_second_opt.has_value()) {
    return false;
  }
  trace_ticks_t ticks_per_second = ticks_per_second_opt.value();
  if (ticks_per_second == 0) {
    return false;
  }

  record_consumer_(Record(Record::Initialization{ticks_per_second}));
  return true;
}

bool TraceReader::ReadStringRecord(Chunk& record, RecordHeader header) {
  auto index = StringRecordFields::StringIndex::Get<trace_string_index_t>(header);
  if (index < TRACE_ENCODED_STRING_REF_MIN_INDEX || index > TRACE_ENCODED_STRING_REF_MAX_INDEX) {
    ReportError("Invalid string index");
    return false;
  }

  auto length = StringRecordFields::StringLength::Get<size_t>(header);
  std::optional string_view = record.ReadString(length);
  if (!string_view.has_value()) {
    return false;
  }
  fbl::String string(string_view.value());

  RegisterString(index, string);
  record_consumer_(Record(Record::String{index, std::move(string)}));
  return true;
}

bool TraceReader::ReadThreadRecord(Chunk& record, RecordHeader header) {
  auto index = ThreadRecordFields::ThreadIndex::Get<trace_thread_index_t>(header);
  if (index < TRACE_ENCODED_THREAD_REF_MIN_INDEX || index > TRACE_ENCODED_THREAD_REF_MAX_INDEX) {
    ReportError("Invalid thread index");
    return false;
  }

  std::optional process_koid = record.ReadUint64();
  if (!process_koid.has_value()) {
    return false;
  }
  std::optional thread_koid = record.ReadUint64();
  if (!thread_koid.has_value()) {
    return false;
  }

  ProcessThread process_thread(process_koid.value(), thread_koid.value());
  RegisterThread(index, process_thread);
  record_consumer_(Record(Record::Thread{index, process_thread}));
  return true;
}

bool TraceReader::ReadEventRecord(Chunk& record, RecordHeader header) {
  auto type = EventRecordFields::EventType::Get<EventType>(header);
  auto argument_count = EventRecordFields::ArgumentCount::Get<size_t>(header);
  auto thread_ref = EventRecordFields::ThreadRef::Get<trace_encoded_thread_ref_t>(header);
  auto category_ref = EventRecordFields::CategoryStringRef::Get<trace_encoded_string_ref_t>(header);
  auto name_ref = EventRecordFields::NameStringRef::Get<trace_encoded_string_ref_t>(header);

  std::optional timestamp_opt = record.ReadUint64();
  if (!timestamp_opt.has_value()) {
    return false;
  }
  const trace_ticks_t timestamp = timestamp_opt.value();
  ProcessThread process_thread;
  if (!DecodeThreadRef(record, thread_ref, &process_thread)) {
    return false;
  }
  fbl::String category;
  if (!DecodeStringRef(record, category_ref, &category)) {
    return false;
  }
  fbl::String name;
  if (!DecodeStringRef(record, name_ref, &name)) {
    return false;
  }
  fbl::Vector<Argument> arguments;
  if (!ReadArguments(record, argument_count, &arguments)) {
    return false;
  }

  switch (type) {
    case EventType::kInstant: {
      std::optional scope = record.ReadUint64();
      if (!scope.has_value()) {
        return false;
      }
      record_consumer_(Record(Record::Event{
          timestamp, process_thread, std::move(category), std::move(name), std::move(arguments),
          EventData(EventData::Instant{static_cast<EventScope>(scope.value())})}));
      break;
    }
    case EventType::kCounter: {
      std::optional id = record.ReadUint64();
      if (!id.has_value()) {
        return false;
      }
      record_consumer_(
          Record(Record::Event{timestamp, process_thread, std::move(category), std::move(name),
                               std::move(arguments), EventData(EventData::Counter{id.value()})}));
      break;
    }
    case EventType::kDurationBegin: {
      record_consumer_(
          Record(Record::Event{timestamp, process_thread, std::move(category), std::move(name),
                               std::move(arguments), EventData(EventData::DurationBegin{})}));
      break;
    }
    case EventType::kDurationEnd: {
      record_consumer_(
          Record(Record::Event{timestamp, process_thread, std::move(category), std::move(name),
                               std::move(arguments), EventData(EventData::DurationEnd{})}));
      break;
    }
    case EventType::kDurationComplete: {
      std::optional end_time = record.ReadUint64();
      if (!end_time.has_value()) {
        return false;
      }
      record_consumer_(Record(Record::Event{
          timestamp, process_thread, std::move(category), std::move(name), std::move(arguments),
          EventData(EventData::DurationComplete{end_time.value()})}));
      break;
    }
    case EventType::kAsyncBegin: {
      std::optional id = record.ReadUint64();
      if (!id.has_value()) {
        return false;
      }
      record_consumer_(Record(Record::Event{timestamp, process_thread, std::move(category),
                                            std::move(name), std::move(arguments),
                                            EventData(EventData::AsyncBegin{id.value()})}));
      break;
    }
    case EventType::kAsyncInstant: {
      std::optional id = record.ReadUint64();
      if (!id.has_value()) {
        return false;
      }
      record_consumer_(Record(Record::Event{timestamp, process_thread, std::move(category),
                                            std::move(name), std::move(arguments),
                                            EventData(EventData::AsyncInstant{id.value()})}));
      break;
    }
    case EventType::kAsyncEnd: {
      std::optional id = record.ReadUint64();
      if (!id.has_value()) {
        return false;
      }
      record_consumer_(
          Record(Record::Event{timestamp, process_thread, std::move(category), std::move(name),
                               std::move(arguments), EventData(EventData::AsyncEnd{id.value()})}));
      break;
    }
    case EventType::kFlowBegin: {
      std::optional id = record.ReadUint64();
      if (!id.has_value()) {
        return false;
      }
      record_consumer_(
          Record(Record::Event{timestamp, process_thread, std::move(category), std::move(name),
                               std::move(arguments), EventData(EventData::FlowBegin{id.value()})}));
      break;
    }
    case EventType::kFlowStep: {
      std::optional id = record.ReadUint64();
      if (!id.has_value()) {
        return false;
      }
      record_consumer_(
          Record(Record::Event{timestamp, process_thread, std::move(category), std::move(name),
                               std::move(arguments), EventData(EventData::FlowStep{id.value()})}));
      break;
    }
    case EventType::kFlowEnd: {
      std::optional id = record.ReadUint64();
      if (!id.has_value()) {
        return false;
      }
      record_consumer_(
          Record(Record::Event{timestamp, process_thread, std::move(category), std::move(name),
                               std::move(arguments), EventData(EventData::FlowEnd{id.value()})}));
      break;
    }
    default: {
      // Ignore unknown event types for forward compatibility.
      ReportError(
          fbl::StringPrintf("Skipping event of unknown type %d", static_cast<uint32_t>(type)));
      break;
    }
  }
  return true;
}

bool TraceReader::ReadBlobRecord(Chunk& record, RecordHeader header, void** out_blob) {
  auto blob_type = BlobRecordFields::BlobType::Get<trace_blob_type_t>(header);
  auto name_ref = BlobRecordFields::NameStringRef::Get<trace_encoded_string_ref_t>(header);
  auto blob_size = BlobRecordFields::BlobSize::Get<size_t>(header);
  fbl::String name;
  if (!DecodeStringRef(record, name_ref, &name))
    return false;
  auto blob_words = BytesToWords(blob_size);
  std::optional blob = record.ReadInPlace(blob_words);
  if (!blob.has_value()) {
    return false;
  }

  record_consumer_(Record(Record::Blob{blob_type, name, blob.value(), blob_size}));
  return true;
}

bool TraceReader::ReadKernelObjectRecord(Chunk& record, RecordHeader header) {
  auto object_type = KernelObjectRecordFields::ObjectType::Get<zx_obj_type_t>(header);
  auto name_ref = KernelObjectRecordFields::NameStringRef::Get<trace_encoded_string_ref_t>(header);
  auto argument_count = KernelObjectRecordFields::ArgumentCount::Get<size_t>(header);

  std::optional koid_opt = record.ReadUint64();
  if (!koid_opt.has_value()) {
    return false;
  }
  zx_koid_t koid = koid_opt.value();
  fbl::String name;
  if (!DecodeStringRef(record, name_ref, &name)) {
    return false;
  }
  fbl::Vector<Argument> arguments;
  if (!ReadArguments(record, argument_count, &arguments)) {
    return false;
  }

  record_consumer_(Record(Record::KernelObject{koid, object_type, name, std::move(arguments)}));
  return true;
}

bool TraceReader::ReadContextSwitchRecord(Chunk& record, RecordHeader header) {
  auto cpu_number = ContextSwitchRecordFields::CpuNumber::Get<trace_cpu_number_t>(header);
  auto outgoing_thread_state =
      ContextSwitchRecordFields::OutgoingThreadState::Get<ThreadState>(header);
  auto outgoing_thread_priority =
      ContextSwitchRecordFields::OutgoingThreadPriority::Get<trace_thread_priority_t>(header);
  auto incoming_thread_priority =
      ContextSwitchRecordFields::IncomingThreadPriority::Get<trace_thread_priority_t>(header);
  auto outgoing_thread_ref =
      ContextSwitchRecordFields::OutgoingThreadRef::Get<trace_encoded_thread_ref_t>(header);
  auto incoming_thread_ref =
      ContextSwitchRecordFields::IncomingThreadRef::Get<trace_encoded_thread_ref_t>(header);

  std::optional timestamp_opt = record.ReadUint64();
  if (!timestamp_opt.has_value()) {
    return false;
  }
  trace_ticks_t timestamp = timestamp_opt.value();
  ProcessThread outgoing_thread;
  if (!DecodeThreadRef(record, outgoing_thread_ref, &outgoing_thread)) {
    return false;
  }
  ProcessThread incoming_thread;
  if (!DecodeThreadRef(record, incoming_thread_ref, &incoming_thread)) {
    return false;
  }

  record_consumer_(Record(
      Record::ContextSwitch{timestamp, cpu_number, outgoing_thread_state, outgoing_thread,
                            incoming_thread, outgoing_thread_priority, incoming_thread_priority}));
  return true;
}

bool TraceReader::ReadLogRecord(Chunk& record, RecordHeader header) {
  auto log_message_length = LogRecordFields::LogMessageLength::Get<uint16_t>(header);

  if (log_message_length > LogRecordFields::kMaxMessageLength)
    return false;

  auto thread_ref = LogRecordFields::ThreadRef::Get<trace_encoded_thread_ref_t>(header);
  std::optional timestamp_opt = record.ReadUint64();
  if (!timestamp_opt.has_value()) {
    return false;
  }
  trace_ticks_t timestamp = timestamp_opt.value();
  ProcessThread process_thread;
  if (!DecodeThreadRef(record, thread_ref, &process_thread)) {
    return false;
  }
  std::optional log_message = record.ReadString(log_message_length);
  if (!log_message.has_value()) {
    return false;
  }
  record_consumer_(
      Record(Record::Log{timestamp, process_thread, fbl::String(log_message.value())}));
  return true;
}

bool TraceReader::ReadLargeRecord(trace::Chunk& record, trace::RecordHeader header) {
  auto large_type = LargeRecordFields::LargeType::Get<LargeRecordType>(header);

  switch (large_type) {
    case LargeRecordType::kBlob:
      return ReadLargeBlob(record, header);
    default:
      ReportError(
          fbl::StringPrintf("Skipping unknown large record type %d", ToUnderlyingType(large_type)));
  }
  return true;
}

bool TraceReader::ReadLargeBlob(trace::Chunk& record, trace::RecordHeader header) {
  auto format_type = LargeBlobFields::BlobFormat::Get<trace_blob_format_t>(header);

  switch (format_type) {
    case TRACE_BLOB_FORMAT_EVENT: {
      std::optional format_header_opt = record.ReadUint64();
      if (!format_header_opt.has_value()) {
        return false;
      }
      uint64_t format_header = format_header_opt.value();

      using Format = BlobFormatEventFields;
      auto category_ref = Format::CategoryStringRef::Get<trace_encoded_string_ref_t>(format_header);
      auto name_ref = Format::NameStringRef::Get<trace_encoded_string_ref_t>(format_header);
      auto argument_count = Format::ArgumentCount::Get<size_t>(format_header);
      auto thread_ref = Format::ThreadRef::Get<trace_encoded_thread_ref_t>(format_header);

      fbl::String category;
      if (!DecodeStringRef(record, category_ref, &category)) {
        return false;
      }
      fbl::String name;
      if (!DecodeStringRef(record, name_ref, &name)) {
        return false;
      }
      std::optional timestamp_opt = record.ReadUint64();
      if (!timestamp_opt.has_value()) {
        return false;
      }
      trace_ticks_t timestamp = timestamp_opt.value();
      ProcessThread process_thread;
      if (!DecodeThreadRef(record, thread_ref, &process_thread)) {
        return false;
      }
      fbl::Vector<Argument> arguments;
      if (!ReadArguments(record, argument_count, &arguments)) {
        return false;
      }
      std::optional blob_size = record.ReadUint64();
      if (!blob_size.has_value()) {
        return false;
      }
      std::optional blob = record.ReadInPlace(trace::BytesToWords(trace::Pad(blob_size.value())));
      if (!blob.has_value()) {
        return false;
      }

      record_consumer_(Record(Record::Large(LargeRecordData::Blob(LargeRecordData::BlobEvent{
          std::move(category),
          std::move(name),
          timestamp,
          process_thread,
          std::move(arguments),
          blob.value(),
          blob_size.value(),
      }))));
      break;
    }
    case TRACE_BLOB_FORMAT_ATTACHMENT: {
      std::optional format_header_opt = record.ReadUint64();
      if (!format_header_opt.has_value()) {
        return false;
      }
      uint64_t format_header = format_header_opt.value();

      using Format = BlobFormatAttachmentFields;
      auto category_ref = Format::CategoryStringRef::Get<trace_encoded_string_ref_t>(format_header);
      auto name_ref = Format::NameStringRef::Get<trace_encoded_string_ref_t>(format_header);

      fbl::String category;
      if (!DecodeStringRef(record, category_ref, &category)) {
        return false;
      }
      fbl::String name;
      if (!DecodeStringRef(record, name_ref, &name)) {
        return false;
      }
      std::optional blob_size = record.ReadUint64();
      if (!blob_size.has_value()) {
        return false;
      }
      std::optional blob = record.ReadInPlace(trace::BytesToWords(trace::Pad(blob_size.value())));
      if (!blob.has_value()) {
        return false;
      }

      record_consumer_(Record(Record::Large(LargeRecordData::Blob(LargeRecordData::BlobAttachment{
          std::move(category),
          std::move(name),
          blob.value(),
          blob_size.value(),
      }))));
      break;
    }
    default:
      ReportError(fbl::StringPrintf("Skipping unknown large blob record format %d",
                                    ToUnderlyingType(format_type)));
  }
  return true;
}

bool TraceReader::ReadArguments(Chunk& record, size_t count, fbl::Vector<Argument>* out_arguments) {
  while (count-- > 0) {
    std::optional header_opt = record.ReadUint64();
    if (!header_opt.has_value()) {
      ReportError("Failed to read argument header");
      return false;
    }
    ArgumentHeader header = header_opt.value();

    auto size = ArgumentFields::ArgumentSize::Get<size_t>(header);
    if (size == 0) {
      ReportError("Invalid argument size");
      return false;
    }

    std::optional arg_opt = record.ReadChunk(size - 1);
    if (!arg_opt.has_value()) {
      ReportError("Failed to read argument");
      return false;
    }
    Chunk& arg = arg_opt.value();

    auto name_ref = ArgumentFields::NameRef::Get<trace_encoded_string_ref_t>(header);
    fbl::String name;
    if (!DecodeStringRef(arg, name_ref, &name)) {
      ReportError("Failed to read argument name");
      return false;
    }

    auto type = ArgumentFields::Type::Get<ArgumentType>(header);
    switch (type) {
      case ArgumentType::kNull: {
        out_arguments->push_back(Argument{std::move(name), ArgumentValue::MakeNull()});
        break;
      }
      case ArgumentType::kBool: {
        auto value = BoolArgumentFields::Value::Get<bool>(header);
        out_arguments->push_back(Argument{std::move(name), ArgumentValue::MakeBool(value)});
        break;
      }
      case ArgumentType::kInt32: {
        auto value = Int32ArgumentFields::Value::Get<int32_t>(header);
        out_arguments->push_back(Argument{std::move(name), ArgumentValue::MakeInt32(value)});
        break;
      }
      case ArgumentType::kUint32: {
        auto value = Uint32ArgumentFields::Value::Get<uint32_t>(header);
        out_arguments->push_back(Argument{std::move(name), ArgumentValue::MakeUint32(value)});
        break;
      }
      case ArgumentType::kInt64: {
        std::optional value = arg.ReadInt64();
        if (!value.has_value()) {
          ReportError("Failed to read int64 argument value");
          return false;
        }
        out_arguments->push_back(
            Argument{std::move(name), ArgumentValue::MakeInt64(value.value())});
        break;
      }
      case ArgumentType::kUint64: {
        std::optional value = arg.ReadUint64();
        if (!value.has_value()) {
          ReportError("Failed to read uint64 argument value");
          return false;
        }
        out_arguments->push_back(
            Argument{std::move(name), ArgumentValue::MakeUint64(value.value())});
        break;
      }
      case ArgumentType::kDouble: {
        std::optional value = arg.ReadDouble();
        if (!value.has_value()) {
          ReportError("Failed to read double argument value");
          return false;
        }
        out_arguments->push_back(
            Argument{std::move(name), ArgumentValue::MakeDouble(value.value())});
        break;
      }
      case ArgumentType::kString: {
        auto string_ref = StringArgumentFields::Index::Get<trace_encoded_string_ref_t>(header);
        fbl::String value;
        if (!DecodeStringRef(arg, string_ref, &value)) {
          ReportError("Failed to read string argument value");
          return false;
        }
        out_arguments->push_back(
            Argument{std::move(name), ArgumentValue::MakeString(std::move(value))});
        break;
      }
      case ArgumentType::kPointer: {
        std::optional value = arg.ReadUint64();
        if (!value.has_value()) {
          ReportError("Failed to read pointer argument value");
          return false;
        }
        out_arguments->push_back(
            Argument{std::move(name), ArgumentValue::MakePointer(value.value())});
        break;
      }
      case ArgumentType::kKoid: {
        std::optional value = arg.ReadUint64();
        if (!value.has_value()) {
          ReportError("Failed to read koid argument value");
          return false;
        }
        out_arguments->push_back(Argument{std::move(name), ArgumentValue::MakeKoid(value.value())});
        break;
      }
      default: {
        // Ignore unknown argument types for forward compatibility.
        ReportError(fbl::StringPrintf("Skipping argument of unknown type %d, argument name %s",
                                      static_cast<uint32_t>(type), name.c_str()));
        break;
      }
    }
  }
  return true;
}

fbl::String TraceReader::GetProviderName(ProviderId id) const {
  auto it = providers_.find(id);
  if (it != providers_.end())
    return it->name;
  return fbl::String();
}

void TraceReader::SetCurrentProvider(ProviderId id) {
  auto it = providers_.find(id);
  if (it != providers_.end()) {
    current_provider_ = &*it;
    return;
  }
  ReportError(fbl::StringPrintf("Registering non-existent provider %u\n", id));
  RegisterProvider(id, "");
}

void TraceReader::RegisterProvider(ProviderId id, fbl::String name) {
  auto provider = std::make_unique<ProviderInfo>();
  provider->id = id;
  provider->name = std::move(name);
  current_provider_ = provider.get();

  providers_.insert_or_replace(std::move(provider));
}

void TraceReader::RegisterString(trace_string_index_t index, const fbl::String& string) {
  ZX_DEBUG_ASSERT(index >= TRACE_ENCODED_STRING_REF_MIN_INDEX &&
                  index <= TRACE_ENCODED_STRING_REF_MAX_INDEX);

  auto entry = std::make_unique<StringTableEntry>(index, string);
  current_provider_->string_table.insert_or_replace(std::move(entry));
}

void TraceReader::RegisterThread(trace_thread_index_t index, const ProcessThread& process_thread) {
  ZX_DEBUG_ASSERT(index >= TRACE_ENCODED_THREAD_REF_MIN_INDEX &&
                  index <= TRACE_ENCODED_THREAD_REF_MAX_INDEX);

  auto entry = std::make_unique<ThreadTableEntry>(index, process_thread);
  current_provider_->thread_table.insert_or_replace(std::move(entry));
}

bool TraceReader::DecodeStringRef(Chunk& chunk, trace_encoded_string_ref_t string_ref,
                                  fbl::String* out_string) const {
  if (string_ref == TRACE_ENCODED_STRING_REF_EMPTY) {
    out_string->clear();
    return true;
  }

  if (string_ref & TRACE_ENCODED_STRING_REF_INLINE_FLAG) {
    size_t length = string_ref & TRACE_ENCODED_STRING_REF_LENGTH_MASK;
    if (length > TRACE_ENCODED_STRING_REF_MAX_LENGTH) {
      ReportError("Could not read inline string");
      return false;
    }

    std::optional string_view = chunk.ReadString(length);
    if (!string_view.has_value()) {
      ReportError("Could not read inline string");
      return false;
    }
    *out_string = string_view.value();
    return true;
  }

  auto it = current_provider_->string_table.find(string_ref);
  if (it == current_provider_->string_table.end()) {
    ReportError("String ref not in table");
    return false;
  }
  *out_string = it->string;
  return true;
}

bool TraceReader::DecodeThreadRef(Chunk& chunk, trace_encoded_thread_ref_t thread_ref,
                                  ProcessThread* out_process_thread) const {
  if (thread_ref == TRACE_ENCODED_THREAD_REF_INLINE) {
    std::optional process_koid = chunk.ReadUint64();
    if (!process_koid.has_value()) {
      ReportError("Could not read inline process");
      return false;
    }
    std::optional thread_koid = chunk.ReadUint64();
    if (!thread_koid.has_value()) {
      ReportError("Could not read inline thread");
      return false;
    }
    *out_process_thread = ProcessThread(process_koid.value(), thread_koid.value());
    return true;
  }

  auto it = current_provider_->thread_table.find(thread_ref);
  if (it == current_provider_->thread_table.end()) {
    ReportError(fbl::StringPrintf("Thread ref 0x%x not in table", thread_ref));
    return false;
  }
  *out_process_thread = it->process_thread;
  return true;
}

void TraceReader::ReportError(fbl::String error) const {
  if (error_handler_)
    error_handler_(std::move(error));
}

Chunk::Chunk(const uint64_t* begin, size_t num_words)
    : begin_(begin), current_(begin), end_(begin_ + num_words) {}

std::optional<uint64_t> Chunk::ReadUint64() {
  if (current_ < end_) {
    return *current_++;
  }
  return std::nullopt;
}

std::optional<int64_t> Chunk::ReadInt64() {
  if (current_ < end_) {
    return *reinterpret_cast<const int64_t*>(current_++);
  }
  return std::nullopt;
}

std::optional<double> Chunk::ReadDouble() {
  if (current_ < end_) {
    return *reinterpret_cast<const double*>(current_++);
  }
  return std::nullopt;
}

std::optional<Chunk> Chunk::ReadChunk(size_t num_words) {
  if (current_ + num_words > end_)
    return std::nullopt;

  Chunk chunk(current_, num_words);
  current_ += num_words;
  return chunk;
}

std::optional<std::string_view> Chunk::ReadString(size_t length) {
  auto num_words = BytesToWords(length);
  if (current_ + num_words > end_)
    return std::nullopt;

  std::string_view view(reinterpret_cast<const char*>(current_), length);
  current_ += num_words;
  return view;
}

std::optional<void const*> Chunk::ReadInPlace(size_t num_words) {
  if (current_ + num_words > end_)
    return std::nullopt;
  void const* current = current_;
  current_ += num_words;
  return current;
}

}  // namespace trace
