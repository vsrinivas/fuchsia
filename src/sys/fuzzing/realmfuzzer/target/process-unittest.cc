// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests only cover the basic configuration and operation of the Process class. Testing
// functionality that leads to the process exiting is tricky. It can require specific build
// configurations (i.e. link against ASan or LSan) and more complex process lifecycle management. As
// a result, this functionality is tested using integration rather than unit tests.

#include "src/sys/fuzzing/realmfuzzer/target/process.h"

#include <stddef.h>
#include <stdint.h>
#include <zircon/status.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/realmfuzzer/engine/coverage-data.h"
#include "src/sys/fuzzing/realmfuzzer/engine/module-pool.h"
#include "src/sys/fuzzing/realmfuzzer/testing/coverage.h"
#include "src/sys/fuzzing/realmfuzzer/testing/module.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageDataProviderPtr;
using ::fuchsia::fuzzer::Options;

// Test fixtures.

class ProcessTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    coverage_ = std::make_unique<FakeCoverage>(executor());
    eventpair_ = std::make_shared<AsyncEventPair>(executor());
    pool_ = ModulePool::MakePtr();

    auto provider_handler = coverage_->GetProviderHandler();
    provider_handler(provider_.NewRequest(executor()->dispatcher()));
    Configure(DefaultOptions());
  }

  // Accessors.
  ModulePoolPtr pool() const { return pool_; }
  uint64_t target_id() const { return target_id_; }
  size_t num_added() const { return added_.size(); }
  std::shared_ptr<AsyncEventPair> eventpair() const { return eventpair_; }

  // Returns options that limit the number of spurious warnings during tests.
  static OptionsPtr DefaultOptions(bool disable_warnings = true) {
    auto options = MakeOptions();
    if (disable_warnings) {
      options->set_malloc_limit(0);
      options->set_purge_interval(0);
    }
    AddDefaults(options.get());
    return options;
  }

  // Copies the given |options| to the watcher, to be given to new processes.
  void Configure(OptionsPtr options) {
    provider_->SetOptions(CopyOptions(*options));
    RunOnce();
  }

  // Returns a promises to connect the given process to the fake "engine" provided by the test.
  // Tests typically need to call |WatchForProcess| and |WatchForModule| for this promise to
  // complete.
  ZxPromise<> Connect(Process* process) {
    fidl::InterfaceHandle<CoverageDataCollector> collector;
    auto collector_handler = coverage_->GetCollectorHandler();
    collector_handler(collector.NewRequest());
    auto eventpair = std::make_shared<AsyncEventPair>(executor());
    auto task = process->Connect(std::move(collector), eventpair->Create()).wrap_with(scope_);
    executor()->schedule_task(std::move(task));
    return fpromise::make_promise([eventpair, wait = ZxFuture<zx_signals_t>()](
                                      Context& context) mutable -> ZxResult<> {
             if (!wait) {
               wait = eventpair->WaitFor(kSync);
             }
             if (!wait(context)) {
               return fpromise::pending();
             }
             if (wait.is_error()) {
               return fpromise::error(wait.error());
             }
             return fpromise::ok();
           })
        .wrap_with(scope_);
  }

  // Creates a fake module for the current process, but defers adding its coverage. Returns the
  // unique module ID.
  std::string CreateModule() {
    FakeRealmFuzzerModule module(static_cast<uint32_t>(modules_.size() + 1));
    auto id = module.id();
    auto result = modules_.emplace(id, std::move(module));
    FX_CHECK(result.second);
    return id;
  }

  // Creates a fake module for the current process and adds its coverage. Returns the unique module
  // ID.
  std::string AddModule() {
    auto id = CreateModule();
    auto* module = GetModule(id);
    __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
    __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
    return id;
  }

  // The returned pointer may be invalidated by calls to |AddModule|.
  FakeRealmFuzzerModule* GetModule(const std::string& id) {
    auto i = modules_.find(id);
    return i == modules_.end() ? nullptr : &i->second;
  }

  // Returns a promise to handle an expected coverage event from a new process. Completes
  // with an error if the next coverage event is for an LLVM module.
  Promise<> WatchForProcess() {
    Bridge<CoverageData> bridge;
    provider_->GetCoverageData(bridge.completer.bind());
    return bridge.consumer.promise_or(fpromise::error())
        .and_then([this](CoverageData& coverage_data) -> Result<> {
          if (!coverage_data.is_instrumented()) {
            return fpromise::error();
          }
          auto& instrumented = coverage_data.instrumented();
          target_id_ = GetTargetId(instrumented.process);
          eventpair_->Pair(std::move(instrumented.eventpair));
          return fpromise::ok();
        })
        .wrap_with(scope_);
  }

  // Returns a promise to handle an expected coverage event from a new module. Completes
  // with an error if the next coverage event is for an instrumented process.
  Promise<> WatchForModule() {
    Bridge<CoverageData> bridge;
    provider_->GetCoverageData(bridge.completer.bind());
    return bridge.consumer.promise_or(fpromise::error())
        .and_then([this](CoverageData& coverage_data) -> Result<> {
          if (!coverage_data.is_inline_8bit_counters()) {
            return fpromise::error();
          }
          auto& inline_8bit_counters = coverage_data.inline_8bit_counters();
          auto module_id = GetModuleId(inline_8bit_counters);
          SharedMemory counters;
          if (auto status = counters.Link(std::move(inline_8bit_counters)); status != ZX_OK) {
            return fpromise::error();
          }
          auto* module = pool_->Get(module_id, counters.size());
          module->Add(counters.data(), counters.size());
          added_.push_back(std::move(counters));
          return fpromise::ok();
        })
        .wrap_with(scope_);
  }

 private:
  std::unique_ptr<FakeCoverage> coverage_;
  std::shared_ptr<AsyncEventPair> eventpair_;
  ModulePoolPtr pool_;
  CoverageDataProviderPtr provider_;
  uint64_t target_id_ = kInvalidTargetId;
  std::unordered_map<std::string, FakeRealmFuzzerModule> modules_;
  std::vector<SharedMemory> added_;
  Completer<zx_signals_t> completer_;
  Scope scope_;
};

// Unit tests.

TEST_F(ProcessTest, ConnectProcess) {
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  auto self = zx::process::self();
  zx_info_handle_basic_t info;
  EXPECT_EQ(self->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  EXPECT_EQ(target_id(), info.koid);
}

TEST_F(ProcessTest, ConnectWithDefaultOptions) {
  Configure(DefaultOptions(/* disable_warnings */ false));

  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  const auto& options = process.options();
  EXPECT_EQ(options.detect_leaks(), kDefaultDetectLeaks);
  EXPECT_EQ(options.malloc_limit(), kDefaultMallocLimit);
  EXPECT_EQ(options.oom_limit(), kDefaultOomLimit);
  EXPECT_EQ(options.purge_interval(), kDefaultPurgeInterval);
  EXPECT_EQ(options.malloc_exitcode(), kDefaultMallocExitcode);
  EXPECT_EQ(options.death_exitcode(), kDefaultDeathExitcode);
  EXPECT_EQ(options.leak_exitcode(), kDefaultLeakExitcode);
  EXPECT_EQ(options.oom_exitcode(), kDefaultOomExitcode);
}

TEST_F(ProcessTest, ConnectDisableLimits) {
  auto options = DefaultOptions();
  options->set_malloc_limit(0);
  options->set_purge_interval(0);
  Configure(options);

  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  EXPECT_EQ(process.malloc_limit(), std::numeric_limits<size_t>::max());
  EXPECT_EQ(process.next_purge(), zx::time::infinite());
}

TEST_F(ProcessTest, ConnectAndAddModules) {
  // Modules can be added "early", i.e. before the |Process| constructor...
  auto id1 = AddModule();
  auto id2 = AddModule();
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));

  // Add ALL the modules. This may include extras if the test itself is instrumented. The promise
  // will be dropped when the test completes and the scope object is destroyed.
  Scope scope;
  auto task = WatchForProcess()
                  .and_then([this, watch = Future<>()](Context& context) mutable -> Result<> {
                    while (true) {
                      if (!watch) {
                        watch = WatchForModule();
                      }
                      if (!watch(context)) {
                        return fpromise::pending();
                      }
                      if (watch.is_error()) {
                        return fpromise::error();
                      }
                      watch = nullptr;
                    }
                  })
                  .wrap_with(scope);
  executor()->schedule_task(std::move(task));

  // ...or late, i.e. via `dlopen`.
  auto id3 = AddModule();
  auto id4 = AddModule();
  RunUntilIdle();

  EXPECT_NE(GetModule(id1), nullptr);
  EXPECT_NE(GetModule(id2), nullptr);
  EXPECT_NE(GetModule(id3), nullptr);
  EXPECT_NE(GetModule(id4), nullptr);
}

TEST_F(ProcessTest, ConnectBadModules) {
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  // |initial| may be non-zero when the test is instrumented.
  size_t initial = num_added();

  // Empty-length module.
  auto* module = GetModule(CreateModule());
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters());
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs());
  EXPECT_EQ(num_added(), initial);

  // Module ends before it begins.
  __sanitizer_cov_8bit_counters_init(module->counters() + 1, module->counters());
  __sanitizer_cov_pcs_init(module->pcs() + 2, module->pcs());
  EXPECT_EQ(num_added(), initial);

  // Mismatched length.
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end() - 1);
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  EXPECT_EQ(num_added(), initial);
}

TEST_F(ProcessTest, ConnectLateModules) {
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  // |initial| may be non-zero when the test is instrumented.
  size_t initial = num_added();

  // Modules with missing fields are deferred.
  FUZZING_EXPECT_OK(WatchForModule());
  auto id1 = CreateModule();
  auto* module = GetModule(id1);
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  RunOnce();
  EXPECT_EQ(num_added(), initial);

  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  RunUntilIdle();
  EXPECT_EQ(num_added(), initial + 1);

  FUZZING_EXPECT_OK(WatchForModule());
  auto id2 = CreateModule();
  module = GetModule(id2);
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  RunOnce();
  EXPECT_EQ(num_added(), initial + 1);

  FUZZING_EXPECT_OK(WatchForModule());
  auto id3 = CreateModule();
  module = GetModule(id3);
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  RunOnce();
  EXPECT_EQ(num_added(), initial + 1);

  module = GetModule(id2);
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  RunOnce();
  EXPECT_EQ(num_added(), initial + 2);

  module = GetModule(id3);
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  RunUntilIdle();
  EXPECT_EQ(num_added(), initial + 3);
}

TEST_F(ProcessTest, ImplicitStart) {
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  // Processes should be implicitly |Start|ed on |Connect|ing.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinish));
  EXPECT_EQ(eventpair()->SignalPeer(0, kFinish), ZX_OK);
  RunUntilIdle();

  EXPECT_EQ(pool()->Measure(), 0U);
}

TEST_F(ProcessTest, UpdateOnFinish) {
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  auto* module = GetModule(AddModule());
  FUZZING_EXPECT_OK(WatchForModule());
  RunUntilIdle();

  // No new coverage.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinish));
  EXPECT_EQ(eventpair()->SignalPeer(0, kFinish), ZX_OK);
  RunUntilIdle();

  EXPECT_EQ(pool()->Measure(), 0U);

  // Add some counters.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kStart));
  EXPECT_EQ(eventpair()->SignalPeer(kFinish, kStart), ZX_OK);
  RunUntilIdle();

  (*module)[0] = 4;
  (*module)[module->num_pcs() / 2] = 16;
  (*module)[module->num_pcs() - 1] = 128;

  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinish));
  EXPECT_EQ(eventpair()->SignalPeer(kStart, kFinish), ZX_OK);
  RunUntilIdle();

  EXPECT_EQ(pool()->Measure(), 3U);
}

TEST_F(ProcessTest, UpdateOnExit) {
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  auto* module = GetModule(AddModule());
  FUZZING_EXPECT_OK(WatchForModule());
  RunUntilIdle();

  // Add some counters.
  (*module)[module->num_pcs() - 4] = 64;
  (*module)[module->num_pcs() - 3] = 32;
  (*module)[module->num_pcs() - 2] = 16;
  (*module)[module->num_pcs() - 1] = 8;

  //  Fake a call to |exit|.
  process.OnExit();
  EXPECT_EQ(pool()->Measure(), 4U);
}

TEST_F(ProcessTest, FinishWithoutLeaks) {
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  // No mallocs/frees, and no leak detection.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinish));
  EXPECT_EQ(eventpair()->SignalPeer(0, kFinish), ZX_OK);
  RunUntilIdle();

  // Balanced mallocs/frees, and no leak detection.
  // The pointers and sizes don't actually matter; just the number of calls.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kStart));
  EXPECT_EQ(eventpair()->SignalPeer(0, kStart), ZX_OK);
  RunUntilIdle();

  process.OnMalloc(nullptr, 0);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  process.OnFree(nullptr);

  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinish));
  EXPECT_EQ(eventpair()->SignalPeer(0, kFinish), ZX_OK);
  RunUntilIdle();

  // No mallocs/frees, with leak detection.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kStart));
  EXPECT_EQ(eventpair()->SignalPeer(0, kStartLeakCheck), ZX_OK);
  RunUntilIdle();

  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinish));
  EXPECT_EQ(eventpair()->SignalPeer(0, kFinish), ZX_OK);
  RunUntilIdle();

  // Balanced mallocs/frees, with leak detection.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kStart));
  EXPECT_EQ(eventpair()->SignalPeer(0, kStartLeakCheck), ZX_OK);
  RunUntilIdle();

  process.OnMalloc(nullptr, 0);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  process.OnFree(nullptr);

  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinish));
  EXPECT_EQ(eventpair()->SignalPeer(0, kFinish), ZX_OK);
  RunUntilIdle();
}

TEST_F(ProcessTest, FinishWithLeaks) {
  Process process(executor());
  FUZZING_EXPECT_OK(Connect(&process));
  FUZZING_EXPECT_OK(WatchForProcess());
  RunUntilIdle();

  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinish));
  EXPECT_EQ(eventpair()->SignalPeer(0, kFinish), ZX_OK);
  RunUntilIdle();

  // Unbalanced mallocs/frees, and no leak detection.
  // The pointers and sizes don't actually matter; just the number of calls.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kStart));
  EXPECT_EQ(eventpair()->SignalPeer(kFinish, kStart), ZX_OK);
  RunUntilIdle();

  process.OnMalloc(nullptr, 0);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);

  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinishWithLeaks));
  EXPECT_EQ(eventpair()->SignalPeer(kStart, kFinish), ZX_OK);
  RunUntilIdle();

  // Unbalanced mallocs/frees, with leak detection.
  // Since these aren't real leaks, this will not abort.
  FUZZING_EXPECT_OK(eventpair()->WaitFor(kStart));
  EXPECT_EQ(eventpair()->SignalPeer(kFinish, kStartLeakCheck), ZX_OK);
  RunUntilIdle();

  process.OnMalloc(nullptr, 0);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);

  FUZZING_EXPECT_OK(eventpair()->WaitFor(kFinishWithLeaks));
  EXPECT_EQ(eventpair()->SignalPeer(kStartLeakCheck, kFinish), ZX_OK);
  RunUntilIdle();
}

}  // namespace fuzzing
