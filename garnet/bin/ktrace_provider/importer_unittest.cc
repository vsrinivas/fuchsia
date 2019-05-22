// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/importer.h"

#include <fbl/algorithm.h>
#include <gtest/gtest.h>
#include <trace-engine/instrumentation.h>
#include <trace-test-utils/fixture.h>

#include "garnet/bin/ktrace_provider/test_reader.h"

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
    fixture_set_up(kNoAttachToThread, TRACE_BUFFERING_MODE_ONESHOT,
                   kFxtBufferSize);
    fixture_start_tracing();
    context_ = trace_acquire_context();
    ASSERT_NE(context_, nullptr);
  }

  void StopTracing() {
    if (context_) {
      trace_release_context(context_);
      context_ = nullptr;
    }
    fixture_stop_tracing();
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
          if (kobj.object_type == ZX_OBJ_TYPE_PROCESS && kobj.koid == 0u &&
              kobj.name == "kernel") {
            skipping = false;
          }
        }
      } else {
        out_records->push_back(std::move(rec));
      }
    }

    // We should have found the kernel process record.
    if (skipping) {
      FXL_VLOG(1) << "Kernel process record not found";
      return false;
    }

    return true;
  }

  void CompareRecord(const trace::Record& rec, const char* expected) {
    EXPECT_STREQ(rec.ToString().c_str(), expected);
  }

  void EmitKtraceRecord(const void* record, size_t record_size) {
    ASSERT_LE(record_size, KtraceAvailableBytes());
    memcpy(ktrace_buffer_next_, record, record_size);
    ktrace_buffer_next_ += record_size;
  }

  void EmitKtrace32Record(uint32_t tag, uint32_t tid, uint64_t ts, uint32_t a,
                          uint32_t b, uint32_t c, uint32_t d) {
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

  void EmitContextSwitchRecord(uint64_t ts, uint32_t old_thread_tid,
                               uint32_t new_thread_tid, uint8_t cpu,
                               KernelThreadState old_thread_state,
                               uint8_t old_thread_prio, uint8_t new_thread_prio,
                               uint32_t new_kernel_thread) {
    uint32_t old_kernel_thread = 0;  // importer ignores this
    EmitKtrace32Record(TAG_CONTEXT_SWITCH, old_thread_tid, ts, new_thread_tid,
                       (cpu | (static_cast<uint8_t>(old_thread_state) << 8) |
                        (old_thread_prio << 16) | (new_thread_prio << 24)),
                       old_kernel_thread, new_kernel_thread);
  }

  void EmitInheritPriorityStartRecord(uint64_t ts, uint32_t event_id,
                                      uint8_t cpu_id) {
    EmitKtrace32Record(TAG_INHERIT_PRIORITY_START, 0, ts, event_id, 0, 0,
                       cpu_id);
  }

  void EmitInheritPriorityRecord(uint64_t ts, uint32_t event_id, uint32_t tid,
                                 int8_t old_effective, int8_t new_effective,
                                 int8_t old_inherited, int8_t new_inherited,
                                 uint8_t cpu_id, bool is_kernel_tid,
                                 bool final_event) {
    const uint32_t prios = (static_cast<uint32_t>(old_effective) << 0) |
                           (static_cast<uint32_t>(new_effective) << 8) |
                           (static_cast<uint32_t>(old_inherited) << 16) |
                           (static_cast<uint32_t>(new_inherited) << 24);
    const uint32_t flags =
        cpu_id |
        (is_kernel_tid ? KTRACE_FLAGS_INHERIT_PRIORITY_KERNEL_TID : 0) |
        (final_event ? KTRACE_FLAGS_INHERIT_PRIORITY_FINAL_EVT : 0);

    EmitKtrace32Record(TAG_INHERIT_PRIORITY, 0, ts, event_id, tid, prios,
                       flags);
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

  size_t ktrace_buffer_written() const {
    return ktrace_buffer_next_ - ktrace_buffer_;
  }

 private:
  static constexpr size_t kKtraceBufferSize = 65536;
  static constexpr size_t kFxtBufferSize = 65536;

  size_t KtraceAvailableBytes() {
    return std::distance(ktrace_buffer_next_, ktrace_buffer_end_);
  }

  char ktrace_buffer_[kKtraceBufferSize];
  char* ktrace_buffer_next_ = ktrace_buffer_;
  char* ktrace_buffer_end_ = ktrace_buffer_ + kKtraceBufferSize;
  trace_context_t* context_ = nullptr;
};

TEST_F(TestImporter, ContextSwitch) {
  // Establish initial running thread.
  EmitContextSwitchRecord(
      99,                                 // ts
      0,                                  // old_thread_tid
      42,                                 // new_thread_tid
      1,                                  // cpu
      KernelThreadState::THREAD_RUNNING,  // old_thread_state
      3,                                  // old_thread_prio
      4,                                  // new_thread_prio
      0);
  // Test switching to user thread.
  EmitContextSwitchRecord(
      100,                                // ts
      42,                                 // old_thread_tid
      43,                                 // new_thread_tid
      1,                                  // cpu
      KernelThreadState::THREAD_RUNNING,  // old_thread_state
      5,                                  // old_thread_prio
      6,                                  // new_thread_prio
      0);
  // Test switching to kernel thread.
  EmitContextSwitchRecord(
      101,                                // ts
      43,                                 // old_thread_tid
      0,                                  // 0 --> kernel thread
      1,                                  // cpu
      KernelThreadState::THREAD_RUNNING,  // old_thread_state
      7,                                  // old_thread_prio
      8,                                  // new_thread_prio
      12345678);
  static const char* const expected[] = {
      "ContextSwitch(ts: 99, cpu: 1, os: running, opt: 0/0, ipt: 0/42, oprio: "
      "3, iprio: 4)",
      "ContextSwitch(ts: 100, cpu: 1, os: running, opt: 0/42, ipt: 0/43, "
      "oprio: 5, iprio: 6)",
      // 4307312974 = 12345678 | kKernelThreadFlag
      "ContextSwitch(ts: 101, cpu: 1, os: running, opt: 0/43, ipt: "
      "0/4307312974, oprio: 7, iprio: 8)",
  };

  fbl::Vector<trace::Record> records;
  ASSERT_TRUE(StopTracingAndImportRecords(&records));
  ASSERT_EQ(records.size(), fbl::count_of(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    CompareRecord(records[i], expected[i]);
  }
}

TEST_F(TestImporter, InheritPriority) {
  // Emit the record which starts the flow and identifies the initiator
  EmitInheritPriorityStartRecord(100,    // ts
                                 12345,  // evt_id
                                 1);     // cpu
  // Emit a record linked by the event id which shows a thread receiveing
  // pressure from a wait queue.  Indicate that the target thread is a kernel
  // thread.  Do not inicate that this is the last event in the flow.
  EmitInheritPriorityRecord(200,     // ts
                            12345,   // evt_id
                            10001,   // tid
                            16,      // old effective
                            20,      // new effective
                            -1,      // old inherited
                            20,      // new inherited
                            1,       // cpu
                            true,    // is_kernel_tid,
                            false);  // final_event
  // Emit another record linked by the event id.  Indicate that the target
  // thread is a user-mode thread and that this is the last event in the flow.
  EmitInheritPriorityRecord(300,      // ts
                            12345,    // evt_id
                            8765432,  // tid
                            18,       // old effective
                            20,       // new effective
                            18,       // old inherited
                            20,       // new inherited
                            1,        // cpu
                            false,    // is_kernel_tid,
                            true);    // final_event
  static const char* const expected[] = {
      "Event(ts: 50, pt: 0/0, category: \"kernel:sched\", name: "
      "\"inherit_prio\", DurationComplete(end_ts: 100), {})",
      "Event(ts: 90, pt: 0/0, category: \"kernel:sched\", name: "
      "\"inherit_prio\", FlowBegin(id: 12345), {})",
      "Event(ts: 200, pt: 0/4294977297, category: \"kernel:sched\", name: "
      "\"inherit_prio\", DurationComplete(end_ts: 250), {old_inherited_prio: "
      "int32(-1), new_inherited_prio: int32(-1), old_effective_prio: "
      "int32(16), new_effective_prio: int32(20)})",
      "Event(ts: 210, pt: 0/4294977297, category: \"kernel:sched\", name: "
      "\"inherit_prio\", FlowStep(id: 12345), {})",
      "Event(ts: 300, pt: 0/8765432, category: \"kernel:sched\", name: "
      "\"inherit_prio\", DurationComplete(end_ts: 350), {old_inherited_prio: "
      "int32(18), new_inherited_prio: int32(20), old_effective_prio: "
      "int32(18), new_effective_prio: int32(20)})",
      "Event(ts: 310, pt: 0/8765432, category: \"kernel:sched\", name: "
      "\"inherit_prio\", FlowEnd(id: 12345), {})",
  };

  fbl::Vector<trace::Record> records;
  ASSERT_TRUE(StopTracingAndImportRecords(&records));
  ASSERT_EQ(records.size(), fbl::count_of(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    CompareRecord(records[i], expected[i]);
  }
}

}  // namespace
}  // namespace ktrace_provider
