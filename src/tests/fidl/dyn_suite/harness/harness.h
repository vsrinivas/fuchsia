// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_DYN_SUITE_HARNESS_HARNESS_H_
#define SRC_TESTS_FIDL_DYN_SUITE_HARNESS_HARNESS_H_

#include <fidl/fidl.dynsuite/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/service/llcpp/service.h>
#include <zircon/assert.h>

#include <chrono>
#include <iostream>
#include <thread>

#include <gtest/gtest.h>

// An Observation value is exposed to the test DSL and represents specific
// observations made by instrumenting the target bindings under test.
//
// These observations represent a subset of the variants of
// `fidl.dynsuite/Observation`, and meant to be augmented with accessors for
// easy manipulation by the test DSL.
class Observation final {
 public:
  enum class Kind {
    kOnBind,
    kOnUnbind,
    kOnComplete,
    kOnError,
    kOnMethodInvocation,
  };
  Observation(Kind kind) : kind_(kind) {}
  Kind kind() { return kind_; }

 private:
  Kind kind_;
};

// The Observations collection represents a group of observations.
class Observations final {
 public:
  Observations(std::vector<Observation>& obs) : obs_(obs) {}
  bool has(Observation::Kind kind) const {
    for (auto&& observation : obs_) {
      if (observation.kind() == kind) {
        return true;
      }
    }
    return false;
  }
  size_t size() const { return obs_.size(); }
  Observation& operator[](size_t index) const { return obs_[index]; }

 private:
  std::vector<Observation>& obs_;
};

// The ObserverOrchestrator is responsible for listening to
// `fidl.dynsuite/Observation` sent by the bindings under test, and orchestating
// the actions that these should lead to, e.g. record them, release a program
// point, etc.
class ObserverOrchestrator final : public fidl::WireServer<fidl_dynsuite::Observer> {
 public:
  ObserverOrchestrator(async_dispatcher_t* dispatcher,
                       fidl::ServerEnd<fidl_dynsuite::Observer> server_end,
                       std::function<void()> on_completion_callback)
      : server_ref_(fidl::BindServer(dispatcher, std::move(server_end), this)),
        on_completion_callback_(on_completion_callback) {}

  void Observe(ObserveRequestView request, ObserveCompleter::Sync& _completer);

  void RecordInto(std::vector<Observation>* observations);
  void SyncOnProgramPoint(uint64_t program_point);
  bool HasReachedProgramPoint(uint64_t program_point);

 private:
  void ReleaseProgramPoint(uint64_t actual_program_point);

  fidl::ServerBindingRef<fidl_dynsuite::Observer> server_ref_;

  std::optional<uint64_t> actual_program_point_;
  std::function<void()> on_completion_callback_;
  std::vector<Observation>* to_record_ = nullptr;
};

class TestBase;

// The TestContext holds the various pieces that a test needs to run, and is
// meant to be used both by server tests, i.e. tests which exercise the server
// surface of the bindings under test, or client tests.
class TestContext final {
 public:
  TestContext(TestBase* test_base, async_dispatcher_t* dispatcher,
              fidl::WireClient<fidl_dynsuite::Entry>& entry_client)
      : stage_(Stage::kInitial),
        test_base_(test_base),
        dispatcher_(dispatcher),
        entry_client_(entry_client) {}

  void start_server_test();
  void start_client_test();
  zx::channel TakeClientEndToTest();
  zx::channel TakeServerEndToTest();
  bool has_completed();
  void SyncOnProgramPoint(uint64_t program_point);
  bool HasReachedProgramPoint(uint64_t program_point);
  void run(std::function<void()> when, std::function<bool(const Observations)> wait_for,
           std::function<void(const Observations)> then_observe);
  std::vector<Observation> when_then_observe(std::function<void()> fn);
  bool HasNNewObservations(uint32_t n);

 private:
  void NotifyOfCompletion();
  enum class Stage {
    kInitial,
    kStarting,
    kStarted,
    kStopped,
  };
  Stage stage_;
  TestBase* test_base_;
  async_dispatcher_t* dispatcher_;
  fidl::WireClient<fidl_dynsuite::Entry>& entry_client_;
  fidl::ClientEnd<fidl_dynsuite::ServerTest> client_end_to_test_;
  fidl::ServerEnd<fidl_dynsuite::ClientTest> server_end_to_test_;
  std::optional<ObserverOrchestrator> observer_orchestrator_;
  std::unique_ptr<std::vector<Observation>> observations_being_recorded_;
};

class WaitFor;

// Then When class is a helper class to support the
// `when().wait_for().then_observe()` DSL. It is specifically responsible for
// the `.wait_for()` portion.
//
// See also:
// * `WaitFor` class
// * `TestBase::when` method
class When {
 public:
  When(TestContext& context, std::function<void()> when_fn)
      : context_(context), when_fn_(when_fn) {}
  [[nodiscard]] WaitFor wait_for(std::function<bool(const Observations)> wait_for_fn);

 protected:
  TestContext& context_;
  std::function<void()> when_fn_;
};

// Then WaitFor class is a helper class to support the
// `when().wait_for().then_observe()` DSL. It is specifically responsible for
// the `.then_observe()` portion.
//
// See also:
// * `When` class
// * `TestBase::when` method
class WaitFor : private When {
 public:
  WaitFor(TestContext& context, std::function<void()> when_fn,
          std::function<bool(const Observations)> wait_for_fn)
      : When(context, when_fn), wait_for_fn_(wait_for_fn) {}
  void then_observe(std::function<void(const Observations)> then_observe_fn);

 protected:
  std::function<bool(const Observations)> wait_for_fn_;
};

class TestBase : private ::loop_fixture::RealLoop, public ::testing::Test {
 public:
  void run_until(std::function<bool()> condition);
  [[nodiscard]] When when(std::function<void()> when_fn);

 protected:
  void SetUp() override;
  void TearDown() override;
  TestContext& context();

 private:
  std::optional<fidl::WireClient<fidl_dynsuite::Entry>> entry_client_;
  std::optional<TestContext> context_;
};

class ServerTest : public TestBase {
 protected:
  void SetUp() override;
  void TearDown() override;
  zx_handle_t client_end;
};

class ClientTest : public TestBase {
 protected:
  void SetUp() override;
  void TearDown() override;
  zx_handle_t server_end;
};

#endif  // SRC_TESTS_FIDL_DYN_SUITE_HARNESS_HARNESS_H_
