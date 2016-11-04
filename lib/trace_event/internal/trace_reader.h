// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_LIB_TRACE_EVENT_INTERNAL_TRACE_READER_H_
#define APPS_TRACING_LIB_TRACE_EVENT_INTERNAL_TRACE_READER_H_

#include <stdint.h>

#include <unordered_map>
#include <vector>

#include "apps/tracing/lib/trace_event/internal/trace_types.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace tracing {
namespace internal {

using Chunk = std::vector<uint64_t>;

class InputReader {
 public:
  virtual ~InputReader();
  virtual bool Read(void* destination, size_t size) = 0;

 protected:
  InputReader();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(InputReader);
};

class MemoryInputReader : public InputReader {
 public:
  explicit MemoryInputReader(void* memory, size_t size);

  // |InputReader| implementation.
  bool Read(void* destination, size_t size) override;

 private:
  uintptr_t current_;
  uintptr_t end_;
};

class ChunkInputReader {
 public:
  explicit ChunkInputReader(Chunk::const_iterator begin,
                            Chunk::const_iterator end);

  explicit operator bool() const;

  uint64_t Get();
  bool Read(size_t words, Chunk& chunk);
  bool Read(size_t words, uint64_t* to);

  Chunk::const_iterator current() const { return current_; }

 private:
  Chunk::const_iterator current_;
  Chunk::const_iterator end_;
};

struct Thread {
  explicit operator bool() const {
    return thread_koid != 0 && process_koid != 0;
  }

  uint64_t thread_koid;
  uint64_t process_koid;
};

class StringTable {
 public:
  std::string DecodeString(uint16_t index, ChunkInputReader& reader) const;
  void Register(uint16_t index, const std::string& string);

 private:
  std::unordered_map<uint16_t, std::string> table_;
};

class ThreadTable {
 public:
  Thread DecodeThread(uint16_t index, ChunkInputReader& reader) const;
  void Register(uint16_t index, const Thread& thread);

 private:
  std::unordered_map<uint16_t, Thread> table_;
};

template <typename T>
struct Argument {
  std::string name;
  T value;
};

template <>
struct Argument<void> {
  std::string name;
};

class ArgumentVisitor {
 public:
  virtual ~ArgumentVisitor();

  virtual void operator()(const Argument<void>&) = 0;
  virtual void operator()(const Argument<int32_t>&) = 0;
  virtual void operator()(const Argument<int64_t>&) = 0;
  virtual void operator()(const Argument<double>&) = 0;
  virtual void operator()(const Argument<std::string>&) = 0;
  virtual void operator()(const Argument<const void*>&) = 0;
  virtual void operator()(const Argument<uint64_t>&) = 0;

 protected:
  ArgumentVisitor();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ArgumentVisitor);
};

class ArgumentPrinter : public ArgumentVisitor {
 public:
  void operator()(const Argument<void>&) override;
  void operator()(const Argument<int32_t>&) override;
  void operator()(const Argument<int64_t>&) override;
  void operator()(const Argument<double>&) override;
  void operator()(const Argument<std::string>&) override;
  void operator()(const Argument<const void*>&) override;
  void operator()(const Argument<uint64_t>&) override;
};

class ArgumentReader {
 public:
  explicit ArgumentReader(const ChunkInputReader& reader,
                          const StringTable& table);

  void ForEachArgument(ArgumentVisitor& visitor);

 private:
  void HandleNullArgument(ArgumentHeader header,
                          const std::string& name,
                          ChunkInputReader& payload,
                          ArgumentVisitor& visitor);

  void HandleInt32Argument(ArgumentHeader header,
                           const std::string& name,
                           ChunkInputReader& payload,
                           ArgumentVisitor& visitor);

  void HandleInt64Argument(ArgumentHeader header,
                           const std::string& name,
                           ChunkInputReader& payload,
                           ArgumentVisitor& visitor);

  void HandleDoubleArgument(ArgumentHeader header,
                            const std::string& name,
                            ChunkInputReader& payload,
                            ArgumentVisitor& visitor);

  void HandleStringArgument(ArgumentHeader header,
                            const std::string& name,
                            ChunkInputReader& payload,
                            ArgumentVisitor& visitor);

  void HandlePointerArgument(ArgumentHeader header,
                             const std::string& name,
                             ChunkInputReader& payload,
                             ArgumentVisitor& visitor);

  void HandleKernelObjectIdArgument(ArgumentHeader header,
                                    const std::string& name,
                                    ChunkInputReader& payload,
                                    ArgumentVisitor& visitor);

  ChunkInputReader reader_;
  StringTable string_table_;
};

struct DurationBegin {};
struct DurationEnd {};
struct AsyncBegin {
  uint64_t id;
};
struct AsyncInstant {
  uint64_t id;
};
struct AsyncEnd {
  uint64_t id;
};

class EventVisitor {
 public:
  virtual ~EventVisitor();

  virtual void operator()(const DurationBegin&) = 0;
  virtual void operator()(const DurationEnd&) = 0;
  virtual void operator()(const AsyncBegin&) = 0;
  virtual void operator()(const AsyncInstant&) = 0;
  virtual void operator()(const AsyncEnd&) = 0;

 protected:
  EventVisitor();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(EventVisitor);
};

class EventPrinter : public EventVisitor {
 public:
  void operator()(const DurationBegin&) override;
  void operator()(const DurationEnd&) override;
  void operator()(const AsyncBegin&) override;
  void operator()(const AsyncInstant&) override;
  void operator()(const AsyncEnd&) override;
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
  void Visit(EventVisitor& visitor) const;
  void Visit(const StringTable& string_table, ArgumentVisitor& visit) const;

  TraceEventType event_type;
  uint64_t timestamp;
  Thread thread;
  std::string name;
  std::string cat;
  size_t argument_count;
  Chunk payload;
};

class RecordVisitor {
 public:
  virtual ~RecordVisitor();

  virtual void operator()(const InitializationRecord&) = 0;
  virtual void operator()(const StringRecord&) = 0;
  virtual void operator()(const ThreadRecord&) = 0;
  virtual void operator()(const EventRecord&) = 0;

 protected:
  RecordVisitor();

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(RecordVisitor);
};

class RecordPrinter : public RecordVisitor {
 public:
  void operator()(const InitializationRecord&) override;
  void operator()(const StringRecord&) override;
  void operator()(const ThreadRecord&) override;
  void operator()(const EventRecord& record) override;

 private:
  StringTable string_table_;
};

class TraceReader {
 public:
  void VisitEachRecord(InputReader& reader, RecordVisitor& visitor);

 private:
  void HandleInitializationRecord(RecordHeader header,
                                  const Chunk& payload,
                                  RecordVisitor& visitor);

  void HandleStringRecord(RecordHeader header,
                          const Chunk& payload,
                          RecordVisitor& visitor);

  void HandleThreadRecord(RecordHeader header,
                          const Chunk& payload,
                          RecordVisitor& visitor);

  void HandleEventRecord(RecordHeader header,
                         const Chunk& payload,
                         RecordVisitor& visitor);

  ThreadTable thread_table_;
  StringTable string_table_;
};

}  // namespace internal
}  // namespace tracing

#endif  // APPS_TRACING_LIB_TRACE_EVENT_INTERNAL_TRACE_READER_H_
