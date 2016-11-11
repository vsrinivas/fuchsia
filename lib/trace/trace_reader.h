// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_TRACE_READER_H_
#define APPS_TRACING_LIB_TRACE_TRACE_READER_H_

#include <stdint.h>

#include <iosfwd>
#include <unordered_map>
#include <vector>

#include "apps/tracing/lib/trace/internal/trace_types.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace tracing {
namespace reader {

using ArgumentType = internal::ArgumentType;
using RecordType = internal::RecordType;
using TraceEventType = internal::TraceEventType;

class Chunk {
 public:
  Chunk();
  explicit Chunk(const uint64_t* begin, size_t size);

  bool Read(uint64_t* value);
  bool ReadChunk(size_t num_words, Chunk* out);
  bool ReadString(size_t length, ftl::StringView* out);

 private:
  const uint64_t* current_;
  const uint64_t* end_;
};

class TraceInput {
 public:
  virtual ~TraceInput();
  virtual bool ReadChunk(size_t num_words, Chunk* chunk) = 0;

 protected:
  TraceInput();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(TraceInput);
};

class MemoryTraceInput : public TraceInput {
 public:
  explicit MemoryTraceInput(void* memory, size_t size);

  // |TraceInput| implementation.
  bool ReadChunk(size_t num_words, Chunk* chunk) override;

 private:
  Chunk chunk_;
};

class StreamTraceInput : public TraceInput {
 public:
  explicit StreamTraceInput(std::istream& in);

  // |TraceInput| implementation.
  bool ReadChunk(size_t num_words, Chunk* chunk) override;

 private:
  std::istream& in_;
  std::vector<uint64_t> buffer_;
};

struct Thread {
  explicit operator bool() const {
    return thread_koid != 0 && process_koid != 0;
  }

  uint64_t process_koid;
  uint64_t thread_koid;
};

static_assert(sizeof(Thread) == 2 * sizeof(uint64_t), "");

using TraceErrorHandler = std::function<void(const std::string&)>;

class TraceContext {
 public:
  explicit TraceContext(const TraceErrorHandler& error_handler);

  void OnError(const std::string& error) const;

  ftl::StringView DecodeStringRef(uint16_t string_ref, Chunk& chunk) const;
  void RegisterStringRef(uint16_t index, const ftl::StringView& string);

  Thread DecodeThreadRef(uint16_t thread_ref, Chunk& chunk) const;
  void RegisterThreadRef(uint16_t index, const Thread& thread);

 private:
  TraceErrorHandler error_handler_;
  std::unordered_map<uint16_t, std::string> string_table_;
  std::unordered_map<uint16_t, Thread> thread_table_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceContext);
};

class ArgumentValue {
 public:
  static ArgumentValue MakeNull() { return ArgumentValue(); }

  static ArgumentValue MakeInt32(int32_t value) { return ArgumentValue(value); }

  static ArgumentValue MakeInt64(int64_t value) { return ArgumentValue(value); }

  static ArgumentValue MakeDouble(double value) { return ArgumentValue(value); }

  static ArgumentValue MakeString(const std::string& value) {
    return ArgumentValue(value);
  }

  static ArgumentValue MakePointer(uintptr_t value) {
    return ArgumentValue(PointerTag(), value);
  }

  static ArgumentValue MakeKernelObjectId(uint64_t value) {
    return ArgumentValue(KoidTag(), value);
  }

  ArgumentValue(const ArgumentValue& other) : type_(other.type_) {
    Copy(other);
  }

  ~ArgumentValue() { Destroy(); }

  ArgumentValue& operator=(const ArgumentValue& rhs) {
    return Destroy().Copy(rhs);
  }

  int32_t GetInt32() const {
    FTL_DCHECK(type_ == ArgumentType::kInt32);
    return int32_;
  }

  int64_t GetInt64() const {
    FTL_DCHECK(type_ == ArgumentType::kInt64);
    return int64_;
  }

  double GetDouble() const {
    FTL_DCHECK(type_ == ArgumentType::kDouble);
    return double_;
  }

  const std::string& GetString() const {
    FTL_DCHECK(type_ == ArgumentType::kString);
    return string_;
  }

  uintptr_t GetPointer() const {
    FTL_DCHECK(type_ == ArgumentType::kPointer);
    return uint64_;
  }

  uint64_t GetKernelObjectId() const {
    FTL_DCHECK(type_ == ArgumentType::kKernelObjectId);
    return uint64_;
  }

  ArgumentType type() const { return type_; }

 private:
  struct PointerTag {};
  struct KoidTag {};

  ArgumentValue() : type_(ArgumentType::kNull) {}

  explicit ArgumentValue(int32_t int32)
      : type_(ArgumentType::kInt32), int32_(int32) {}

  explicit ArgumentValue(int64_t int64)
      : type_(ArgumentType::kInt64), int64_(int64) {}

  explicit ArgumentValue(double d) : type_(ArgumentType::kDouble), double_(d) {}

  explicit ArgumentValue(const std::string& string)
      : type_(ArgumentType::kString) {
    new (&string_) std::string(string);
  }

  explicit ArgumentValue(PointerTag, uintptr_t pointer)
      : type_(ArgumentType::kPointer), uint64_(pointer) {}

  explicit ArgumentValue(KoidTag, uint64_t koid)
      : type_(ArgumentType::kKernelObjectId), uint64_(koid) {}

  ArgumentValue& Destroy();
  ArgumentValue& Copy(const ArgumentValue& other);

  ArgumentType type_;
  union {
    int32_t int32_;
    int64_t int64_;
    double double_;
    std::string string_;
    uint64_t uint64_;
  };
};

struct Argument {
  std::string name;
  ArgumentValue value;
};

class ArgumentReader {
 public:
  using Visitor = std::function<void(const Argument&)>;

  bool ForEachArgument(TraceContext& context,
                       size_t argument_count,
                       Chunk& chunk,
                       const Visitor& visitor);

 private:
  bool HandleNullArgument(TraceContext& context,
                          internal::ArgumentHeader header,
                          const ftl::StringView& name,
                          Chunk& chunk,
                          const Visitor& visitor);

  bool HandleInt32Argument(TraceContext& context,
                           internal::ArgumentHeader header,
                           const ftl::StringView& name,
                           Chunk& chunk,
                           const Visitor& visitor);

  bool HandleInt64Argument(TraceContext& context,
                           internal::ArgumentHeader header,
                           const ftl::StringView& name,
                           Chunk& chunk,
                           const Visitor& visitor);

  bool HandleDoubleArgument(TraceContext& context,
                            internal::ArgumentHeader header,
                            const ftl::StringView& name,
                            Chunk& chunk,
                            const Visitor& visitor);

  bool HandleStringArgument(TraceContext& context,
                            internal::ArgumentHeader header,
                            const ftl::StringView& name,
                            Chunk& chunk,
                            const Visitor& visitor);

  bool HandlePointerArgument(TraceContext& context,
                             internal::ArgumentHeader header,
                             const ftl::StringView& name,
                             Chunk& chunk,
                             const Visitor& visitor);

  bool HandleKernelObjectIdArgument(TraceContext& context,
                                    internal::ArgumentHeader header,
                                    const ftl::StringView& name,
                                    Chunk& chunk,
                                    const Visitor& visitor);
};

struct DurationBegin {};

struct DurationEnd {};

struct AsyncBegin {
  uint64_t id;
};
static_assert(sizeof(AsyncBegin) == sizeof(uint64_t), "");

struct AsyncInstant {
  uint64_t id;
};
static_assert(sizeof(AsyncInstant) == sizeof(uint64_t), "");

struct AsyncEnd {
  uint64_t id;
};
static_assert(sizeof(AsyncInstant) == sizeof(uint64_t), "");

class EventData {
 public:
  explicit EventData(const DurationBegin& duration_begin)
      : type_(TraceEventType::kDurationBegin),
        duration_begin_(duration_begin) {}

  explicit EventData(const DurationEnd& duration_end)
      : type_(TraceEventType::kDurationEnd), duration_end_(duration_end) {}

  explicit EventData(const AsyncBegin& async_begin)
      : type_(TraceEventType::kAsyncStart), async_begin_(async_begin) {}

  explicit EventData(const AsyncInstant& async_instant)
      : type_(TraceEventType::kAsyncInstant), async_instant_(async_instant) {}

  explicit EventData(const AsyncEnd& async_end)
      : type_(TraceEventType::kAsyncEnd), async_end_(async_end) {}

  const AsyncBegin& GetAsyncBegin() const {
    FTL_DCHECK(type_ == TraceEventType::kAsyncStart);
    return async_begin_;
  };

  const AsyncInstant& GetAsyncInstant() const {
    FTL_DCHECK(type_ == TraceEventType::kAsyncInstant);
    return async_instant_;
  }

  const AsyncEnd& GetAsyncEnd() const {
    FTL_DCHECK(type_ == TraceEventType::kAsyncEnd);
    return async_end_;
  }

  TraceEventType type() const { return type_; }

 private:
  TraceEventType type_;
  union {
    DurationBegin duration_begin_;
    DurationEnd duration_end_;
    AsyncBegin async_begin_;
    AsyncInstant async_instant_;
    AsyncEnd async_end_;
  };
};

struct InitializationRecord {
  uint64_t ticks_per_second;
};

struct StringRecord {
  uint16_t index;
  std::string string;
};

struct ThreadRecord {
  uint16_t index;
  Thread thread;
};

struct EventRecord {
  TraceEventType event_type;
  uint64_t timestamp;
  Thread thread;
  std::string name;
  std::string cat;
  std::vector<Argument> arguments;
  EventData event_data;
};

class Record {
 public:
  explicit Record(const InitializationRecord& record)
      : type_(RecordType::kInitialization), initialization_record_(record) {}

  explicit Record(const StringRecord& record) : type_(RecordType::kString) {
    new (&string_record_) StringRecord(record);
  }

  explicit Record(const ThreadRecord& record) : type_(RecordType::kThread) {
    new (&thread_record_) ThreadRecord(record);
  }

  explicit Record(const EventRecord& record) : type_(RecordType::kEvent) {
    new (&event_record_) EventRecord(record);
  }

  Record(const Record& other) { Copy(other); }

  ~Record() { Destroy(); }

  Record& operator=(const Record& rhs) { return Destroy().Copy(rhs); }

  const InitializationRecord& GetInitializationRecord() const {
    FTL_DCHECK(type_ == RecordType::kInitialization);
    return initialization_record_;
  }

  const StringRecord& GetStringRecord() const {
    FTL_DCHECK(type_ == RecordType::kString);
    return string_record_;
  }

  const ThreadRecord& GetThreadRecord() const {
    FTL_DCHECK(type_ == RecordType::kThread);
    return thread_record_;
  };

  const EventRecord& GetEventRecord() const {
    FTL_DCHECK(type_ == RecordType::kEvent);
    return event_record_;
  }

  RecordType type() const { return type_; }

 private:
  Record& Destroy();
  Record& Copy(const Record& other);

  RecordType type_;
  union {
    InitializationRecord initialization_record_;
    StringRecord string_record_;
    ThreadRecord thread_record_;
    EventRecord event_record_;
  };
};

using RecordVisitor = std::function<void(const Record&)>;

class TraceReader {
 public:
  explicit TraceReader(const TraceErrorHandler& error_handler,
                       const RecordVisitor& visitor);

  void ForEachRecord(TraceInput& input);

 private:
  void HandleInitializationRecord(internal::RecordHeader header, Chunk& input);

  void HandleStringRecord(internal::RecordHeader header, Chunk& input);

  void HandleThreadRecord(internal::RecordHeader header, Chunk& input);

  void HandleEventRecord(internal::RecordHeader header, Chunk& input);

  TraceContext trace_context_;
  RecordVisitor visitor_;
};

}  // namespace reader
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_TRACE_READER_H_
