// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/target/process.h"

#include <lib/backtrace-request/backtrace-request.h>
#include <lib/syslog/cpp/macros.h>
#include <stddef.h>
#include <stdlib.h>
#include <zircon/status.h>

#include <iomanip>
#include <iostream>

#include "src/sys/fuzzing/common/module.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/realmfuzzer/target/weak-symbols.h"

namespace fuzzing {
namespace {

using fuchsia::fuzzer::InstrumentedProcess;

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
    : executor_(executor),
      eventpair_(executor),
      counters_receiver_(&counters_sender_),
      pcs_receiver_(&pcs_sender_),
      next_purge_(zx::time::infinite()) {
  FX_CHECK(!gContext.process);

  // Forward coverage for modules added up to this point.
  FX_CHECK(gContext.num_counters == gContext.num_pcs);
  for (size_t i = 0; i < gContext.num_counters; ++i) {
    AddCounters(std::move(gContext.counters[i]));
    AddPCs(std::move(gContext.pcs[i]));
  }
  AddDefaults(&options_);
  gContext.process = this;
}

Process::~Process() { memset(&gContext, 0, sizeof(gContext)); }

void Process::AddCounters(CountersInfo counters) {
  // Ensure the AsyncDeque is only accessed from the dispatcher thread.
  auto task =
      fpromise::make_promise([this, counters = std::move(counters)]() mutable -> ZxResult<> {
        if (auto status = counters_sender_.Send(std::move(counters)); status != ZX_OK) {
          FX_LOGS(WARNING) << "Failed to send counters to engine: " << zx_status_get_string(status);
          return fpromise::error(status);
        }
        return fpromise::ok();
      })
          .wrap_with(scope_)
          .wrap_with(sequencer_);
  executor_->schedule_task(std::move(task));
}

void Process::AddPCs(PCsInfo pcs) {
  // Ensure the AsyncDeque is only accessed from the dispatcher thread.
  auto task = fpromise::make_promise([this, pcs = std::move(pcs)]() mutable -> ZxResult<> {
                if (auto status = pcs_sender_.Send(std::move(pcs)); status != ZX_OK) {
                  FX_LOGS(WARNING)
                      << "Failed to send PCs to engine: " << zx_status_get_string(status);
                  return fpromise::error(status);
                }
                return fpromise::ok();
              })
                  .wrap_with(scope_)
                  .wrap_with(sequencer_);
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
  // Exits may not be fatal, e.g. if detect_exits=false. Make sure the process publishes all its
  // coverage before it ends as the engine will keep fuzzing.
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

ZxPromise<> Process::Connect(fidl::InterfaceHandle<CoverageDataCollector> collector,
                             zx::eventpair eventpair) {
  Bridge<Options> bridge;
  return fpromise::make_promise([this, collector = std::move(collector)]() mutable -> ZxResult<> {
           // Connect the `fuchsia.fuzzer.CoverageDataCollector`.
           if (auto status = collector_.Bind(collector.TakeChannel()); status != ZX_OK) {
             FX_LOGS(WARNING) << "Failed to bind `fuchsia.fuzzer.CoverageDataCollector`: "
                              << zx_status_get_string(status);
             return fpromise::error(status);
           }
           return fpromise::ok();
         })
      .and_then([]() -> ZxResult<zx::process> {
        // Duplicate this process.
        auto self = zx::process::self();
        zx::process process;
        if (auto status = self->duplicate(ZX_RIGHT_SAME_RIGHTS, &process); status != ZX_OK) {
          FX_LOGS(WARNING) << "Failed to duplicate process handle: "
                           << zx_status_get_string(status);
          return fpromise::error(status);
        }
        return fpromise::ok(std::move(process));
      })
      .and_then([this](zx::process& process) -> ZxResult<zx::process> {
        // Next, determine this process's target id, which is just its koid. The process will
        // annotate all modules it shares with this id to allow the engine to clean up the module
        // pool if this process exits.
        zx_info_handle_basic_t info;
        if (auto status =
                process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
            status != ZX_OK) {
          FX_LOGS(WARNING) << "Failed to set target id: " << zx_status_get_string(status);
          return fpromise::error(status);
        }
        target_id_ = info.koid;
        return fpromise::ok(std::move(process));
      })
      .and_then([this, completer = std::move(bridge.completer)](
                    zx::process& process) mutable -> ZxResult<> {
        // Now create an |InstrumentedProcess| for this process and send it to the collector.
        InstrumentedProcess instrumented{
            .eventpair = eventpair_.Create(),
            .process = std::move(process),
        };
        collector_->Initialize(std::move(instrumented), completer.bind());
        return fpromise::ok();
      })
      .and_then([this, connect = Future<Options>(bridge.consumer.promise_or(fpromise::error()))](
                    Context& context) mutable -> ZxResult<> {
        // Wait for the collector to respond with options, and use them to configure this process.
        if (!connect(context)) {
          return fpromise::pending();
        }
        if (connect.is_error()) {
          return fpromise::error(ZX_ERR_CANCELED);
        }
        Configure(connect.take_value());
        return fpromise::ok();
      })
      .and_then([this, eventpair = std::move(eventpair), add = ZxFuture<>(),
                 run = ZxFuture<>()](Context& context) mutable -> ZxResult<> {
        // Now execute both the |AddModules| and |Run| futures. These only complete on error, and
        // need to be executed concurrently.
        if (!add) {
          add = AddModules(std::move(eventpair));
        }
        if (!run) {
          run = Run();
        }
        if (add(context)) {
          return add.take_result();
        }
        if (run(context)) {
          return run.take_result();
        }
        return fpromise::pending();
      })
      .wrap_with(scope_);
}

void Process::Configure(Options options) {
  SetOptions(&options_, options);

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

ZxPromise<> Process::AddModules(zx::eventpair eventpair) {
  return fpromise::make_promise([this, eventpair = std::move(eventpair), num_modules = 0ULL,
                                 add_module =
                                     ZxFuture<>()](Context& context) mutable -> ZxResult<> {
    while (true) {
      // Notify the engine when initial modules have all been sent and acknowledged.
      if (!add_module) {
        if (num_modules == gContext.num_pcs) {
          if (auto status = eventpair.signal_peer(0, kSync); status != ZX_OK) {
            FX_LOGS(WARNING) << "Failed to acknowledge module: " << zx_status_get_string(status);
          }
        }
        add_module = AddModule();
      }
      if (!add_module(context)) {
        return fpromise::pending();
      }
      auto result = add_module.take_result();
      if (result.is_error()) {
        FX_LOGS(ERROR) << "Failed to add module: " << zx_status_get_string(result.error());
      }
      ++num_modules;
    }
  });
}

ZxPromise<> Process::AddModule() {
  Bridge<> bridge;
  return fpromise::make_promise([recv = Future<CountersInfo>(counters_receiver_.Receive())](
                                    Context& context) mutable -> ZxResult<CountersInfo> {
           // Get the next |CountersInfo|.
           if (!recv(context)) {
             return fpromise::pending();
           }
           if (recv.is_error()) {
             FX_LOGS(WARNING) << "Missing expected inline 8-bit counters.";
             return fpromise::error(ZX_ERR_BAD_STATE);
           }
           return fpromise::ok(recv.take_value());
         })
      .and_then([recv = Future<PCsInfo>(pcs_receiver_.Receive())](
                    Context& context, CountersInfo& counters) mutable -> ZxResult<Module> {
        // Get the next |PCsInfo|.
        if (!recv(context)) {
          return fpromise::pending();
        }
        if (recv.is_error()) {
          FX_LOGS(WARNING) << "Missing expected PC table.";
          return fpromise::error(ZX_ERR_BAD_STATE);
        }
        // Combine into a |Module|.
        auto pcs = recv.take_value();
        if (counters.len != pcs.len * sizeof(uintptr_t) / sizeof(ModulePC)) {
          FX_LOGS(WARNING) << "Length mismatch: counters=" << counters.len << ", pcs=" << pcs.len;
          return fpromise::error(ZX_ERR_BAD_STATE);
        }
        Module module;
        if (auto status = module.Import(counters.data, pcs.data, counters.len); status != ZX_OK) {
          FX_LOGS(WARNING) << "Failed to import module data: " << zx_status_get_string(status);
          return fpromise::error(status);
        }
        module.Clear();
        return fpromise::ok(std::move(module));
      })
      .and_then(
          [this, completer = std::move(bridge.completer)](Module& module) mutable -> ZxResult<> {
            zx::vmo inline_8bit_counters;
            if (auto status = module.Share(target_id_, &inline_8bit_counters); status != ZX_OK) {
              FX_LOGS(WARNING) << "Failed to share inline 8-bit counters: "
                               << zx_status_get_string(status);
              return fpromise::error(status);
            }
            modules_.emplace_back(std::move(module));
            collector_->AddLlvmModule(std::move(inline_8bit_counters), completer.bind());
            return fpromise::ok();
          })
      .and_then([this, wait = Future<>(bridge.consumer.promise_or(fpromise::error()))](
                    Context& context) mutable -> ZxResult<> {
        if (!wait(context)) {
          return fpromise::pending();
        }
        if (wait.is_error()) {
          return fpromise::error(ZX_ERR_CANCELED);
        }
        if (awaiting_ && modules_.size() >= gContext.num_pcs) {
          awaiting_.resume_task();
        }
        return fpromise::ok();
      });
}

ZxPromise<> Process::Run() {
  // Processes typically connect during a fuzzing run, but may connect between runs as well. As a
  // result, the first wait is for any run-related signal.
  auto expected = kStart | kStartLeakCheck | kFinish;
  return fpromise::make_promise([this, expected, wait = ZxFuture<zx_signals_t>()](
                                    Context& context) mutable -> ZxResult<> {
           while (true) {
             if (!wait) {
               wait = eventpair_.WaitFor(expected);
             }
             if (!wait(context)) {
               return fpromise::pending();
             }
             if (wait.is_error()) {
               return fpromise::error(wait.error());
             }
             auto observed = wait.take_value();
             if (auto status = eventpair_.SignalSelf(observed, 0); status != ZX_OK) {
               return fpromise::error(status);
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
             if (auto status = eventpair_.SignalPeer(0, reply); status != ZX_OK) {
               return fpromise::error(status);
             }
           }
         })
      .or_else([](const zx_status_t& status) -> ZxResult<> {
        if (status != ZX_ERR_PEER_CLOSED) {
          FX_LOGS(WARNING) << "Failed to exchange signals with engine: "
                           << zx_status_get_string(status);
          return fpromise::error(status);
        }
        return fpromise::ok();
      });
}

void Process::ConfigureLeakDetection() {
  if (can_detect_leaks_ && !detecting_leaks_) {
    detecting_leaks_ = true;
    __lsan_disable();
  }
}

bool Process::DetectLeak() {
  // As described in the header, full leak detection is expensive. Realmfuzzer imitates libfuzzer
  // and performs a two-pass process:
  //   1a. Upon starting a fuzzing iteration,i.e. |OnSignal(kStart)|, it tracks |num_mallocs| and
  //       |num_frees|.
  //   1b. Upon finishing an iteration, i.e. |OnSignal(kFinish)|, it checks if |num_mallocs| equals
  //       |num_frees| and returns |kFinish| or |kFinishWithLeaks|, as appropriate.
  //   2a. Returning |kFinishWithLeaks| will cause the engine to repeat the input with leak
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
