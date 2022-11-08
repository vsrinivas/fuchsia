// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/ktrace_provider/importer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace-engine/instrumentation.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>
#include <trace-test-utils/fixture.h>

#include "src/performance/ktrace_provider/test_reader.h"

namespace ktrace_provider {
namespace {

class TestImporter : public ::testing::Test {
 public:
  // A copy of kernel/thread.h:thread_state values we use.
  enum class KernelThreadState : uint8_t {
    // The naming style chosen here is to be consistent with thread.h.
    // If its values change, just re-cut-n-paste.
    THREAD_INITIAL = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_BLOCKED_READ_LOCK,
    THREAD_SLEEPING,
    THREAD_SUSPENDED,
    THREAD_DEATH,
  };

  void SetUp() {
    fixture_set_up(kNoAttachToThread, TRACE_BUFFERING_MODE_ONESHOT, kFxtBufferSize);
    fixture_initialize_and_start_tracing();
    context_ = trace_acquire_context();
    ASSERT_NE(context_, nullptr);
  }

  void StopTracing() {
    if (context_) {
      trace_release_context(context_);
      context_ = nullptr;
    }
    fixture_stop_and_terminate_tracing();
  }

  void TearDown() {
    // Stop tracing maybe again just in case.
    StopTracing();
    fixture_tear_down();
  }

  // Extract the records in the buffer, discarding administrative records
  // that the importer creates.
  // TODO(dje): Use std::vector when fixture is ready.
  bool ExtractRecords(fbl::Vector<trace::Record>* out_records) {
    fbl::Vector<trace::Record> records;

    if (!fixture_read_records(&records)) {
      return false;
    }

    // The kernel process record is the last administrative record.
    // Drop every record up to and including that one.
    bool skipping = true;
    for (auto& rec : records) {
      if (skipping) {
        if (rec.type() == trace::RecordType::kKernelObject) {
          const trace::Record::KernelObject& kobj = rec.GetKernelObject();
          if (kobj.object_type == ZX_OBJ_TYPE_PROCESS && kobj.koid == 0u && kobj.name == "kernel") {
            skipping = false;
          }
        }
      } else {
        out_records->push_back(std::move(rec));
      }
    }

    // We should have found the kernel process record.
    if (skipping) {
      FX_VLOGS(1) << "Kernel process record not found";
      return false;
    }

    return true;
  }

#define COMPARE_RECORD(rec, expected) EXPECT_STREQ((rec).ToString().c_str(), (expected))

  void EmitKtraceRecord(const void* record, size_t record_size) {
    ASSERT_LE(record_size, KtraceAvailableBytes());
    memcpy(ktrace_buffer_next_, record, record_size);
    ktrace_buffer_next_ += record_size;
  }

  void EmitKtrace32Record(uint32_t tag, uint32_t tid, uint64_t ts, uint32_t a, uint32_t b,
                          uint32_t c, uint32_t d) {
    const ktrace_rec_32b record{
        .tag = tag,
        .tid = tid,
        .ts = ts,
        .a = a,
        .b = b,
        .c = c,
        .d = d,
    };
    EmitKtraceRecord(&record, sizeof(record));
  }

  void EmitKtrace32Record(uint32_t tag, uint32_t tid, uint64_t ts, uint64_t a, uint64_t b) {
    const ktrace_rec_32b record{
        .tag = tag,
        .tid = tid,
        .ts = ts,
        .a = static_cast<uint32_t>(a),
        .b = static_cast<uint32_t>(a >> 32),
        .c = static_cast<uint32_t>(b),
        .d = static_cast<uint32_t>(b >> 32),
    };
    EmitKtraceRecord(&record, sizeof(record));
  }

  void EmitKernelCounterRecord(uint64_t ts, uint8_t cpu_id, uint8_t group, int string_ref,
                               int64_t value, uint64_t counter_id) {
    const uint32_t tag = KTRACE_TAG_FLAGS(TAG_COUNTER(string_ref, group), KTRACE_FLAGS_CPU);
    EmitKtrace32Record(tag, cpu_id, ts, counter_id, value);
  }

  bool StopTracingAndImportRecords(fbl::Vector<trace::Record>* out_records) {
    TestReader reader{ktrace_buffer(), ktrace_buffer_written()};
    Importer importer{context()};

    if (!importer.Import(reader)) {
      return false;
    }

    // Do this after importing as the importer needs tracing to be running in
    // order to acquire a "context" with which to write records.
    StopTracing();

    return ExtractRecords(out_records);
  }

  trace_context_t* context() const { return context_; }

  const char* ktrace_buffer() const { return ktrace_buffer_; }

  size_t ktrace_buffer_written() const { return ktrace_buffer_next_ - ktrace_buffer_; }

 private:
  static constexpr size_t kKtraceBufferSize = 65536;
  static constexpr size_t kFxtBufferSize = 65536;

  size_t KtraceAvailableBytes() { return std::distance(ktrace_buffer_next_, ktrace_buffer_end_); }

  char ktrace_buffer_[kKtraceBufferSize] __ALIGNED(alignof(ktrace_header_t));
  char* ktrace_buffer_next_ = ktrace_buffer_;
  char* ktrace_buffer_end_ = ktrace_buffer_ + kKtraceBufferSize;
  trace_context_t* context_ = nullptr;
};

TEST_F(TestImporter, Counter) {
  EmitKernelCounterRecord(99,              // ts
                          0,               // cpu_id
                          KTRACE_GRP_IPC,  // group
                          0,               // string_ref
                          10,              // value
                          0                // counter_id
  );
  EmitKernelCounterRecord(100,               // ts
                          1,                 // cpu_id
                          KTRACE_GRP_TASKS,  // group
                          1,                 // string_ref
                          20,                // value
                          1                  // counter_id
  );
  EmitKernelCounterRecord(101,             // ts
                          3,               // cpu_id
                          KTRACE_GRP_IRQ,  // group
                          2,               // string_ref
                          30,              // value
                          2                // counter_id
  );
  static const char* const expected[] = {
      "String(index: 17, \"process\")",
      "KernelObject(koid: 1895825408, type: thread, name: \"cpu-0\", {process: koid(0)})",
      "Thread(index: 1, 0/1895825408)",
      "String(index: 18, \"probe 0\")",
      "Event(ts: 99, pt: 0/1895825408, category: \"kernel:ipc\", name: \"probe 0\", Counter(id: "
      "0), {arg0: int64(10)})",
      "KernelObject(koid: 1895825409, type: thread, name: \"cpu-1\", {process: koid(0)})",
      "Thread(index: 2, 0/1895825409)",
      "String(index: 19, \"probe 0x1\")",
      "Event(ts: 100, pt: 0/1895825409, category: \"kernel:tasks\", name: \"probe 0x1\", "
      "Counter(id: 1), {arg0: int64(20)})",
      "KernelObject(koid: 1895825411, type: thread, name: \"cpu-3\", {process: koid(0)})",
      "Thread(index: 3, 0/1895825411)",
      "String(index: 20, \"probe 0x2\")",
      "Event(ts: 101, pt: 0/1895825411, category: \"kernel:irq\", name: \"probe 0x2\", Counter(id: "
      "2), {arg0: int64(30)})",
  };

  fbl::Vector<trace::Record> records;
  ASSERT_TRUE(StopTracingAndImportRecords(&records));
  ASSERT_EQ(records.size(), std::size(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    COMPARE_RECORD(records[i], expected[i]);
  }
}

TEST_F(TestImporter, SkipPlaceholder) {
  // This record should be output
  EmitKernelCounterRecord(99,              // ts
                          0,               // cpu_id
                          KTRACE_GRP_IPC,  // group
                          5,               // string_ref
                          10,              // value
                          8                // counter_id
  );
  // This record has a group of 0, and should be skipped as a placeholder.
  EmitKernelCounterRecord(100,  // ts
                          0,    // cpu_id
                          0,    // group
                          6,    // string_ref
                          20,   // value
                          9     // counter_id
  );
  // This record should be output
  EmitKernelCounterRecord(101,             // ts
                          0,               // cpu_id
                          KTRACE_GRP_IRQ,  // group
                          7,               // string_ref
                          30,              // value
                          10               // counter_id
  );

  static const char* const expected[] = {
      // Records generated for us to identify the "cpu-0" thread.
      "String(index: 17, \"process\")",
      "KernelObject(koid: 1895825408, type: thread, name: \"cpu-0\", {process: koid(0)})",
      "Thread(index: 1, 0/1895825408)",

      // The first expected record.
      "String(index: 18, \"probe 0x5\")",
      "Event(ts: 99, pt: 0/1895825408, category: \"kernel:ipc\", name: \"probe 0x5\", Counter(id: "
      "8), {arg0: int64(10)})",

      // The second record will be skipped.
      /*
      "String(index: 54, \"probe 0x6\")",
      "Event(ts: 100, pt: 0/1895825408, category: \"\", name: \"probe 0x6\", "
      "9), {arg0: int64(20)})",
      */

      // The final record.
      "String(index: 19, \"probe 0x7\")",
      "Event(ts: 101, pt: 0/1895825408, category: \"kernel:irq\", name: \"probe 0x7\", Counter(id: "
      "10), {arg0: int64(30)})",
  };

  fbl::Vector<trace::Record> records;
  ASSERT_TRUE(StopTracingAndImportRecords(&records));
  ASSERT_EQ(records.size(), std::size(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    COMPARE_RECORD(records[i], expected[i]);
  }
}

TEST_F(TestImporter, ZeroLenRecords) {
  // Attempt to output 3 counter records, but encode a length of 0 in the tag
  // for the second record. This should cause the importer to terminate
  // processing early, and produce a trace with only the first record in it.
  EmitKernelCounterRecord(99,              // ts
                          0,               // cpu_id
                          KTRACE_GRP_IPC,  // group
                          5,               // string_ref
                          10,              // value
                          8                // counter_id
  );

  // Construct a tag identical to the previous record, but force the length to
  // be 0.
  constexpr uint32_t kValidTag = KTRACE_TAG_FLAGS(TAG_COUNTER(5, KTRACE_GRP_IPC), KTRACE_FLAGS_CPU);
  constexpr uint32_t kZeroLenTag =
      KTRACE_TAG_EX(KTRACE_EVENT(kValidTag), KTRACE_GROUP(kValidTag), 0, KTRACE_FLAGS(kValidTag));
  EmitKtrace32Record(kZeroLenTag,
                     0,                         // cpu_id
                     100,                       // ts
                     static_cast<uint64_t>(9),  // counter_id
                     static_cast<uint64_t>(20)  // value
  );

  // This record will never make it to the output.
  EmitKernelCounterRecord(101,             // ts
                          0,               // cpu_id
                          KTRACE_GRP_IRQ,  // group
                          7,               // string_ref
                          30,              // value
                          10               // counter_id
  );

  static const char* const expected[] = {
      // Records generated for us to identify the "cpu-0" thread.
      "String(index: 17, \"process\")",
      "KernelObject(koid: 1895825408, type: thread, name: \"cpu-0\", {process: koid(0)})",
      "Thread(index: 1, 0/1895825408)",

      // The first expected record.
      "String(index: 18, \"probe 0x5\")",
      ("Event(ts: 99, pt: 0/1895825408, category: \"kernel:ipc\", name: \"probe 0x5\", Counter(id: "
       "8), {arg0: int64(10)})"),

      // No other records should be output.
  };

  fbl::Vector<trace::Record> records;
  ASSERT_TRUE(StopTracingAndImportRecords(&records));
  ASSERT_EQ(records.size(), std::size(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    COMPARE_RECORD(records[i], expected[i]);
  }
}

}  // namespace
}  // namespace ktrace_provider
