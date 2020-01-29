// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <io-scheduler/io-scheduler.h>
#include <zxtest/zxtest.h>

namespace ioscheduler {

enum class Stage { kStageInput = 0, kStageAcquired, kStageIssued, kStageCompleted, kStageReleased };

// Wrapper around StreamOp.
class TestOp : public fbl::DoublyLinkedListable<fbl::RefPtr<TestOp>>,
               public fbl::RefCounted<TestOp> {
 public:
  TestOp(uint32_t id, uint32_t stream_id, uint32_t group = kOpGroupNone)
      : id_(id), sop_(OpType::kOpTypeUnknown, stream_id, group, 0, this) {}

  void set_id(uint32_t id) { id_ = id; }
  uint32_t id() { return id_; }

  // Should the op return a failure status at issue?
  void set_should_fail(bool should_fail) { should_fail_ = should_fail; }
  bool should_fail() { return should_fail_; }

  void set_result(zx_status_t result) { sop_.set_result(result); }

  // Should the op execute asynchronously?
  void set_async(bool async) { async_ = async; }
  bool async() { return async_; }

  void set_completion_race(bool race) { completion_race_ = race; }
  bool completion_race() { return completion_race_; }

  void SetExpected() {
    if (should_fail_) {
      sop_.set_result(ZX_ERR_BAD_PATH);
    } else {
      sop_.set_result(ZX_OK);
    }
  }

  bool CheckExpected() {
    return (should_fail_ ? (sop_.result() != ZX_OK) : (sop_.result() == ZX_OK));
  }

  zx_status_t result() { return sop_.result(); }
  StreamOp* sop() { return &sop_; }

  Stage stage() { return stage_; }
  void set_stage(Stage stage) { stage_ = stage; }

 private:
  uint32_t id_ = 0;
  bool async_ = false;        // Should the op be completed asynchronously.
  bool should_fail_ = false;  // Should Issue() return an error for this op.
  bool completion_race_ = false;
  Stage stage_ = Stage::kStageInput;
  StreamOp sop_{};
};

using TopRef = fbl::RefPtr<TestOp>;

class IOSchedTestFixture : public zxtest::Test, public SchedulerClient {
 public:
  Scheduler* scheduler() { return sched_.get(); }

 protected:
  // Called before every test of this test case.
  void SetUp() override {
    fbl::AutoLock lock(&lock_);
    ASSERT_EQ(sched_, nullptr);
    end_requested_ = false;
    end_of_stream_ = false;
    in_total_ = 0;
    acquired_total_ = 0;
    issued_total_ = 0;
    completed_total_ = 0;
    released_total_ = 0;
    sched_.reset(new Scheduler());
  }

  // Called after every test of this test case.
  void TearDown() override {
    fbl::AutoLock lock(&lock_);
    in_list_.clear();
    acquired_list_.clear();
    issued_list_.clear();
    completed_list_.clear();
    released_list_.clear();
    sched_.reset();
  }

  // Return true or false roughly in the percentage requested.
  // Argument is percentage of trues returned.
  // 0 and 100 will always return false and true, respectively.
  bool RandomBool(uint32_t percent);

  void DoServeTest(uint32_t ops, bool async, uint32_t fail_random);
  void DoMultistreamTest(uint32_t async_pct);
  void DoInvalidStreamTest(uint32_t async_pct);

  void InsertOp(TopRef top);

  // Wait until all inserted ops have been acquired.
  void WaitAcquire();

  // Complete pending async requests.
  void CompleteAsync();
  bool CompleteOneAsync();

  void CheckExpectedResult();
  void CheckExpectedResultWithFailures(uint32_t acquire_failures);

  // Callback methods.
  bool CanReorder(StreamOp* first, StreamOp* second) override { return false; }

  zx_status_t Acquire(StreamOp** sop_list, size_t list_count, size_t* actual_count,
                      bool wait) override;
  zx_status_t Issue(StreamOp* sop) override;
  void Release(StreamOp* sop) override;
  void CancelAcquire() override;
  void Fatal() override;

  std::unique_ptr<Scheduler> sched_ = nullptr;

 private:
  void EndStreamLocked() __TA_REQUIRES(lock_);

  fbl::Mutex lock_;
  bool end_requested_ __TA_GUARDED(lock_) = false;  // Request closing the stream.
  bool end_of_stream_ __TA_GUARDED(lock_) = false;  // Stream has been closed.

  // Fields to track the ops passing through the stages of the pipeline.

  // Number of ops inserted into test fixture.
  uint32_t in_total_ __TA_GUARDED(lock_) = 0;

  // Number of ops pulled by scheduler via Acquire callback.
  uint32_t acquired_total_ __TA_GUARDED(lock_) = 0;

  // Number of ops seen via the Issue callback.
  uint32_t issued_total_ __TA_GUARDED(lock_) = 0;

  // Number of ops whose status has been reported as completed, either synchronously through Issue
  // return or via an AsyncComplete call.
  uint32_t completed_total_ __TA_GUARDED(lock_) = 0;

  // Number of ops released via the Release callback.
  uint32_t released_total_ __TA_GUARDED(lock_) = 0;

  // Event signalling ops are available in in_list_.
  // Used by the acquire callback to block on input.
  fbl::ConditionVariable in_avail_ __TA_GUARDED(lock_);

  // Event signalling all pending ops have been acquired.  Used by test shutdown threads to drain
  // the input pipeline.
  fbl::ConditionVariable acquired_all_ __TA_GUARDED(lock_);

  // Event signalling all acquired ops have been issued.
  fbl::ConditionVariable issued_all_ __TA_GUARDED(lock_);

  // Event signalling all acquired ops have been released.
  fbl::ConditionVariable released_all_ __TA_GUARDED(lock_);

  // List of ops inserted by the test but not yet acquired.
  fbl::DoublyLinkedList<TopRef> in_list_ __TA_GUARDED(lock_);

  // List of ops acquired by the scheduler but not yet issued.
  fbl::DoublyLinkedList<TopRef> acquired_list_ __TA_GUARDED(lock_);

  // List of ops issued by the scheduler but not yet complete.
  fbl::DoublyLinkedList<TopRef> issued_list_ __TA_GUARDED(lock_);

  // List of ops completed by the scheduler but not yet released.
  fbl::DoublyLinkedList<TopRef> completed_list_ __TA_GUARDED(lock_);

  // List of ops released by the scheduler.
  fbl::DoublyLinkedList<TopRef> released_list_ __TA_GUARDED(lock_);
};

void IOSchedTestFixture::InsertOp(TopRef top) {
  fbl::AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT(end_requested_ == false);
  bool was_empty = in_list_.is_empty();
  in_list_.push_back(std::move(top));
  in_total_++;
  if (was_empty) {
    in_avail_.Signal();
  }
}

void IOSchedTestFixture::EndStreamLocked() {
  // Request exit.
  end_requested_ = true;
  in_avail_.Signal();
}

bool IOSchedTestFixture::RandomBool(uint32_t percent) {
  if (percent == 0)
    return false;
  if (percent >= 100)
    return true;
  if ((static_cast<uint32_t>(rand()) % 100u) < percent)
    return true;
  return false;
}

void IOSchedTestFixture::WaitAcquire() {
  fbl::AutoLock lock(&lock_);
  EndStreamLocked();
  // Wait for acknowledgement and stream to be drained.
  while (!end_of_stream_ || !in_list_.is_empty()) {
    acquired_all_.Wait(&lock_);
  }
  ZX_DEBUG_ASSERT(in_list_.is_empty());
  ZX_DEBUG_ASSERT(in_total_ == acquired_total_);
}

bool IOSchedTestFixture::CompleteOneAsync() {
  fbl::AutoLock lock(&lock_);
  TopRef top = issued_list_.pop_front();
  if (top == nullptr) {
    return false;
  }
  top->SetExpected();
  StreamOp* sop = top->sop();
  top->set_stage(Stage::kStageCompleted);
  completed_list_.push_back(std::move(top));
  completed_total_++;
  lock.release();
  sched_->AsyncComplete(sop);
  return true;
}

void IOSchedTestFixture::CompleteAsync() {
  {
    fbl::AutoLock lock(&lock_);
    // Drain the input queue.
    while (!end_of_stream_ || !in_list_.is_empty()) {
      acquired_all_.Wait(&lock_);
    }
    // Wait for all ops to be issued.
    while (!acquired_list_.is_empty()) {
      issued_all_.Wait(&lock_);
    }
  }

  // Mark all pending async ops as complete.
  while (CompleteOneAsync()) {}

  {
    // Wait for all ops to be released.
    fbl::AutoLock lock(&lock_);
    while (released_total_ != acquired_total_) {
      released_all_.Wait(&lock_);
    }
  }
}

void IOSchedTestFixture::CheckExpectedResult() { CheckExpectedResultWithFailures(0); }

void IOSchedTestFixture::CheckExpectedResultWithFailures(uint32_t acquire_failures) {
  fbl::AutoLock lock(&lock_);
  ASSERT_EQ(in_total_, acquired_total_);
  ASSERT_EQ(in_total_, issued_total_ + acquire_failures);
  ASSERT_EQ(in_total_, completed_total_ + acquire_failures);
  ASSERT_EQ(in_total_, released_total_);
  ASSERT_TRUE(in_list_.is_empty());
  ASSERT_TRUE(acquired_list_.is_empty());
  ASSERT_TRUE(issued_list_.is_empty());
  ASSERT_TRUE(completed_list_.is_empty());
  for (;;) {
    TopRef top = released_list_.pop_front();
    if (top == nullptr) {
      break;
    }
    ASSERT_TRUE(top->CheckExpected());
  }
}

zx_status_t IOSchedTestFixture::Acquire(StreamOp** sop_list, size_t list_count,
                                        size_t* actual_count, bool wait) {
  fbl::AutoLock lock(&lock_);
  while (in_list_.is_empty()) {
    if (end_requested_) {
      end_of_stream_ = true;
      acquired_all_.Broadcast();
      return ZX_ERR_CANCELED;
    }
    if (!wait) {
      return ZX_ERR_SHOULD_WAIT;
    }
    in_avail_.Wait(&lock_);
  }

  size_t i = 0;
  for (; i < list_count; i++) {
    TopRef top = in_list_.pop_front();
    if (top == nullptr) {
      break;
    }
    top->set_stage(Stage::kStageAcquired);
    sop_list[i] = top->sop();
    acquired_list_.push_back(std::move(top));
  }
  acquired_total_ += static_cast<uint32_t>(i);
  *actual_count = i;
  return ZX_OK;
}

zx_status_t IOSchedTestFixture::Issue(StreamOp* sop) {
  bool early_complete = false;
  zx_status_t status = ZX_OK;

  fbl::AutoLock lock(&lock_);
  issued_total_++;
  TopRef top = acquired_list_.erase(*static_cast<TestOp*>(sop->cookie()));
  if (top->async()) {
    // Will be completed asynchronously.
    top->set_stage(Stage::kStageIssued);
    early_complete = top->completion_race();
    issued_list_.push_back(std::move(top));
    status = ZX_ERR_ASYNC;
  } else {
    // Executing op here...
    // Todo: pretend to do work here.
    top->SetExpected();
    top->set_stage(Stage::kStageCompleted);
    completed_list_.push_back(std::move(top));
    completed_total_++;
  }
  // Signal if all ops have been issued.
  if (end_of_stream_ && (acquired_total_ == issued_total_)) {
    issued_all_.Broadcast();
  }
  lock.release();

  if (early_complete) {
    CompleteOneAsync();
  }
  return status;
}

void IOSchedTestFixture::Release(StreamOp* sop) {
  fbl::AutoLock lock(&lock_);
  TestOp* top = static_cast<TestOp*>(sop->cookie());
  TopRef ref;
  Stage stage = top->stage();
  switch (stage) {
    case Stage::kStageAcquired:
      ref = acquired_list_.erase(*top);
      break;
    case Stage::kStageIssued:
      ref = issued_list_.erase(*top);
      break;
    case Stage::kStageCompleted:
      ref = completed_list_.erase(*top);
      break;
    default:
      fprintf(stderr, "Invalid op stage %u\n", static_cast<uint32_t>(stage));
      ZX_DEBUG_ASSERT(false);
      return;
  }
  top->set_stage(Stage::kStageReleased);
  released_list_.push_back(std::move(ref));
  released_total_++;
  if (end_of_stream_ && (acquired_total_ == released_total_)) {
    released_all_.Broadcast();
  }
}

void IOSchedTestFixture::CancelAcquire() {
  fbl::AutoLock lock(&lock_);
  if (!end_of_stream_) {
    EndStreamLocked();
  }
}

void IOSchedTestFixture::Fatal() { ZX_DEBUG_ASSERT(false); }

// Create and destroy scheduler.
TEST_F(IOSchedTestFixture, CreateTest) { ASSERT_TRUE(true); }

// Init scheduler.
TEST_F(IOSchedTestFixture, InitTest) {
  zx_status_t status = sched_->Init(this, kOptionStrictlyOrdered);
  ASSERT_OK(status, "Failed to init scheduler");
  sched_->Shutdown();
}

// Open streams.
TEST_F(IOSchedTestFixture, OpenTest) {
  zx_status_t status = sched_->Init(this, kOptionStrictlyOrdered);
  ASSERT_OK(status, "Failed to init scheduler");

  // Open streams.
  status = sched_->StreamOpen(5, kDefaultPriority);
  ASSERT_OK(status, "Failed to open stream");
  status = sched_->StreamOpen(0, kDefaultPriority);
  ASSERT_OK(status, "Failed to open stream");
  status = sched_->StreamOpen(5, kDefaultPriority);
  ASSERT_NOT_OK(status, "Expected failure to open duplicate stream");
  status = sched_->StreamOpen(3, 100000);
  ASSERT_NOT_OK(status, "Expected failure to open with invalid priority");
  status = sched_->StreamOpen(3, 1);
  ASSERT_OK(status, "Failed to open stream");

  // Close streams.
  status = sched_->StreamClose(5);
  ASSERT_OK(status, "Failed to close stream");
  status = sched_->StreamClose(3);
  ASSERT_OK(status, "Failed to close stream");
  // Stream 0 intentionally left open here.

  sched_->Shutdown();
}

void IOSchedTestFixture::DoServeTest(uint32_t num_ops, bool async, uint32_t fail_pct) {
  zx_status_t status = sched_->Init(this, kOptionStrictlyOrdered);
  ASSERT_OK(status, "Failed to init scheduler");
  status = sched_->StreamOpen(0, kDefaultPriority);
  ASSERT_OK(status, "Failed to open stream");

  for (uint32_t i = 0; i < num_ops; i++) {
    TopRef top = fbl::AdoptRef(new TestOp(i, 0));
    top->set_should_fail(RandomBool(fail_pct));
    top->set_async(async);
    InsertOp(std::move(top));
  }
  ASSERT_OK(sched_->Serve(), "Failed to begin service");

  // Wait until all ops have been acquired.
  WaitAcquire();
  if (async) {
    // Wait until all ops have been issued and complete pending async requests.
    CompleteAsync();
  }

  ASSERT_OK(sched_->StreamClose(0), "Failed to close stream");
  sched_->Shutdown();

  // Assert all ops completed.
  CheckExpectedResult();
}

TEST_F(IOSchedTestFixture, ServeTestSingle) { DoServeTest(1, false, 0); }
TEST_F(IOSchedTestFixture, ServeTestSingleAsync) { DoServeTest(1, true, 0); }
TEST_F(IOSchedTestFixture, ServeTestMulti) { DoServeTest(191, false, 0); }
TEST_F(IOSchedTestFixture, ServeTestMultiAsync) { DoServeTest(193, true, 0); }
TEST_F(IOSchedTestFixture, ServeTestMultiFailures) { DoServeTest(197, false, 10); }
TEST_F(IOSchedTestFixture, ServeTestMultiFailuresAsync) { DoServeTest(199, true, 10); }

// Test a race condition between issue and completion.
TEST_F(IOSchedTestFixture, AsyncCompletionRaceTest) {
  zx_status_t status = sched_->Init(this, kOptionStrictlyOrdered);
  ASSERT_OK(status, "Failed to init scheduler");
  status = sched_->StreamOpen(0, kDefaultPriority);
  ASSERT_OK(status, "Failed to open stream");
  ASSERT_OK(sched_->Serve(), "Failed to begin service");
  TopRef top = fbl::AdoptRef(new TestOp(99, 0));
  top->set_async(true);
  top->set_completion_race(true);
  InsertOp(std::move(top));
  // Wait until all ops have been acquired.
  WaitAcquire();
  ASSERT_OK(sched_->StreamClose(0), "Failed to close stream");
  sched_->Shutdown();
  // Assert all ops completed.
  CheckExpectedResult();
}

void IOSchedTestFixture::DoMultistreamTest(uint32_t async_pct) {
  zx_status_t status = sched_->Init(this, kOptionStrictlyOrdered);
  ASSERT_OK(status, "Failed to init scheduler");
  const uint32_t num_streams = 5;
  for (uint32_t i = 0; i < num_streams; i++) {
    status = sched_->StreamOpen(i, kDefaultPriority);
    ASSERT_OK(status, "Failed to open stream");
  }

  const uint32_t num_ops = num_streams * 1000;
  uint32_t op_id;
  // Add half of the ops before starting the server.
  for (op_id = 0; op_id < num_ops / 2; op_id++) {
    uint32_t stream_id = (static_cast<uint32_t>(rand()) % num_streams);
    TopRef top = fbl::AdoptRef(new TestOp(op_id, stream_id));
    top->set_async(RandomBool(async_pct));
    InsertOp(std::move(top));
  }

  ASSERT_OK(sched_->Serve(), "Failed to begin service");

  // Add other half while running.
  for (; op_id < num_ops; op_id++) {
    uint32_t stream_id = (static_cast<uint32_t>(rand()) % num_streams);
    TopRef top = fbl::AdoptRef(new TestOp(op_id, stream_id));
    InsertOp(std::move(top));
  }

  // Wait until all ops have been acquired.
  WaitAcquire();
  if (async_pct > 0) {
    // Wait until all ops have been issued and complete pending async requests.
    CompleteAsync();
  }

  ASSERT_OK(sched_->StreamClose(0), "Failed to close stream");
  // Other streams intentionally left open. Will be closed by Shutdown().
  sched_->Shutdown();

  // Assert all ops completed.
  CheckExpectedResult();
}

TEST_F(IOSchedTestFixture, ServeTestMultistream) { DoMultistreamTest(0); }
TEST_F(IOSchedTestFixture, ServeTestMultistreamAsync) { DoMultistreamTest(100); }
TEST_F(IOSchedTestFixture, ServeTestMultistreamMixed) { DoMultistreamTest(50); }

void IOSchedTestFixture::DoInvalidStreamTest(uint32_t async_pct) {
  zx_status_t status = sched_->Init(this, kOptionStrictlyOrdered);
  ASSERT_OK(status, "Failed to init scheduler");

  status = sched_->StreamOpen(1, kDefaultPriority);
  ASSERT_OK(status, "Failed to open stream");

  const uint32_t num_ops = 41;
  uint32_t num_failures = 0;
  for (uint32_t i = 0; i < num_ops; i++) {
    // Every other op has an invalid stream (0).
    uint32_t stream = i & 1;
    TopRef top = fbl::AdoptRef(new TestOp(i, stream));
    top->set_async(RandomBool(async_pct));
    if (stream == 0) {
      top->set_should_fail(true);
      num_failures++;
    }
    InsertOp(std::move(top));
  }
  ASSERT_OK(sched_->Serve(), "Failed to begin service");

  // Wait until all ops have been acquired.
  WaitAcquire();
  if (async_pct > 0) {
    // Wait until all ops have been issued and complete pending async requests.
    CompleteAsync();
  }

  ASSERT_OK(sched_->StreamClose(1), "Failed to close stream");
  sched_->Shutdown();

  // Assert all ops were released.
  CheckExpectedResultWithFailures(num_failures);
}

TEST_F(IOSchedTestFixture, ServeTestInvalidStreams) { DoInvalidStreamTest(0); }
TEST_F(IOSchedTestFixture, ServeTestInvalidStreamsAsync) { DoInvalidStreamTest(100); }
TEST_F(IOSchedTestFixture, ServeTestInvalidStreamsMixed) { DoInvalidStreamTest(50); }

}  // namespace ioscheduler
