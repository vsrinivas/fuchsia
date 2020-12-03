// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

    // Wait for the receiver to notify us back.
    {
      std::unique_lock lock{mutex_};
      condvar_.wait(lock, [this] { return this->MessageHandled(); });
    }
  }

  Message Receive() {
    // Wait for, and receive, a message from the sender.
    Message message = [this] {
      std::unique_lock lock{mutex_};
      condvar_.wait(lock, [this] { return this->HasMessage(); });
      Message message = std::move(*message_);
      message_ = std::nullopt;
      return message;
    }();

    condvar_.notify_one();
    return message;
  }

 private:
  bool HasMessage() const { return message_.has_value(); }
  bool MessageHandled() const { return !message_.has_value(); }

  std::mutex mutex_;
  std::condition_variable condvar_;

  std::optional<Message> message_ = std::nullopt;
};

enum Operation {
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

 private:
  static void* Handler(void* self) {
    auto thread = static_cast<Thread*>(self);
    thread->Run();
    return nullptr;
  }

  void Run() {
    for (;;) {
      Operation op = gate_->Receive();

      switch (op) {
        case kDetach: {
          int ret = pthread_detach(thread_);
          // We are already detached, and testing that this returns
          // EINVAL. Specifically
          ASSERT_EQ(ret, EINVAL);
          break;
        }

        case kExit: {
          pthread_exit(nullptr);
          break;
        }
      }
    }
  }

  pthread_t thread_;
  Gate<Operation>* gate_;
};

TEST(PthreadDetach, Idempotent) {
  Gate<Operation> gate;

  // Create a |Thread|, and assert that the construction of the
  // pthread_t had no fatal errors.
  Thread thread{&gate};
  ASSERT_NO_FATAL_FAILURES();

  // Ten is an arbitrary number greater than 1, to exercise the
  // behavior a few times.
  for (int count = 0; count < 10; ++count) {
    ASSERT_NO_FATAL_FAILURES(gate.Send(kDetach));
  }

  gate.Send(kExit);
}

}  // namespace
