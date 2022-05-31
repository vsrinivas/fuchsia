// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Given a Writer implementing the Writer CRTP in writer.h, provide an api
// over the writer to allow serializing fxt to the Writer.
//
// Based heavily on libTrace in zircon/system/ulib/trace to allow compatibility,
// but modified to enable passing in an arbitrary buffering system.

#ifndef SRC_LIB_FXT_INCLUDE_LIB_FXT_SERIALIZER_H_
#define SRC_LIB_FXT_INCLUDE_LIB_FXT_SERIALIZER_H_

#include <lib/zx/status.h>
#include <zircon/assert.h>

#include <cstdint>
#include <optional>

#include "fields.h"
#include "record_types.h"
#include "writer_internal.h"
#include "zircon/types.h"

namespace fxt {

// Represents an FXT StringRecord which is either inline in the record body, or
// an index included in the record header.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#string-record
template <RefType>
class StringRef;

template <>
class StringRef<RefType::kInline> {
 public:
  explicit StringRef(const char* string) : string_{string} {}

  WordSize PayloadSize() const { return WordSize::FromBytes(strnlen(string_, FXT_MAX_STR_LEN)); }

  uint64_t HeaderEntry() const { return 0x8000 | strnlen(string_, FXT_MAX_STR_LEN); }

  template <typename Reservation>
  void Write(Reservation& res) const {
    size_t num_bytes = strnlen(string_, FXT_MAX_STR_LEN);
    res.WriteBytes(string_, num_bytes);
  }

 private:
  static const size_t FXT_MAX_STR_LEN = 32000;
  const char* string_;
};
#if __cplusplus >= 201703L
StringRef(const char*)->StringRef<RefType::kInline>;
#endif

template <>
class StringRef<RefType::kId> {
 public:
  explicit StringRef(uint16_t id) : id_(id) {
    ZX_ASSERT_MSG(id < 0x8000, "The msb of a StringRef's id must be 0");
  }

  static WordSize PayloadSize() { return WordSize(0); }

  uint64_t HeaderEntry() const { return id_; }

  template <typename Reservation>
  void Write(Reservation& res) const {
    // Nothing, data in in the header
  }

 private:
  uint16_t id_;
};
#if __cplusplus >= 201703L
StringRef(uint16_t)->StringRef<RefType::kId>;
#endif

inline constexpr uint64_t MakeHeader(RecordType type, WordSize size_words) {
  return RecordFields::Type::Make(ToUnderlyingType(type)) |
         RecordFields::RecordSize::Make(size_words.SizeInWords());
}

// Represents an FXT Thread Reference which is either inline in the record
// body, or an index included in the record header
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#thread-references
template <RefType>
class ThreadRef;

template <>
class ThreadRef<RefType::kInline> {
 public:
  ThreadRef(zx_koid_t process, zx_koid_t thread) : process_(process), thread_(thread) {}

  static WordSize PayloadSize() { return WordSize(2); }
  static uint64_t HeaderEntry() { return 0; }

  template <typename Reservation>
  void Write(Reservation& res) const {
    res.WriteWord(process_);
    res.WriteWord(thread_);
  }

 private:
  zx_koid_t process_;
  zx_koid_t thread_;
};
#if __cplusplus >= 201703L
ThreadRef(zx_koid_t, zx_koid_t)->ThreadRef<RefType::kInline>;
#endif

template <>
class ThreadRef<RefType::kId> {
 public:
  explicit ThreadRef(uint8_t id) : id_(id) {}

  static WordSize PayloadSize() { return WordSize(0); }
  uint64_t HeaderEntry() const { return id_; }

  template <typename Reservation>
  void Write(Reservation& res) const {}

 private:
  uint8_t id_;
};
#if __cplusplus >= 201703L
ThreadRef(uint8_t)->ThreadRef<RefType::kId>;
#endif

// Represents an FXT Argument, a typed Key Value pair.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#arguments
template <ArgumentType argument_type, RefType name_type, RefType val_type = RefType::kId>
class Argument;

template <RefType name_type>
class Argument<ArgumentType::kNull, name_type> {
 public:
  explicit Argument(StringRef<name_type> name) : name_(name) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize();
  }
  uint64_t Header() const {
    return ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kNull)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
  }

 private:
  StringRef<name_type> name_;
};
#if __cplusplus >= 201703L
template <RefType name_type>
Argument(StringRef<name_type>) -> Argument<ArgumentType::kNull, name_type>;
#endif

template <RefType name_type>
class Argument<ArgumentType::kBool, name_type> {
 public:
  Argument(StringRef<name_type> name, bool val) : name_(name), val_(val) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize();
  }
  uint64_t Header() const {
    return BoolArgumentFields::Value::Make(val_) |
           ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kBool)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
  }

 private:
  StringRef<name_type> name_;
  bool val_;
};
#if __cplusplus >= 201703L
template <RefType name_type>
Argument(StringRef<name_type>, bool) -> Argument<ArgumentType::kBool, name_type>;
#endif

template <RefType name_type>
class Argument<ArgumentType::kInt32, name_type> {
 public:
  Argument(StringRef<name_type> name, int32_t val) : name_(name), val_(val) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize();
  }
  uint64_t Header() const {
    return Int32ArgumentFields::Value::Make(val_) |
           ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kInt32)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
  }

 private:
  StringRef<name_type> name_;
  int32_t val_;
};
#if __cplusplus >= 201703L
template <RefType name_type>
Argument(StringRef<name_type>, int32_t) -> Argument<ArgumentType::kInt32, name_type>;
#endif

template <RefType name_type>
class Argument<ArgumentType::kUint32, name_type> {
 public:
  Argument(StringRef<name_type> name, uint32_t val) : name_(name), val_(val) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize();
  }
  uint64_t Header() const {
    return Uint32ArgumentFields::Value::Make(val_) |
           ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kUint32)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
  }

 private:
  StringRef<name_type> name_;
  uint32_t val_;
};
#if __cplusplus >= 201703L
template <RefType name_type>
Argument(StringRef<name_type>, uint32_t) -> Argument<ArgumentType::kUint32, name_type>;
#endif

template <RefType name_type>
class Argument<ArgumentType::kInt64, name_type> {
 public:
  Argument(StringRef<name_type> name, int64_t val) : name_(name), val_(val) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize() + WordSize(1);
  }
  uint64_t Header() const {
    return ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kInt64)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
    res.WriteWord(val_);
  }

 private:
  StringRef<name_type> name_;
  int64_t val_;
};
#if __cplusplus >= 201703L
template <RefType name_type>
Argument(StringRef<name_type>, int64_t) -> Argument<ArgumentType::kInt64, name_type>;
#endif

template <RefType name_type>
class Argument<ArgumentType::kUint64, name_type> {
 public:
  Argument(StringRef<name_type> name, uint64_t val) : name_(name), val_(val) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize() + WordSize(1);
  }
  uint64_t Header() const {
    return ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kUint64)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
    res.WriteWord(val_);
  }

 private:
  StringRef<name_type> name_;
  uint64_t val_;
};

template <RefType name_type>
class Argument<ArgumentType::kDouble, name_type> {
 public:
  Argument(StringRef<name_type> name, double val) : name_(name), val_(val) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize() + WordSize(1);
  }
  uint64_t Header() const {
    return ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kDouble)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
    res.WriteBytes(&val_, 8);
  }

 private:
  StringRef<name_type> name_;
  double val_;
};
#if __cplusplus >= 201703L
template <RefType name_type>
Argument(StringRef<name_type>, double) -> Argument<ArgumentType::kDouble, name_type>;
#endif

template <RefType name_type>
class Argument<ArgumentType::kPointer, name_type> {
 public:
  Argument(StringRef<name_type> name, uintptr_t val) : name_(name), val_(val) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize() + WordSize(1);
  }
  uint64_t Header() const {
    return ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kPointer)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
    res.WriteWord(val_);
  }

 private:
  StringRef<name_type> name_;
  uintptr_t val_;
};

template <RefType name_type>
class Argument<ArgumentType::kKoid, name_type> {
 public:
  Argument(StringRef<name_type> name, zx_koid_t val) : name_(name), val_(val) {}
  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize() + WordSize(1);
  }
  uint64_t Header() const {
    return ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kKoid)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
    res.WriteWord(val_);
  }

 private:
  StringRef<name_type> name_;
  zx_koid_t val_;
};

template <RefType name_type, RefType val_type>
class Argument<ArgumentType::kString, name_type, val_type> {
 public:
  Argument(StringRef<name_type> name, StringRef<val_type> val) : name_(name), val_(val) {}

  WordSize PayloadSize() const {
    return WordSize::FromBytes(sizeof(ArgumentHeader)) + name_.PayloadSize() + val_.PayloadSize();
  }

  uint64_t Header() const {
    return StringArgumentFields::Index::Make(val_.HeaderEntry()) |
           ArgumentFields::Type::Make(ToUnderlyingType(ArgumentType::kString)) |
           ArgumentFields::ArgumentSize::Make(PayloadSize().SizeInWords()) |
           ArgumentFields::NameRef::Make(name_.HeaderEntry());
  }

  template <typename Reservation>
  void Write(Reservation res) const {
    res.WriteWord(Header());
    name_.Write(res);
    val_.Write(res);
  }

 private:
  StringRef<name_type> name_;
  StringRef<val_type> val_;
};
#if __cplusplus >= 201703L
Argument(StringRef<RefType::kInline>, StringRef<RefType::kId>)
    ->Argument<ArgumentType::kString, RefType::kInline, RefType::kId>;
Argument(StringRef<RefType::kId>, StringRef<RefType::kId>)
    ->Argument<ArgumentType::kString, RefType::kId, RefType::kId>;
Argument(StringRef<RefType::kInline>, StringRef<RefType::kInline>)
    ->Argument<ArgumentType::kString, RefType::kInline, RefType::kInline>;
Argument(StringRef<RefType::kId>, StringRef<RefType::kInline>)
    ->Argument<ArgumentType::kString, RefType::kId, RefType::kInline>;
#endif

// Write an Initialization Record using Writer
// An Initialization Record provides additional information which modifies how
// following records are interpreted.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#initialization-record
template <typename Writer, internal::EnableIfWriter<Writer> = 0>
zx_status_t WriteInitializationRecord(Writer* writer, zx_ticks_t ticks_per_second) {
  const WordSize record_size(2);
  uint64_t header = MakeHeader(RecordType::kInitialization, record_size);
  zx::status<typename internal::WriterTraits<Writer>::Reservation> res = writer->Reserve(header);
  if (res.is_ok()) {
    res->WriteWord(ticks_per_second);
    res->Commit();
  }
  return res.status_value();
}

namespace internal {
inline WordSize EventContentWords(EventType eventType) {
  switch (eventType) {
    case EventType::kInstant:
    case EventType::kDurationBegin:
    case EventType::kDurationEnd:
      return WordSize(0);
    case EventType::kCounter:
    case EventType::kDurationComplete:
    case EventType::kAsyncBegin:
    case EventType::kAsyncInstant:
    case EventType::kAsyncEnd:
    case EventType::kFlowBegin:
    case EventType::kFlowStep:
    case EventType::kFlowEnd:
      return WordSize(1);
  }
}

template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
void WriteEventRecord(typename internal::WriterTraits<Writer>::Reservation& res,
                      uint64_t event_time, const ThreadRef<thread_type>& thread_ref,
                      const StringRef<category_type>& category_ref,
                      const StringRef<name_type>& name_ref,
                      const Argument<arg_types, ref_types>&... args) {
  res.WriteWord(event_time);
  thread_ref.Write(res);
  category_ref.Write(res);
  name_ref.Write(res);
  bool array[] = {(args.Write(res), false)...};
  (void)array;
}

template <RefType thread_type, RefType name_type, RefType category_type, ArgumentType... arg_types,
          RefType... ref_types>
uint64_t MakeEventHeader(fxt::EventType eventType, const ThreadRef<thread_type>& thread_ref,
                         const StringRef<category_type>& category_ref,
                         const StringRef<name_type>& name_ref,
                         const Argument<arg_types, ref_types>&... args) {
  const WordSize content_size = fxt::internal::EventContentWords(eventType);
  WordSize record_size = WordSize::FromBytes(sizeof(RecordHeader)) + WordSize(1) +
                         thread_ref.PayloadSize() + category_ref.PayloadSize() +
                         name_ref.PayloadSize() + content_size;
  WordSize word_sizes[] = {args.PayloadSize()...};
  for (auto i : word_sizes) {
    record_size += i;
  }
  return MakeHeader(RecordType::kEvent, record_size) |
         EventRecordFields::EventType::Make(ToUnderlyingType(eventType)) |
         EventRecordFields::ArgumentCount::Make(sizeof...(args)) |
         EventRecordFields::ThreadRef::Make(thread_ref.HeaderEntry()) |
         EventRecordFields::CategoryStringRef::Make(category_ref.HeaderEntry()) |
         EventRecordFields::NameStringRef::Make(name_ref.HeaderEntry());
}

// Write an event with no event specific data such as an Instant Event or
// Duration Begin Event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteZeroWordEventRecord(Writer* writer, uint64_t event_time,
                                     const ThreadRef<thread_type>& thread_ref,
                                     const StringRef<category_type>& category_ref,
                                     const StringRef<name_type>& name_ref, fxt::EventType eventType,
                                     const Argument<arg_types, ref_types>&... args) {
  uint64_t header =
      internal::MakeEventHeader(eventType, thread_ref, category_ref, name_ref, args...);
  zx::status<typename internal::WriterTraits<Writer>::Reservation> res = writer->Reserve(header);
  if (res.is_ok()) {
    fxt::internal::WriteEventRecord<Writer>(*res, event_time, thread_ref, category_ref, name_ref,
                                            args...);
    res->Commit();
  }
  return res.status_value();
}

// Write an event with one word of event specific data such as a Counter Event
// or Async Begin Event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteOneWordEventRecord(Writer* writer, uint64_t event_time,
                                    const ThreadRef<thread_type>& thread_ref,
                                    const StringRef<category_type>& category_ref,
                                    const StringRef<name_type>& name_ref, fxt::EventType eventType,
                                    uint64_t content,
                                    const Argument<arg_types, ref_types>&... args) {
  uint64_t header =
      internal::MakeEventHeader(eventType, thread_ref, category_ref, name_ref, args...);
  zx::status<typename internal::WriterTraits<Writer>::Reservation> res = writer->Reserve(header);
  if (res.is_ok()) {
    fxt::internal::WriteEventRecord<Writer>(*res, event_time, thread_ref, category_ref, name_ref,
                                            args...);
    res->WriteWord(content);
    res->Commit();
  }
  return res.status_value();
}

}  // namespace internal

// Write an Instant Event using the given Writer
//
// Instant Events marks a moment in time on a thread.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#instant-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteInstantEventRecord(Writer* writer, uint64_t event_time,
                                    const ThreadRef<thread_type>& thread_ref,
                                    const StringRef<category_type>& category_ref,
                                    const StringRef<name_type>& name_ref,
                                    const Argument<arg_types, ref_types>&... args) {
  return internal::WriteZeroWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                            fxt::EventType::kInstant, args...);
}

// Write a Counter Event using the given Writer
//
// Counter Events sample values of each argument as data in a time series
// associated with the counter's name and id.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#instant-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteCounterEventRecord(Writer* writer, uint64_t event_time,
                                    const ThreadRef<thread_type>& thread_ref,
                                    const StringRef<category_type>& category_ref,
                                    const StringRef<name_type>& name_ref, uint64_t counter_id,
                                    const Argument<arg_types, ref_types>&... args) {
  return internal::WriteOneWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                           fxt::EventType::kCounter, counter_id, args...);
}

// Write a Duration Begin Event using the given Writer
//
// A Duration Begin Event marks the beginning of an operation on a particular
// thread. Must be matched by a duration end event. May be nested.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#duration-begin-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteDurationBeginEventRecord(Writer* writer, uint64_t event_time,
                                          const ThreadRef<thread_type>& thread_ref,
                                          const StringRef<category_type>& category_ref,
                                          const StringRef<name_type>& name_ref,
                                          Argument<arg_types, ref_types>... args) {
  return internal::WriteZeroWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                            fxt::EventType::kDurationBegin, args...);
}

// Write a Duration End Event using the given Writer
//
// A Duration End Event marks the end of an operation on a particular
// thread.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#duration-end-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteDurationEndEventRecord(Writer* writer, uint64_t event_time,
                                        const ThreadRef<thread_type>& thread_ref,
                                        const StringRef<category_type>& category_ref,
                                        const StringRef<name_type>& name_ref,
                                        Argument<arg_types, ref_types>... args) {
  return internal::WriteZeroWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                            fxt::EventType::kDurationEnd, args...);
}

// Write a Duration Complete Event using the given Writer
//
// A Duration Complete Event marks the beginning and end of an operation on a particular thread.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#duration-complete-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteDurationCompleteEventRecord(Writer* writer, uint64_t start_time,
                                             const ThreadRef<thread_type>& thread_ref,
                                             const StringRef<category_type>& category_ref,
                                             const StringRef<name_type>& name_ref,
                                             uint64_t end_time,
                                             Argument<arg_types, ref_types>... args) {
  return internal::WriteOneWordEventRecord(writer, start_time, thread_ref, category_ref, name_ref,
                                           fxt::EventType::kDurationComplete, end_time, args...);
}

// Write an Async Begin Event using the given Writer
//
// An Async Begin event marks the beginning of an operation that may span
// threads. Must be matched by an async end event using the same async
// correlation id.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#async-begin-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteAsyncBeginEventRecord(Writer* writer, uint64_t event_time,
                                       const ThreadRef<thread_type>& thread_ref,
                                       const StringRef<category_type>& category_ref,
                                       const StringRef<name_type>& name_ref, uint64_t async_id,
                                       Argument<arg_types, ref_types>... args) {
  return internal::WriteOneWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                           fxt::EventType::kAsyncBegin, async_id, args...);
}

// Write an Async Instant Event using the given Writer
//
// An Async Instant Event marks a moment within an operation that may span
// threads. Must appear between async begin event and async end event using the
// same async correlation id.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#async-instant-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteAsyncInstantEventRecord(Writer* writer, uint64_t event_time,
                                         const ThreadRef<thread_type>& thread_ref,
                                         const StringRef<category_type>& category_ref,
                                         const StringRef<name_type>& name_ref, uint64_t async_id,
                                         Argument<arg_types, ref_types>... args) {
  return internal::WriteOneWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                           fxt::EventType::kAsyncInstant, async_id, args...);
}

// Write an Async End Event using the given Writer
//
// An Async End event marks the end of an operation that may span
// threads.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#async-end-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteAsyncEndEventRecord(Writer* writer, uint64_t event_time,
                                     const ThreadRef<thread_type>& thread_ref,
                                     const StringRef<category_type>& category_ref,
                                     const StringRef<name_type>& name_ref, uint64_t async_id,
                                     Argument<arg_types, ref_types>... args) {
  return internal::WriteOneWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                           fxt::EventType::kAsyncEnd, async_id, args...);
}

// Write a Flow Begin Event to the given Writer
//
// A Flow Begin Event marks the beginning of an operation, which results in a
// sequence of actions that may span multiple threads or abstraction layers.
// Must be matched by a flow end event using the same flow correlation id. This
// can be envisioned as an arrow between duration events. The beginning of the
// flow is associated with the enclosing duration event for this thread; it
// begins where the enclosing duration event ends.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#flow-begin-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteFlowBeginEventRecord(Writer* writer, uint64_t event_time,
                                      const ThreadRef<thread_type>& thread_ref,
                                      const StringRef<category_type>& category_ref,
                                      const StringRef<name_type>& name_ref, uint64_t flow_id,
                                      Argument<arg_types, ref_types>... args) {
  return internal::WriteOneWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                           fxt::EventType::kFlowBegin, flow_id, args...);
}

// Write a Flow Step Event to the given Writer
//
// Marks a point within a flow. The step is associated with the enclosing
// duration event for this thread; the flow resumes where the enclosing
// duration event begins then is suspended at the point where the enclosing
// duration event event ends.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#flow-step-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteFlowStepEventRecord(Writer* writer, uint64_t event_time,
                                     const ThreadRef<thread_type>& thread_ref,
                                     const StringRef<category_type>& category_ref,
                                     const StringRef<name_type>& name_ref, uint64_t flow_id,
                                     Argument<arg_types, ref_types>... args) {
  return internal::WriteOneWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                           fxt::EventType::kFlowStep, flow_id, args...);
}

// Write a Flow End Event to the given Writer
//
// Marks the end of a flow. The end of the flow is associated with the
// enclosing duration event for this thread; the flow resumes where the
// enclosing duration event begins.
//
// See also: https://fuchsia.dev/fuchsia-src/reference/tracing/trace-format#flow-end-event
template <typename Writer, internal::EnableIfWriter<Writer> = 0, RefType thread_type,
          RefType name_type, RefType category_type, ArgumentType... arg_types, RefType... ref_types>
zx_status_t WriteFlowEndEventRecord(Writer* writer, uint64_t event_time,
                                    const ThreadRef<thread_type>& thread_ref,
                                    const StringRef<category_type>& category_ref,
                                    const StringRef<name_type>& name_ref, uint64_t flow_id,
                                    Argument<arg_types, ref_types>... args) {
  return internal::WriteOneWordEventRecord(writer, event_time, thread_ref, category_ref, name_ref,
                                           fxt::EventType::kFlowEnd, flow_id, args...);
}
}  // namespace fxt

#endif  // SRC_LIB_FXT_INCLUDE_LIB_FXT_SERIALIZER_H_
