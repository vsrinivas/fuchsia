// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_INTERNAL_WRITER_H_
#define APPS_TRACING_LIB_TRACE_INTERNAL_WRITER_H_

#include <stdint.h>

#include <string>
#include <type_traits>
#include <utility>

#include <mx/vmo.h>

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/lib/trace/types.h"
#include "lib/ftl/macros.h"

namespace tracing {
namespace internal {
class TraceEngine;
}  // namespace internal
namespace writer {

// Sets up the Writer API to use |buffer| as destination for
// incoming trace records.
void StartTracing(mx::vmo current,
                  mx::vmo next,
                  std::vector<std::string> enabled_categories);

// Tears down the Writer API and frees up all allocated resources.
void StopTracing();

// Returns true if the tracer has been initialized by a call to |StartTracing|
// and the specified |category| has been enabled.
//
// |category| must be a string constant such as would be passed to
// |PrepareCategory| or |WriteEventRecord|.
bool IsTracingEnabledForCategory(const char* category);

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

  Payload& WriteBytes(const void* src, size_t size) {
    memcpy(ptr_, src, size);
    ptr_ += size >> 3;
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
  // Constructs an uninitialized instance.
  StringRef() {}

  static StringRef MakeEmpty() {
    return StringRef(::tracing::internal::StringRefFields::kEmpty, nullptr);
  }

  static StringRef MakeInlined(const char* string, size_t length) {
    size_t trim = std::min(
        length, size_t(::tracing::internal::StringRefFields::kMaxLength));
    return StringRef(
        static_cast<EncodedStringRef>(
            trim | ::tracing::internal::StringRefFields::kInlineFlag),
        string);
  }

  static StringRef MakeInlinedOrEmpty(const char* string, size_t length) {
    return length ? MakeInlined(string, length) : MakeEmpty();
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
  // Constructs an uninitialized instance.
  ThreadRef() {}

  static ThreadRef MakeInlined(uint64_t process_koid, uint64_t thread_koid) {
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
  uint64_t inline_process_koid() const { return inline_process_koid_; }
  uint64_t inline_thread_koid() const { return inline_thread_koid_; }

  size_t Size() const { return is_inlined() ? 2 * sizeof(uint64_t) : 0; }

  void WriteTo(Payload& payload) const {
    if (is_inlined())
      payload.Write(inline_process_koid_).Write(inline_thread_koid_);
  }

 private:
  explicit ThreadRef(EncodedThreadRef encoded_value,
                     uint64_t inline_process_koid,
                     uint64_t inline_thread_koid)
      : encoded_value_(encoded_value),
        inline_process_koid_(inline_process_koid),
        inline_thread_koid_(inline_thread_koid) {}

  EncodedThreadRef encoded_value_;
  uint64_t inline_process_koid_;
  uint64_t inline_thread_koid_;
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

class RetainedStringArgument : public StringArgument {
 public:
  explicit RetainedStringArgument(StringRef name_ref, std::string value)
      : StringArgument(std::move(name_ref),
                       StringRef::MakeInlinedOrEmpty(value)),
        retained_value_(std::move(value)) {}

 private:
  std::string retained_value_;
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
  explicit KoidArgument(StringRef name_ref, uint64_t koid)
      : Argument(std::move(name_ref)), koid_(koid) {}

  size_t Size() const { return Argument::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    Argument::WriteTo(payload, ArgumentType::kKoid, Size());
    payload.Write(koid_);
  }

 private:
  uint64_t const koid_;
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
class TraceWriter {
 public:
  // Releases the trace writer.
  //
  // Note: This is non-virtual to avoid initialization of a vtable.
  // The only subclass is |CategorizedTraceWriter| which only adds POD members
  // and is final.
  ~TraceWriter();

  // Prepares to write trace records.
  // If tracing is enabled, returns a valid |TraceWriter|.
  static TraceWriter Prepare();

  // Returns true if the trace writer is valid.
  // It is illegal to call functions on an invalid trace writer.
  explicit operator bool() const { return engine_; }

  // Registers a constant string in the string table.
  StringRef RegisterString(const char* string);

  // Registers the current thread in the thread table.
  ThreadRef RegisterCurrentThread();

  // Writes an initialization record into the trace buffer.
  // Discards the record if it cannot be written.
  void WriteInitializationRecord(uint64_t ticks_per_second);

  // Writes a string record into the trace buffer.
  // Discards the record if it cannot be written.
  void WriteStringRecord(StringIndex index, const char* string);

  // Writes a thread record into the trace buffer.
  // Discards the record if it cannot be written.
  void WriteThreadRecord(ThreadIndex index,
                         uint64_t process_koid,
                         uint64_t thread_koid);

 private:
  // Private friend (instead of protected) to prevent subclassing of
  // TraceWriter by anyone else since it could be unsafe.
  friend class CategorizedTraceWriter;

  explicit TraceWriter(::tracing::internal::TraceEngine* engine)
      : engine_(engine) {}

  ::tracing::internal::TraceEngine* const engine_;
};

// Writes events in a particular category.
class CategorizedTraceWriter final : public TraceWriter {
 public:
  // Prepares to write trace records for a specified event category.
  // If tracing is enabled for the category, returns a valid |TraceWriter|
  // which is bound to the category.
  static CategorizedTraceWriter Prepare(const char* category);

  // Writes a duration begin event record with arguments into the trace
  // buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteDurationBeginEventRecord(const char* name, Args&&... args) {
    if (Payload payload =
            WriteEventRecord(EventType::kDurationBegin, name, sizeof...(Args),
                             SizeArguments(std::forward<Args>(args)...))) {
      payload.WriteValues(std::forward<Args>(args)...);
    }
  }

  // Writes a duration end event record with arguments into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteDurationEndEventRecord(const char* name, Args&&... args) {
    if (Payload payload =
            WriteEventRecord(EventType::kDurationEnd, name, sizeof...(Args),
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
    if (Payload payload = WriteEventRecord(
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
    if (Payload payload = WriteEventRecord(
            EventType::kAsyncInstant, name, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
      payload.WriteValues(std::forward<Args>(args)...).Write(id);
    }
  }

  // Writes an asynchronous end event record into the trace buffer.
  // Discards the record if it cannot be written.
  template <typename... Args>
  void WriteAsyncEndEventRecord(const char* name, uint64_t id, Args&&... args) {
    if (Payload payload = WriteEventRecord(
            EventType::kAsyncEnd, name, sizeof...(Args),
            SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
      payload.WriteValues(std::forward<Args>(args)...).Write(id);
    }
  }

 protected:
  explicit CategorizedTraceWriter(::tracing::internal::TraceEngine* engine,
                                  const StringRef& category_ref)
      : TraceWriter(engine), category_ref_(category_ref) {}

  Payload WriteEventRecord(EventType type,
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
      TRACE_INTERNAL_WRITER.WriteDurationBeginEventRecord(                     \
          scope_label.category() args))

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

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_WRITER_H_
