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
namespace writer {

// Sets up the Writer API to use |buffer| as destination for
// incoming trace records.
void StartTracing(mx::vmo current,
                  mx::vmo next,
                  std::vector<std::string> categories);

// Tears down the Writer API and frees up all allocated resources.
void StopTracing();

// Returns true if the tracer has been initialized by a call to |StartTracing|
// and the specified |category| has been enabled.
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

  EncodedStringRef const encoded_value_;
  const char* const inline_string_;
};

// Registers a constant string in the string table.
StringRef RegisterString(const char* string);

// A thread reference which is either encoded inline or indirectly by
// thread table index.
class ThreadRef {
 public:
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

  EncodedThreadRef const encoded_value_;
  uint64_t const inline_process_koid_;
  uint64_t const inline_thread_koid_;
};

// Registers the current thread in the thread table.
ThreadRef RegisterCurrentThread();

// Represents a named argument and value pair.
class ArgumentBase {
 public:
  explicit ArgumentBase(const char* name) : name_ref_(RegisterString(name)) {}

 protected:
  size_t Size() const { return sizeof(uint64_t) + name_ref_.Size(); }

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

template <typename T>
class Argument;

template <>
class Argument<int32_t> : public ArgumentBase {
 public:
  explicit Argument(const char* name, int32_t value)
      : ArgumentBase(name), value_(value) {}

  size_t Size() const { return ArgumentBase::Size(); }

  void WriteTo(Payload& payload) const {
    ArgumentBase::WriteTo(
        payload, ArgumentType::kInt32, Size(),
        ::tracing::internal::Int32ArgumentFields::Value::Make(value_));
  }

 private:
  int32_t const value_;
};

template <>
class Argument<int64_t> : public ArgumentBase {
 public:
  explicit Argument(const char* name, int64_t value)
      : ArgumentBase(name), value_(value) {}

  size_t Size() const { return ArgumentBase::Size() + sizeof(int64_t); }

  void WriteTo(Payload& payload) const {
    ArgumentBase::WriteTo(payload, ArgumentType::kInt64, Size());
    payload.WriteInt64(value_);
  }

 private:
  int64_t const value_;
};

template <>
class Argument<double> : public ArgumentBase {
 public:
  explicit Argument(const char* name, double value)
      : ArgumentBase(name), value_(value) {}

  size_t Size() const { return ArgumentBase::Size() + sizeof(double); }

  void WriteTo(Payload& payload) const {
    ArgumentBase::WriteTo(payload, ArgumentType::kDouble, Size());
    payload.WriteDouble(value_);
  }

 private:
  double const value_;
};

class StringArgumentBase : public ArgumentBase {
 public:
  explicit StringArgumentBase(const char* name, StringRef value_ref)
      : ArgumentBase(name), value_ref_(std::move(value_ref)) {}

  size_t Size() const { return ArgumentBase::Size() + value_ref_.Size(); }

  void WriteTo(Payload& payload) const {
    ArgumentBase::WriteTo(
        payload, ArgumentType::kString, Size(),
        ::tracing::internal::StringArgumentFields::Index::Make(
            value_ref_.encoded_value()));
    payload.WriteValue(value_ref_);
  }

 private:
  StringRef const value_ref_;
};

template <>
class Argument<const char*> : public StringArgumentBase {
 public:
  explicit Argument(const char* name, const char* value)
      : StringArgumentBase(name, RegisterString(value)) {}
};

template <size_t n>
class Argument<char[n]> : public StringArgumentBase {
 public:
  explicit Argument(const char* name, const char* value)
      : StringArgumentBase(name, RegisterString(value)) {}
};

template <>
class Argument<std::string> : public StringArgumentBase {
 public:
  explicit Argument(const char* name, const std::string& value)
      : StringArgumentBase(name, StringRef::MakeInlinedOrEmpty(value)) {}
};

template <typename T>
class Argument<T*> : public ArgumentBase {
 public:
  explicit Argument(const char* name, const T* value)
      : ArgumentBase(name), value_(reinterpret_cast<uintptr_t>(value)) {}

  size_t Size() const { return ArgumentBase::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    ArgumentBase::WriteTo(payload, ArgumentType::kPointer, Size());
    payload.Write(value_);
  }

 private:
  uintptr_t value_;
};

template <>
class Argument<Koid> : public ArgumentBase {
 public:
  explicit Argument(const char* name, const Koid& koid)
      : ArgumentBase(name), koid_(koid) {}

  size_t Size() const { return ArgumentBase::Size() + sizeof(uint64_t); }

  void WriteTo(Payload& payload) const {
    ArgumentBase::WriteTo(payload, ArgumentType::kKernelObjectId, Size());
    payload.Write(koid_.value);
  }

 private:
  Koid const koid_;
};

// Helps make arguments, particularly enums and integer literals.
template <typename T, typename Enable = void>
struct ArgumentMaker {
  using ResultType = Argument<T>;
  static ResultType Make(const char* name, const T& value) {
    return ResultType(name, value);
  }
};

template <typename T>
struct ArgumentMaker<T, typename std::enable_if<std::is_enum<T>::value>::type> {
  using UnderlyingType = typename std::underlying_type<T>::type;
  using NumericType =
      typename std::conditional<sizeof(UnderlyingType) < sizeof(int32_t),
                                int32_t,
                                int64_t>::type;
  using ResultType = Argument<NumericType>;
  static ResultType Make(const char* name, T value) {
    return ResultType(name, static_cast<NumericType>(value));
  }
};

template <typename T>
struct ArgumentMaker<
    T,
    typename std::enable_if<std::is_unsigned<T>::value>::type> {
  using NumericType = typename std::
      conditional<sizeof(T) < sizeof(int32_t), int32_t, int64_t>::type;
  using ResultType = Argument<NumericType>;
  static ResultType Make(const char* name, T value) {
    return ResultType(name, static_cast<NumericType>(value));
  }
};

// Makes an argument with given name and value.
template <typename T>
inline typename ArgumentMaker<T>::ResultType MakeArgument(const char* name,
                                                          const T& value) {
  return ArgumentMaker<T>::Make(name, value);
}

// Gets the total size of a list of arguments.
inline size_t SizeArguments() {
  return 0;
}

template <typename Head, typename... Tail>
inline size_t SizeArguments(Head&& head, Tail&&... tail) {
  return head.Size() + SizeArguments(std::forward<Tail>(tail)...);
}

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

// Writes an event record into the trace buffer, returning a payload object
// into which event type specific data and arguments can be written.
// Discards the record and returns an invalid payload object if it cannot
// be written.
Payload WriteEventRecord(EventType type,
                         const char* category,
                         const char* name,
                         size_t argument_count,
                         uint64_t payload_size);

// Writes a duration begin event record with arguments into the trace buffer.
// Discards the record if it cannot be written.
template <typename... Args>
void WriteDurationBeginEventRecord(const char* cat,
                                   const char* name,
                                   Args&&... args) {
  if (Payload payload = WriteEventRecord(
          EventType::kDurationBegin, cat, name, sizeof...(Args),
          SizeArguments(std::forward<Args>(args)...))) {
    payload.WriteValues(std::forward<Args>(args)...);
  }
}

// Writes a duration end event record with arguments into the trace buffer.
// Discards the record if it cannot be written.
template <typename... Args>
void WriteDurationEndEventRecord(const char* cat,
                                 const char* name,
                                 Args&&... args) {
  if (Payload payload =
          WriteEventRecord(EventType::kDurationEnd, cat, name, sizeof...(Args),
                           SizeArguments(std::forward<Args>(args)...))) {
    payload.WriteValues(std::forward<Args>(args)...);
  }
}

// Writes an asynchronous begin event record into the trace buffer.
// Discards the record if it cannot be written.
template <typename... Args>
void WriteAsyncBeginEventRecord(const char* cat,
                                const char* name,
                                uint64_t id,
                                Args&&... args) {
  if (Payload payload = WriteEventRecord(
          EventType::kAsyncStart, cat, name, sizeof...(Args),
          SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
    payload.WriteValues(std::forward<Args>(args)...).Write(id);
  }
}

// Writes an asynchronous instant event record into the trace buffer.
// Discards the record if it cannot be written.
template <typename... Args>
void WriteAsyncInstantEventRecord(const char* cat,
                                  const char* name,
                                  uint64_t id,
                                  Args&&... args) {
  if (auto payload = WriteEventRecord(
          EventType::kAsyncInstant, cat, name, sizeof...(Args),
          SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
    payload.WriteValues(std::forward<Args>(args)...).Write(id);
  }
}

// Writes an asynchronous end event record into the trace buffer.
// Discards the record if it cannot be written.
template <typename... Args>
void WriteAsyncEndEventRecord(const char* cat,
                              const char* name,
                              uint64_t id,
                              Args&&... args) {
  if (Payload payload = WriteEventRecord(
          EventType::kAsyncEnd, cat, name, sizeof...(Args),
          SizeArguments(std::forward<Args>(args)...) + sizeof(uint64_t))) {
    payload.WriteValues(std::forward<Args>(args)...).Write(id);
  }
}

// Relies on RAII to trace begin and end of a duration event.
class ScopedDurationEventWriter {
 public:
  // Initializes a new instance, determining whether a given category should
  // be traced, and storing the category and name to inject a duration end
  // event when destroyed.  The creator is responsible for calling |Begin|.
  //
  // We assume that the lifetime of |cat| and |name| exceeds the lifetime
  // of this instance.
  explicit ScopedDurationEventWriter(const char* cat, const char* name)
      : is_enabled_(IsTracingEnabledForCategory(cat)), cat_(cat), name_(name) {}

  // Injects a duration end event with the parameters passed in
  // on construction into the tracing infrastructure.
  ~ScopedDurationEventWriter() {
    if (is_enabled_)
      WriteDurationEndEventRecord(cat_, name_);
  }

  // Returns true if this event should be traced.
  bool is_enabled() const { return is_enabled_; }

  // Writes a duration begin event.
  // Must only be called if |is_enabled()| returns true.
  template <typename... Args>
  void Begin(Args&&... args) const {
    WriteDurationBeginEventRecord(cat_, name_, std::forward<Args>(args)...);
  }

 private:
  bool const is_enabled_;
  const char* const cat_;
  const char* const name_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ScopedDurationEventWriter);
};

}  // namespace writer
}  // namespace tracing

#define TRACE_INTERNAL_CATEGORY_ENABLED(cat) \
  ::tracing::writer::IsTracingEnabledForCategory(cat)

#define TRACE_INTERNAL_MAKE_ARG(key, value) \
  ::tracing::writer::MakeArgument(key, value)

#define TRACE_INTERNAL_MAKE_ARGS1(k1, v1) TRACE_INTERNAL_MAKE_ARG(k1, v1)
#define TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2) \
  TRACE_INTERNAL_MAKE_ARGS1(k1, v1), TRACE_INTERNAL_MAKE_ARG(k2, v2)
#define TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3) \
  TRACE_INTERNAL_MAKE_ARGS2(k1, v1, k2, v2), TRACE_INTERNAL_MAKE_ARG(k3, v3)
#define TRACE_INTERNAL_MAKE_ARGS4(k1, v1, k2, v2, k3, v3, k4, v4) \
  TRACE_INTERNAL_MAKE_ARGS3(k1, v1, k2, v2, k3, v3)               \
  , TRACE_INTERNAL_MAKE_ARG(k4, v4)

#define TRACE_INTERNAL_SCOPE_LABEL_(token) __trace_scope_##token
#define TRACE_INTERNAL_SCOPE_LABEL() TRACE_INTERNAL_SCOPE_LABEL_(__LINE__)

#define TRACE_INTERNAL_EVENT_DURATION(cat, name, args...)                    \
  ::tracing::writer::ScopedDurationEventWriter TRACE_INTERNAL_SCOPE_LABEL()( \
      cat, name);                                                            \
  if (TRACE_INTERNAL_SCOPE_LABEL().is_enabled()) {                           \
    TRACE_INTERNAL_SCOPE_LABEL().Begin(args);                                \
  }

#define TRACE_INTERNAL_EVENT_ASYNC_BEGIN(cat, name, id_and_args...)        \
  if (TRACE_INTERNAL_CATEGORY_ENABLED(cat)) {                              \
    ::tracing::writer::WriteAsyncBeginEventRecord(cat, name, id_and_args); \
  }

#define TRACE_INTERNAL_EVENT_ASYNC_INSTANT(cat, name, id_and_args...)        \
  if (TRACE_INTERNAL_CATEGORY_ENABLED(cat)) {                                \
    ::tracing::writer::WriteAsyncInstantEventRecord(cat, name, id_and_args); \
  }

#define TRACE_INTERNAL_EVENT_ASYNC_END(cat, name, id_and_args...)        \
  if (TRACE_INTERNAL_CATEGORY_ENABLED(cat)) {                            \
    ::tracing::writer::WriteAsyncEndEventRecord(cat, name, id_and_args); \
  }

#endif  // APPS_TRACING_LIB_TRACE_INTERNAL_WRITER_H_
