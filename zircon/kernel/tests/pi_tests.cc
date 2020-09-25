// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <platform.h>
#include <zircon/types.h>

#include <new>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/macros.h>
#include <kernel/owned_wait_queue.h>
#include <kernel/scheduler.h>
#include <kernel/thread.h>
#include <kernel/wait.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/atomic.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <ktl/type_traits.h>
#include <ktl/unique_ptr.h>

#include "tests.h"

namespace {

constexpr int TEST_LOWEST_PRIORITY = LOWEST_PRIORITY + 1;
constexpr int TEST_HIGHEST_PRIORITY = HIGHEST_PRIORITY;
constexpr int TEST_DEFAULT_PRIORITY = DEFAULT_PRIORITY;
constexpr int TEST_PRIORTY_COUNT = TEST_HIGHEST_PRIORITY - TEST_LOWEST_PRIORITY;

class TestThread;  // fwd decl

// An RAII style helper which lets us auto boost the priority of our test thread
// to maximum, but return it to whatever it was when the test ends.  Many of
// these tests need to rely on timing in order to control the order with which
// threads time out of various wait queues.  Since we don't have deterministic
// control over timing in our tests, we rely on our high priority test thread
// being scheduled and pre-empting all other threads when it's timer goes off in
// order to reduce the chances of timing related flake in the tests.
class AutoPrioBooster {
 public:
  AutoPrioBooster() {
    Thread* t = Thread::Current::Get();
    initial_base_prio_ = t->scheduler_state().base_priority();
    t->SetPriority(TEST_HIGHEST_PRIORITY);
  }

  ~AutoPrioBooster() { Thread::Current::Get()->SetPriority(initial_base_prio_); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(AutoPrioBooster);

 private:
  int initial_base_prio_;
};

// A small helper which creates diffierent distributions of numbers which can be
// used for things like determining priority order, or release order, for the
// various tests.
struct DistroSpec {
  enum class Type { ASCENDING, DESCENDING, SAME, RANDOM, SHUFFLE };

  constexpr DistroSpec(Type t, uint32_t o, uint64_t s = 0) : type(t), offset(o), seed(s) {}

  const Type type;
  const uint32_t offset;
  const uint64_t seed;
};

void CreateDistribution(uint32_t* data, uint32_t N, const DistroSpec& spec) {
  DEBUG_ASSERT(data);
  uint64_t prng = spec.seed;

  switch (spec.type) {
    // Create an ascending sequence from [0, N] offset by spec.offset
    case DistroSpec::Type::ASCENDING:
      for (uint32_t i = 0; i < N; ++i) {
        data[i] = i + spec.offset;
      }
      break;

    // Create a descending sequence from (N, 0] offset by spec.offset
    case DistroSpec::Type::DESCENDING:
      for (uint32_t i = 0; i < N; ++i) {
        data[i] = static_cast<uint32_t>(N - i - 1 + spec.offset);
      }
      break;

    // Set all of the values to just offset.
    case DistroSpec::Type::SAME:
      for (uint32_t i = 0; i < N; ++i) {
        data[i] = spec.offset;
      }
      break;

    // Set all of the values to a random number on the range [0, N) + offset
    case DistroSpec::Type::RANDOM:
      for (uint32_t i = 0; i < N; ++i) {
        data[i] = spec.offset + (rand_r(&prng) % N);
      }
      break;

    // Create a range of values from [0, N) + offset, but shuffle the order of
    // those values in the set.
    case DistroSpec::Type::SHUFFLE:
      // Start by filling our array with a illegal sentinel value (N will do
      // the job just fine), then foreach i in the range [0, num_links) pick a
      // random position in the output to put i, and linearly probe until we
      // find the first unused position in order to shuffle.  Finally, offset
      // by 'offset' and we should be done.
      for (uint32_t i = 0; i < N; ++i) {
        data[i] = N;
      }

      for (uint32_t i = 0; i < N; ++i) {
        uint32_t pos = (rand_r(&prng) % N);
        while (data[pos] != N) {
          pos = (pos + 1) % N;
        }
        data[pos] = i;
      }

      for (uint32_t i = 0; i < N; ++i) {
        data[i] += spec.offset;
      }
      break;
  }
}

template <typename DATA_TYPE, size_t N>
void CreateDistribution(DATA_TYPE (&data)[N], const DistroSpec& spec) {
  static_assert(ktl::is_one_of<DATA_TYPE, int32_t, uint32_t>::value,
                "CreateDistribution only operates on 32 bit integer types!");
  static_assert(N <= ktl::numeric_limits<uint32_t>::max(),
                "CreateDistribution array size must be expressible using a 32 bit unsigned int");
  CreateDistribution(reinterpret_cast<uint32_t*>(data), static_cast<uint32_t>(N), spec);
}

// A simple barrier class which can be waited on by multiple threads.  Used to
// stall test threads at various parts of their execution in order to sequence
// things in a deterministic fashion.
class Barrier {
 public:
  constexpr Barrier(bool signaled = false) : signaled_{signaled} {}
  ~Barrier() {
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    ASSERT(queue_.IsEmpty());
  }

  void Signal(bool state) {
    bool expected = !state;
    if (signaled_.compare_exchange_strong(expected, state) && state) {
      Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
      queue_.WakeAll(true, ZX_OK);
    }
  }

  void Wait(Deadline deadline = Deadline::infinite()) {
    if (state()) {
      return;
    }

    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    if (state()) {
      return;
    }

    queue_.Block(deadline, Interruptible::Yes);
  }

  bool state() const { return signaled_.load(); }

 private:
  ktl::atomic<bool> signaled_;
  WaitQueue queue_;
};

// Helper wrapper for an owned wait queue which manages grabbing and releasing
// the thread lock at appropriate times for us.  Mostly, this is just about
// saving some typing.
class LockedOwnedWaitQueue : public OwnedWaitQueue {
 public:
  constexpr LockedOwnedWaitQueue() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(LockedOwnedWaitQueue);

  void ReleaseAllThreads() TA_EXCL(thread_lock) {
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

    if (OwnedWaitQueue::WakeThreads(ktl::numeric_limits<uint32_t>::max())) {
      Scheduler::Reschedule();
    }
  }

  void ReleaseOneThread() TA_EXCL(thread_lock) {
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

    auto hook = [](Thread*, void*) { return Hook::Action::SelectAndAssignOwner; };

    if (OwnedWaitQueue::WakeThreads(1u, {hook, nullptr})) {
      Scheduler::Reschedule();
    }
  }

  void AssignOwner(TestThread* thread) TA_EXCL(thread_lock);
};

// LoopIterPrinter
// A small RAII style class which helps us to print out where a loop iterator
// is when a test fails and bails out.  Note: loop iterator types must be
// convertible to int64_t.
template <typename T>
class LoopIterPrinter {
 public:
  constexpr LoopIterPrinter(const char* field_name, T iter_val)
      : field_name_(field_name), iter_val_(iter_val) {}

  ~LoopIterPrinter() {
    if (field_name_ != nullptr) {
      printf("Test failed with %s == %ld\n", field_name_, static_cast<int64_t>(iter_val_));
    }
  }

  DISALLOW_COPY_ASSIGN_AND_MOVE(LoopIterPrinter);

  void cancel() { field_name_ = nullptr; }

 private:
  const char* field_name_;
  T iter_val_;
};

#define PRINT_LOOP_ITER(_var_name) LoopIterPrinter print_##_var_name(#_var_name, _var_name)

// The core test thread object.  We use this object to build various graphs of
// priority inheritance chains, and then evaluate that the effective priorities
// of the threads involved in the graph are what we expect them to be after
// various mutations of the graph have taken place.
class TestThread {
 public:
  enum class State : uint32_t {
    INITIAL,
    CREATED,
    WAITING_TO_START,
    STARTED,
    WAITING_FOR_SHUTDOWN,
    SHUTDOWN,
  };

  enum class Condition : uint32_t {
    BLOCKED,
    WAITING_FOR_SHUTDOWN,
  };

  TestThread() = default;
  ~TestThread() { Reset(); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(TestThread);

  // Reset the barrier at the start of a test in order to prevent threads from
  // exiting after they have completed their operation..
  static void ResetShutdownBarrier() { allow_shutdown_.Signal(false); }

  // Clear the barrier and allow shutdown.
  static void ClearShutdownBarrier() { allow_shutdown_.Signal(true); }

  static Barrier& allow_shutdown() { return allow_shutdown_; }

  // Create a thread, settings its entry point and initial base priority in
  // the process, but do not start it yet.
  bool Create(int initial_base_priority);

  // Start the thread, have it do nothing but wait to be allowed to exit.
  bool DoStall();

  // Start the thread and have it block on an owned wait queue, declaring the
  // specified test thread to be the owner of that queue in the process.
  bool BlockOnOwnedQueue(OwnedWaitQueue* owned_wq, TestThread* owner,
                         Deadline timeout = Deadline::infinite());

  // Directly take ownership of the specified wait queue using AssignOwner.
  bool TakeOwnership(OwnedWaitQueue* owned_wq);

  // Change base the priority of the thread
  bool SetBasePriority(int base_prio) {
    BEGIN_TEST;
    ASSERT_NONNULL(thread_);
    ASSERT_EQ(state(), State::STARTED);
    ASSERT_GE(base_prio, TEST_LOWEST_PRIORITY);
    ASSERT_LT(base_prio, TEST_HIGHEST_PRIORITY);

    thread_->SetPriority(base_prio);

    END_TEST;
  }

  // Reset the thread back to its initial state.  If |explicit_kill| is true,
  // then do not wait for the thread to exit normally if it has been started.
  // Simply send it the kill signal.
  bool Reset(bool explicit_kill = false);

  int inherited_priority() const {
    if (thread_ == nullptr) {
      return -2;
    }

    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    return thread_->scheduler_state().inherited_priority();
  }

  int effective_priority() const {
    if (thread_ == nullptr) {
      return -2;
    }

    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    return thread_->scheduler_state().effective_priority();
  }

  int base_priority() const {
    if (thread_ == nullptr) {
      return -2;
    }

    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    return thread_->scheduler_state().base_priority();
  }

  State state() const { return state_.load(); }

  template <Condition condition>
  bool WaitFor();

 private:
  // Test threads in the various tests use lambdas in order to store their
  // customized test operations.  In order to allow these lambda's to capture
  // context from their local scope, but not need to use the heap in order to
  // allocate the storage for the scope, we need to know the worst case
  // capture storage requirements across all of these tests.  Armed with this
  // knowledge, we can use a fbl::InlineFunction to pre-allocate storage in
  // the TestThread object for the worst case lambda we will encounter in the
  // test suite.
  //
  // Currently, this bound is 6 pointer's worth of storage.  If this grows in
  // the future, this constexpr bound should be updated to match the new worst
  // case storage requirement.
  static constexpr size_t kMaxOpLambdaCaptureStorageBytes = sizeof(void*) * 6;

  friend class LockedOwnedWaitQueue;

  int ThreadEntry();

  static Barrier allow_shutdown_;

  Thread* thread_ = nullptr;
  ktl::atomic<State> state_{State::INITIAL};
  fbl::InlineFunction<void(void), kMaxOpLambdaCaptureStorageBytes> op_;
};

Barrier TestThread::allow_shutdown_;

bool TestThread::Create(int initial_base_priority) {
  BEGIN_TEST;

  ASSERT_NULL(thread_);
  ASSERT_EQ(state(), State::INITIAL);
  ASSERT_GE(initial_base_priority, TEST_LOWEST_PRIORITY);
  ASSERT_LT(initial_base_priority, TEST_HIGHEST_PRIORITY);

  thread_ = Thread::Create(
      "pi_test_thread",
      [](void* ctx) -> int { return reinterpret_cast<TestThread*>(ctx)->ThreadEntry(); },
      reinterpret_cast<void*>(this), initial_base_priority);

  ASSERT_NONNULL(thread_);

  state_.store(State::CREATED);

  END_TEST;
}

bool TestThread::DoStall() {
  BEGIN_TEST;
  ASSERT_EQ(state(), State::CREATED);
  ASSERT_FALSE(static_cast<bool>(op_));

  op_ = []() {};

  state_.store(State::WAITING_TO_START);
  thread_->Resume();

  ASSERT_TRUE(WaitFor<Condition::BLOCKED>());

  END_TEST;
}

bool TestThread::BlockOnOwnedQueue(OwnedWaitQueue* owned_wq, TestThread* owner, Deadline timeout) {
  BEGIN_TEST;
  ASSERT_EQ(state(), State::CREATED);
  ASSERT_FALSE(static_cast<bool>(op_));

  op_ = [owned_wq, owner_thrd = owner ? owner->thread_ : nullptr, timeout]() {
    Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
    owned_wq->BlockAndAssignOwner(timeout, owner_thrd, ResourceOwnership::Normal,
                                  Interruptible::Yes);
  };

  state_.store(State::WAITING_TO_START);
  thread_->Resume();

  ASSERT_TRUE(WaitFor<Condition::BLOCKED>());

  END_TEST;
}

bool TestThread::Reset(bool explicit_kill) {
  BEGIN_TEST;

  // If we are explicitly killing the thread as part of the test, then we
  // should not expect the shutdown barrier to be cleared.
  if (!explicit_kill) {
    EXPECT_TRUE(allow_shutdown_.state());
  }

  constexpr zx_duration_t join_timeout = ZX_MSEC(500);

  switch (state()) {
    case State::INITIAL:
      break;
    case State::CREATED:
      // Created but not started?  thread_forget seems to be the proper way to
      // cleanup a thread which was never started.
      ASSERT(thread_ != nullptr);
      thread_->Forget();
      thread_ = nullptr;
      break;

    case State::WAITING_TO_START:
    case State::STARTED:
    case State::WAITING_FOR_SHUTDOWN:
    case State::SHUTDOWN:
      // If we are explicitly killing the thread, send it the kill signal now.
      if (explicit_kill) {
        thread_->Kill();
      }

      // Hopefully, the thread is on it's way to termination as we speak.
      // Attempt to join it.  If this fails, print a warning and then kill it.
      ASSERT(thread_ != nullptr);
      int ret_code;
      zx_status_t res = thread_->Join(&ret_code, current_time() + join_timeout);
      if (res != ZX_OK) {
        printf("Failed to join thread %p (res %d); attempting to kill\n", thread_, res);

        // If we have already sent the kill signal to the thread and failed,
        // there is no point in trying to do so gain.
        if (!explicit_kill) {
          thread_->Kill();
          res = thread_->Join(&ret_code, current_time() + join_timeout);
        }

        if (res != ZX_OK) {
          panic("Failed to stop thread during PI tests!! (res = %d)\n", res);
        }
      }
      thread_ = nullptr;
  }

  state_.store(State::INITIAL);
  op_ = nullptr;
  ASSERT_NULL(thread_);

  END_TEST;
}

int TestThread::ThreadEntry() {
  if (!static_cast<bool>(op_) || (state() != State::WAITING_TO_START)) {
    return -1;
  }

  state_.store(State::STARTED);
  op_();
  state_.store(State::WAITING_FOR_SHUTDOWN);
  allow_shutdown_.Wait();

  state_.store(State::SHUTDOWN);
  op_ = nullptr;

  return 0;
}

template <TestThread::Condition condition>
bool TestThread::WaitFor() {
  BEGIN_TEST;

  constexpr zx_duration_t timeout = ZX_SEC(10);
  constexpr zx_duration_t poll_interval = ZX_USEC(100);
  zx_time_t deadline = current_time() + timeout;

  while (true) {
    if constexpr (condition == Condition::BLOCKED) {
      thread_state cur_state;
      {
        Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
        cur_state = thread_->state();
      }

      if (cur_state == THREAD_BLOCKED) {
        break;
      }

      if (cur_state != THREAD_RUNNING) {
        ASSERT_EQ(THREAD_READY, cur_state);
      }
    } else {
      static_assert(condition == Condition::WAITING_FOR_SHUTDOWN);
      if (state() == State::WAITING_FOR_SHUTDOWN) {
        break;
      }
    }

    zx_time_t now = current_time();
    ASSERT_LT(now, deadline);
    Thread::Current::SleepRelative(poll_interval);
  }

  END_TEST;
}

void LockedOwnedWaitQueue::AssignOwner(TestThread* thread) {
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  if (OwnedWaitQueue::AssignOwner(thread ? thread->thread_ : nullptr)) {
    Scheduler::Reschedule();
  }
}

bool pi_test_basic() {
  BEGIN_TEST;

  enum class ReleaseMethod { WAKE = 0, TIMEOUT, KILL };

  AutoPrioBooster pboost;
  constexpr int END_PRIO = TEST_DEFAULT_PRIORITY;
  constexpr int PRIO_DELTAS[] = {-1, 0, 1};
  constexpr ReleaseMethod REL_METHODS[] = {ReleaseMethod::WAKE, ReleaseMethod::TIMEOUT,
                                           ReleaseMethod::KILL};

  for (auto prio_delta : PRIO_DELTAS) {
    for (auto rel_method : REL_METHODS) {
      PRINT_LOOP_ITER(prio_delta);
      PRINT_LOOP_ITER(rel_method);

      LockedOwnedWaitQueue owq;
      TestThread pressure_thread;
      TestThread blocking_thread;

      auto cleanup = fbl::MakeAutoCall([&]() {
        TestThread::ClearShutdownBarrier();
        owq.ReleaseAllThreads();
        pressure_thread.Reset();
        blocking_thread.Reset();
      });

      int pressure_prio = END_PRIO + prio_delta;
      int expected_prio = (prio_delta > 0) ? pressure_prio : END_PRIO;

      // Make sure that our default barriers have been reset to their proper
      // initial states.
      TestThread::ResetShutdownBarrier();

      // Create 2 threads, one which will sit at the end of the priority
      // chain, and one which will exert priority pressure on the end of the
      // chain.
      ASSERT_TRUE(blocking_thread.Create(END_PRIO));
      ASSERT_TRUE(pressure_thread.Create(pressure_prio));

      // Start the first thread, wait for it to block, and verify that it's
      // priority is correct (it should not be changed).
      ASSERT_TRUE(blocking_thread.DoStall());
      ASSERT_EQ(TEST_DEFAULT_PRIORITY, blocking_thread.effective_priority());

      // Start the second thread, and have it block on the owned wait queue,
      // and declare the blocking thread to be the owner of the queue at the
      // same time.  Then check to be sure that the effective priority of the
      // blocking thread matches what we expect to see.
      Deadline timeout = (rel_method == ReleaseMethod::TIMEOUT) ? Deadline::after(ZX_MSEC(20))
                                                                : Deadline::infinite();
      ASSERT_TRUE(pressure_thread.BlockOnOwnedQueue(&owq, &blocking_thread, timeout));
      ASSERT_EQ(expected_prio, blocking_thread.effective_priority());

      // Finally, release the thread from the owned wait queue based on
      // the release method we are testing.  We will either explicitly
      // wake it up, let it time out, or kill the thread outright.
      //
      // Then, verify that the priority drops back down to what we
      // expected.
      switch (rel_method) {
        case ReleaseMethod::WAKE:
          owq.ReleaseAllThreads();
          break;

        case ReleaseMethod::TIMEOUT:
          // Wait until the pressure thread times out and has exited.
          ASSERT_TRUE(pressure_thread.WaitFor<TestThread::Condition::WAITING_FOR_SHUTDOWN>());
          break;

        case ReleaseMethod::KILL:
          pressure_thread.Reset(true);
          break;
      }
      ASSERT_EQ(TEST_DEFAULT_PRIORITY, blocking_thread.effective_priority());

      print_prio_delta.cancel();
      print_rel_method.cancel();
    }
  }

  END_TEST;
}

bool pi_test_changing_priority() {
  BEGIN_TEST;

  AutoPrioBooster pboost;
  LockedOwnedWaitQueue owq;
  TestThread pressure_thread;
  TestThread blocking_thread;

  auto cleanup = fbl::MakeAutoCall([&]() {
    TestThread::ClearShutdownBarrier();
    owq.ReleaseAllThreads();
    pressure_thread.Reset();
    blocking_thread.Reset();
  });

  // Make sure that our default barriers have been reset to their proper
  // initial states.
  TestThread::ResetShutdownBarrier();

  // Create our threads.
  ASSERT_TRUE(blocking_thread.Create(TEST_DEFAULT_PRIORITY));
  ASSERT_TRUE(pressure_thread.Create(TEST_LOWEST_PRIORITY));

  // Start the first thread, wait for it to block, and verify that it's
  // priority is correct (it should not be changed).
  ASSERT_TRUE(blocking_thread.DoStall());
  ASSERT_EQ(TEST_DEFAULT_PRIORITY, blocking_thread.effective_priority());

  // Block the second thread behind the first.
  ASSERT_TRUE(pressure_thread.BlockOnOwnedQueue(&owq, &blocking_thread));
  ASSERT_EQ(TEST_DEFAULT_PRIORITY, blocking_thread.effective_priority());

  // Run up and down through a bunch of priorities
  for (int ascending = TEST_LOWEST_PRIORITY; ascending < TEST_HIGHEST_PRIORITY; ++ascending) {
    PRINT_LOOP_ITER(ascending);
    int expected = ktl::max(ascending, TEST_DEFAULT_PRIORITY);
    ASSERT_TRUE(pressure_thread.SetBasePriority(ascending));
    ASSERT_EQ(expected, blocking_thread.effective_priority());
    print_ascending.cancel();
  }

  for (int descending = TEST_HIGHEST_PRIORITY - 1; descending >= TEST_LOWEST_PRIORITY;
       --descending) {
    PRINT_LOOP_ITER(descending);
    int expected = ktl::max(descending, TEST_DEFAULT_PRIORITY);
    ASSERT_TRUE(pressure_thread.SetBasePriority(descending));
    ASSERT_EQ(expected, blocking_thread.effective_priority());
    print_descending.cancel();
  }

  // Release the pressure thread, validate that the priority is what we
  // started with and we are done.
  owq.ReleaseAllThreads();
  ASSERT_EQ(TEST_DEFAULT_PRIORITY, blocking_thread.effective_priority());

  END_TEST;
}

template <uint32_t CHAIN_LEN>
bool pi_test_chain() {
  static_assert(CHAIN_LEN >= 2, "Must have at least 2 nodes to form a PI chain");
  static_assert(CHAIN_LEN < TEST_PRIORTY_COUNT,
                "Cannot create a chain which would result in a thread being created at "
                "TEST_HIGHEST_PRIORITY");

  BEGIN_TEST;
  fbl::AllocChecker ac;

  enum class ReleaseOrder : uint64_t { ASCENDING = 0, DESCENDING };

  AutoPrioBooster pboost;
  TestThread threads[CHAIN_LEN];
  struct Link {
    LockedOwnedWaitQueue queue;
    bool active = false;
  };
  auto links = ktl::make_unique<ktl::array<Link, CHAIN_LEN - 1>>(&ac);
  ASSERT_TRUE(ac.check());

  const DistroSpec PRIORITY_GENERATORS[] = {
      {DistroSpec::Type::ASCENDING, TEST_LOWEST_PRIORITY},
      {DistroSpec::Type::DESCENDING, TEST_LOWEST_PRIORITY},
      {DistroSpec::Type::SAME, TEST_DEFAULT_PRIORITY},
      {DistroSpec::Type::RANDOM, TEST_LOWEST_PRIORITY, 0xa064eba4bf1b5087},
      {DistroSpec::Type::RANDOM, TEST_LOWEST_PRIORITY, 0x87251211471cb789},
      {DistroSpec::Type::SHUFFLE, TEST_LOWEST_PRIORITY, 0xbd6f3bfe33d51c8e},
      {DistroSpec::Type::SHUFFLE, TEST_LOWEST_PRIORITY, 0x857ce1aa3209ecc7},
  };

  const DistroSpec RELEASE_ORDERS[]{
      {DistroSpec::Type::ASCENDING, 0},
      {DistroSpec::Type::DESCENDING, 0},
      {DistroSpec::Type::SHUFFLE, 0, 0xac8d4a8ed016caf0},
      {DistroSpec::Type::SHUFFLE, 0, 0xb51e76ca5cf20875},
  };

  for (uint32_t pgen_ndx = 0; pgen_ndx < ktl::size(PRIORITY_GENERATORS); ++pgen_ndx) {
    PRINT_LOOP_ITER(pgen_ndx);

    // Generate the priority map for this pass.
    int prio_map[ktl::size(threads)];
    CreateDistribution(prio_map, PRIORITY_GENERATORS[pgen_ndx]);

    for (uint32_t ro_ndx = 0; ro_ndx < ktl::size(RELEASE_ORDERS); ++ro_ndx) {
      PRINT_LOOP_ITER(ro_ndx);

      // Generate the order in which we will release the links for this
      // pass
      uint32_t release_ordering[CHAIN_LEN - 1];
      CreateDistribution(release_ordering, RELEASE_ORDERS[ro_ndx]);

      auto cleanup = fbl::MakeAutoCall([&]() {
        TestThread::ClearShutdownBarrier();
        for (auto& l : *links) {
          l.queue.ReleaseAllThreads();
        }
        for (auto& t : threads) {
          t.Reset();
        }
      });

      // Lambda used to validate the current thread priorities.
      auto ValidatePriorities = [&]() -> bool {
        BEGIN_TEST;

        int expected_prio = -1;

        for (uint32_t tndx = ktl::size(threads); tndx-- > 0;) {
          PRINT_LOOP_ITER(tndx);

          // All threads should either be created, started or waiting for
          // shutdown.  If they are merely created, they have no effective
          // priority to evaluate at the moment, so just skip them.
          const auto& t = threads[tndx];
          const TestThread::State cur_state = t.state();
          if (cur_state == TestThread::State::CREATED) {
            print_tndx.cancel();
            continue;
          }

          if (cur_state != TestThread::State::WAITING_FOR_SHUTDOWN) {
            ASSERT_EQ(TestThread::State::STARTED, cur_state);
          }

          // If the link behind us in the chain does not exist, or exists
          // but is not currently active, then reset the expected priority
          // pressure.  Otherwise, the expected priority should be the
          // priority of the maximum of the base priorities we have
          // traversed so far.
          ASSERT_LT(tndx, ktl::size(prio_map));
          if ((tndx >= links->size()) || !(*links)[tndx].active) {
            expected_prio = prio_map[tndx];
          } else {
            expected_prio = ktl::max(expected_prio, prio_map[tndx]);
          }

          ASSERT_EQ(expected_prio, t.effective_priority());
          print_tndx.cancel();
        }

        END_TEST;
      };

      // Make sure that our default barriers have been reset to their proper
      // initial states.
      TestThread::ResetShutdownBarrier();

      // Create our threads.
      for (uint32_t tndx = 0; tndx < ktl::size(threads); ++tndx) {
        ASSERT_LT(tndx, ktl::size(prio_map));
        PRINT_LOOP_ITER(tndx);
        ASSERT_TRUE(threads[tndx].Create(prio_map[tndx]));
        print_tndx.cancel();
      }

      // Start the head of the chain, wait for it to block, then verify that its
      // priority is correct (it should not be changed).
      auto& chain_head = threads[0];
      ASSERT_TRUE(chain_head.DoStall());
      ASSERT_TRUE(ValidatePriorities());

      // Start each of the threads in the chain one at a time.  Make sure that the
      // pressure of the threads in the chain is properly transmitted each time.
      for (uint32_t tndx = 1; tndx < ktl::size(threads); ++tndx) {
        PRINT_LOOP_ITER(tndx);

        auto& link = (*links)[tndx - 1];
        ASSERT_TRUE(threads[tndx].BlockOnOwnedQueue(&link.queue, &threads[tndx - 1]));
        link.active = true;
        ASSERT_TRUE(ValidatePriorities());

        print_tndx.cancel();
      }

      // Tear down the chain according to the release ordering for this
      // pass.  Make sure that the priority properly relaxes for each of
      // the threads as we do so.
      for (auto link_ndx : release_ordering) {
        PRINT_LOOP_ITER(link_ndx);

        ASSERT_LT(link_ndx, links->size());
        auto& link = (*links)[link_ndx];
        link.queue.ReleaseAllThreads();
        link.active = false;
        ASSERT_TRUE(ValidatePriorities());

        print_link_ndx.cancel();
      }

      print_ro_ndx.cancel();
    }

    print_pgen_ndx.cancel();
  }

  END_TEST;
}

template <uint32_t WAITER_CNT>
bool pi_test_multi_waiter() {
  static_assert(WAITER_CNT >= 2, "Must have at least 2 waiters in the multi-waiter test");
  static_assert(WAITER_CNT < TEST_PRIORTY_COUNT,
                "Multi waiter test must have fewer waiters than priority levels");
  BEGIN_TEST;
  fbl::AllocChecker ac;
  AutoPrioBooster pboost;

  LockedOwnedWaitQueue blocking_queue;
  TestThread blocking_thread;
  struct Waiter {
    TestThread thread;
    bool is_waiting = false;
    int prio = 0;
  };
  auto waiters = ktl::make_unique<ktl::array<Waiter, WAITER_CNT>>(&ac);
  ASSERT_TRUE(ac.check());

  const int BLOCKING_THREAD_PRIO[] = {TEST_LOWEST_PRIORITY, TEST_DEFAULT_PRIORITY,
                                      TEST_HIGHEST_PRIORITY - 1};
  const DistroSpec PRIORITY_GENERATORS[] = {
      {DistroSpec::Type::ASCENDING, TEST_LOWEST_PRIORITY},
      {DistroSpec::Type::DESCENDING, TEST_LOWEST_PRIORITY},
      {DistroSpec::Type::SAME, TEST_DEFAULT_PRIORITY},
      {DistroSpec::Type::RANDOM, TEST_LOWEST_PRIORITY, 0xa064eba4bf1b5087},
      {DistroSpec::Type::RANDOM, TEST_LOWEST_PRIORITY, 0x87251211471cb789},
      {DistroSpec::Type::SHUFFLE, TEST_LOWEST_PRIORITY, 0xbd6f3bfe33d51c8e},
      {DistroSpec::Type::SHUFFLE, TEST_LOWEST_PRIORITY, 0x857ce1aa3209ecc7},
  };

  for (auto bt_prio : BLOCKING_THREAD_PRIO) {
    PRINT_LOOP_ITER(bt_prio);

    for (uint32_t pgen_ndx = 0; pgen_ndx < ktl::size(PRIORITY_GENERATORS); ++pgen_ndx) {
      PRINT_LOOP_ITER(pgen_ndx);

      // At the end of the tests, success or failure, be sure to clean up.
      auto cleanup = fbl::MakeAutoCall([&]() {
        TestThread::ClearShutdownBarrier();
        blocking_queue.ReleaseAllThreads();
        blocking_thread.Reset();
        for (auto& w : *waiters) {
          w.thread.Reset();
        }
      });

      // Make sure that our barriers have been reset.
      TestThread::ResetShutdownBarrier();

      // Generate the priority map for this pass.
      int prio_map[WAITER_CNT];
      CreateDistribution(prio_map, PRIORITY_GENERATORS[pgen_ndx]);

      // Create all of the threads.
      ASSERT_TRUE(blocking_thread.Create(bt_prio));
      for (uint32_t waiter_ndx = 0; waiter_ndx < waiters->size(); ++waiter_ndx) {
        PRINT_LOOP_ITER(waiter_ndx);

        auto& w = (*waiters)[waiter_ndx];
        w.prio = prio_map[waiter_ndx];
        ASSERT_TRUE(w.thread.Create(w.prio));

        print_waiter_ndx.cancel();
      }

      // Define a small lambda we will use to validate the expected priorities of
      // each of our threads.
      TestThread* current_owner = &blocking_thread;
      auto ValidatePriorities = [&]() -> bool {
        BEGIN_TEST;

        // All threads in the test who are not the current owner should have
        // their effective priority be equal to their base priority
        if (&blocking_thread != current_owner) {
          ASSERT_EQ(bt_prio, blocking_thread.effective_priority());
        }

        for (uint32_t waiter_ndx = 0; waiter_ndx < waiters->size(); ++waiter_ndx) {
          PRINT_LOOP_ITER(waiter_ndx);

          auto& w = (*waiters)[waiter_ndx];
          if (&w.thread != current_owner) {
            ASSERT_EQ(prio_map[waiter_ndx], w.thread.effective_priority());
          }

          print_waiter_ndx.cancel();
        }

        // The current owner (if any) should have the max priority across all of
        // the waiters, and its own base priority.
        ASSERT_NONNULL(current_owner);
        int expected_prio = current_owner->base_priority();
        for (const auto& w : *waiters) {
          if (w.is_waiting && (expected_prio < w.prio)) {
            expected_prio = w.prio;
          }
        }
        ASSERT_EQ(expected_prio, current_owner->effective_priority());

        END_TEST;
      };

      // Start the blocking thread.
      ASSERT_TRUE(blocking_thread.DoStall());
      ASSERT_TRUE(ValidatePriorities());

      // Start each of the threads and have them block on the blocking_queue,
      // declaring blocking_thread to be the owner as they go.  Verify that the
      // blocking thread has the priority of the highest priority thread who is
      // currently waiting.
      for (uint32_t waiter_ndx = 0; waiter_ndx < waiters->size(); ++waiter_ndx) {
        PRINT_LOOP_ITER(waiter_ndx);

        auto& w = (*waiters)[waiter_ndx];
        ASSERT_TRUE(w.thread.BlockOnOwnedQueue(&blocking_queue, current_owner));
        w.is_waiting = true;
        ASSERT_TRUE(ValidatePriorities());

        print_waiter_ndx.cancel();
      }

      // Now wake the threads, one at a time, assigning ownership to the thread
      // which was woken each time.  Note that we should not be assuming which
      // thread is going to be woken.  We will need to request that a thread be
      // woken, then figure out after the fact which one was.
      for (uint32_t tndx = 0; tndx < waiters->size(); ++tndx) {
        PRINT_LOOP_ITER(tndx);

        blocking_queue.ReleaseOneThread();

        TestThread* new_owner = nullptr;
        zx_time_t deadline = current_time() + ZX_SEC(10);
        while (current_time() < deadline) {
          for (auto& w : *waiters) {
            // If the waiter's is_waiting flag is set, but the thread has
            // reached the WAITING_FOR_SHUTDOWN state, then we know that
            // this was a thread which was just woken.
            if (w.is_waiting && (w.thread.state() == TestThread::State::WAITING_FOR_SHUTDOWN)) {
              new_owner = &w.thread;
              w.is_waiting = false;
              break;
            }
          }

          if (new_owner != nullptr) {
            break;
          }

          Thread::Current::SleepRelative(ZX_USEC(100));
        }

        // Sanity checks.  Make sure that the new owner exists, and is not the
        // same as the old owner.  Also make sure that none of the other threads
        // have been released but have not been recognized yet.
        ASSERT_NONNULL(new_owner);
        ASSERT_NE(new_owner, current_owner);
        for (auto& w : *waiters) {
          if (w.is_waiting) {
            ASSERT_EQ(TestThread::State::STARTED, w.thread.state());
          } else {
            ASSERT_EQ(TestThread::State::WAITING_FOR_SHUTDOWN, w.thread.state());
          }
        }
        current_owner = new_owner;

        // Validate our priorities.
        ASSERT_TRUE(ValidatePriorities());

        print_tndx.cancel();
      }

      print_pgen_ndx.cancel();
    }
    print_bt_prio.cancel();
  }

  END_TEST;
}

template <uint32_t QUEUE_CNT>
bool pi_test_multi_owned_queues() {
  static_assert(QUEUE_CNT >= 2, "Must have at least 2 owned queues in the multi-waiter test");
  static_assert(QUEUE_CNT < TEST_PRIORTY_COUNT,
                "Multi waiter test must have fewer owned queues than priority levels");
  BEGIN_TEST;
  fbl::AllocChecker ac;
  AutoPrioBooster pboost;

  TestThread blocking_thread;
  struct Waiter {
    TestThread thread;
    LockedOwnedWaitQueue queue;
    bool is_waiting = false;
    int prio = 0;
  };
  auto queues = ktl::make_unique<ktl::array<Waiter, QUEUE_CNT>>(&ac);
  ASSERT_TRUE(ac.check());

  const int BLOCKING_THREAD_PRIO[] = {TEST_LOWEST_PRIORITY, TEST_DEFAULT_PRIORITY,
                                      TEST_HIGHEST_PRIORITY - 1};
  const DistroSpec PRIORITY_GENERATORS[] = {
      {DistroSpec::Type::ASCENDING, TEST_LOWEST_PRIORITY},
      {DistroSpec::Type::DESCENDING, TEST_LOWEST_PRIORITY},
      {DistroSpec::Type::SAME, TEST_DEFAULT_PRIORITY},
      {DistroSpec::Type::RANDOM, TEST_LOWEST_PRIORITY, 0xef900a44da89a82d},
      {DistroSpec::Type::RANDOM, TEST_LOWEST_PRIORITY, 0xb89e3b7442b95a1c},
      {DistroSpec::Type::SHUFFLE, TEST_LOWEST_PRIORITY, 0xa23574c4fb9b0a10},
      {DistroSpec::Type::SHUFFLE, TEST_LOWEST_PRIORITY, 0x06ec82d4ade8efba},
  };

  for (auto bt_prio : BLOCKING_THREAD_PRIO) {
    PRINT_LOOP_ITER(bt_prio);

    for (uint32_t pgen_ndx = 0; pgen_ndx < ktl::size(PRIORITY_GENERATORS); ++pgen_ndx) {
      PRINT_LOOP_ITER(pgen_ndx);

      // At the end of the tests, success or failure, be sure to clean up.
      auto cleanup = fbl::MakeAutoCall([&]() {
        TestThread::ClearShutdownBarrier();
        blocking_thread.Reset();
        for (auto& q : *queues) {
          q.queue.ReleaseAllThreads();
        }
        for (auto& q : *queues) {
          q.thread.Reset();
        }
      });

      // Make sure that our barriers have been reset.
      TestThread::ResetShutdownBarrier();

      // Generate the priority map for this pass.
      int prio_map[QUEUE_CNT];
      CreateDistribution(prio_map, PRIORITY_GENERATORS[pgen_ndx]);

      // Create all of the threads.
      ASSERT_TRUE(blocking_thread.Create(bt_prio));
      for (uint32_t queue_ndx = 0; queue_ndx < queues->size(); ++queue_ndx) {
        PRINT_LOOP_ITER(queue_ndx);

        auto& q = (*queues)[queue_ndx];
        q.prio = prio_map[queue_ndx];
        ASSERT_TRUE(q.thread.Create(q.prio));

        print_queue_ndx.cancel();
      }

      // Define a small lambda we will use to validate the expected priorities of
      // each of our threads.
      auto ValidatePriorities = [&]() -> bool {
        BEGIN_TEST;

        // Each of the queue threads should simply have their base
        // priority.  Verify this while we compute the maximum priority
        // across all of the threads who are still applying pressure to
        // the blocking thread.
        int max_pressure = -1;
        for (const auto& q : *queues) {
          ASSERT_EQ(q.prio, q.thread.effective_priority());
          if (q.is_waiting) {
            max_pressure = ktl::max(max_pressure, q.prio);
          }
        }

        for (uint32_t queue_ndx = 0; queue_ndx < queues->size(); ++queue_ndx) {
          PRINT_LOOP_ITER(queue_ndx);
          const auto& q = (*queues)[queue_ndx];

          ASSERT_EQ(q.prio, q.thread.effective_priority());
          if (q.is_waiting) {
            max_pressure = ktl::max(max_pressure, q.prio);
          }

          print_queue_ndx.cancel();
        }

        // Now that we know the pressure which is being applied to the
        // blocking thread, verify its effective priority.
        int expected_prio = ktl::max(max_pressure, bt_prio);
        ASSERT_EQ(expected_prio, blocking_thread.effective_priority());

        END_TEST;
      };

      // Start the blocking thread.
      ASSERT_TRUE(blocking_thread.DoStall());
      ASSERT_TRUE(ValidatePriorities());

      // Start each of the threads and have them block on their associated
      // queue, declaring blocking_thread to be the owner of their queue
      // as they go.  Validate priorities at each step.
      for (uint32_t queue_ndx = 0; queue_ndx < queues->size(); ++queue_ndx) {
        PRINT_LOOP_ITER(queue_ndx);

        auto& q = (*queues)[queue_ndx];
        ASSERT_TRUE(q.thread.BlockOnOwnedQueue(&q.queue, &blocking_thread));
        q.is_waiting = true;
        ASSERT_TRUE(ValidatePriorities());

        print_queue_ndx.cancel();
      }

      // Now wake the threads, one at a time, verifying priorities as we
      // go.
      for (uint32_t queue_ndx = 0; queue_ndx < queues->size(); ++queue_ndx) {
        PRINT_LOOP_ITER(queue_ndx);

        auto& q = (*queues)[queue_ndx];
        q.queue.ReleaseOneThread();
        q.is_waiting = false;
        ASSERT_TRUE(ValidatePriorities());

        print_queue_ndx.cancel();
      }

      print_pgen_ndx.cancel();
    }
    print_bt_prio.cancel();
  }

  END_TEST;
}

template <uint32_t CYCLE_LEN>
bool pi_test_cycle() {
  static_assert(CYCLE_LEN >= 2, "Must have at least 2 nodes to form a PI cycle");
  static_assert(CYCLE_LEN < TEST_PRIORTY_COUNT,
                "Cannot create a cycle which would result in a thread being created at "
                "TEST_HIGHEST_PRIORITY");
  BEGIN_TEST;
  fbl::AllocChecker ac;
  AutoPrioBooster pboost;

  // Deliberately create a cycle and make sure that we don't hang or otherwise
  // exhibit bad behavior.
  struct Link {
    TestThread thread;
    LockedOwnedWaitQueue link;
  };
  auto nodes = ktl::make_unique<ktl::array<Link, CYCLE_LEN>>(&ac);
  ASSERT_TRUE(ac.check());

  // At the end of the tests, success or failure, be sure to clean up.
  auto cleanup = fbl::MakeAutoCall([&]() {
    TestThread::ClearShutdownBarrier();
    for (auto& n : *nodes) {
      n.link.ReleaseAllThreads();
    }
    for (auto& n : *nodes) {
      n.thread.Reset();
    }
  });

  // Create the priorities we will assign to each thread.
  int prio_map[CYCLE_LEN];
  CreateDistribution(prio_map, {DistroSpec::Type::ASCENDING, TEST_LOWEST_PRIORITY});

  // Create each thread
  for (uint32_t tndx = 0; tndx < nodes->size(); ++tndx) {
    PRINT_LOOP_ITER(tndx);
    ASSERT_TRUE((*nodes)[tndx].thread.Create(prio_map[tndx]));
    print_tndx.cancel();
  }

  // Let each thread run, blocking it on its own link and declaring the next
  // thread in the list to be the owner of the link.  When we hit the last
  // thread, we form a cycle.  Our threads are in ascending priority order, so
  // we should not see any PI ripple until the final link has been made.  At
  // that point, all of the threads in the test should have the priority of
  // the final thread.
  for (uint32_t tndx = 0; tndx < nodes->size(); ++tndx) {
    PRINT_LOOP_ITER(tndx);

    auto owner_thread = &(*nodes)[(tndx + 1) % nodes->size()].thread;
    auto link_ptr = &(*nodes)[tndx].link;
    ASSERT_TRUE((*nodes)[tndx].thread.BlockOnOwnedQueue(link_ptr, owner_thread));

    for (uint32_t validation_ndx = 0; validation_ndx <= tndx; ++validation_ndx) {
      PRINT_LOOP_ITER(validation_ndx);

      int expected_prio = prio_map[(tndx == (nodes->size() - 1)) ? tndx : validation_ndx];
      ASSERT_EQ(expected_prio, (*nodes)[validation_ndx].thread.effective_priority());

      print_validation_ndx.cancel();
    }

    print_tndx.cancel();
  }

  END_TEST;
}

// Exercise the specific failure tracked down during the investigation of fxbug.dev/33934
//
// There are a few different ways that this situation can be forced to happen.
// See the bug writeup for details.
bool pi_test_zx4153() {
  BEGIN_TEST;
  AutoPrioBooster pboost;

  // Repro of this involves 2 threads and 2 wait queues involved in a PI
  // cycle.  The simplest repro is as follows.
  //
  // Let T1.prio == 16
  // Let T2.prio == 17
  //
  // 1) Block T1 on Q2 and declare T2 to be the owner of Q2
  // 2) Block T2 on Q1 and declare T1 to be the owner of Q1.  T1 and T2 now
  //    form a cycle.  The inherited priority of the cycle is now 17.
  // 3) Raise T1's priority to 20.  The cycle priority is now up to 20.
  // 4) Lower T1's priority back down to 16.  The cycle priority remains at
  //    20.  It cannot relax until the cycle is broken.
  // 5) Break the cycle by declaring Q1 to have no owner.  Do not wake T1.
  //
  // If the bookkeeping error found in fxbug.dev/33934 was still around, the effect
  // would be...
  //
  // 1) T1 no longer feels pressure from Q1.  T1's effective priority drops
  //    from 20 to 16 (its base priority)
  // 2) T1 is the only waiter on Q2.  Q2's pressure drops from 20 -> 16
  // 3) The pressure applied to T2 drops from 20 -> 16.  T2's effective
  //    priority is now 17 (its base priority).
  // 4) T2 is the only waiter on Q1.  Q1's pressure drops from 20 -> 17
  // 5) Q1's owner is still mistakenly set to T1.  T1 receives Q1's pressure,
  //    and its inherited priority goes from -1 -> 17.
  // 6) Q1 now owns no queues, but has inherited priority.  This should be
  //    impossible, and triggers the assert.
  //

  TestThread T1, T2;
  LockedOwnedWaitQueue Q1, Q2;

  // At the end of the tests, success or failure, be sure to clean up.
  auto cleanup = fbl::MakeAutoCall([&]() {
    TestThread::ClearShutdownBarrier();
    Q1.ReleaseAllThreads();
    Q2.ReleaseAllThreads();
    T1.Reset();
    T2.Reset();
  });

  constexpr int kT1InitialPrio = 16;
  constexpr int kT2InitialPrio = 17;
  constexpr int kT1BoostPrio = 20;

  // Create the threads.
  ASSERT_TRUE(T1.Create(kT1InitialPrio));
  ASSERT_TRUE(T2.Create(kT2InitialPrio));

  ASSERT_EQ(T1.base_priority(), kT1InitialPrio);
  ASSERT_EQ(T1.inherited_priority(), -1);
  ASSERT_EQ(T1.effective_priority(), kT1InitialPrio);

  ASSERT_EQ(T2.base_priority(), kT2InitialPrio);
  ASSERT_EQ(T2.inherited_priority(), -1);
  ASSERT_EQ(T2.effective_priority(), kT2InitialPrio);

  // Form the cycle, verify the priorities
  ASSERT_TRUE(T1.BlockOnOwnedQueue(&Q2, &T2));
  ASSERT_TRUE(T2.BlockOnOwnedQueue(&Q1, &T1));

  ASSERT_EQ(T1.base_priority(), kT1InitialPrio);
  ASSERT_EQ(T1.inherited_priority(), kT2InitialPrio);
  ASSERT_EQ(T1.effective_priority(), kT2InitialPrio);

  ASSERT_EQ(T2.base_priority(), kT2InitialPrio);
  ASSERT_EQ(T2.inherited_priority(), kT2InitialPrio);
  ASSERT_EQ(T2.effective_priority(), kT2InitialPrio);

  // Boost T1's priority.
  ASSERT_TRUE(T1.SetBasePriority(kT1BoostPrio));

  ASSERT_EQ(T1.base_priority(), kT1BoostPrio);
  ASSERT_EQ(T1.inherited_priority(), kT1BoostPrio);
  ASSERT_EQ(T1.effective_priority(), kT1BoostPrio);

  ASSERT_EQ(T2.base_priority(), kT2InitialPrio);
  ASSERT_EQ(T2.inherited_priority(), kT1BoostPrio);
  ASSERT_EQ(T2.effective_priority(), kT1BoostPrio);

  // Relax T1's priority.  The cycle's priority cannot relax yet.
  ASSERT_TRUE(T1.SetBasePriority(kT1InitialPrio));

  ASSERT_EQ(T1.base_priority(), kT1InitialPrio);
  ASSERT_EQ(T1.inherited_priority(), kT1BoostPrio);
  ASSERT_EQ(T1.effective_priority(), kT1BoostPrio);

  ASSERT_EQ(T2.base_priority(), kT2InitialPrio);
  ASSERT_EQ(T2.inherited_priority(), kT1BoostPrio);
  ASSERT_EQ(T2.effective_priority(), kT1BoostPrio);

  // Release ownership of Q1, breaking the cycle.  T2 should feel the pressure
  // from T1, but T1 should not be inheriting any priority anymore.
  Q1.AssignOwner(nullptr);

  ASSERT_EQ(T1.base_priority(), kT1InitialPrio);
  ASSERT_EQ(T1.inherited_priority(), -1);
  ASSERT_EQ(T1.effective_priority(), kT1InitialPrio);

  ASSERT_EQ(T2.base_priority(), kT2InitialPrio);
  ASSERT_EQ(T2.inherited_priority(), kT1InitialPrio);
  ASSERT_EQ(T2.effective_priority(), kT2InitialPrio);

  // Success!  Let the cleanup AutoCall do its job.

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(pi_tests)
UNITTEST("basic", pi_test_basic)
UNITTEST("changing priority", pi_test_changing_priority)
UNITTEST("long chains", pi_test_chain<29>)
UNITTEST("multiple waiters", pi_test_multi_waiter<29>)
UNITTEST("multiple owned queues", pi_test_multi_owned_queues<29>)
UNITTEST("cycles", pi_test_cycle<29>)
UNITTEST("fxbug.dev/33934", pi_test_zx4153)
UNITTEST_END_TESTCASE(pi_tests, "pi", "Priority inheritance tests for OwnedWaitQueues")
