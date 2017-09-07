// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-reader/reader.h>

#include <fbl/string_printf.h>
#include <trace-engine/fields.h>

namespace trace {

TraceReader::TraceReader(RecordConsumer record_consumer,
                         ErrorHandler error_handler)
    : record_consumer_(fbl::move(record_consumer)),
      error_handler_(fbl::move(error_handler)) {
    RegisterProvider(0u, "");
}

bool TraceReader::ReadRecords(Chunk& chunk) {
    while (true) {
        if (!pending_header_ && !chunk.ReadUint64(&pending_header_))
            return true; // need more data

        auto size = RecordFields::RecordSize::Get<size_t>(pending_header_);
        if (size == 0) {
            ReportError("Unexpected record of size 0");
            return false; // fatal error
        }
        MX_DEBUG_ASSERT(size <= RecordFields::kMaxRecordSizeWords);

        Chunk record;
        if (!chunk.ReadChunk(size - 1, &record))
            return true; // need more data to decode record

        auto type = RecordFields::Type::Get<RecordType>(pending_header_);
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
        default: {
            // Ignore unknown record types for forward compatibility.
            ReportError(fbl::StringPrintf(
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
        fbl::StringPiece name_view;
        if (!record.ReadString(name_length, &name_view))
            return false;
        fbl::String name(name_view);

        RegisterProvider(id, name);
        record_consumer_(Record(Record::Metadata{
            MetadataContent(MetadataContent::ProviderInfo{id, name})}));
        break;
    }
    case MetadataType::kProviderSection: {
        auto id =
            ProviderSectionMetadataRecordFields::Id::Get<ProviderId>(header);

        SetCurrentProvider(id);
        record_consumer_(Record(
            Record::Metadata{MetadataContent(MetadataContent::ProviderSection{id})}));
        break;
    }
    default: {
        // Ignore unknown metadata types for forward compatibility.
        ReportError(fbl::StringPrintf(
            "Skipping metadata of unknown type %d", static_cast<uint32_t>(type)));
        break;
    }
    }
    return true;
}

bool TraceReader::ReadInitializationRecord(Chunk& record, RecordHeader header) {
    trace_ticks_t ticks_per_second;
    if (!record.ReadUint64(&ticks_per_second) || !ticks_per_second)
        return false;

    record_consumer_(Record(Record::Initialization{ticks_per_second}));
    return true;
}

bool TraceReader::ReadStringRecord(Chunk& record, RecordHeader header) {
    auto index = StringRecordFields::StringIndex::Get<trace_string_index_t>(header);
    if (index < TRACE_ENCODED_STRING_REF_MIN_INDEX ||
        index > TRACE_ENCODED_STRING_REF_MAX_INDEX) {
        ReportError("Invalid string index");
        return false;
    }

    auto length = StringRecordFields::StringLength::Get<size_t>(header);
    fbl::StringPiece string_view;
    if (!record.ReadString(length, &string_view))
        return false;
    fbl::String string(string_view);

    RegisterString(index, string);
    record_consumer_(Record(Record::String{index, fbl::move(string)}));
    return true;
}

bool TraceReader::ReadThreadRecord(Chunk& record, RecordHeader header) {
    auto index = ThreadRecordFields::ThreadIndex::Get<trace_thread_index_t>(header);
    if (index < TRACE_ENCODED_THREAD_REF_MIN_INDEX ||
        index > TRACE_ENCODED_THREAD_REF_MAX_INDEX) {
        ReportError("Invalid thread index");
        return false;
    }

    mx_koid_t process_koid, thread_koid;
    if (!record.ReadUint64(&process_koid) ||
        !record.ReadUint64(&thread_koid))
        return false;

    ProcessThread process_thread(process_koid, thread_koid);
    RegisterThread(index, process_thread);
    record_consumer_(Record(Record::Thread{index, process_thread}));
    return true;
}

bool TraceReader::ReadEventRecord(Chunk& record, RecordHeader header) {
    auto type = EventRecordFields::EventType::Get<EventType>(header);
    auto argument_count = EventRecordFields::ArgumentCount::Get<size_t>(header);
    auto thread_ref = EventRecordFields::ThreadRef::Get<trace_encoded_thread_ref_t>(header);
    auto category_ref =
        EventRecordFields::CategoryStringRef::Get<trace_encoded_string_ref_t>(header);
    auto name_ref =
        EventRecordFields::NameStringRef::Get<trace_encoded_string_ref_t>(header);

    trace_ticks_t timestamp;
    ProcessThread process_thread;
    fbl::String category;
    fbl::String name;
    fbl::Vector<Argument> arguments;
    if (!record.ReadUint64(&timestamp) ||
        !DecodeThreadRef(record, thread_ref, &process_thread) ||
        !DecodeStringRef(record, category_ref, &category) ||
        !DecodeStringRef(record, name_ref, &name) ||
        !ReadArguments(record, argument_count, &arguments))
        return false;

    switch (type) {
    case EventType::kInstant: {
        uint64_t scope;
        if (!record.ReadUint64(&scope))
            return false;
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments),
            EventData(EventData::Instant{static_cast<EventScope>(scope)})}));
        break;
    }
    case EventType::kCounter: {
        trace_counter_id_t id;
        if (!record.ReadUint64(&id))
            return false;
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::Counter{id})}));
        break;
    }
    case EventType::kDurationBegin: {
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::DurationBegin{})}));
        break;
    }
    case EventType::kDurationEnd: {
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::DurationEnd{})}));
        break;
    }
    case EventType::kAsyncBegin: {
        trace_async_id_t id;
        if (!record.ReadUint64(&id))
            return false;
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::AsyncBegin{id})}));
        break;
    }
    case EventType::kAsyncInstant: {
        trace_async_id_t id;
        if (!record.ReadUint64(&id))
            return false;
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::AsyncInstant{id})}));
        break;
    }
    case EventType::kAsyncEnd: {
        trace_async_id_t id;
        if (!record.ReadUint64(&id))
            return false;
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::AsyncEnd{id})}));
        break;
    }
    case EventType::kFlowBegin: {
        trace_flow_id_t id;
        if (!record.ReadUint64(&id))
            return false;
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::FlowBegin{id})}));
        break;
    }
    case EventType::kFlowStep: {
        trace_flow_id_t id;
        if (!record.ReadUint64(&id))
            return false;
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::FlowStep{id})}));
        break;
    }
    case EventType::kFlowEnd: {
        trace_flow_id_t id;
        if (!record.ReadUint64(&id))
            return false;
        record_consumer_(Record(Record::Event{
            timestamp, process_thread, fbl::move(category), fbl::move(name),
            fbl::move(arguments), EventData(EventData::FlowEnd{id})}));
        break;
    }
    default: {
        // Ignore unknown event types for forward compatibility.
        ReportError(fbl::StringPrintf(
            "Skipping event of unknown type %d", static_cast<uint32_t>(type)));
        break;
    }
    }
    return true;
}

bool TraceReader::ReadKernelObjectRecord(Chunk& record, RecordHeader header) {
    auto object_type =
        KernelObjectRecordFields::ObjectType::Get<mx_obj_type_t>(header);
    auto name_ref =
        KernelObjectRecordFields::NameStringRef::Get<trace_encoded_string_ref_t>(header);
    auto argument_count =
        KernelObjectRecordFields::ArgumentCount::Get<size_t>(header);

    mx_koid_t koid;
    fbl::String name;
    fbl::Vector<Argument> arguments;
    if (!record.ReadUint64(&koid) ||
        !DecodeStringRef(record, name_ref, &name) ||
        !ReadArguments(record, argument_count, &arguments))
        return false;

    record_consumer_(
        Record(Record::KernelObject{koid, object_type, name, fbl::move(arguments)}));
    return true;
}

bool TraceReader::ReadContextSwitchRecord(Chunk& record, RecordHeader header) {
    auto cpu_number =
        ContextSwitchRecordFields::CpuNumber::Get<trace_cpu_number_t>(header);
    auto outgoing_thread_state =
        ContextSwitchRecordFields::OutgoingThreadState::Get<ThreadState>(header);
    auto outgoing_thread_ref =
        ContextSwitchRecordFields::OutgoingThreadRef::Get<trace_encoded_thread_ref_t>(
            header);
    auto incoming_thread_ref =
        ContextSwitchRecordFields::IncomingThreadRef::Get<trace_encoded_thread_ref_t>(
            header);

    trace_ticks_t timestamp;
    ProcessThread outgoing_thread;
    ProcessThread incoming_thread;
    if (!record.ReadUint64(&timestamp) ||
        !DecodeThreadRef(record, outgoing_thread_ref,
                         &outgoing_thread) ||
        !DecodeThreadRef(record, incoming_thread_ref, &incoming_thread))
        return false;

    record_consumer_(
        Record(Record::ContextSwitch{timestamp, cpu_number, outgoing_thread_state,
                                     outgoing_thread, incoming_thread}));
    return true;
}

bool TraceReader::ReadLogRecord(Chunk& record, RecordHeader header) {
    auto log_message_length =
        LogRecordFields::LogMessageLength::Get<uint16_t>(header);

    if (log_message_length > LogRecordFields::kMaxMessageLength)
        return false;

    auto thread_ref = LogRecordFields::ThreadRef::Get<trace_encoded_thread_ref_t>(header);
    trace_ticks_t timestamp;
    ProcessThread process_thread;
    fbl::StringPiece log_message;
    if (!record.ReadUint64(&timestamp) ||
        !DecodeThreadRef(record, thread_ref, &process_thread) ||
        !record.ReadString(log_message_length, &log_message))
        return false;
    record_consumer_(
        Record(Record::Log{timestamp, process_thread, fbl::String(log_message)}));
    return true;
}

bool TraceReader::ReadArguments(Chunk& record,
                                size_t count,
                                fbl::Vector<Argument>* out_arguments) {
    while (count-- > 0) {
        ArgumentHeader header;
        if (!record.ReadUint64(&header)) {
            ReportError("Failed to read argument header");
            return false;
        }

        auto size = ArgumentFields::ArgumentSize::Get<size_t>(header);
        Chunk arg;
        if (!size || !record.ReadChunk(size - 1, &arg)) {
            ReportError("Invalid argument size");
            return false;
        }

        auto name_ref = ArgumentFields::NameRef::Get<trace_encoded_string_ref_t>(header);
        fbl::String name;
        if (!DecodeStringRef(arg, name_ref, &name)) {
            ReportError("Failed to read argument name");
            return false;
        }

        auto type = ArgumentFields::Type::Get<ArgumentType>(header);
        switch (type) {
        case ArgumentType::kNull: {
            out_arguments->push_back(Argument{fbl::move(name),
                                              ArgumentValue::MakeNull()});
            break;
        }
        case ArgumentType::kInt32: {
            auto value = Int32ArgumentFields::Value::Get<int32_t>(header);
            out_arguments->push_back(Argument{fbl::move(name),
                                              ArgumentValue::MakeInt32(value)});
            break;
        }
        case ArgumentType::kUint32: {
            auto value = Uint32ArgumentFields::Value::Get<uint32_t>(header);
            out_arguments->push_back(Argument{fbl::move(name),
                                              ArgumentValue::MakeUint32(value)});
            break;
        }
        case ArgumentType::kInt64: {
            int64_t value;
            if (!arg.ReadInt64(&value)) {
                ReportError("Failed to read int64 argument value");
                return false;
            }
            out_arguments->push_back(Argument{fbl::move(name),
                                              ArgumentValue::MakeInt64(value)});
            break;
        }
        case ArgumentType::kUint64: {
            uint64_t value;
            if (!arg.ReadUint64(&value)) {
                ReportError("Failed to read uint64 argument value");
                return false;
            }
            out_arguments->push_back(Argument{fbl::move(name),
                                              ArgumentValue::MakeUint64(value)});
            break;
        }
        case ArgumentType::kDouble: {
            double value;
            if (!arg.ReadDouble(&value)) {
                ReportError("Failed to read double argument value");
                return false;
            }
            out_arguments->push_back(Argument{fbl::move(name),
                                              ArgumentValue::MakeDouble(value)});
            break;
        }
        case ArgumentType::kString: {
            auto string_ref =
                StringArgumentFields::Index::Get<trace_encoded_string_ref_t>(header);
            fbl::String value;
            if (!DecodeStringRef(arg, string_ref, &value)) {
                ReportError("Failed to read string argument value");
                return false;
            }
            out_arguments->push_back(
                Argument{fbl::move(name),
                         ArgumentValue::MakeString(fbl::move(value))});
            break;
        }
        case ArgumentType::kPointer: {
            uint64_t value;
            if (!arg.ReadUint64(&value)) {
                ReportError("Failed to read pointer argument value");
                return false;
            }
            out_arguments->push_back(Argument{fbl::move(name),
                                              ArgumentValue::MakePointer(value)});
            break;
        }
        case ArgumentType::kKoid: {
            mx_koid_t value;
            if (!arg.ReadUint64(&value)) {
                ReportError("Failed to read koid argument value");
                return false;
            }
            out_arguments->push_back(Argument{fbl::move(name),
                                              ArgumentValue::MakeKoid(value)});
            break;
        }
        default: {
            // Ignore unknown argument types for forward compatibility.
            ReportError(fbl::StringPrintf(
                "Skipping argument of unknown type %d, argument name %s",
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
    RegisterProvider(id, "");
}

void TraceReader::RegisterProvider(ProviderId id, fbl::String name) {
    auto provider = fbl::make_unique<ProviderInfo>();
    provider->id = id;
    provider->name = name;
    current_provider_ = provider.get();

    providers_.insert_or_replace(fbl::move(provider));
}

void TraceReader::RegisterString(trace_string_index_t index, fbl::String string) {
    MX_DEBUG_ASSERT(index >= TRACE_ENCODED_STRING_REF_MIN_INDEX &&
                    index <= TRACE_ENCODED_STRING_REF_MAX_INDEX);

    auto entry = fbl::make_unique<StringTableEntry>(index, string);
    current_provider_->string_table.insert_or_replace(fbl::move(entry));
}

void TraceReader::RegisterThread(trace_thread_index_t index,
                                 const ProcessThread& process_thread) {
    MX_DEBUG_ASSERT(index >= TRACE_ENCODED_THREAD_REF_MIN_INDEX &&
                    index <= TRACE_ENCODED_THREAD_REF_MAX_INDEX);

    auto entry = fbl::make_unique<ThreadTableEntry>(index, process_thread);
    current_provider_->thread_table.insert_or_replace(fbl::move(entry));
}

bool TraceReader::DecodeStringRef(Chunk& chunk,
                                  trace_encoded_string_ref_t string_ref,
                                  fbl::String* out_string) const {
    if (string_ref == TRACE_ENCODED_STRING_REF_EMPTY) {
        out_string->clear();
        return true;
    }

    if (string_ref & TRACE_ENCODED_STRING_REF_INLINE_FLAG) {
        size_t length = string_ref & TRACE_ENCODED_STRING_REF_LENGTH_MASK;
        fbl::StringPiece string_view;
        if (length > TRACE_ENCODED_STRING_REF_MAX_LENGTH ||
            !chunk.ReadString(length, &string_view)) {
            ReportError("Could not read inline string");
            return false;
        }
        *out_string = string_view;
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

bool TraceReader::DecodeThreadRef(Chunk& chunk,
                                  trace_encoded_thread_ref_t thread_ref,
                                  ProcessThread* out_process_thread) const {
    if (thread_ref == TRACE_ENCODED_THREAD_REF_INLINE) {
        mx_koid_t process_koid, thread_koid;
        if (!chunk.ReadUint64(&process_koid) ||
            !chunk.ReadUint64(&thread_koid)) {
            ReportError("Could not read inline process and thread");
            return false;
        }
        *out_process_thread = ProcessThread(process_koid, thread_koid);
        return true;
    }

    auto it = current_provider_->thread_table.find(thread_ref);
    if (it == current_provider_->thread_table.end()) {
        ReportError("Thread ref not in table");
        return false;
    }
    *out_process_thread = it->process_thread;
    return true;
}

void TraceReader::ReportError(fbl::String error) const {
    if (error_handler_)
        error_handler_(fbl::move(error));
}

Chunk::Chunk()
    : current_(nullptr), end_(nullptr) {}

Chunk::Chunk(const uint64_t* begin, size_t num_words)
    : current_(begin), end_(current_ + num_words) {}

bool Chunk::ReadUint64(uint64_t* out_value) {
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

bool Chunk::ReadString(size_t length, fbl::StringPiece* out_string) {
    auto num_words = BytesToWords(length);
    if (current_ + num_words > end_)
        return false;

    *out_string =
        fbl::StringPiece(reinterpret_cast<const char*>(current_), length);
    current_ += num_words;
    return true;
}

} // namespace trace
