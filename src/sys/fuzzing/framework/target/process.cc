// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/target/process.h"

#include <lib/backtrace-request/backtrace-request.h>
#include <lib/syslog/cpp/macros.h>
#include <stddef.h>
#include <stdlib.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/target/weak-symbols.h"

namespace fuzzing {
namespace {

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
  struct {
    uint8_t* counters;
    size_t counters_len;
    const uintptr_t* pcs;
    size_t pcs_len;
  } modules[kMaxModules];
  size_t num_counters = 0;
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
  if (start < stop && fuzzing::gContext.num_counters < fuzzing::kMaxModules) {
    auto& info = fuzzing::gContext.modules[fuzzing::gContext.num_counters++];
    info.counters = start;
    info.counters_len = stop - start;
  }
  if (fuzzing::gContext.process) {
    fuzzing::gContext.process->AddModules();
  }
}

void __sanitizer_cov_pcs_init(const uintptr_t* start, const uintptr_t* stop) {
  using fuzzing::gContext;
  if (start < stop && gContext.num_pcs < fuzzing::kMaxModules) {
    auto& info = gContext.modules[gContext.num_pcs++];
    info.pcs = start;
    info.pcs_len = stop - start;
  }
  if (gContext.process) {
    gContext.process->AddModules();
  }
}

}  // extern "C"

namespace fuzzing {

Process::Process() : connected_(false) {
  FX_CHECK(!gContext.process);
  gContext.process = this;
  AddDefaults(&options_);
}

Process::~Process() { memset(&gContext, 0, sizeof(gContext)); }

void Process::InstallHooks() {
  // This method can only be called once.
  static bool first = true;
  FX_CHECK(!first) << "InstallHooks called more than once!";
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

void Process::Connect(ProcessProxySyncPtr&& proxy) {
  std::lock_guard<std::mutex> lock(mutex_);

  // This method can only be called once.
  if (connected_) {
    return;
  }
  proxy_ = std::move(proxy);
  connected_ = true;

  // Create the eventpair.
  auto eventpair =
      coordinator_.Create([this](zx_signals_t observed) { return OnSignal(observed); });

  // Duplicate a handle to ourselves.
  zx::process process;
  auto self = zx::process::self();
  self->duplicate(ZX_RIGHT_SAME_RIGHTS, &process);

  // Connect to the engine.
  proxy_->Connect(std::move(eventpair), std::move(process), &options_);
  AddDefaults(&options_);

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

  // Send the early modules to the engine.
  AddModulesLocked();
  if (modules_.empty()) {
    FX_LOGS(FATAL) << "No modules found; is the code instrumented for fuzzing?";
  }

  // Processes connect when started, as a result of processing a test input during a fuzzing run.
  // This is after the engine would have sent a |kStart| signal, so match that state in |OnSignal|.
  num_mallocs_ = 0;
  num_frees_ = 0;
  detecting_leaks_ = false;
}

void Process::AddModules() {
  std::lock_guard<std::mutex> lock(mutex_);
  AddModulesLocked();
}

void Process::AddModulesLocked() {
  FX_DCHECK(proxy_.is_bound());
  for (size_t i = modules_.size(); i < gContext.num_counters && i < gContext.num_pcs; ++i) {
    auto& info = gContext.modules[i];
    FX_DCHECK(info.counters);
    FX_DCHECK(info.counters_len);
    FX_DCHECK(info.pcs);
    FX_DCHECK(info.pcs_len);
    if (info.counters_len == info.pcs_len * sizeof(uintptr_t) / sizeof(Module::PC)) {
      Module module(info.counters, info.pcs, info.counters_len);
      Feedback feedback;
      feedback.set_id(module.id());
      feedback.set_inline_8bit_counters(module.Share());
      proxy_->AddFeedback(std::move(feedback));
      modules_.push_back(std::move(module));
    } else {
      FX_LOGS(WARNING) << "Length mismatch: counters=" << info.counters_len
                       << ", pcs=" << info.pcs_len << "; module will be skipped.";
    }
    memset(&info, 0, sizeof(info));
  }
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
  Update();
}

bool Process::OnSignal(zx_signals_t observed) {
  if (observed & ZX_EVENTPAIR_PEER_CLOSED) {
    return false;
  }
  switch (observed) {
    case kStart:
    case kStartLeakCheck: {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& module : modules_) {
          module.Clear();
        }
      }
      // See |DetectLeaks| below.
      num_mallocs_ = 0;
      num_frees_ = 0;
      if (can_detect_leaks_ && observed == kStartLeakCheck && !detecting_leaks_) {
        detecting_leaks_ = true;
        __lsan_disable();
      }
      return coordinator_.SignalPeer(kStart);
    }
    case kFinish: {
      Update();
      // See |DetectLeaks| below.
      bool has_leak = DetectLeak();
      if (next_purge_ < zx::clock::get_monotonic()) {
        __sanitizer_purge_allocator();
        next_purge_ = zx::deadline_after(zx::duration(options_.purge_interval()));
      }
      // TODO(fxbug.dev/84368): The check for OOM is missing!
      return coordinator_.SignalPeer(has_leak ? kFinishWithLeaks : kFinish);
    }
    default:
      FX_LOGS(FATAL) << "unexpected signal: 0x" << std::hex << observed << std::dec;
      return false;
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
  if (!can_detect_leaks_ || !detecting_leaks_) {
    return has_leak;
  }
  __lsan_enable();
  detecting_leaks_ = false;
  if (has_leak && __lsan_do_recoverable_leak_check() && AcquireCrashState()) {
    if (__sanitizer_print_memory_profile) {
      __sanitizer_print_memory_profile(kTopPercentChunks, kMaxUniqueContexts);
    }
    _Exit(options_.leak_exitcode());
  }
  return has_leak;
}

void Process::Update() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& module : modules_) {
    module.Update();
  }
}

bool Process::AcquireCrashState() {
  return __sanitizer_acquire_crash_state && __sanitizer_acquire_crash_state();
}

}  // namespace fuzzing
