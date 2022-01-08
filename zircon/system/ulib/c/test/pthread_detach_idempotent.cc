// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <pthread.h>

#include <condition_variable>
#include <mutex>
#include <optional>

#include <zxtest/zxtest.h>

// While detaching or joining a pthread_t or thrd_t multiple times is
// not well defined, our libc does detect this behavior in some
// circumstances.
//
// TODO(fxbug.dev/65753) precisely define our behavior in this sort of
// situation.

namespace {

// A gate which lets one side send a message to the other, and for the
// sender to wait on the other side to finish.
template <typename Message>
class Gate {
 public:
  void Send(Message message) {
    // Send the message.
    {
      std::unique_lock lock{mutex_};
      message_ = message;
    }

    condvar_.notify_one();

    // Wait for the receiver ack that it has processed the command.
    {
      std::unique_lock lock{mutex_};
      condvar_.wait(lock, [this] { return this->MessageHandled(); });
    }
  }

  Message PeekMessage() {
    // Return a copy of any pending message.  Do not ack that the message has
    // been processed yet.
    std::unique_lock lock{mutex_};
    condvar_.wait(lock, [this] { return this->HasMessage(); });
    ZX_DEBUG_ASSERT(message_.has_value());
    return message_.value();
  }

  void AckMessage() {
    // Clear out any pending message and poke the condvar, signalling to the
    // transmitter that they may process the results from the previous message,
    // and send the next message.
    {
      std::unique_lock lock{mutex_};
      message_ = std::nullopt;
    }

    condvar_.notify_one();
  }

 private:
  bool HasMessage() const { return message_.has_value(); }
  bool MessageHandled() const { return !message_.has_value(); }

  std::mutex mutex_;
  std::condition_variable condvar_;
  std::optional<Message> message_ = std::nullopt;
};

enum class Operation {
  kExit,
  kDetach,
};

// A thread which is created in the detached state, and which
// repeatedly waits to either detach or exit.
class Thread {
 public:
  explicit Thread(Gate<Operation>* gate) : gate_(gate) {
    pthread_attr_t attrs;
    int ret = pthread_attr_init(&attrs);
    ASSERT_EQ(ret, 0);

    ret = pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    ASSERT_EQ(ret, 0);

    ret = pthread_create(&thread_, &attrs, &Thread::Handler, this);
    ASSERT_EQ(ret, 0);

    ret = pthread_attr_destroy(&attrs);
    ASSERT_EQ(ret, 0);
  }

  void WaitForThreadExited() { sync_completion_wait(&thread_exited_, ZX_TIME_INFINITE); }

 private:
  static void* Handler(void* self) {
    auto thread = static_cast<Thread*>(self);
    thread->Run();

    // Remember to ack our last received message.  It was either a kExit
    // command, or a kDetach command in the case that the test failed and
    // aborted early.  Either way, we do not want to leave the main thread
    // waiting forever.
    thread->gate_->AckMessage();
    sync_completion_signal(&thread->thread_exited_);
    return nullptr;
  }

  void Run() {
    for (;;) {
      Operation op = gate_->PeekMessage();

      if (op == Operation::kExit) {
        return;
      }

      int ret = pthread_detach(thread_);
      // We are already detached, and testing that this returns
      // EINVAL. Specifically
      ASSERT_EQ(ret, EINVAL);
      gate_->AckMessage();
    }
  }

  pthread_t thread_;
  Gate<Operation>* gate_;
  sync_completion_t thread_exited_;
};

TEST(PthreadDetach, Idempotent) {
  Gate<Operation> gate;

  // Create a |Thread|, and assert that the construction of the
  // pthread_t had no fatal errors.
  Thread thread{&gate};
  ASSERT_NO_FATAL_FAILURE();

  // Now that our thread has been successfully created, run our main test from
  // within the scope of the lambda.  If the test encounters a fatal failure and
  // we need to bail out early, we still need to wait for the thread to have
  // exited, or we risk a stack-use-after-free situation.
  auto run_test = [&gate]() {
    // Ten is an arbitrary number greater than 1, to exercise the
    // behavior a few times.
    for (int count = 0; count < 10; ++count) {
      ASSERT_NO_FATAL_FAILURE(gate.Send(Operation::kDetach));
    }
    gate.Send(Operation::kExit);
  };

  run_test();

  // Note, do not skip this waiting step.  It is not safe to remove the Thread
  // and Gate instances from the stack yet, even after the acknowledgement of
  // the last message.  While it would be nice to use a join for this, we have
  // detached this thread, so we will have to make due with a completion.
  // See the write-up in fxb/70261 for details.
  thread.WaitForThreadExited();
}

}  // namespace
