// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/importer.h"

#include <lib/trace-engine/instrumentation.h>

#include <iterator>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>
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

  void CompareRecord(const trace::Record& rec, const char* expected) {
    EXPECT_STREQ(rec.ToString().c_str(), expected);
  }

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

  void EmitContextSwitchRecord(uint64_t ts, uint32_t old_thread_tid, uint32_t new_thread_tid,
                               uint8_t cpu, KernelThreadState old_thread_state,
                               uint8_t old_thread_prio, uint8_t new_thread_prio,
                               uint32_t new_kernel_thread) {
    uint32_t old_kernel_thread = 0;  // importer ignores this
    EmitKtrace32Record(TAG_CONTEXT_SWITCH, old_thread_tid, ts, new_thread_tid,
                       (cpu | (static_cast<uint8_t>(old_thread_state) << 8) |
                        (old_thread_prio << 16) | (new_thread_prio << 24)),
                       old_kernel_thread, new_kernel_thread);
  }

  void EmitInheritPriorityStartRecord(uint64_t ts, uint32_t event_id, uint8_t cpu_id) {
    EmitKtrace32Record(TAG_INHERIT_PRIORITY_START, 0, ts, event_id, 0, 0, cpu_id);
  }

  void EmitInheritPriorityRecord(uint64_t ts, uint32_t event_id, uint32_t tid, int8_t old_effective,
                                 int8_t new_effective, int8_t old_inherited, int8_t new_inherited,
                                 uint8_t cpu_id, bool is_kernel_tid, bool final_event) {
    const uint32_t prios =
        (static_cast<uint32_t>(old_effective) << 0) | (static_cast<uint32_t>(new_effective) << 8) |
        (static_cast<uint32_t>(old_inherited) << 16) | (static_cast<uint32_t>(new_inherited) << 24);
    const uint32_t flags = cpu_id | (is_kernel_tid ? KTRACE_FLAGS_INHERIT_PRIORITY_KERNEL_TID : 0) |
                           (final_event ? KTRACE_FLAGS_INHERIT_PRIORITY_FINAL_EVT : 0);

    EmitKtrace32Record(TAG_INHERIT_PRIORITY, 0, ts, event_id, tid, prios, flags);
  }

  void EmitFutexWaitRecord(uint64_t ts, uint32_t futex_id_lo, uint32_t futex_id_hi,
                           uint32_t new_owner_tid, uint8_t cpu_id) {
    EmitKtrace32Record(TAG_FUTEX_WAIT, 0, ts, futex_id_lo, futex_id_hi, new_owner_tid, cpu_id);
  }

  void EmitFutexWokeRecord(uint64_t ts, uint32_t futex_id_lo, uint32_t futex_id_hi,
                           zx_status_t wait_result, uint8_t cpu_id) {
    EmitKtrace32Record(TAG_FUTEX_WOKE, 0, ts, futex_id_lo, futex_id_hi,
                       static_cast<uint32_t>(wait_result), cpu_id);
  }

  void EmitFutexWakeRecord(uint64_t ts, uint32_t futex_id_lo, uint32_t futex_id_hi,
                           uint32_t assigned_owner_tid, uint8_t cpu_id, uint8_t count,
                           bool requeue_op, bool futex_was_active) {
    uint32_t flags = cpu_id | (static_cast<uint32_t>(count) << KTRACE_FLAGS_FUTEX_COUNT_SHIFT) |
                     (requeue_op ? KTRACE_FLAGS_FUTEX_WAS_REQUEUE_FLAG : 0) |
                     (futex_was_active ? KTRACE_FLAGS_FUTEX_WAS_ACTIVE_FLAG : 0);
    EmitKtrace32Record(TAG_FUTEX_WAKE, 0, ts, futex_id_lo, futex_id_hi, assigned_owner_tid, flags);
  }

  void EmitFutexRequeueRecord(uint64_t ts, uint32_t futex_id_lo, uint32_t futex_id_hi,
                              uint32_t assigned_owner_tid, uint8_t cpu_id, uint8_t count,
                              bool futex_was_active) {
    uint32_t flags = cpu_id | (static_cast<uint32_t>(count) << KTRACE_FLAGS_FUTEX_COUNT_SHIFT) |
                     (futex_was_active ? KTRACE_FLAGS_FUTEX_WAS_ACTIVE_FLAG : 0);
    EmitKtrace32Record(TAG_FUTEX_REQUEUE, 0, ts, futex_id_lo, futex_id_hi, assigned_owner_tid,
                       flags);
  }

  void EmitKernelMutexRecord(uint32_t tag, uint64_t ts, uint32_t mutex_addr, uint32_t tid,
                             uint32_t threads_blocked, uint8_t cpu_id, bool user_mode_tid) {
    uint32_t flags = cpu_id | (user_mode_tid ? KTRACE_FLAGS_KERNEL_MUTEX_USER_MODE_TID : 0);

    EmitKtrace32Record(tag, 0, ts, mutex_addr, tid, threads_blocked, flags);
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

  char ktrace_buffer_[kKtraceBufferSize];
  char* ktrace_buffer_next_ = ktrace_buffer_;
  char* ktrace_buffer_end_ = ktrace_buffer_ + kKtraceBufferSize;
  trace_context_t* context_ = nullptr;
};

TEST_F(TestImporter, ContextSwitch) {
  // Establish initial running thread.
  EmitContextSwitchRecord(99,                                 // ts
                          0,                                  // old_thread_tid
                          42,                                 // new_thread_tid
                          1,                                  // cpu
                          KernelThreadState::THREAD_RUNNING,  // old_thread_state
                          3,                                  // old_thread_prio
                          4,                                  // new_thread_prio
                          0);
  // Test switching to user thread.
  EmitContextSwitchRecord(100,                                // ts
                          42,                                 // old_thread_tid
                          43,                                 // new_thread_tid
                          1,                                  // cpu
                          KernelThreadState::THREAD_RUNNING,  // old_thread_state
                          5,                                  // old_thread_prio
                          6,                                  // new_thread_prio
                          0);
  // Test switching to kernel thread.
  EmitContextSwitchRecord(101,                                // ts
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
  ASSERT_EQ(records.size(), std::size(expected));
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
  ASSERT_EQ(records.size(), std::size(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    CompareRecord(records[i], expected[i]);
  }
}

TEST_F(TestImporter, FutexRecords) {
  // Simulate a record of a thread waiting on a futex and declaring no owner.
  // futex_id should be 5 + (6 << 32) == 25769803781
  EmitFutexWaitRecord(100,  // ts
                      5,    // futex_lo
                      6,    // futex_hi
                      0,    // new owner tid
                      1);   // cpu_id

  // Simulate a record of a thread waiting on a futex and declaring an owner
  // tid == 12345
  EmitFutexWaitRecord(200,    // ts
                      5,      // futex_lo
                      6,      // futex_hi
                      12345,  // new owner tid
                      1);     // cpu_id

  // Simulate records of wake events.  Make sure to exercise cases where...
  // 1) Ownership is assigned to a specific thread vs. no thread.
  // 2) Finite specific wake counts, finite indeterminate counts, and unlimited
  //    counts.
  // 3) Wake events as part of a requeue op vs. wake ops
  // 4) Wake events where the futex was not active.
  EmitFutexWakeRecord(300,    // ts
                      5,      // futex_lo
                      6,      // futex_hi
                      12345,  // assigned owner tid
                      2,      // cpu_id
                      1,      // a finite count of 1
                      false,  // wake operation
                      true);  // active futex

  EmitFutexWakeRecord(400,    // ts
                      5,      // futex_lo
                      6,      // futex_hi
                      0,      // no owner assigned
                      2,      // cpu_id
                      0xFE,   // a finite, but indeterminate, count
                      true,   // requeue operation
                      true);  // active futex

  EmitFutexWakeRecord(500,     // ts
                      5,       // futex_lo
                      6,       // futex_hi
                      0,       // no owner assigned
                      3,       // cpu_id
                      0xFF,    // unlimited count
                      false,   // wake operation
                      false);  // inactive futex

  // Simulate records of a woke events.  Exercise a case where the woke record
  // reports a successful wait, and one where the wait timed out.  Switch up the
  // futex ID while we are at it.  We expect 45 + (88 << 32) == 377957122093
  EmitFutexWokeRecord(600,    // ts
                      45,     // futex_lo
                      88,     // futex_hi
                      ZX_OK,  // success status,
                      0);     // cpu_id

  EmitFutexWokeRecord(700,               // ts
                      45,                // futex_lo
                      88,                // futex_hi
                      ZX_ERR_TIMED_OUT,  // timeout status,
                      1);                // cpu_id

  // Simulate records of requeue events.  Make sure to exercise cases where...
  // 1) Ownership is assigned to a specific thread vs. no thread.
  // 2) Finite specific requeue counts, finite indeterminate counts, and
  //    unlimited counts.
  // 3) Requeue events where the futex was not active.
  EmitFutexRequeueRecord(800,    // ts
                         45,     // futex_lo
                         88,     // futex_hi
                         54321,  // assigned owner tid
                         2,      // cpu_id
                         1,      // a finite count of 1
                         true);  // active futex

  EmitFutexRequeueRecord(900,    // ts
                         45,     // futex_lo
                         88,     // futex_hi
                         0,      // no owner assigned
                         2,      // cpu_id
                         0xFE,   // a finite, but indeterminate, count
                         true);  // active futex

  EmitFutexRequeueRecord(1000,    // ts
                         45,      // futex_lo
                         88,      // futex_hi
                         0,       // no owner assigned
                         3,       // cpu_id
                         0xFF,    // unlimited count
                         false);  // inactive futex

  static const char* const expected[] = {
      // Wait events
      "Event(ts: 100, pt: 0/0, category: \"kernel:sched\", name: "
      "\"futex_wait\", DurationComplete(end_ts: 150), {futex_id: "
      "uint64(25769803781), new_owner_TID: uint32(0)})",

      "Event(ts: 200, pt: 0/0, category: \"kernel:sched\", name: "
      "\"futex_wait\", DurationComplete(end_ts: 250), {futex_id: "
      "uint64(25769803781), new_owner_TID: uint32(12345)})",

      // Wake events
      "Event(ts: 300, pt: 0/0, category: \"kernel:sched\", name: "
      "\"futex_wake\", DurationComplete(end_ts: 350), {futex_id: "
      "uint64(25769803781), new_owner_TID: uint32(12345), count: uint32(1), "
      "was_requeue: bool(false), futex_was_active: bool(true)})",

      "Event(ts: 400, pt: 0/0, category: \"kernel:sched\", name: "
      "\"futex_wake\", DurationComplete(end_ts: 450), {futex_id: "
      "uint64(25769803781), new_owner_TID: uint32(0), count: uint32(254), "
      "was_requeue: bool(true), futex_was_active: bool(true)})",

      "Event(ts: 500, pt: 0/0, category: \"kernel:sched\", name: "
      "\"futex_wake\", DurationComplete(end_ts: 550), {futex_id: "
      "uint64(25769803781), new_owner_TID: uint32(0), count: "
      "uint32(4294967295), was_requeue: bool(false), futex_was_active: "
      "bool(false)})",

      // Woke events
      "Event(ts: 600, pt: 0/0, category: \"kernel:sched\", name: "
      "\"Thread_woke_from_futex_wait\", DurationComplete(end_ts: 650), "
      "{futex_id: uint64(377957122093), wait_result: int32(0)})",
      "Event(ts: 700, pt: 0/0, category: \"kernel:sched\", name: "
      "\"Thread_woke_from_futex_wait\", DurationComplete(end_ts: 750), "
      "{futex_id: uint64(377957122093), wait_result: int32(-21)})",

      // Requeue events
      "Event(ts: 800, pt: 0/0, category: \"kernel:sched\", name: "
      "\"futex_requeue\", DurationComplete(end_ts: 850), {futex_id: "
      "uint64(377957122093), new_owner_TID: uint32(54321), count: uint32(1), "
      "futex_was_active: bool(true)})",
      "Event(ts: 900, pt: 0/0, category: \"kernel:sched\", name: "
      "\"futex_requeue\", DurationComplete(end_ts: 950), {futex_id: "
      "uint64(377957122093), new_owner_TID: uint32(0), count: uint32(254), "
      "futex_was_active: bool(true)})",
      "Event(ts: 1000, pt: 0/0, category: \"kernel:sched\", name: "
      "\"futex_requeue\", DurationComplete(end_ts: 1050), {futex_id: "
      "uint64(377957122093), new_owner_TID: uint32(0), count: "
      "uint32(4294967295), futex_was_active: bool(false)})",
  };

  fbl::Vector<trace::Record> records;
  ASSERT_TRUE(StopTracingAndImportRecords(&records));
  ASSERT_EQ(records.size(), std::size(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    CompareRecord(records[i], expected[i]);
  }
}

TEST_F(TestImporter, KernelMutexRecords) {
  // Emit records of the three main type: Acquire, Release, and Block
  const uint32_t TAGS[] = {
      TAG_KERNEL_MUTEX_ACQUIRE,
      TAG_KERNEL_MUTEX_RELEASE,
      TAG_KERNEL_MUTEX_BLOCK,
  };

  uint64_t ts = 0;
  for (auto tag : TAGS) {
    ts += 100;
    EmitKernelMutexRecord(tag,
                          ts,        // ts
                          87654321,  // mutex addr
                          77777777,  // tid
                          0,         // threads blocked
                          0,         // cpu_id
                          false);    // is user mode id

    ts += 100;
    EmitKernelMutexRecord(tag,
                          ts,        // ts
                          87654321,  // mutex addr
                          22222222,  // tid
                          1,         // threads blocked
                          1,         // cpu_id
                          true);     // is user mode id
  }

  static const char* const expected[] = {
      "Event(ts: 100, pt: 0/0, category: \"kernel:sched\", name: "
      "\"kernel_mutex_acquire\", DurationComplete(end_ts: 150), {mutex_id: "
      "uint32(87654321), tid: uint32(77777777), tid_type: "
      "string(\"kernel_mode\"), waiter_count: uint32(0)})",
      "Event(ts: 200, pt: 0/0, category: \"kernel:sched\", name: "
      "\"kernel_mutex_acquire\", DurationComplete(end_ts: 250), {mutex_id: "
      "uint32(87654321), tid: uint32(22222222), tid_type: "
      "string(\"user_mode\"), waiter_count: uint32(1)})",
      "Event(ts: 300, pt: 0/0, category: \"kernel:sched\", name: "
      "\"kernel_mutex_release\", DurationComplete(end_ts: 350), {mutex_id: "
      "uint32(87654321), tid: uint32(77777777), tid_type: "
      "string(\"kernel_mode\"), waiter_count: uint32(0)})",
      "Event(ts: 400, pt: 0/0, category: \"kernel:sched\", name: "
      "\"kernel_mutex_release\", DurationComplete(end_ts: 450), {mutex_id: "
      "uint32(87654321), tid: uint32(22222222), tid_type: "
      "string(\"user_mode\"), waiter_count: uint32(1)})",
      "Event(ts: 500, pt: 0/0, category: \"kernel:sched\", name: "
      "\"kernel_mutex_block\", DurationComplete(end_ts: 550), {mutex_id: "
      "uint32(87654321), tid: uint32(77777777), tid_type: "
      "string(\"kernel_mode\"), waiter_count: uint32(0)})",
      "Event(ts: 600, pt: 0/0, category: \"kernel:sched\", name: "
      "\"kernel_mutex_block\", DurationComplete(end_ts: 650), {mutex_id: "
      "uint32(87654321), tid: uint32(22222222), tid_type: "
      "string(\"user_mode\"), waiter_count: uint32(1)})",
  };

  fbl::Vector<trace::Record> records;
  ASSERT_TRUE(StopTracingAndImportRecords(&records));
  ASSERT_EQ(records.size(), std::size(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    CompareRecord(records[i], expected[i]);
  }
}

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
      "String(index: 51, \"process\")",
      "KernelObject(koid: 1895825408, type: thread, name: \"cpu-0\", {process: koid(0)})",
      "Thread(index: 1, 0/1895825408)",
      "String(index: 52, \"probe 0\")",
      "Event(ts: 99, pt: 0/1895825408, category: \"kernel:ipc\", name: \"probe 0\", Counter(id: "
      "0), {arg0: int64(10)})",
      "KernelObject(koid: 1895825409, type: thread, name: \"cpu-1\", {process: koid(0)})",
      "Thread(index: 2, 0/1895825409)",
      "String(index: 53, \"probe 0x1\")",
      "Event(ts: 100, pt: 0/1895825409, category: \"kernel:tasks\", name: \"probe 0x1\", "
      "Counter(id: 1), {arg0: int64(20)})",
      "KernelObject(koid: 1895825411, type: thread, name: \"cpu-3\", {process: koid(0)})",
      "Thread(index: 3, 0/1895825411)",
      "String(index: 54, \"probe 0x2\")",
      "Event(ts: 101, pt: 0/1895825411, category: \"kernel:irq\", name: \"probe 0x2\", Counter(id: "
      "2), {arg0: int64(30)})",
  };

  fbl::Vector<trace::Record> records;
  ASSERT_TRUE(StopTracingAndImportRecords(&records));
  ASSERT_EQ(records.size(), std::size(expected));
  for (size_t i = 0; i < records.size(); ++i) {
    CompareRecord(records[i], expected[i]);
  }
}

}  // namespace
}  // namespace ktrace_provider
