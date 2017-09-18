// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_WRITER_H_
#define APPS_TRACING_LIB_TRACE_WRITER_H_

#include <stdint.h>

#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <zx/eventpair.h>
#include <zx/vmo.h>

#include "garnet/lib/trace/cwriter.h"
#include "garnet/lib/trace/internal/fields.h"
#include "garnet/lib/trace/types.h"
#include "lib/fxl/macros.h"

namespace tracing {

namespace internal {
class TraceEngine;
TraceEngine* AcquireEngine();
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
bool StartTracing(zx::vmo buffer,
                  zx::eventpair fence,
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

  Payload& WriteUint64(uint64_t value) {
    *reinterpret_cast<uint64_t*>(ptr_++) = value;
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

  explicit StringRef(const ctrace_stringref_t& sr)
    : StringRef(sr.encoded_value, sr.inline_string) {}

  bool is_empty() const {
    return ref_.encoded_value == ::tracing::internal::StringRefFields::kEmpty;
  }
  bool is_inlined() const {
    return ref_.encoded_value & ::tracing::internal::StringRefFields::kInlineFlag;
  }
  bool is_indexed() const { return !is_empty() && !is_inlined(); }

  const ctrace_stringref_t& c_ref() const { return ref_; }
  EncodedStringRef encoded_value() const { return ref_.encoded_value; }
  const char* inline_string() const { return ref_.inline_string; }

  size_t Size() const {
    return is_inlined() ? ::tracing::internal::Pad(
                              ref_.encoded_value &
                              ::tracing::internal::StringRefFields::kLengthMask)
                        : 0;
  }

  void WriteTo(Payload& payload) const {
    if (is_inlined()) {
      payload.WriteBytes(
          ref_.inline_string,
          ref_.encoded_value & ::tracing::internal::StringRefFields::kLengthMask);
    }
  }

 private:
  explicit StringRef(EncodedStringRef encoded_value, const char* inline_string) {
    ref_.encoded_value = encoded_value;
    ref_.inline_string = inline_string;
  }

  ctrace_stringref_t ref_;
};

// A thread reference which is either encoded inline or indirectly by
// thread table index.
class ThreadRef {
 public:
  static ThreadRef MakeInlined(zx_koid_t process_koid, zx_koid_t thread_koid) {
    return ThreadRef(::tracing::internal::ThreadRefFields::kInline,
                     process_koid, thread_koid);
  }

  static ThreadRef MakeIndexed(ThreadIndex index) {
    return ThreadRef(index, 0u, 0u);
  }

  static ThreadRef MakeUnknown() { return MakeInlined(0u, 0u); }

  explicit ThreadRef(const ctrace_threadref_t& tr)
    : ThreadRef(tr.encoded_value, tr.inline_process_koid, tr.inline_thread_koid) {}

  bool is_inlined() const {
    return ref_.encoded_value == ::tracing::internal::ThreadRefFields::kInline;
  }

  bool is_unknown() const {
    return is_inlined() && !ref_.inline_process_koid && !ref_.inline_thread_koid;
  }

  const ctrace_threadref_t& c_ref() const { return ref_; }
  EncodedThreadRef encoded_value() const { return ref_.encoded_value; }
  zx_koid_t inline_process_koid() const { return ref_.inline_process_koid; }
  zx_koid_t inline_thread_koid() const { return ref_.inline_thread_koid; }

  size_t Size() const { return is_inlined() ? 2 * sizeof(uint64_t) : 0; }

  void WriteTo(Payload& payload) const {
    if (is_inlined())
      payload.Write(ref_.inline_process_koid).Write(ref_.inline_thread_koid);
  }

 private:
  explicit ThreadRef(EncodedThreadRef encoded_value,
                     zx_koid_t inline_process_koid,
                     zx_koid_t inline_thread_koid) {
    ref_.encoded_value = encoded_value;
    ref_.inline_process_koid = inline_process_koid;
    ref_.inline_thread_koid = inline_thread_koid;
  }

  ctrace_threadref_t ref_;
};

// Represents a named argument and value pair.
class Argument {
 public:
  explicit Argument(const StringRef& name_ref) : name_ref_(name_ref) {}

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
  explicit NullArgument(const StringRef& name_ref) : Argument(name_ref) {}

  size_t Size() const { return Argument::Size(); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kNull, Size());
  }
};

class Int32Argument : public Argument {
 public:
  explicit Int32Argument(const StringRef& name_ref, int32_t value)
      : Argument(name_ref), value_(value) {}

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(
        payload, ArgumentType::kInt32, Size(),
        ::tracing::internal::Int32ArgumentFields::Value::Make(value_));
  }

 private:
  int32_t const value_;
};

class Uint32Argument : public Argument {
 public:
  explicit Uint32Argument(const StringRef& name_ref, uint32_t value)
      : Argument(name_ref), value_(value) {}

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(
        payload, ArgumentType::kUint32, Size(),
        ::tracing::internal::Uint32ArgumentFields::Value::Make(value_));
  }

 private:
  uint32_t const value_;
};

class Int64Argument : public Argument {
 public:
  explicit Int64Argument(const StringRef& name_ref, int64_t value)
      : Argument(name_ref), value_(value) {}

  size_t Size() const { return Argument::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kInt64, Size());
    payload.WriteInt64(value_);
  }

 private:
  int64_t const value_;
};

class Uint64Argument : public Argument {
 public:
  explicit Uint64Argument(const StringRef& name_ref, uint64_t value)
      : Argument(name_ref), value_(value) {}

  size_t Size() const { return Argument::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kUint64, Size());
    payload.WriteUint64(value_);
  }

 private:
  uint64_t const value_;
};

class DoubleArgument : public Argument {
 public:
  explicit DoubleArgument(const StringRef& name_ref, double value)
      : Argument(name_ref), value_(value) {}

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
  explicit StringArgument(const StringRef& name_ref, StringRef value_ref)
      : Argument(name_ref), value_ref_(std::move(value_ref)) {}

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
  explicit RetainedStringArgument(const StringRef& name_ref, std::string value)
      : Argument(name_ref),
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
  explicit PointerArgument(const StringRef& name_ref, uintptr_t value)
      : Argument(name_ref), value_(value) {}

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
  explicit KoidArgument(const StringRef& name_ref, zx_koid_t koid)
      : Argument(name_ref), koid_(koid) {}

  size_t Size() const { return Argument::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kKoid, Size());
    payload.Write(koid_);
  }

 private:
  zx_koid_t const koid_;
};

// Represents a list of arguments.
template <typename... Args>
class ArgumentList;

template <>
class ArgumentList<> {
 public:
  constexpr size_t Size() const { return 0u; }

  constexpr size_t ElementCount() const { return 0u; }

  constexpr void WriteTo(Payload& payload) const {}
};

template <typename Arg, typename ArgList>
class ArgumentList<Arg, ArgList> {
 public:
  explicit constexpr ArgumentList(Arg arg, ArgList next)
      : arg_(std::move(arg)), next_(std::move(next)) {}

  constexpr size_t Size() const { return arg_.Size() + next_.Size(); }

  constexpr size_t ElementCount() const { return 1 + next_.ElementCount(); }

  void WriteTo(Payload& payload) const {
    arg_.WriteTo(payload);
    next_.WriteTo(payload);
  }

 private:
  const Arg arg_;
  const ArgList next_;
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
class TraceWriter final {
 public:
  TraceWriter(TraceWriter&& other) : engine_(other.engine_) {
    other.engine_ = nullptr;
  }

  // Releases the trace writer.
  ~TraceWriter() {
    if (engine_)
      ::tracing::internal::ReleaseEngine();
  }

  TraceWriter& operator=(TraceWriter&& other) {
    engine_ = other.engine_;
    other.engine_ = nullptr;
    return *this;
  }

  // Prepares to write trace records.
  // If tracing is enabled, returns a valid |TraceWriter|.
  static TraceWriter Prepare();

  // Returns true if the trace writer is valid.
  // It is illegal to call functions on an invalid trace writer.
  explicit operator bool() const { return engine_; }

  // Registers the string into the string table without copying its contents.
  // The |constant| is not copied; it must outlive the trace session.
  // If |check_category| is true, returns an empty string ref if the string
  // is not one of the enabled categories.
  StringRef RegisterString(const char* constant, bool check_category = false);

  // Registers a non-constant string into the string table and copies it.
  StringRef RegisterStringCopy(const std::string& string);

  // Registers the current thread into the thread table and automatically
  // writes its description.  Automatically writes a description of the
  // current process and thread when first used.
  ThreadRef RegisterCurrentThread();

  // Registers process and thread id into the thread table.
  // The caller is responsible for writing the process and thread descriptions
  // separately if needed.
  ThreadRef RegisterThread(zx_koid_t process_koid, zx_koid_t thread_koid);

  // Writes a description of the specified process.
  void WriteProcessDescription(zx_koid_t process_koid,
                               const std::string& process_name);

  // Writes a description of the specified thread.
  void WriteThreadDescription(zx_koid_t process_koid,
                              zx_koid_t thread_koid,
                              const std::string& thread_name);

  // Writes a kernel object record about |handle| into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteKernelObjectRecord(zx_handle_t handle,
                               const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteKernelObjectRecordBase(
            handle, args.ElementCount(), args.Size())) {
      payload.WriteValue(args);
    }
  }

  // Writes a context switch record into the trace buffer.
  // Discards the record if it cannot be written.
  void WriteContextSwitchRecord(Ticks event_time,
                                CpuNumber cpu_number,
                                ThreadState outgoing_thread_state,
                                const ThreadRef& outgoing_thread_ref,
                                const ThreadRef& incoming_thread_ref);

  // Writes a log record into the trace buffer.
  // Discards the record if it cannot be written or if
  // log_message == nullptr.
  void WriteLogRecord(Ticks event_time,
                      const ThreadRef& thread_ref,
                      const char* log_message,
                      size_t log_message_length);

  // Writes an instant event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteInstantEventRecord(Ticks event_time,
                               const ThreadRef& thread_ref,
                               const StringRef& category_ref,
                               const StringRef& name_ref,
                               EventScope scope,
                               const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kInstant, event_time, thread_ref, category_ref, name_ref,
            args.ElementCount(), args.Size() + sizeof(uint64_t))) {
      payload.WriteValue(args).Write(
          ::tracing::internal::ToUnderlyingType(scope));
    }
  }

  // Writes a counter event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteCounterEventRecord(Ticks event_time,
                               const ThreadRef& thread_ref,
                               const StringRef& category_ref,
                               const StringRef& name_ref,
                               uint64_t id,
                               const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kCounter, event_time, thread_ref, category_ref, name_ref,
            args.ElementCount(), args.Size() + sizeof(uint64_t))) {
      payload.WriteValue(args).Write(id);
    }
  }

  // Writes a duration event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteDurationEventRecord(Ticks start_time,
                                Ticks end_time,
                                const ThreadRef& thread_ref,
                                const StringRef& category_ref,
                                const StringRef& name_ref,
                                const ArgumentList<Args...>& args = {}) {
    // For the moment we just write begin and end records.  There's an
    // opportunity to save space by creating a new record type which coalesces
    // the two.
    WriteDurationBeginEventRecord(start_time, thread_ref, category_ref,
                                  name_ref, std::forward(args));
    WriteDurationEndEventRecord(end_time, thread_ref, category_ref, name_ref);
  }

  // Writes a duration begin event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteDurationBeginEventRecord(Ticks event_time,
                                     const ThreadRef& thread_ref,
                                     const StringRef& category_ref,
                                     const StringRef& name_ref,
                                     const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kDurationBegin, event_time, thread_ref, category_ref,
            name_ref, args.ElementCount(), args.Size())) {
      payload.WriteValue(args);
    }
  }

  // Writes a duration end event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteDurationEndEventRecord(Ticks event_time,
                                   const ThreadRef& thread_ref,
                                   const StringRef& category_ref,
                                   const StringRef& name_ref,
                                   const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kDurationEnd, event_time, thread_ref, category_ref,
            name_ref, args.ElementCount(), args.Size())) {
      payload.WriteValue(args);
    }
  }

  // Writes an asynchronous begin event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteAsyncBeginEventRecord(Ticks event_time,
                                  const ThreadRef& thread_ref,
                                  const StringRef& category_ref,
                                  const StringRef& name_ref,
                                  uint64_t id,
                                  const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kAsyncStart, event_time, thread_ref, category_ref,
            name_ref, args.ElementCount(), args.Size() + sizeof(uint64_t))) {
      payload.WriteValue(args).Write(id);
    }
  }

  // Writes an asynchronous instant event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteAsyncInstantEventRecord(Ticks event_time,
                                    const ThreadRef& thread_ref,
                                    const StringRef& category_ref,
                                    const StringRef& name_ref,
                                    uint64_t id,
                                    const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kAsyncInstant, event_time, thread_ref, category_ref,
            name_ref, args.ElementCount(), args.Size() + sizeof(uint64_t))) {
      payload.WriteValue(args).Write(id);
    }
  }

  // Writes an asynchronous end event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteAsyncEndEventRecord(Ticks event_time,
                                const ThreadRef& thread_ref,
                                const StringRef& category_ref,
                                const StringRef& name_ref,
                                uint64_t id,
                                const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kAsyncEnd, event_time, thread_ref, category_ref,
            name_ref, args.ElementCount(), args.Size() + sizeof(uint64_t))) {
      payload.WriteValue(args).Write(id);
    }
  }

  // Writes a flow begin event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteFlowBeginEventRecord(Ticks event_time,
                                 const ThreadRef& thread_ref,
                                 const StringRef& category_ref,
                                 const StringRef& name_ref,
                                 uint64_t id,
                                 const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kFlowBegin, event_time, thread_ref, category_ref,
            name_ref, args.ElementCount(), args.Size() + sizeof(uint64_t))) {
      payload.WriteValue(args).Write(id);
    }
  }

  // Writes a flow step event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteFlowStepEventRecord(Ticks event_time,
                                const ThreadRef& thread_ref,
                                const StringRef& category_ref,
                                const StringRef& name_ref,
                                uint64_t id,
                                const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kFlowStep, event_time, thread_ref, category_ref,
            name_ref, args.ElementCount(), args.Size() + sizeof(uint64_t))) {
      payload.WriteValue(args).Write(id);
    }
  }

  // Writes a flow end event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteFlowEndEventRecord(Ticks event_time,
                               const ThreadRef& thread_ref,
                               const StringRef& category_ref,
                               const StringRef& name_ref,
                               uint64_t id,
                               const ArgumentList<Args...>& args = {}) {
    if (Payload payload = WriteEventRecordBase(
            EventType::kFlowEnd, event_time, thread_ref, category_ref, name_ref,
            args.ElementCount(), args.Size() + sizeof(uint64_t))) {
      payload.WriteValue(args).Write(id);
    }
  }

 private:
  explicit TraceWriter(::tracing::internal::TraceEngine* engine)
      : engine_(engine) {}

  Payload WriteKernelObjectRecordBase(zx_handle_t handle,
                                      size_t argument_count,
                                      size_t payload_size);
  Payload WriteEventRecordBase(EventType event_type,
                               Ticks event_time,
                               const ThreadRef& thread_ref,
                               const StringRef& category_ref,
                               const StringRef& name_ref,
                               size_t argument_count,
                               size_t payload_size);

  ::tracing::internal::TraceEngine* engine_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TraceWriter);
};

// Helps construct named arguments using SFINAE to coerce types.
template <typename T, typename Enable = void>
struct ArgumentMaker;

template <>
struct ArgumentMaker<nullptr_t> {
  using ResultType = NullArgument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         nullptr_t value) {
    return ResultType(name_ref);
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_integral<T>::value &&
                            !std::is_unsigned<T>::value &&
                            (sizeof(T) <= sizeof(int32_t))>::type> {
  using ResultType = Int32Argument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const T& value) {
    return ResultType(name_ref, static_cast<int32_t>(value));
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_integral<T>::value &&
                            std::is_unsigned<T>::value &&
                            (sizeof(T) <= sizeof(uint32_t))>::type> {
  using ResultType = Uint32Argument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const T& value) {
    return ResultType(name_ref, static_cast<uint32_t>(value));
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_integral<T>::value &&
                            !std::is_unsigned<T>::value &&
                            (sizeof(T) > sizeof(int32_t)) &&
                            (sizeof(T) <= sizeof(int64_t))>::type> {
  using ResultType = Int64Argument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const T& value) {
    return ResultType(name_ref, static_cast<int64_t>(value));
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_integral<T>::value &&
                            std::is_unsigned<T>::value &&
                            (sizeof(T) > sizeof(uint32_t)) &&
                            (sizeof(T) <= sizeof(uint64_t))>::type> {
  using ResultType = Uint64Argument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const T& value) {
    return ResultType(name_ref, static_cast<uint64_t>(value));
  }
};

template <typename T>
struct ArgumentMaker<T, typename std::enable_if<std::is_enum<T>::value>::type> {
  using UnderlyingType = typename std::underlying_type<T>::type;
  using ResultType = typename ArgumentMaker<UnderlyingType>::ResultType;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const T& value) {
    return ArgumentMaker<UnderlyingType>::Make(
        writer, name_ref, static_cast<UnderlyingType>(value));
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_floating_point<T>::value>::type> {
  using ResultType = DoubleArgument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const T& value) {
    return ResultType(name_ref, static_cast<double>(value));
  }
};

template <size_t n>
struct ArgumentMaker<char[n]> {
  using ResultType = StringArgument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const char* value) {
    return ResultType(name_ref, writer.RegisterString(value));
  }
};

template <>
struct ArgumentMaker<const char*> {
  using ResultType = StringArgument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const char* value) {
    return ResultType(name_ref, writer.RegisterString(value));
  }
};

template <>
struct ArgumentMaker<std::string> {
  using ResultType = RetainedStringArgument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         std::string value) {
    return ResultType(name_ref, std::move(value));
  }
};

template <typename T>
struct ArgumentMaker<T*> {
  using ResultType = PointerArgument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         const T* pointer) {
    return ResultType(name_ref, reinterpret_cast<uintptr_t>(pointer));
  }
};

template <>
struct ArgumentMaker<Koid> {
  using ResultType = KoidArgument;
  static ResultType Make(TraceWriter& writer,
                         const StringRef& name_ref,
                         Koid koid) {
    return ResultType(name_ref, koid.value);
  }
};

// Makes an argument with given name and value.
template <typename T>
typename ArgumentMaker<T>::ResultType MakeArgument(TraceWriter& writer,
                                                   const char* name,
                                                   const T& value) {
  return ArgumentMaker<T>::Make(writer, writer.RegisterString(name), value);
}

// Computes the ArgumentList<> type associated with a sequence of alternating
// key and value types.
template <typename... Args>
struct ComputeArgumentListTypeFromPairs {};

template <>
struct ComputeArgumentListTypeFromPairs<> {
  using ResultType = ArgumentList<>;
};

template <typename KT, typename VT, typename... Args>
struct ComputeArgumentListTypeFromPairs<KT, VT, Args...> {
  using ResultType = ArgumentList<
      typename ArgumentMaker<typename std::decay<VT>::type>::ResultType,
      typename ComputeArgumentListTypeFromPairs<Args...>::ResultType>;
};

// Builds an ArgumentList from a sequence of alternating keys and values.
template <typename... Args>
inline typename ComputeArgumentListTypeFromPairs<Args...>::ResultType
MakeArgumentList(TraceWriter& writer, Args&&... args);

template <>
inline ArgumentList<> MakeArgumentList(TraceWriter& writer) {
  return ArgumentList<>();
}

template <typename KT, typename VT, typename... Args>
inline ArgumentList<
    typename ArgumentMaker<typename std::decay<VT>::type>::ResultType,
    typename ComputeArgumentListTypeFromPairs<Args...>::ResultType>
MakeArgumentList(TraceWriter& writer, KT&& name, VT&& value, Args&&... args) {
  static_assert(std::is_same<typename std::decay<KT>::type, const char*>::value,
                "The key must be a constant string.");
  return ArgumentList<
      typename ArgumentMaker<typename std::decay<VT>::type>::ResultType,
      typename ComputeArgumentListTypeFromPairs<Args...>::ResultType>(
      MakeArgument(writer, std::forward<KT>(name), std::forward<VT>(value)),
      MakeArgumentList(writer, std::forward<Args>(args)...));
}

// Compute the ArgumentList<> type associated with a sequence of Argument.
template <typename... Args>
struct ComputeArgumentListTypeFromArgs;

template <>
struct ComputeArgumentListTypeFromArgs<> {
  using ResultType = ArgumentList<>;
};

template <typename Arg, typename... Args>
struct ComputeArgumentListTypeFromArgs<Arg, Args...> {
  using ResultType = ArgumentList<
      Arg,
      typename ComputeArgumentListTypeFromArgs<Args...>::ResultType>;
};

// Builds an ArgumentList from a sequence of Argument.
template <typename... Args>
typename ComputeArgumentListTypeFromArgs<Args...>::ResultType ToArgumentList(
    Args&&... args);

template <>
inline ArgumentList<> ToArgumentList() {
  return ArgumentList<>();
}

template <typename Arg, typename... Args>
inline ArgumentList<
    Arg,
    typename ComputeArgumentListTypeFromArgs<Args...>::ResultType>
ToArgumentList(Arg&& arg, Args&&... args) {
  return ArgumentList<
      Arg, typename ComputeArgumentListTypeFromArgs<Args...>::ResultType>(
      std::forward<Arg>(arg), ToArgumentList(std::forward<Args>(args)...));
}

}  // namespace writer
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_WRITER_H_
