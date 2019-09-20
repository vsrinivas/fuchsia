// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_THREADING_MODEL_FIXTURE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_THREADING_MODEL_FIXTURE_H_

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/gtest/test_loop_fixture.h>
#include <zircon/compiler.h>

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/threading_model.h"

namespace media::audio::testing {

// Implements a |ThreadingModel| on top of the |async::TestLoop| to enable easily writing unit tests
// against components that depend on |ThreadingModel|.
class TestThreadingModel : public ThreadingModel {
 public:
  explicit TestThreadingModel(async::TestLoop* test_loop) : loop_(test_loop) {}
  ~TestThreadingModel() override = default;

  // |ThreadingModel|
  ExecutionDomain& FidlDomain() override { return fidl_holder_.domain; }
  ExecutionDomain& IoDomain() override { return io_holder_.domain; }
  OwnedDomainPtr AcquireMixDomain() override {
    return OwnedDomainPtr(&mix_holder_.domain, [](...) {});
  }
  void Quit() override { loop_->Quit(); }
  // Note we should never call this on the |TestThreadingModel|. Execution should instead be
  // controlled using the |async::TestLoop| used to construct this |ThreadingModel|.
  void RunAndJoinAllThreads() override {
    FXL_CHECK(false) << "RunAndJoinAllThreads not supported on TestThreadingModel.";
  }

 private:
  struct DomainHolder {
    DomainHolder(async::TestLoop* test_loop)
        : loop(test_loop->StartNewLoop()),
          executor(loop->dispatcher()),
          domain{loop->dispatcher(), &executor} {}

    std::unique_ptr<async::LoopInterface> loop;
    async::Executor executor;
    ExecutionDomain domain;
  };

  async::TestLoop* loop_;
  DomainHolder fidl_holder_{loop_};
  DomainHolder io_holder_{loop_};
  DomainHolder mix_holder_{loop_};
};

// A test fixture that provides a |ThreadingModel| on top of an |async::TestLoop|. We inherit from
// |gtest::TestLoopFixture| to make it simple to convert tests that are already using test loops.
//
// Ex:
//   TEST_F(MyTest, Foo) {
//     UnderTest bar(&threading_model());
//     bar.ScheduleSomeWork();
//     RunLoopUntilIdle();
//
//     AssertScheduledWorkCompleted(&bar);
//   }
class ThreadingModelFixture : public gtest::TestLoopFixture {
 protected:
  // This threading model will be backed by an |async::TestLoop|. Control the loop using the methods
  // in |gtest::TestLoopFixture|.
  ThreadingModel& threading_model() { return threading_model_; };

 private:
  TestThreadingModel threading_model_{&test_loop()};
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_THREADING_MODEL_FIXTURE_H_
