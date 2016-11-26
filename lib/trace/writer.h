// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_WRITER_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_WRITER_H_

#include <stdint.h>

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <mx/eventpair.h>
#include <mx/vmo.h>

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/lib/trace/types.h"
#include "lib/ftl/macros.h"

namespace tracing {
namespace internal {
class TraceEngine;
void ReleaseEngine();
}  // namespace internal
namespace writer {

// Describes the final disposition of tracing when it stopped.
enum class TraceDisposition {
  // The trace was stopped using |StopTracing| and finished normally
  // without errors.
  kFinishedNormally,

  // The trace was aborted due to a loss of connection with the trace manager.
  kConnectionLost,

  // The trace was aborted due the trace buffer becoming completely full.
  kBufferExhausted,
};

// Callback which is invoked when tracing stops.
using TraceFinishedCallback = std::function<void(TraceDisposition)>;

// Starts writing trace records into the specified buffer.
//
// |buffer| and |fence| must be valid handles which were provided to
// |TraceProvider.StartTracing| by the |TraceRegistry|.  See |TraceProvider|
// for a description of the use of these handles.
//
// If |enabled_categories| is empty then all events will be written to the
// trace regardless of their category.  Otherwise only those events whose
// category exactly matches one of the strings in |enabled_categories| will
// be written to the trace.
//
// The |finished_callback| will be posted to the current message loop when
// tracing stops and indicate the disposition of the trace.
//
// This function must be called on a thread which has a message loop since
// tracing requires the loop to handle incoming events.
//
// Returns true if tracing started successfully, false if the trace buffer
// could not be mapped.
bool StartTracing(mx::vmo buffer,
                  mx::eventpair fence,
                  std::vector<std::string> enabled_categories,
                  TraceFinishedCallback finished_callback);

// Stops tracing.
//
// Stopping tracing is completed asynchronously once all active |TraceWriter|
// instances have been released (including those which may be in use on
// other threads).
//
// Once finished, the |TraceFinishedCallback| provided to |StartTracing|
// will be posted to the message loop on which |StartTracing| was called.
//
// It is not safe to call |StartTracing| again until the finished callback
// is posted.
//
// This method must be called on the same thread as |StartTracing| was.
void StopTracing();

// Returns true if tracing is active.
//
// This method is thread-safe.
bool IsTracingEnabled();

// Returns true if the tracer has been initialized by a call to |StartTracing|
// and the specified |category| has been enabled.
//
// This method is thread-safe.
bool IsTracingEnabledForCategory(const char* category);

// Gets the list of enabled categories.
// Returns an empty list if tracing is disabled.
//
// This method is thread-safe.
std::vector<std::string> GetEnabledCategories();

// Describes the state of the trace system.
enum class TraceState {
  // Trace has started.
  // Trace handlers should initialize themselves.
  kStarted,
  // Trace is stopping.
  // Trace handlers should finish writing pending events before returning.
  kStopping,
  // Trace has stopped.
  // Trace handlers can no longer write any new events.
  // The tracing system is waiting for all existing |TraceWriter| instances
  // to be released before finishing the trace.
  kStopped,
  // Trace has finished completely.
  // It is now OK to start a new trace session.
  kFinished,
};

// Gets the current trace state.
// To determine if trace events are currently being collected use
// |IsTracingEnabled| or |IsTracingEnabledForCategory| instead.
//
// This method is thread-safe.
TraceState GetTraceState();

// Trace handlers are given an opportunity to directly participate in the
// tracing lifecycle.  This mechanism can be useful for controlling
// heavy-weight tracing functions which require explicit coordination
// to start and stop.
using TraceHandler = std::function<void(TraceState)>;
using TraceHandlerKey = uint64_t;

// Adds a trace handler.
// The handler will be notified of all future change to the tracing state.
// Callbacks will occur on the thread which originally called |StartTracing|.
//
// This method is thread-safe.
TraceHandlerKey AddTraceHandler(TraceHandler handler);

// Removes a trace handler.
//
// This method is thread-safe.
void RemoveTraceHandler(TraceHandlerKey handler_key);

// Provides support for writing sequences of 64-bit words into a buffer.
class Payload {
 public:
  explicit Payload(uint64_t* ptr) : ptr_(ptr) {}

  explicit operator bool() const { return ptr_; }

  Payload& Write(uint64_t value) {
    *ptr_++ = value;
    return *this;
  }

  Payload& WriteInt64(int64_t value) {
    *reinterpret_cast<int64_t*>(ptr_++) = value;
    return *this;
  }

  Payload& WriteDouble(double value) {
    *reinterpret_cast<double*>(ptr_++) = value;
    return *this;
  }

  Payload& WriteBytes(const void* src, size_t length) {
    memcpy(ptr_, src, length);
    ptr_ += ::tracing::internal::BytesToWords(::tracing::internal::Pad(length));
    return *this;
  }

  template <typename T>
  Payload& WriteValue(const T& value) {
    value.WriteTo(*this);
    return *this;
  }

  template <typename Head, typename... Tail>
  Payload& WriteValues(Head&& head, Tail&&... tail) {
    WriteValue(head);
    return WriteValues(std::forward<Tail>(tail)...);
  }

  Payload& WriteValues() { return *this; }

 private:
  uint64_t* ptr_;
};

// A string reference which is either encoded inline or indirectly by
// string table index.
class StringRef {
 public:
  static StringRef MakeEmpty() {
    return StringRef(::tracing::internal::StringRefFields::kEmpty, nullptr);
  }

  static StringRef MakeInlinedOrEmpty(const char* string, size_t length) {
    if (!length)
      return MakeEmpty();
    size_t trim = std::min(
        length, size_t(::tracing::internal::StringRefFields::kMaxLength));
    return StringRef(
        static_cast<EncodedStringRef>(
            trim | ::tracing::internal::StringRefFields::kInlineFlag),
        string);
  }

  // Constructs a |StringRef| from a std::string.
  // Lifetime of |string| must exceed the lifetime of the
  // returned |StringRef| instance.
  static StringRef MakeInlinedOrEmpty(const std::string& string) {
    return MakeInlinedOrEmpty(string.c_str(), string.size());
  }

  static StringRef MakeIndexed(StringIndex index) {
    return StringRef(index, nullptr);
  }

  bool is_empty() const {
    return encoded_value_ == ::tracing::internal::StringRefFields::kEmpty;
  }
  bool is_inlined() const {
    return encoded_value_ & ::tracing::internal::StringRefFields::kInlineFlag;
  }
  bool is_indexed() const { return !is_empty() && !is_inlined(); }

  EncodedStringRef encoded_value() const { return encoded_value_; }
  const char* inline_string() const { return inline_string_; }

  size_t Size() const {
    return is_inlined() ? ::tracing::internal::Pad(
                              encoded_value_ &
                              ::tracing::internal::StringRefFields::kLengthMask)
                        : 0;
  }

  void WriteTo(Payload& payload) const {
    if (is_inlined()) {
      payload.WriteBytes(
          inline_string_,
          encoded_value_ & ::tracing::internal::StringRefFields::kLengthMask);
    }
  }

 private:
  explicit StringRef(EncodedStringRef encoded_value, const char* inline_string)
      : encoded_value_(encoded_value), inline_string_(inline_string) {}

  EncodedStringRef encoded_value_;
  const char* inline_string_;
};

// A thread reference which is either encoded inline or indirectly by
// thread table index.
class ThreadRef {
 public:
  static ThreadRef MakeInlined(mx_koid_t process_koid, mx_koid_t thread_koid) {
    return ThreadRef(::tracing::internal::ThreadRefFields::kInline,
                     process_koid, thread_koid);
  }

  static ThreadRef MakeIndexed(ThreadIndex index) {
    return ThreadRef(index, 0u, 0u);
  }

  bool is_inlined() const {
    return encoded_value_ == ::tracing::internal::ThreadRefFields::kInline;
  }

  EncodedThreadRef encoded_value() const { return encoded_value_; }
  mx_koid_t inline_process_koid() const { return inline_process_koid_; }
  mx_koid_t inline_thread_koid() const { return inline_thread_koid_; }

  size_t Size() const { return is_inlined() ? 2 * sizeof(uint64_t) : 0; }

  void WriteTo(Payload& payload) const {
    if (is_inlined())
      payload.Write(inline_process_koid_).Write(inline_thread_koid_);
  }

 private:
  explicit ThreadRef(EncodedThreadRef encoded_value,
                     mx_koid_t inline_process_koid,
                     mx_koid_t inline_thread_koid)
      : encoded_value_(encoded_value),
        inline_process_koid_(inline_process_koid),
        inline_thread_koid_(inline_thread_koid) {}

  EncodedThreadRef encoded_value_;
  mx_koid_t inline_process_koid_;
  mx_koid_t inline_thread_koid_;
};

// Represents a named argument and value pair.
class Argument {
 public:
  explicit Argument(StringRef name_ref) : name_ref_(std::move(name_ref)) {}

  size_t Size() const { return sizeof(uint64_t) + name_ref_.Size(); }

 protected:
  void WriteTo(Payload& payload,
               ArgumentType type,
               size_t size,
               ::tracing::internal::ArgumentHeader header_extras = 0u) const {
    payload
        .Write(
            ::tracing::internal::ArgumentFields::Type::Make(
                ::tracing::internal::ToUnderlyingType(type)) |
            ::tracing::internal::ArgumentFields::ArgumentSize::Make(size >> 3) |
            ::tracing::internal::ArgumentFields::NameRef::Make(
                name_ref_.encoded_value()) |
            header_extras)
        .WriteValue(name_ref_);
  }

 private:
  StringRef const name_ref_;
};

class NullArgument : public Argument {
 public:
  explicit NullArgument(StringRef name_ref) : Argument(std::move(name_ref)) {}

  size_t Size() const { return Argument::Size(); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kNull, Size());
  }
};

class Int32Argument : public Argument {
 public:
  explicit Int32Argument(StringRef name_ref, int32_t value)
      : Argument(std::move(name_ref)), value_(value) {}

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(
        payload, ArgumentType::kInt32, Size(),
        ::tracing::internal::Int32ArgumentFields::Value::Make(value_));
  }

 private:
  int32_t const value_;
};

class Int64Argument : public Argument {
 public:
  explicit Int64Argument(StringRef name_ref, int64_t value)
      : Argument(std::move(name_ref)), value_(value) {}

  size_t Size() const { return Argument::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kInt64, Size());
    payload.WriteInt64(value_);
  }

 private:
  int64_t const value_;
};

class DoubleArgument : public Argument {
 public:
  explicit DoubleArgument(StringRef name_ref, double value)
      : Argument(std::move(name_ref)), value_(value) {}

  size_t Size() const { return Argument::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kDouble, Size());
    payload.WriteDouble(value_);
  }

 private:
  double const value_;
};

class StringArgument : public Argument {
 public:
  explicit StringArgument(StringRef name_ref, StringRef value_ref)
      : Argument(std::move(name_ref)), value_ref_(std::move(value_ref)) {}

  size_t Size() const { return Argument::Size() + value_ref_.Size(); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kString, Size(),
                      ::tracing::internal::StringArgumentFields::Index::Make(
                          value_ref_.encoded_value()));
    payload.WriteValue(value_ref_);
  }

 private:
  StringRef const value_ref_;
};

class RetainedStringArgument : public Argument {
 public:
  explicit RetainedStringArgument(StringRef name_ref, std::string value)
      : Argument(std::move(name_ref)),
        retained_value_(std::move(value)),
        value_ref_(StringRef::MakeInlinedOrEmpty(retained_value_)) {}

  size_t Size() const { return Argument::Size() + value_ref_.Size(); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kString, Size(),
                      ::tracing::internal::StringArgumentFields::Index::Make(
                          value_ref_.encoded_value()));
    payload.WriteValue(value_ref_);
  }

 private:
  std::string const retained_value_;
  StringRef const value_ref_;
};

class PointerArgument : public Argument {
 public:
  explicit PointerArgument(StringRef name_ref, uintptr_t value)
      : Argument(std::move(name_ref)), value_(value) {}

  size_t Size() const { return Argument::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kPointer, Size());
    payload.Write(value_);
  }

 private:
  uintptr_t const value_;
};

class KoidArgument : public Argument {
 public:
  explicit KoidArgument(StringRef name_ref, mx_koid_t koid)
      : Argument(std::move(name_ref)), koid_(koid) {}

  size_t Size() const { return Argument::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kKoid, Size());
    payload.Write(koid_);
  }

 private:
  mx_koid_t const koid_;
};

// Gets the total size of a list of arguments.
constexpr size_t SizeArguments() {
  return 0;
}

template <typename Head, typename... Tail>
constexpr size_t SizeArguments(Head&& head, Tail&&... tail) {
  return head.Size() + SizeArguments(std::forward<Tail>(tail)...);
}

// Scope for writing one or more related trace records.
// The trace infrastructure keeps track of how many pending trace writers
// exist and waits for them to be destroyed before shutting them down.
//
// Instances of this class are thread-safe.
class TraceWriter {
 public:
  // Releases the trace writer.
  //
  // Note: This is non-virtual to avoid initialization of a vtable.
  // The only subclass is |CategorizedTraceWriter| which only adds POD members
  // and is final.  Keeping this inline to avoid a function call when tracing
  // is inactive.
  ~TraceWriter() {
    if (engine_)
      ::tracing::internal::ReleaseEngine();
  }

  // Prepares to write trace records.
  // If tracing is enabled, returns a valid |TraceWriter|.
  static TraceWriter Prepare();

  // Returns true if the trace writer is valid.
  // It is illegal to call functions on an invalid trace writer.
  explicit operator bool() const { return engine_; }

  // Registers a constant string in the string table.
  // The |constant| is not copied; it must outlive the trace engine.
  StringRef RegisterString(const char* constant);

  // Registers the current thread in the thread table.
  ThreadRef RegisterCurrentThread();

  // Writes an initialization record into the trace buffer.
  // Discards the record if it cannot be written.
  void WriteInitializationRecord(uint64_t ticks_per_second);

  // Writes a string record into the trace buffer.
  // The |value| will be copied into the string record.
  // Discards the record if it cannot be written.
  void WriteStringRecord(StringIndex index, const char* value);

  // Writes a thread record into the trace buffer.
  // Discards the record if it cannot be written.
  void WriteThreadRecord(ThreadIndex index,
                         mx_koid_t process_koid,
                         mx_koid_t thread_koid);

  // Writes a kernel object record about |handle| into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteKernelObjectRecord(mx_handle_t handle, Args&&... args) {
    if (Payload payload = WriteKernelObjectRecordBase(
            handle, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...))) {
      payload.WriteValues(std::forward<Args>(args)...);
    }
  }

 private:
  // Private friend (instead of protected) to prevent subclassing of
  // TraceWriter by anyone else since it could be unsafe.
  friend class CategorizedTraceWriter;

  explicit TraceWriter(::tracing::internal::TraceEngine* engine)
      : engine_(engine) {}

  Payload WriteKernelObjectRecordBase(mx_handle_t handle,
                                      size_t argument_count,
                                      size_t payload_size);

  ::tracing::internal::TraceEngine* const engine_;
};

// Writes events in a particular category.
//
// Instances of this class are thread-safe.
class CategorizedTraceWriter final : public TraceWriter {
 public:
  // Prepares to write trace records for a specified event category.
  // If tracing is enabled for the category, returns a valid |TraceWriter|
  // which is bound to the category.
  // The |category_constant| is not copied; it must outlive the trace engine.
  static CategorizedTraceWriter Prepare(const char* category_constant);

  // Writes an instant event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteInstantEventRecord(const char* name,
                               EventScope scope,
                               Args&&... args) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kInstant, name, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
      payload.WriteValues(std::forward<Args>(args)...)
          .Write(::tracing::internal::ToUnderlyingType(scope));
    }
  }

  // Writes a counter event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteCounterEventRecord(const char* name, uint64_t id, Args&&... args) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kCounter, name, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
      payload.WriteValues(std::forward<Args>(args)...).Write(id);
    }
  }

  // Writes a duration begin event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteDurationBeginEventRecord(const char* name, Args&&... args) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kDurationBegin, name, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...))) {
      payload.WriteValues(std::forward<Args>(args)...);
    }
  }

  // Writes a duration end event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteDurationEndEventRecord(const char* name, Args&&... args) {
    if (Payload payload =
            WriteEventRecordBase(EventType::kDurationEnd, name, sizeof...(Args),
                                 SizeArguments(std::forward<Args>(args)...))) {
      payload.WriteValues(std::forward<Args>(args)...);
    }
  }

  // Writes an asynchronous begin event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteAsyncBeginEventRecord(const char* name,
                                  uint64_t id,
                                  Args&&... args) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kAsyncStart, name, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
      payload.WriteValues(std::forward<Args>(args)...).Write(id);
    }
  }

  // Writes an asynchronous instant event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteAsyncInstantEventRecord(const char* name,
                                    uint64_t id,
                                    Args&&... args) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kAsyncInstant, name, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
      payload.WriteValues(std::forward<Args>(args)...).Write(id);
    }
  }

  // Writes an asynchronous end event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteAsyncEndEventRecord(const char* name, uint64_t id, Args&&... args) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kAsyncEnd, name, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
      payload.WriteValues(std::forward<Args>(args)...).Write(id);
    }
  }

 protected:
  explicit CategorizedTraceWriter(::tracing::internal::TraceEngine* engine,
                                  const StringRef& category_ref)
      : TraceWriter(engine), category_ref_(category_ref) {}

  Payload WriteEventRecordBase(EventType type,
                               const char* name,
                               size_t argument_count,
                               size_t payload_size);

  StringRef const category_ref_;
};

// Helps construct named arguments using SFINAE to coerce types.
template <typename T, typename Enable = void>
struct ArgumentMaker;

template <>
struct ArgumentMaker<nullptr_t> {
  using ResultType = NullArgument;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         nullptr_t value) {
    return ResultType(std::move(name_ref));
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_integral<T>::value &&
                            (sizeof(T) <= sizeof(int32_t))>::type> {
  using ResultType = Int32Argument;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         const T& value) {
    return ResultType(std::move(name_ref), static_cast<int32_t>(value));
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_integral<T>::value &&
                            (sizeof(T) > sizeof(int32_t)) &&
                            (sizeof(T) <= sizeof(int64_t))>::type> {
  using ResultType = Int64Argument;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         const T& value) {
    return ResultType(std::move(name_ref), static_cast<int64_t>(value));
  }
};

template <typename T>
struct ArgumentMaker<T, typename std::enable_if<std::is_enum<T>::value>::type> {
  using UnderlyingType = typename std::underlying_type<T>::type;
  using ResultType = typename ArgumentMaker<UnderlyingType>::ResultType;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         const T& value) {
    return ArgumentMaker<UnderlyingType>::Make(
        writer, std::move(name_ref), static_cast<UnderlyingType>(value));
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_floating_point<T>::value>::type> {
  using ResultType = DoubleArgument;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         const T& value) {
    return ResultType(std::move(name_ref), static_cast<double>(value));
  }
};

template <size_t n>
struct ArgumentMaker<char[n]> {
  using ResultType = StringArgument;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         const char* value) {
    return ResultType(std::move(name_ref), writer.RegisterString(value));
  }
};

template <>
struct ArgumentMaker<const char*> {
  using ResultType = StringArgument;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         const char* value) {
    return ResultType(std::move(name_ref), writer.RegisterString(value));
  }
};

template <>
struct ArgumentMaker<std::string> {
  using ResultType = RetainedStringArgument;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         std::string value) {
    return ResultType(std::move(name_ref), std::move(value));
  }
};

template <typename T>
struct ArgumentMaker<T*> {
  using ResultType = PointerArgument;
  static ResultType Make(TraceWriter& writer,
                         StringRef name_ref,
                         const T* pointer) {
    return ResultType(std::move(name_ref),
                      reinterpret_cast<uintptr_t>(pointer));
  }
};

template <>
struct ArgumentMaker<Koid> {
  using ResultType = KoidArgument;
  static ResultType Make(TraceWriter& writer, StringRef name_ref, Koid koid) {
    return ResultType(std::move(name_ref), koid.value);
  }
};

// Makes an argument with given name and value.
template <typename T>
typename ArgumentMaker<T>::ResultType MakeArgument(TraceWriter& writer,
                                                   const char* name,
                                                   const T& value) {
  return ArgumentMaker<T>::Make(writer, writer.RegisterString(name), value);
}

// When destroyed, writes the end of a duration event.
class DurationEventScope {
 public:
  explicit DurationEventScope(const char* category, const char* name)
      : category_(category), name_(name) {}

  ~DurationEventScope() {
    auto writer = CategorizedTraceWriter::Prepare(category_);
    if (writer)
      writer.WriteDurationEndEventRecord(name_);
  }

  const char* category() const { return category_; }
  const char* name() const { return name_; }

 private:
  const char* const category_;
  const char* const name_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DurationEventScope);
};

}  // namespace writer
}  // namespace tracing

#define TRACE_INTERNAL_ENABLED() ::tracing::writer::IsTracingEnabled()
#define TRACE_INTERNAL_CATEGORY_ENABLED(category) \
  ::tracing::writer::IsTracingEnabledForCategory(category)

#define TRACE_INTERNAL_SCOPE_LABEL__(token) __trace_scope_##token
#define TRACE_INTERNAL_SCOPE_LABEL_(token) TRACE_INTERNAL_SCOPE_LABEL__(token)
#define TRACE_INTERNAL_SCOPE_LABEL() TRACE_INTERNAL_SCOPE_LABEL_(__COUNTER__)

#define TRACE_INTERNAL_WRITER __trace_writer

#define TRACE_INTERNAL_MAKE_ARG(key, value) \
  , MakeArgument(TRACE_INTERNAL_WRITER, key, value)
#define TRACE_INTERNAL_MAKE_ARGS1(k1, v1) TRACE_INTERNAL_MAKE_ARG(k1, v1)
#define TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2) \
  TRACE_INTERNAL_MAKE_ARGS1(k1, v1) TRACE_INTERNAL_MAKE_ARG(k2, v2)
#define TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2) TRACE_INTERNAL_MAKE_ARG(k3, v3)
#define TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3)               \
  TRACE_INTERNAL_MAKE_ARG(k4, v4)

#define TRACE_INTERNAL_SIMPLE(stmt)                                         \
  do {                                                                      \
    auto TRACE_INTERNAL_WRITER = ::tracing::writer::TraceWriter::Prepare(); \
    if (TRACE_INTERNAL_WRITER) {                                            \
      stmt;                                                                 \
    }                                                                       \
  } while (0)

#define TRACE_INTERNAL_CATEGORIZED(category, stmt)                    \
  do {                                                                \
    auto TRACE_INTERNAL_WRITER =                                      \
        ::tracing::writer::CategorizedTraceWriter::Prepare(category); \
    if (TRACE_INTERNAL_WRITER) {                                      \
      stmt;                                                           \
    }                                                                 \
  } while (0)

#define TRACE_INTERNAL_DURATION_SCOPE(scope_label, scope_category, scope_name, \
                                      args...)                                 \
  ::tracing::writer::DurationEventScope scope_label(scope_category,            \
                                                    scope_name);               \
  TRACE_INTERNAL_CATEGORIZED(                                                  \
      scope_label.category(),                                                  \
      TRACE_INTERNAL_WRITER.WriteDurationBeginEventRecord(scope_label.name()   \
                                                              args))

#define TRACE_INTERNAL_INSTANT(category, name, scope, args...) \
  TRACE_INTERNAL_CATEGORIZED(                                  \
      category,                                                \
      TRACE_INTERNAL_WRITER.WriteInstantEventRecord(name, scope args))

#define TRACE_INTERNAL_COUNTER(category, name, id, args...) \
  TRACE_INTERNAL_CATEGORIZED(                               \
      category, TRACE_INTERNAL_WRITER.WriteCounterEventRecord(name, id args))

#define TRACE_INTERNAL_DURATION(scope_category, scope_name, args...)          \
  TRACE_INTERNAL_DURATION_SCOPE(TRACE_INTERNAL_SCOPE_LABEL(), scope_category, \
                                scope_name, args)

#define TRACE_INTERNAL_DURATION_BEGIN(category, name, args...) \
  TRACE_INTERNAL_CATEGORIZED(                                  \
      category,                                                \
      TRACE_INTERNAL_WRITER.WriteDurationBeginEventRecord(name args))

#define TRACE_INTERNAL_DURATION_END(category, name, args...) \
  TRACE_INTERNAL_CATEGORIZED(                                \
      category, TRACE_INTERNAL_WRITER.WriteDurationEndEventRecord(name args))

#define TRACE_INTERNAL_ASYNC_BEGIN(category, name, id, args...) \
  TRACE_INTERNAL_CATEGORIZED(                                   \
      category,                                                 \
      TRACE_INTERNAL_WRITER.WriteAsyncBeginEventRecord(name, id args))

#define TRACE_INTERNAL_ASYNC_INSTANT(category, name, id, args...) \
  TRACE_INTERNAL_CATEGORIZED(                                     \
      category,                                                   \
      TRACE_INTERNAL_WRITER.WriteAsyncInstantEventRecord(name, id args))

#define TRACE_INTERNAL_ASYNC_END(category, name, id, args...) \
  TRACE_INTERNAL_CATEGORIZED(                                 \
      category, TRACE_INTERNAL_WRITER.WriteAsyncEndEventRecord(name, id args))

#define TRACE_INTERNAL_HANDLE(handle, args...) \
  TRACE_INTERNAL_SIMPLE(                       \
      TRACE_INTERNAL_WRITER.WriteKernelObjectRecord(handle args))

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_WRITER_H_
