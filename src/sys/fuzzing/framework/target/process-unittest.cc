// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests only cover the basic configuration and operation of the Process class. Testing
// functionality that leads to the process exiting is tricky. It can require specific build
// configurations (i.e. link against ASan or LSan) and more complex process lifecycle management. As
// a result, this functionality is tested using integration rather than unit tests.

#include "src/sys/fuzzing/framework/target/process.h"

#include <lib/fidl/cpp/binding.h>

#include <memory>
#include <unordered_map>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/engine/process-proxy.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/process-proxy.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Options;

// Test fixtures.

constexpr uint32_t kNumModules = 4;

// Generates some simple |modules|. |Collect| requires at least one module, so this initializes the
// first one. This method should be called *before* instantiating a |TestProcess|.
std::vector<FakeFrameworkModule> CreateModulesAndInitFirst() {
  std::vector<FakeFrameworkModule> modules;
  for (uint32_t i = 0; i < kNumModules; ++i) {
    modules.emplace_back(i + 1);
  }
  __sanitizer_cov_8bit_counters_init(modules[0].counters(), modules[0].counters_end());
  __sanitizer_cov_pcs_init(modules[0].pcs(), modules[0].pcs_end());
  return modules;
}

// This class simply exposes some protected methods for testing. Notably, it does NOT automatically
// connect to a |fuchsia.fuzzer.ProcessProxy| or call |Process::InstallHooks|, as
// |InstrumentedProcess| does.
class TestProcess final : public Process {
 public:
  explicit TestProcess(ExecutorPtr executor) : Process(std::move(executor)) {}
  ~TestProcess() override = default;
  using Process::AddModules;
  using Process::Connect;
  using Process::malloc_limit;
  using Process::next_purge;
  using Process::options;
  using Process::Run;
};

// The unit test base class.
class ProcessTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    // Create and destroy a process. This will "consume" any extra modules added if the unit test
    // itself is instrumented.
    { TestProcess process(executor()); }
    pool_ = ModulePool::MakePtr();
  }

  // Create a fake |ProcessProxy| with the given |options|.
  std::unique_ptr<FakeProcessProxy> MakeProxy(OptionsPtr options) {
    auto proxy = std::make_unique<FakeProcessProxy>(executor(), pool_);
    proxy->Configure(options);
    return proxy;
  }

  // Returns a |TestProcess| that is |Connect|ed to the given |proxy|.
  std::unique_ptr<TestProcess> MakeProcess(std::unique_ptr<FakeProcessProxy>& proxy) {
    auto process = std::make_unique<TestProcess>(executor());
    process->set_handler(proxy->GetHandler());
    FUZZING_EXPECT_OK(process->Connect());
    FUZZING_EXPECT_OK(proxy->AwaitSent(kSync));
    RunUntilIdle();
    executor()->schedule_task(process->AddModules());
    executor()->schedule_task(process->Run());
    RunOnce();
    return process;
  }

  size_t MeasurePool() { return pool_->Measure(); }

 private:
  ModulePoolPtr pool_;
};

OptionsPtr DefaultOptions(bool disable_warnings = true) {
  auto options = MakeOptions();
  if (disable_warnings) {
    options->set_malloc_limit(0);
    options->set_purge_interval(0);
  }
  Process::AddDefaults(options.get());
  return options;
}

// Unit tests.

TEST_F(ProcessTest, AddDefaults) {
  Options options;
  Process::AddDefaults(&options);
  EXPECT_EQ(options.detect_leaks(), kDefaultDetectLeaks);
  EXPECT_EQ(options.malloc_limit(), kDefaultMallocLimit);
  EXPECT_EQ(options.oom_limit(), kDefaultOomLimit);
  EXPECT_EQ(options.purge_interval(), kDefaultPurgeInterval);
  EXPECT_EQ(options.malloc_exitcode(), kDefaultMallocExitcode);
  EXPECT_EQ(options.death_exitcode(), kDefaultDeathExitcode);
  EXPECT_EQ(options.leak_exitcode(), kDefaultLeakExitcode);
  EXPECT_EQ(options.oom_exitcode(), kDefaultOomExitcode);
}

TEST_F(ProcessTest, ConnectProcess) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  auto process = MakeProcess(proxy);

  auto self = zx::process::self();
  zx_info_handle_basic_t info;
  EXPECT_EQ(self->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  EXPECT_EQ(proxy->process_koid(), info.koid);
}

TEST_F(ProcessTest, ConnectWithDefaultOptions) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions(/* disable_warnings */ false));
  auto process = MakeProcess(proxy);

  const auto& options = process->options();
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
  auto modules = CreateModulesAndInitFirst();
  auto options = DefaultOptions();
  options->set_malloc_limit(0);
  options->set_purge_interval(0);
  auto proxy = MakeProxy(options);
  auto process = MakeProcess(proxy);

  EXPECT_EQ(process->malloc_limit(), std::numeric_limits<size_t>::max());
  EXPECT_EQ(process->next_purge(), zx::time::infinite());
}

TEST_F(ProcessTest, ConnectAndAddModules) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  // Add some, but not all, of the modules (modules[0] was already added).
  for (size_t i = 1; i < kNumModules - 1; ++i) {
    __sanitizer_cov_8bit_counters_init(modules[i].counters(), modules[i].counters_end());
    __sanitizer_cov_pcs_init(modules[i].pcs(), modules[i].pcs_end());
  }
  auto process = MakeProcess(proxy);

  // The mock ProcessProxy should eventually receive exactly the IDs added via
  // |__sanitizer_cov_*_init|.
  while (proxy->num_modules() < kNumModules - 1) {
    FX_LOGS(WARNING) << proxy->num_modules();
    FUZZING_EXPECT_OK(proxy->AwaitSent(kSync));
    RunUntilIdle();
  }
  for (size_t i = 0; i < kNumModules - 1; ++i) {
    EXPECT_TRUE(proxy->has_module(&modules[i]));
  }
  auto* module = &modules[kNumModules - 1];
  EXPECT_FALSE(proxy->has_module(module));

  // Late-added modules (e.g. via `dlopen`) are added automatically.
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  FUZZING_EXPECT_OK(proxy->AwaitSent(kSync));
  RunUntilIdle();
  EXPECT_TRUE(proxy->has_module(module));
}

TEST_F(ProcessTest, ConnectBadModules) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  auto process = MakeProcess(proxy);

  // Empty-length module.
  size_t num_modules = proxy->num_modules();
  auto* module = &modules[1];
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters());
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs());
  EXPECT_EQ(proxy->num_modules(), num_modules);

  // Module ends before it begins.
  __sanitizer_cov_8bit_counters_init(module->counters() + 1, module->counters());
  __sanitizer_cov_pcs_init(module->pcs() + 2, module->pcs());
  EXPECT_EQ(proxy->num_modules(), num_modules);

  // Mismatched length.
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end() - 1);
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  EXPECT_EQ(proxy->num_modules(), num_modules);
}

TEST_F(ProcessTest, ConnectLateModules) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  auto process = MakeProcess(proxy);

  // Modules with missing fields are deferred.
  size_t num_modules = proxy->num_modules();
  auto* module = &modules[1];
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  RunOnce();
  EXPECT_EQ(proxy->num_modules(), num_modules);

  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  FUZZING_EXPECT_OK(proxy->AwaitSent(kSync));
  RunUntilIdle();
  EXPECT_EQ(proxy->num_modules(), num_modules + 1);

  module = &modules[2];
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  RunOnce();
  EXPECT_EQ(proxy->num_modules(), num_modules + 1);

  module = &modules[3];
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  RunOnce();
  EXPECT_EQ(proxy->num_modules(), num_modules + 1);

  module = &modules[2];
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  FUZZING_EXPECT_OK(proxy->AwaitSent(kSync));
  RunUntilIdle();
  EXPECT_EQ(proxy->num_modules(), num_modules + 2);

  module = &modules[3];
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  FUZZING_EXPECT_OK(proxy->AwaitSent(kSync));
  RunUntilIdle();
  EXPECT_EQ(proxy->num_modules(), num_modules + 3);
}

TEST_F(ProcessTest, ImplicitStart) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  auto process = MakeProcess(proxy);

  // Processes should be implicitly |Start|ed on |Connect|ing.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinish));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();

  EXPECT_EQ(MeasurePool(), 0U);
}

TEST_F(ProcessTest, UpdateOnFinish) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  auto process = MakeProcess(proxy);

  // No new coverage.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinish));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();

  EXPECT_EQ(MeasurePool(), 0U);

  // Add some counters.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kStart));
  EXPECT_EQ(proxy->SignalPeer(kStart), ZX_OK);
  RunUntilIdle();

  auto& module = modules[0];
  module[0] = 4;
  module[module.num_pcs() / 2] = 16;
  module[module.num_pcs() - 1] = 128;

  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinish));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();

  EXPECT_EQ(MeasurePool(), 3U);
}

TEST_F(ProcessTest, UpdateOnExit) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  auto process = MakeProcess(proxy);

  // Add some counters.
  auto& module = modules[0];
  module[module.num_pcs() - 4] = 64;
  module[module.num_pcs() - 3] = 32;
  module[module.num_pcs() - 2] = 16;
  module[module.num_pcs() - 1] = 8;

  //  Fake a call to |exit|.
  process->OnExit();
  EXPECT_EQ(MeasurePool(), 4U);
}

TEST_F(ProcessTest, FinishWithoutLeaks) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  auto process = MakeProcess(proxy);

  // No mallocs/frees, and no leak detection.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinish));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();

  // Balanced mallocs/frees, and no leak detection.
  // The pointers and sizes don't actually matter; just the number of calls.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kStart));
  EXPECT_EQ(proxy->SignalPeer(kStart), ZX_OK);
  RunUntilIdle();

  process->OnMalloc(nullptr, 0);
  process->OnMalloc(nullptr, 0);
  process->OnFree(nullptr);
  process->OnMalloc(nullptr, 0);
  process->OnFree(nullptr);
  process->OnFree(nullptr);

  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinish));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();

  // No mallocs/frees, with leak detection.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kStart));
  EXPECT_EQ(proxy->SignalPeer(kStartLeakCheck), ZX_OK);
  RunUntilIdle();

  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinish));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();

  // Balanced mallocs/frees, with leak detection.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kStart));
  EXPECT_EQ(proxy->SignalPeer(kStartLeakCheck), ZX_OK);
  RunUntilIdle();

  process->OnMalloc(nullptr, 0);
  process->OnMalloc(nullptr, 0);
  process->OnFree(nullptr);
  process->OnMalloc(nullptr, 0);
  process->OnFree(nullptr);
  process->OnFree(nullptr);

  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinish));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();
}

TEST_F(ProcessTest, FinishWithLeaks) {
  auto modules = CreateModulesAndInitFirst();
  auto proxy = MakeProxy(DefaultOptions());
  auto process = MakeProcess(proxy);

  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinish));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();

  // Unbalanced mallocs/frees, and no leak detection.
  // The pointers and sizes don't actually matter; just the number of calls.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kStart));
  EXPECT_EQ(proxy->SignalPeer(kStart), ZX_OK);
  RunUntilIdle();

  process->OnMalloc(nullptr, 0);
  process->OnMalloc(nullptr, 0);
  process->OnFree(nullptr);

  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinishWithLeaks));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();

  // Unbalanced mallocs/frees, with leak detection.
  // Since these aren't real leaks, this will not abort.
  FUZZING_EXPECT_OK(proxy->AwaitReceived(kStart));
  EXPECT_EQ(proxy->SignalPeer(kStartLeakCheck), ZX_OK);
  RunUntilIdle();

  process->OnMalloc(nullptr, 0);
  process->OnMalloc(nullptr, 0);
  process->OnFree(nullptr);

  FUZZING_EXPECT_OK(proxy->AwaitReceived(kFinishWithLeaks));
  EXPECT_EQ(proxy->SignalPeer(kFinish), ZX_OK);
  RunUntilIdle();
}

}  // namespace fuzzing
