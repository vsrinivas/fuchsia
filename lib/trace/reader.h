// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_READER_H_
#define APPS_TRACING_LIB_TRACE_READER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/lib/trace/types.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace tracing {
namespace reader {

// Provides support for reading sequences of 64-bit words from a buffer.
class Chunk {
 public:
  Chunk();
  explicit Chunk(const uint64_t* begin, size_t num_words);

  uint64_t remaining_words() const { return end_ - current_; }

  // Reads from the chunk, maintaining proper alignment.
  // Returns true on success, false if the chunk has insufficient remaining
  // words to satisfy the request.
  bool Read(uint64_t* out_value);
  bool ReadInt64(int64_t* out_value);
  bool ReadUint64(uint64_t* out_value);
  bool ReadDouble(double* out_value);
  bool ReadString(size_t length, fxl::StringView* out_string);
  bool ReadChunk(size_t num_words, Chunk* out_chunk);

 private:
  const uint64_t* current_;
  const uint64_t* end_;
};

// Callback invoked when decoding errors are detected in the trace.
using ErrorHandler = std::function<void(std::string)>;

// Retains context needed to decode traces.
class TraceContext {
 public:
  explicit TraceContext(ErrorHandler error_handler);
  ~TraceContext();

  // Reports a decoding error.
  void ReportError(std::string error) const;

  // Gets the current trace provider id.
  // Returns 0 if no providers have been registered yet.
  ProviderId current_provider_id() const { return current_provider_->id; }

  // Gets the name of the current trace provider.
  // Returns "default" if no providers have been registered yet.
  const std::string& current_provider_name() const {
    return current_provider_->name;
  }

  // Gets the name of the specified provider, or an empty string if none.
  std::string GetProviderName(ProviderId id) const;

  // Decodes a string reference from a chunk.
  bool DecodeStringRef(Chunk& chunk,
                       EncodedStringRef string_ref,
                       std::string* out_string) const;

  // Decodes a thread reference from a chunk.
  bool DecodeThreadRef(Chunk& chunk,
                       EncodedThreadRef thread_ref,
                       ProcessThread* out_process_thread) const;

  // Registers a trace provider with the context.
  void RegisterProvider(ProviderId id, std::string name);

  // Registers a string in the current string table.
  void RegisterString(StringIndex index, std::string string);

  // Registers a thread in the current thread table.
  void RegisterThread(ThreadIndex index, const ProcessThread& process_thread);

  // Sets the current provider id.
  void SetCurrentProvider(ProviderId id);

 private:
  struct ProviderInfo {
    ProviderId id;
    std::string name;
    std::unordered_map<StringIndex, std::string> string_table;
    std::unordered_map<ThreadIndex, ProcessThread> thread_table;
  };

  ErrorHandler error_handler_;
  std::unordered_map<ProviderId, std::unique_ptr<ProviderInfo>> providers_;
  ProviderInfo* current_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TraceContext);
};

// A typed argument value.
class ArgumentValue {
 public:
  static ArgumentValue MakeNull() { return ArgumentValue(); }

  static ArgumentValue MakeInt32(int32_t value) { return ArgumentValue(value); }

  static ArgumentValue MakeUint32(uint32_t value) {
    return ArgumentValue(value);
  }

  static ArgumentValue MakeInt64(int64_t value) { return ArgumentValue(value); }

  static ArgumentValue MakeUint64(uint64_t value) {
    return ArgumentValue(value);
  }

  static ArgumentValue MakeDouble(double value) { return ArgumentValue(value); }

  static ArgumentValue MakeString(std::string value) {
    return ArgumentValue(std::move(value));
  }

  static ArgumentValue MakePointer(uintptr_t value) {
    return ArgumentValue(PointerTag(), value);
  }

  static ArgumentValue MakeKoid(mx_koid_t value) {
    return ArgumentValue(KoidTag(), value);
  }

  ArgumentValue(const ArgumentValue& other) : type_(other.type_) {
    Copy(other);
  }

  ~ArgumentValue() { Destroy(); }

  ArgumentValue& operator=(const ArgumentValue& rhs) {
    return Destroy().Copy(rhs);
  }

  ArgumentType type() const { return type_; }

  int32_t GetInt32() const {
    FXL_DCHECK(type_ == ArgumentType::kInt32);
    return int32_;
  }

  uint32_t GetUint32() const {
    FXL_DCHECK(type_ == ArgumentType::kUint32);
    return uint32_;
  }

  int64_t GetInt64() const {
    FXL_DCHECK(type_ == ArgumentType::kInt64);
    return int64_;
  }

  uint64_t GetUint64() const {
    FXL_DCHECK(type_ == ArgumentType::kUint64);
    return uint64_;
  }

  double GetDouble() const {
    FXL_DCHECK(type_ == ArgumentType::kDouble);
    return double_;
  }

  const std::string& GetString() const {
    FXL_DCHECK(type_ == ArgumentType::kString);
    return string_;
  }

  uintptr_t GetPointer() const {
    FXL_DCHECK(type_ == ArgumentType::kPointer);
    return uint64_;
  }

  mx_koid_t GetKoid() const {
    FXL_DCHECK(type_ == ArgumentType::kKoid);
    return uint64_;
  }

 private:
  struct PointerTag {};
  struct KoidTag {};

  ArgumentValue() : type_(ArgumentType::kNull) {}

  explicit ArgumentValue(int32_t int32)
      : type_(ArgumentType::kInt32), int32_(int32) {}

  explicit ArgumentValue(uint32_t uint32)
      : type_(ArgumentType::kUint32), uint32_(uint32) {}

  explicit ArgumentValue(int64_t int64)
      : type_(ArgumentType::kInt64), int64_(int64) {}

  explicit ArgumentValue(uint64_t uint64)
      : type_(ArgumentType::kUint64), uint64_(uint64) {}

  explicit ArgumentValue(double d) : type_(ArgumentType::kDouble), double_(d) {}

  explicit ArgumentValue(std::string string) : type_(ArgumentType::kString) {
    new (&string_) std::string(std::move(string));
  }

  explicit ArgumentValue(PointerTag, uintptr_t pointer)
      : type_(ArgumentType::kPointer), uint64_(pointer) {}

  explicit ArgumentValue(KoidTag, mx_koid_t koid)
      : type_(ArgumentType::kKoid), uint64_(koid) {}

  ArgumentValue& Destroy();
  ArgumentValue& Copy(const ArgumentValue& other);

  ArgumentType type_;
  union {
    int32_t int32_;
    uint32_t uint32_;
    int64_t int64_;
    uint64_t uint64_;
    double double_;
    std::string string_;
  };
};

// Named argument and value.
struct Argument {
  explicit Argument(std::string name, ArgumentValue value)
      : name(std::move(name)), value(std::move(value)) {}

  std::string name;
  ArgumentValue value;
};

// Metadata type specific data.
class MetadataData {
 public:
  // Provider info event data.
  struct ProviderInfo {
    ProviderId id;
    std::string name;
  };

  // Provider section event data.
  struct ProviderSection {
    ProviderId id;
  };

  explicit MetadataData(const ProviderInfo& provider_info)
      : type_(MetadataType::kProviderInfo), provider_info_(provider_info) {}

  explicit MetadataData(const ProviderSection& provider_section)
      : type_(MetadataType::kProviderSection),
        provider_section_(provider_section) {}

  const ProviderInfo& GetProviderInfo() const {
    FXL_DCHECK(type_ == MetadataType::kProviderInfo);
    return provider_info_;
  };

  const ProviderSection& GetProviderSection() const {
    FXL_DCHECK(type_ == MetadataType::kProviderSection);
    return provider_section_;
  }

  MetadataData(const MetadataData& other) : type_(other.type_) { Copy(other); }

  ~MetadataData() { Destroy(); }

  MetadataData& operator=(const MetadataData& rhs) {
    return Destroy().Copy(rhs);
  }

  MetadataType type() const { return type_; }

 private:
  MetadataData& Destroy();
  MetadataData& Copy(const MetadataData& other);

  MetadataType type_;
  union {
    ProviderInfo provider_info_;
    ProviderSection provider_section_;
  };
};

// Event type specific data.
class EventData {
 public:
  // Instant event data.
  struct Instant {
    EventScope scope;
  };

  // Counter event data.
  struct Counter {
    uint64_t id;
  };

  // Duration begin event data.
  struct DurationBegin {};

  // Duration end event data.
  struct DurationEnd {};

  // Async begin event data.
  struct AsyncBegin {
    uint64_t id;
  };

  // Async instant event data.
  struct AsyncInstant {
    uint64_t id;
  };

  // Async end event data.
  struct AsyncEnd {
    uint64_t id;
  };

  // Flow begin event data.
  struct FlowBegin {
    uint64_t id;
  };

  // Flow step event data.
  struct FlowStep {
    uint64_t id;
  };

  // Flow end event data.
  struct FlowEnd {
    uint64_t id;
  };

  explicit EventData(const Instant& instant)
      : type_(EventType::kInstant), instant_(instant) {}

  explicit EventData(const Counter& counter)
      : type_(EventType::kCounter), counter_(counter) {}

  explicit EventData(const DurationBegin& duration_begin)
      : type_(EventType::kDurationBegin), duration_begin_(duration_begin) {}

  explicit EventData(const DurationEnd& duration_end)
      : type_(EventType::kDurationEnd), duration_end_(duration_end) {}

  explicit EventData(const AsyncBegin& async_begin)
      : type_(EventType::kAsyncStart), async_begin_(async_begin) {}

  explicit EventData(const AsyncInstant& async_instant)
      : type_(EventType::kAsyncInstant), async_instant_(async_instant) {}

  explicit EventData(const AsyncEnd& async_end)
      : type_(EventType::kAsyncEnd), async_end_(async_end) {}

  explicit EventData(const FlowBegin& flow_begin)
      : type_(EventType::kFlowBegin), flow_begin_(flow_begin) {}

  explicit EventData(const FlowStep& flow_step)
      : type_(EventType::kFlowStep), flow_step_(flow_step) {}

  explicit EventData(const FlowEnd& flow_end)
      : type_(EventType::kFlowEnd), flow_end_(flow_end) {}

  const Instant& GetInstant() const {
    FXL_DCHECK(type_ == EventType::kInstant);
    return instant_;
  }

  const Counter& GetCounter() const {
    FXL_DCHECK(type_ == EventType::kCounter);
    return counter_;
  }

  const DurationBegin& GetDurationBegin() const {
    FXL_DCHECK(type_ == EventType::kDurationBegin);
    return duration_begin_;
  }

  const DurationEnd& GetDurationEnd() const {
    FXL_DCHECK(type_ == EventType::kDurationEnd);
    return duration_end_;
  }

  const AsyncBegin& GetAsyncBegin() const {
    FXL_DCHECK(type_ == EventType::kAsyncStart);
    return async_begin_;
  };

  const AsyncInstant& GetAsyncInstant() const {
    FXL_DCHECK(type_ == EventType::kAsyncInstant);
    return async_instant_;
  }

  const AsyncEnd& GetAsyncEnd() const {
    FXL_DCHECK(type_ == EventType::kAsyncEnd);
    return async_end_;
  }

  const FlowBegin& GetFlowBegin() const {
    FXL_DCHECK(type_ == EventType::kFlowBegin);
    return flow_begin_;
  }

  const FlowStep& GetFlowStep() const {
    FXL_DCHECK(type_ == EventType::kFlowStep);
    return flow_step_;
  }

  const FlowEnd& GetFlowEnd() const {
    FXL_DCHECK(type_ == EventType::kFlowEnd);
    return flow_end_;
  }

  EventType type() const { return type_; }

 private:
  EventType type_;
  union {
    Instant instant_;
    Counter counter_;
    DurationBegin duration_begin_;
    DurationEnd duration_end_;
    AsyncBegin async_begin_;
    AsyncInstant async_instant_;
    AsyncEnd async_end_;
    FlowBegin flow_begin_;
    FlowStep flow_step_;
    FlowEnd flow_end_;
  };
};

// A decoded record.
class Record {
 public:
  // Metadata record data.
  struct Metadata {
    MetadataType type() const { return data.type(); }
    MetadataData data;
  };

  // Initialization record data.
  struct Initialization {
    Ticks ticks_per_second;
  };

  // String record data.
  struct String {
    StringIndex index;
    std::string string;
  };

  // Thread record data.
  struct Thread {
    ThreadIndex index;
    ProcessThread process_thread;
  };

  // Event record data.
  struct Event {
    EventType type() const { return data.type(); }
    Ticks timestamp;
    ProcessThread process_thread;
    std::string category;
    std::string name;
    std::vector<Argument> arguments;
    EventData data;
  };

  // Kernel Object record data.
  struct KernelObject {
    mx_koid_t koid;
    mx_obj_type_t object_type;
    std::string name;
    std::vector<Argument> arguments;
  };

  // Context Switch record data.
  struct ContextSwitch {
    Ticks timestamp;
    CpuNumber cpu_number;
    ThreadState outgoing_thread_state;
    ProcessThread outgoing_thread;
    ProcessThread incoming_thread;
  };

  // Log record data.
  struct Log {
    Ticks timestamp;
    ProcessThread process_thread;
    std::string message;
  };

  explicit Record(const Metadata& record)
      : type_(RecordType::kMetadata), metadata_(record) {}

  explicit Record(const Initialization& record)
      : type_(RecordType::kInitialization), initialization_(record) {}

  explicit Record(const String& record) : type_(RecordType::kString) {
    new (&string_) String(record);
  }

  explicit Record(const Thread& record) : type_(RecordType::kThread) {
    new (&thread_) Thread(record);
  }

  explicit Record(const Event& record) : type_(RecordType::kEvent) {
    new (&event_) Event(record);
  }

  explicit Record(const KernelObject& record)
      : type_(RecordType::kKernelObject) {
    new (&kernel_object_) KernelObject(record);
  }

  explicit Record(const ContextSwitch& record)
      : type_(RecordType::kContextSwitch) {
    new (&context_switch_) ContextSwitch(record);
  }

  explicit Record(const Log& record) : type_(RecordType::kLog) {
    new (&log_) Log(record);
  }

  Record(const Record& other) { Copy(other); }

  ~Record() { Destroy(); }

  Record& operator=(const Record& rhs) { return Destroy().Copy(rhs); }

  const Metadata& GetMetadata() const {
    FXL_DCHECK(type_ == RecordType::kMetadata);
    return metadata_;
  }

  const Initialization& GetInitialization() const {
    FXL_DCHECK(type_ == RecordType::kInitialization);
    return initialization_;
  }

  const String& GetString() const {
    FXL_DCHECK(type_ == RecordType::kString);
    return string_;
  }

  const Thread& GetThread() const {
    FXL_DCHECK(type_ == RecordType::kThread);
    return thread_;
  };

  const Event& GetEvent() const {
    FXL_DCHECK(type_ == RecordType::kEvent);
    return event_;
  }

  const KernelObject& GetKernelObject() const {
    FXL_DCHECK(type_ == RecordType::kKernelObject);
    return kernel_object_;
  }

  const ContextSwitch& GetContextSwitch() const {
    FXL_DCHECK(type_ == RecordType::kContextSwitch);
    return context_switch_;
  }

  const Log& GetLog() const {
    FXL_DCHECK(type_ == RecordType::kLog);
    return log_;
  }

  RecordType type() const { return type_; }

 private:
  Record& Destroy();
  Record& Copy(const Record& other);

  RecordType type_;
  union {
    Metadata metadata_;
    Initialization initialization_;
    String string_;
    Thread thread_;
    Event event_;
    KernelObject kernel_object_;
    ContextSwitch context_switch_;
    Log log_;
  };
};

// Called once for each record read by |ReadRecords|.
// TODO(jeffbrown): It would be nice to get rid of this by making |ReadRecords|
// return std::optional<Record> as an out parameter.
using RecordConsumer = std::function<void(const Record&)>;

// Reads trace records.
class TraceReader {
 public:
  explicit TraceReader(RecordConsumer record_consumer,
                       ErrorHandler error_handler);

  const TraceContext& context() const { return context_; }

  // Reads as many records as possible from the chunk, invoking the
  // record consumer for each one.  Returns true if the stream could possibly
  // contain more records if the chunk were extended with new data.
  // Returns false if the trace stream is unrecoverably corrupt and no
  // further decoding is possible.  May be called repeatedly with new
  // chunks as they become available to resume decoding.
  bool ReadRecords(Chunk& chunk);

 private:
  bool ReadMetadataRecord(Chunk& record,
                          ::tracing::internal::RecordHeader header);
  bool ReadInitializationRecord(Chunk& record,
                                ::tracing::internal::RecordHeader header);
  bool ReadStringRecord(Chunk& record,
                        ::tracing::internal::RecordHeader header);
  bool ReadThreadRecord(Chunk& record,
                        ::tracing::internal::RecordHeader header);
  bool ReadEventRecord(Chunk& record, ::tracing::internal::RecordHeader header);
  bool ReadKernelObjectRecord(Chunk& record,
                              ::tracing::internal::RecordHeader header);
  bool ReadContextSwitchRecord(Chunk& record,
                               ::tracing::internal::RecordHeader header);
  bool ReadLogRecord(Chunk& record, ::tracing::internal::RecordHeader header);
  bool ReadArguments(Chunk& record,
                     size_t count,
                     std::vector<Argument>* out_arguments);

  RecordConsumer record_consumer_;
  TraceContext context_;
  ::tracing::internal::RecordHeader pending_header_ = 0u;
};

}  // namespace reader
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_READER_H_
