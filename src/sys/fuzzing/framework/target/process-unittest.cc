// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests only cover the basic configuration and operation of the Process class. Testing
// functionality that leads to the process exiting is tricky. It can require specific build
// configurations (i.e. link against ASan or LSan) and more complex process lifecycle management. As
// a result, this functionality is tested using integration rather than unit tests.

#include "src/sys/fuzzing/framework/target/process.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/sync/completion.h>

#include <memory>
#include <unordered_map>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/dispatcher.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/engine/process-proxy.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/process-proxy.h"

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::Options;

// Test fixtures.

constexpr uint32_t kNumModules = 4;

// Generates some simple |modules|. |Collect| requires at least one module, so this initializes the
// first one. This method should be called *before* instantiating a |TestProcess|.
std::vector<FakeModule> CreateModulesAndInitFirst() {
  std::vector<FakeModule> modules;
  for (uint32_t i = 0; i < kNumModules; ++i) {
    modules.emplace_back(FakeModule(i + 1));
  }
  __sanitizer_cov_8bit_counters_init(modules[0].counters(), modules[0].counters_end());
  __sanitizer_cov_pcs_init(modules[0].pcs(), modules[0].pcs_end());
  return modules;
}

// This class simply exposes some protected methods for testing. Notably, it does NOT automatically
// connect to a |fuchsia.fuzzer.ProcessProxy| or call |Process::InstallHooks|, as
// |InstrumentedProcess| does.
class TestProcess : public Process {
 public:
  TestProcess() = default;
  using Process::Connect;
  using Process::malloc_limit;
  using Process::next_purge;
  using Process::options;
};

// The unit test base class.
class ProcessTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create and destroy a process. This will "consume" any extra modules added if the unit test
    // itself is instrumented.
    { TestProcess process; }

    pool_ = std::make_shared<ModulePool>();
  }

  // Create a fake |ProcessProxy|, bind to it, and call |Connect| on it.
  std::unique_ptr<FakeProcessProxy> MakeAndBindProxy(TestProcess& process,
                                                     const std::shared_ptr<Options>& options,
                                                     bool disable_warnings = true) {
    auto proxy = std::make_unique<FakeProcessProxy>(pool_);
    proxy->Configure(options);
    auto ptr = proxy->Bind(dispatcher_.get(), disable_warnings);
    process.Connect(std::move(ptr));
    return proxy;
  }

  size_t MeasurePool() { return pool_->Measure(); }

 private:
  FakeDispatcher dispatcher_;
  std::shared_ptr<ModulePool> pool_;
};

std::shared_ptr<Options> DefaultOptions() {
  auto options = std::make_shared<Options>();
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
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());

  auto self = zx::process::self();
  zx_info_handle_basic_t info;
  EXPECT_EQ(self->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  EXPECT_EQ(proxy->process_koid(), info.koid);
}

TEST_F(ProcessTest, ConnectWithDefaultOptions) {
  auto modules = CreateModulesAndInitFirst();
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions(), /* disable_warnings */ false);

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
  auto modules = CreateModulesAndInitFirst();
  TestProcess process;

  auto options = DefaultOptions();
  options->set_malloc_limit(0);
  options->set_purge_interval(0);
  auto proxy = MakeAndBindProxy(process, options, /* disable_warnings */ false);
  EXPECT_EQ(process.malloc_limit(), std::numeric_limits<size_t>::max());
  EXPECT_EQ(process.next_purge(), zx::time::infinite());
}

TEST_F(ProcessTest, ConnectAndAddModules) {
  auto modules = CreateModulesAndInitFirst();
  // Add some, but not all, of the modules (modules[0] was already added).
  for (size_t i = 1; i < kNumModules - 1; ++i) {
    __sanitizer_cov_8bit_counters_init(modules[i].counters(), modules[i].counters_end());
    __sanitizer_cov_pcs_init(modules[i].pcs(), modules[i].pcs_end());
  }
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());

  // The mock ProcessProxy should have received exactly the IDs added via |__sanitizer_cov_*_init|.
  EXPECT_EQ(proxy->num_modules(), kNumModules - 1);
  for (size_t i = 0; i < kNumModules - 1; ++i) {
    EXPECT_TRUE(proxy->has_module(&modules[i]));
  }
  auto* module = &modules[kNumModules - 1];
  EXPECT_FALSE(proxy->has_module(module));

  // Late-added modules (e.g. via `dlopen`) are added automatically.
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  EXPECT_TRUE(proxy->has_module(module));
}

TEST_F(ProcessTest, ConnectBadModules) {
  auto modules = CreateModulesAndInitFirst();
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());
  size_t num_modules = proxy->num_modules();

  // Empty-length module.
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
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());
  size_t num_modules = proxy->num_modules();

  // Modules with missing fields are deferred.
  auto* module = &modules[1];
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  EXPECT_EQ(proxy->num_modules(), num_modules);

  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  EXPECT_EQ(proxy->num_modules(), num_modules + 1);

  module = &modules[2];
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  EXPECT_EQ(proxy->num_modules(), num_modules + 1);

  module = &modules[3];
  __sanitizer_cov_pcs_init(module->pcs(), module->pcs_end());
  EXPECT_EQ(proxy->num_modules(), num_modules + 1);

  module = &modules[2];
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  EXPECT_EQ(proxy->num_modules(), num_modules + 2);

  module = &modules[3];
  __sanitizer_cov_8bit_counters_init(module->counters(), module->counters_end());
  EXPECT_EQ(proxy->num_modules(), num_modules + 3);
}

TEST_F(ProcessTest, ImplicitStart) {
  auto modules = CreateModulesAndInitFirst();
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());

  // Processes should be implicitly |Start|ed on |Connect|ing.
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinish);
  EXPECT_EQ(MeasurePool(), 0U);
}

TEST_F(ProcessTest, UpdateOnStop) {
  auto modules = CreateModulesAndInitFirst();
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());

  // No new coverage.
  EXPECT_TRUE(proxy->SignalPeer(kStart));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinish);
  EXPECT_EQ(MeasurePool(), 0U);

  // Add some counters.
  EXPECT_TRUE(proxy->SignalPeer(kStart));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  auto& module = modules[0];
  module[0] = 4;
  module[module.num_pcs() / 2] = 16;
  module[module.num_pcs() - 1] = 128;
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinish);
  EXPECT_EQ(MeasurePool(), 3U);
}

TEST_F(ProcessTest, UpdateOnExit) {
  auto modules = CreateModulesAndInitFirst();
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());

  // Add some counters.
  EXPECT_TRUE(proxy->SignalPeer(kStart));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  auto& module = modules[0];
  module[module.num_pcs() - 4] = 64;
  module[module.num_pcs() - 3] = 32;
  module[module.num_pcs() - 2] = 16;
  module[module.num_pcs() - 1] = 8;

  //  Fake a call to |exit|.
  process.OnExit();
  EXPECT_EQ(MeasurePool(), 4U);
}

TEST_F(ProcessTest, StopWithoutLeaks) {
  auto modules = CreateModulesAndInitFirst();
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());

  // No mallocs/frees, and no leak detection.
  EXPECT_TRUE(proxy->SignalPeer(kStart));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinish);

  // Balanced mallocs/frees, and no leak detection.
  // The pointers and sizes don't actually matter; just the number of calls.
  EXPECT_TRUE(proxy->SignalPeer(kStart));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  process.OnMalloc(nullptr, 0);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  process.OnFree(nullptr);
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinish);

  // No mallocs/frees, with leak detection.
  EXPECT_TRUE(proxy->SignalPeer(kStartLeakCheck));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinish);

  // Balanced mallocs/frees, with leak detection.
  EXPECT_TRUE(proxy->SignalPeer(kStartLeakCheck));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  process.OnMalloc(nullptr, 0);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  process.OnFree(nullptr);
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinish);
}

TEST_F(ProcessTest, StopWithLeaks) {
  auto modules = CreateModulesAndInitFirst();
  TestProcess process;
  auto proxy = MakeAndBindProxy(process, DefaultOptions());

  // Unbalanced mallocs/frees, and no leak detection.
  // The pointers and sizes don't actually matter; just the number of calls.
  EXPECT_TRUE(proxy->SignalPeer(kStart));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  process.OnMalloc(nullptr, 0);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinishWithLeaks);

  // Unbalanced mallocs/frees, with leak detection.
  // Since these aren't real leaks, this will not abort.
  EXPECT_TRUE(proxy->SignalPeer(kStartLeakCheck));
  EXPECT_EQ(proxy->AwaitSignal(), kStart);
  process.OnMalloc(nullptr, 0);
  process.OnMalloc(nullptr, 0);
  process.OnFree(nullptr);
  EXPECT_TRUE(proxy->SignalPeer(kFinish));
  EXPECT_EQ(proxy->AwaitSignal(), kFinishWithLeaks);
}

}  // namespace
}  // namespace fuzzing
