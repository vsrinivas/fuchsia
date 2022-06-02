// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/target/process.h"

#include <lib/backtrace-request/backtrace-request.h>
#include <lib/syslog/cpp/macros.h>
#include <stddef.h>
#include <stdlib.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/common/module.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/target/weak-symbols.h"

namespace fuzzing {
namespace {

using ::fuchsia::fuzzer::InstrumentedProcess;
using ::fuchsia::fuzzer::LlvmModule;

// Maximum number of LLVM modules per process. This limit matches libFuzzer.
constexpr size_t kMaxModules = 4096;

// Memory profile parameters; see compiler-rt/lib/asan/asan_memory_profile.cpp.
constexpr size_t kTopPercentChunks = 95;
constexpr size_t kMaxUniqueContexts = 8;

// Static context; used to store module info until the process singleton is created and to find the
// singleton from the static hook functions. This structure is NOT thread-safe, and should only be
// accessed from the main thread. More precisely, do not load multiple shared libraries concurrently
// from different threads.
struct {
  CountersInfo counters[kMaxModules];
  size_t num_counters = 0;
  PCsInfo pcs[kMaxModules];
  size_t num_pcs = 0;
  Process* process = nullptr;
} gContext;

// Hook functions that simply forward to the singleton.
void MallocHook(const volatile void* ptr, size_t size) { gContext.process->OnMalloc(ptr, size); }

void FreeHook(const volatile void* ptr) { gContext.process->OnFree(ptr); }

void DeathHook() { gContext.process->OnDeath(); }

void ExitHook() { gContext.process->OnExit(); }

}  // namespace
}  // namespace fuzzing

extern "C" {

// NOLINTNEXTLINE(readability-non-const-parameter)
void __sanitizer_cov_8bit_counters_init(uint8_t* start, uint8_t* stop) {
  if (start >= stop) {
    return;
  }
  fuzzing::CountersInfo counters;
  counters.data = start;
  counters.len = stop - start;
  // Safe: |gProcess.process| is only modified while the process is single-threaded.
  using fuzzing::gContext;
  if (gContext.process) {
    gContext.process->AddCounters(std::move(counters));
    return;
  }
  // Safe: no threads are created before |gProcess| is constructed.
  if (gContext.num_counters < fuzzing::kMaxModules) {
    gContext.counters[gContext.num_counters++] = std::move(counters);
  }
}

void __sanitizer_cov_pcs_init(const uintptr_t* start, const uintptr_t* stop) {
  if (start >= stop) {
    return;
  }
  fuzzing::PCsInfo pcs;
  pcs.data = start;
  pcs.len = stop - start;
  // Safe: |gProcess.process| is only modified while the process is single-threaded.
  using fuzzing::gContext;
  if (gContext.process) {
    gContext.process->AddPCs(std::move(pcs));
    return;
  }
  // Safe: no threads are created before |gProcess| is constructed.
  if (gContext.num_pcs < fuzzing::kMaxModules) {
    gContext.pcs[gContext.num_pcs++] = std::move(pcs);
  }
}

// TODO(fxbug.dev/85308): Add value-profile support.
void __sanitizer_cov_trace_pc_indir(uintptr_t Callee) {}
void __sanitizer_cov_trace_const_cmp1(uint8_t Arg1, uint8_t Arg2) {}
void __sanitizer_cov_trace_const_cmp2(uint16_t Arg1, uint16_t Arg2) {}
void __sanitizer_cov_trace_const_cmp4(uint32_t Arg1, uint32_t Arg2) {}
void __sanitizer_cov_trace_const_cmp8(uint64_t Arg1, uint64_t Arg2) {}
void __sanitizer_cov_trace_cmp1(uint8_t Arg1, uint8_t Arg2) {}
void __sanitizer_cov_trace_cmp2(uint16_t Arg1, uint16_t Arg2) {}
void __sanitizer_cov_trace_cmp4(uint32_t Arg1, uint32_t Arg2) {}
void __sanitizer_cov_trace_cmp8(uint64_t Arg1, uint64_t Arg2) {}
void __sanitizer_cov_trace_switch(uint64_t Val, uint64_t* Cases) {}
void __sanitizer_cov_trace_div4(uint32_t Val) {}
void __sanitizer_cov_trace_div8(uint64_t Val) {}
void __sanitizer_cov_trace_gep(uintptr_t Idx) {}

}  // extern "C"

namespace fuzzing {

Process::Process(ExecutorPtr executor)
    : executor_(executor), eventpair_(executor), next_purge_(zx::time::infinite()) {
  FX_CHECK(!gContext.process);
  gContext.process = this;
  for (size_t i = 0; i < gContext.num_counters; ++i) {
    if (auto status = counters_.Send(std::move(gContext.counters[i])); status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to send counter data: " << zx_status_get_string(status);
    }
  }
  for (size_t i = 0; i < gContext.num_pcs; ++i) {
    if (auto status = pcs_.Send(std::move(gContext.pcs[i])); status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to send PC data: " << zx_status_get_string(status);
    }
  }
  AddDefaults(&options_);
}

Process::~Process() { memset(&gContext, 0, sizeof(gContext)); }

void Process::AddDefaults(Options* options) {
  if (!options->has_detect_leaks()) {
    options->set_detect_leaks(kDefaultDetectLeaks);
  }
  if (!options->has_malloc_limit()) {
    options->set_malloc_limit(kDefaultMallocLimit);
  }
  if (!options->has_oom_limit()) {
    options->set_oom_limit(kDefaultOomLimit);
  }
  if (!options->has_purge_interval()) {
    options->set_purge_interval(kDefaultPurgeInterval);
  }
  if (!options->has_malloc_exitcode()) {
    options->set_malloc_exitcode(kDefaultMallocExitcode);
  }
  if (!options->has_death_exitcode()) {
    options->set_death_exitcode(kDefaultDeathExitcode);
  }
  if (!options->has_leak_exitcode()) {
    options->set_leak_exitcode(kDefaultLeakExitcode);
  }
  if (!options->has_oom_exitcode()) {
    options->set_oom_exitcode(kDefaultOomExitcode);
  }
}

void Process::AddCounters(CountersInfo counters) {
  // Ensure the AsyncDeque is only accessed from the dispatcher thread.
  auto task =
      fpromise::make_promise([this, counters = std::move(counters)]() mutable -> ZxResult<> {
        if (auto status = counters_.Send(std::move(counters)); status != ZX_OK) {
          FX_LOGS(WARNING) << "Failed to send counter data: " << zx_status_get_string(status);
          return fpromise::error(status);
        }
        return fpromise::ok();
      });
  executor_->schedule_task(std::move(task));
}

void Process::AddPCs(PCsInfo pcs) {
  // Ensure the AsyncDeque is only accessed from the dispatcher thread.
  auto task = fpromise::make_promise([this, pcs = std::move(pcs)]() mutable -> ZxResult<> {
    if (auto status = pcs_.Send(std::move(pcs)); status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to send PC data: " << zx_status_get_string(status);
      return fpromise::error(status);
    }
    return fpromise::ok();
  });
  executor_->schedule_task(std::move(task));
}

void Process::OnMalloc(const volatile void* ptr, size_t size) {
  ++num_mallocs_;
  if (size > malloc_limit_ && AcquireCrashState()) {
    backtrace_request();
    _Exit(options_.malloc_exitcode());
  }
}

void Process::OnFree(const volatile void* ptr) { ++num_frees_; }

void Process::OnDeath() { _Exit(options_.death_exitcode()); }

void Process::OnExit() {
  // Exits may not be fatal, e.g. if detect_exits=false. May sure the process publishes all its
  // coverage before it ends as the framework will keep fuzzing.
  for (auto& module : modules_) {
    module.Update();
  }
}

void Process::InstallHooks() {
  // This method can only be called once.
  static bool first = true;
  FX_CHECK(first) << "InstallHooks called more than once!";
  first = false;

  // Warn about missing symbols.
  if (!__sanitizer_acquire_crash_state) {
    FX_LOGS(WARNING) << "Missing '__sanitizer_acquire_crash_state'.";
  }
  if (!__sanitizer_set_death_callback) {
    FX_LOGS(WARNING) << "Missing '__sanitizer_set_death_callback'.";
  }

  // Install hooks.
  if (__sanitizer_set_death_callback) {
    __sanitizer_set_death_callback(DeathHook);
  }
  if (__sanitizer_install_malloc_and_free_hooks) {
    __sanitizer_install_malloc_and_free_hooks(MallocHook, FreeHook);
  }
  std::atexit([]() { ExitHook(); });
}

Promise<> Process::Connect(fidl::InterfaceRequestHandler<Instrumentation> handler) {
  handler(instrumentation_.NewRequest(executor_->dispatcher()));

  // Create the eventpair.
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(eventpair_.Create());

  // Duplicate a handle to ourselves.
  zx::process process;
  auto self = zx::process::self();
  self->duplicate(ZX_RIGHT_SAME_RIGHTS, &process);
  instrumented.set_process(std::move(process));

  // Connect to the engine and wait for it to acknowledge it has added a proxy for this object.
  Bridge<Options> bridge;
  instrumentation_->Initialize(std::move(instrumented), bridge.completer.bind());
  return bridge.consumer.promise_or(fpromise::error())
      .and_then([this](Options& options) -> Result<> {
        Configure(std::move(options));
        return fpromise::ok();
      })
      .and_then(AwaitSync())
      .wrap_with(scope_);
}

void Process::Configure(Options options) {
  AddDefaults(&options);
  options_ = std::move(options);

  // Configure allocator purging.
  // TODO(fxbug.dev/85284): Add integration tests that produce these and following logs.
  auto purge_interval = options_.purge_interval();
  if (purge_interval && !__sanitizer_purge_allocator) {
    FX_LOGS(WARNING) << "Missing '__sanitizer_purge_allocator'.";
    FX_LOGS(WARNING) << "Allocator purging disabled.";
    purge_interval = 0;
  }
  next_purge_ =
      purge_interval ? zx::deadline_after(zx::duration(purge_interval)) : zx::time::infinite();

  // Check if leak detection is possible.
  if (options_.detect_leaks()) {
    can_detect_leaks_ = false;
    if (!__lsan_enable) {
      FX_LOGS(WARNING) << "Missing '__lsan_enable'.";
    } else if (!__lsan_disable) {
      FX_LOGS(WARNING) << "Missing '__lsan_disable'.";
    } else if (!__lsan_do_recoverable_leak_check) {
      FX_LOGS(WARNING) << "Missing '__lsan_do_recoverable_leak_check'.";
    } else if (!__sanitizer_install_malloc_and_free_hooks) {
      FX_LOGS(WARNING) << "Missing '__sanitizer_install_malloc_and_free_hooks'.";
    } else {
      can_detect_leaks_ = true;
    }
    if (!can_detect_leaks_) {
      FX_LOGS(WARNING) << "Leak detection disabled.";
    }
  }

  // Check if bad malloc detection is possible.
  auto malloc_limit = options_.malloc_limit();
  if (malloc_limit && !__sanitizer_install_malloc_and_free_hooks) {
    FX_LOGS(WARNING) << "Missing '__sanitizer_install_malloc_and_free_hooks'.";
    FX_LOGS(WARNING) << "Large allocation detection disabled.";
  }
  malloc_limit_ = malloc_limit ? malloc_limit : std::numeric_limits<size_t>::max();
}

Promise<> Process::AwaitSync() {
  return eventpair_.WaitFor(kSync).then([](const ZxResult<zx_signals_t>& result) -> Result<> {
    if (result.is_error()) {
      return fpromise::error();
    }
    return fpromise::ok();
  });
}

Promise<> Process::AddModules() {
  if (counters_.is_empty() || pcs_.is_empty()) {
    FX_LOGS(FATAL) << "No modules found; is the code instrumented for fuzzing?";
  }
  return fpromise::make_promise(
      [this, add_module = Future<>()](Context& context) mutable -> Result<> {
        while (true) {
          if (!add_module) {
            add_module = AddModule();
          }
          if (!add_module(context)) {
            return fpromise::pending();
          }
          if (add_module.is_error()) {
            return fpromise::error();
          }
          add_module = nullptr;
        }
      });
}

Promise<> Process::AddModule() {
  return fpromise::make_promise(
             [this, recv_counters = Future<CountersInfo>(),
              recv_pcs = Future<PCsInfo>()](Context& context) mutable -> Result<Module> {
               while (true) {
                 // Get the next |CountersInfo|.
                 if (!recv_counters) {
                   recv_counters = counters_.Receive();
                 }
                 if (!recv_counters(context)) {
                   return fpromise::pending();
                 }
                 if (recv_counters.is_error()) {
                   return fpromise::error();
                 }
                 // Get the next |PCsInfo|.
                 if (!recv_pcs) {
                   recv_pcs = pcs_.Receive();
                 }
                 if (!recv_pcs(context)) {
                   return fpromise::pending();
                 }
                 if (recv_pcs.is_error()) {
                   return fpromise::error();
                 }
                 // Combine into a |Module|.
                 auto counters = recv_counters.take_value();
                 auto pcs = recv_pcs.take_value();
                 if (counters.len != pcs.len * sizeof(uintptr_t) / sizeof(ModulePC)) {
                   FX_LOGS(WARNING) << "Length mismatch: counters=" << counters.len
                                    << ", pcs=" << pcs.len << "; module will be skipped.";
                   continue;
                 }
                 Module module(counters.data, pcs.data, counters.len);
                 module.Clear();
                 return fpromise::ok(std::move(module));
               }
             })
      .and_then(
          [this, add_module = Future<>()](Context& context, Module& module) mutable -> Result<> {
            // Send the module to the coverage component.
            if (!add_module) {
              Bridge<> bridge;
              instrumentation_->AddLlvmModule(module.GetLlvmModule(), bridge.completer.bind());
              modules_.push_back(std::move(module));
              add_module = bridge.consumer.promise_or(fpromise::error()).and_then(AwaitSync());
            }
            if (!add_module(context)) {
              return fpromise::pending();
            }
            return fpromise::ok();
          });
}

Promise<> Process::Run() {
  // Processes typically connect during a fuzzing run, but may connect between runs as well. As a
  // result, the first wait is for any run-related signal.
  auto expected = kStart | kStartLeakCheck | kFinish;
  return fpromise::make_promise(
      [this, expected, wait = ZxFuture<zx_signals_t>()](Context& context) mutable -> Result<> {
        while (true) {
          if (!wait) {
            wait = eventpair_.WaitFor(expected);
          }
          if (!wait(context)) {
            return fpromise::pending();
          }
          if (wait.is_error()) {
            return fpromise::ok();
          }
          auto observed = wait.take_value();
          if (eventpair_.SignalSelf(observed, 0) != ZX_OK) {
            return fpromise::error();
          }
          zx_signals_t reply = 0;
          switch (observed) {
            case kStartLeakCheck:
              ConfigureLeakDetection();
              [[fallthrough]];
            case kStart:
              // Reset coverage data and leak detection.
              for (auto& module : modules_) {
                module.Clear();
              }
              num_mallocs_ = 0;
              num_frees_ = 0;
              reply = kStart;
              expected = kFinish;
              break;
            case kFinish:
              // Forward coverage data to engine, and respond with leak status.
              for (auto& module : modules_) {
                module.Update();
              }
              reply = DetectLeak() ? kFinishWithLeaks : kFinish;
              expected = kStart | kStartLeakCheck;
              break;
            default:
              FX_NOTREACHED();
              break;
          }
          if (eventpair_.SignalPeer(0, reply) != ZX_OK) {
            return fpromise::error();
          }
        }
      });
}

void Process::ConfigureLeakDetection() {
  if (can_detect_leaks_ && !detecting_leaks_) {
    detecting_leaks_ = true;
    __lsan_disable();
  }
}

bool Process::DetectLeak() {
  // As described in the header, full leak detection is expensive. This framework imitates libFuzzer
  // and performs a two-pass process:
  //   1a. Upon starting a fuzzing iteration,i.e. |OnSignal(kStart)|, it tracks |num_mallocs| and
  //       |num_frees|.
  //   1b. Upon finishing an iteration, i.e. |OnSignal(kFinish)|, it checks if |num_mallocs| equals
  //       |num_frees| and returns |kFinish| or |kFinishWithLeaks|, as appropriate.
  //   2a. Returning |kFinishWithLeaks| will cause the framework to repeat the input with leak
  //       detection, i.e. |OnSignal(kStartLeakCheck)|. It will disable LSan for this run to avoid
  //       eventually reporting the same error twice.
  //   2b. Upon finishing the second iteration, i.e. |OnSignal(kFinish)| again, it re-enables LSan.
  //       If |num_mallocs| still does not match |num_frees|, it performs the (expensive) leak
  //       check. If a true leak, it will report it using info from the first iteration and exit.
  bool has_leak = num_mallocs_.exchange(0) != num_frees_.exchange(0);
  if (detecting_leaks_) {
    __lsan_enable();
    detecting_leaks_ = false;
    if (has_leak && __lsan_do_recoverable_leak_check() && AcquireCrashState()) {
      if (__sanitizer_print_memory_profile) {
        __sanitizer_print_memory_profile(kTopPercentChunks, kMaxUniqueContexts);
      }
      _Exit(options_.leak_exitcode());
    }
  }
  // TODO(fxbug.dev/84368): The check for OOM is missing!
  if (next_purge_ < zx::clock::get_monotonic()) {
    __sanitizer_purge_allocator();
    next_purge_ = zx::deadline_after(zx::duration(options_.purge_interval()));
  }
  return has_leak;
}

bool Process::AcquireCrashState() {
  return __sanitizer_acquire_crash_state && __sanitizer_acquire_crash_state();
}

}  // namespace fuzzing
