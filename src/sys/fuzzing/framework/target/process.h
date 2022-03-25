// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TARGET_PROCESS_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TARGET_PROCESS_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/zx/time.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <limits>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/sancov.h"
#include "src/sys/fuzzing/framework/target/module.h"
#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Instrumentation;
using ::fuchsia::fuzzer::InstrumentationPtr;

// Reserved target IDs. |kTimeoutTargetId| is a pseudo-ID used to signify a timeout across all
// target processes rather than an error in a specific one.
constexpr uint64_t kInvalidTargetId = std::numeric_limits<uint64_t>::min();
constexpr uint64_t kTimeoutTargetId = std::numeric_limits<uint64_t>::max();

// This struct is simply a container for holding and moving module details like inline 8-bit
// counters and PC tables that are recorded by the |__sanitizer_cov_*_init| functions. This
// typically occurs before |main| and before some or all dynamic objects are loaded, so it must
// kept simple and POD.
template <typename T>
struct ModuleInfo {
  T* data = nullptr;
  size_t len = 0;

  ModuleInfo() = default;
  ModuleInfo(ModuleInfo&& other) { *this = std::move(other); }
  ~ModuleInfo() = default;

  ModuleInfo& operator=(ModuleInfo&& other) {
    data = other.data;
    len = other.len;
    other.data = nullptr;
    other.len = 0;
    return *this;
  }
};
using CountersInfo = ModuleInfo<uint8_t>;
using PCsInfo = ModuleInfo<const uintptr_t>;

// This class represents a target process being fuzzed. It is a singleton in each process, and its
// methods are typically invoked through various callbacks.
class Process {
 public:
  virtual ~Process();

  void set_handler(fidl::InterfaceRequestHandler<Instrumentation> handler) {
    handler_ = std::move(handler);
  }

  // Adds defaults to unspecified options.
  static void AddDefaults(Options* options);

  // Adds the counters and PCs associated with modules for this process. Invoked via the
  // |__sanitizer_cov_*_init| functions.
  void AddCounters(CountersInfo counters);
  void AddPCs(PCsInfo pcs);

  // |malloc| and |free| hooks, called from a static context via the
  // |__sanitizer_install_malloc_and_free_hooks| function.
  void OnMalloc(const volatile void* ptr, size_t size);
  void OnFree(const volatile void* ptr);

  // Exit hooks, called from a static context via the |__sanitizer_set_death_callback| function an
  // |std::atexit|.
  void OnDeath();
  void OnExit();

 protected:
  explicit Process(ExecutorPtr executor);

  // Accessors for unit testing.
  const Options& options() const { return options_; }
  size_t malloc_limit() const { return malloc_limit_; }
  zx::time next_purge() const { return next_purge_; }

  // Installs the hook functions above in the process' overall global, static context. The methods
  // used, e.g. |__sanitizer_set_death_callback|, do not have corresponding methods to unset the
  // hooks, so there is no corresponding "UninstallHooks". As a result, this method can only be
  // called once per process; subsequent calls will panic.
  static void InstallHooks();

  // Returns a promise to connects to the |Coverage| component and begin fuzzing.
  Promise<> Connect();

  // Returns a promise to send complete pending modules to the coverage component. This promise does
  // not return unless there is an error.
  Promise<> AddModules();

  // Promises to clear and update modules in response to signals from the engine to start and finish
  // fuzzing runs, respectively. This promise does not return unless there is an error.
  Promise<> Run();

 private:
  // Parses the given |options| and prepares this object to manage fuzzing its process.
  void Configure(Options options);

  // Returns a promise that completes when the engine signals it has added a process or module proxy
  // for this object.
  Promise<> AwaitSync();

  // Returns a promise to build a |Module| from |CountersInfo| and |PCsInfo|, and send it to the
  // coverage component.
  Promise<> AddModule();

  // Configures the target for leak detection, if available. See |DetectLeak| below for details.
  void ConfigureLeakDetection();

  // Performs a leak check.
  //
  // Full leak detection is expensive, so the framework imitates libFuzzer's
  // approach to leak detection and uses a heuristic to try and limit the number of false positives:
  // For each input, it tracks the number of mallocs and frees, and reports whether these numbers
  // match when the run finishes. Upon mismatch, the framework will try the same input again using a
  // |kStartLeakCheck| signal. This is to distinguish between leaks and memory being accumulated in
  // some global state without being leaked. For this second pass, LSan is *disabled* to avoid
  // reporting the same leak twice. If the input still causes more mallocs than frees, the full leak
  // check is performed. If it is a true leak, LSan will report details of the leak from the first
  // run.
  //
  // Returns true if more mallocs were observed than frees. Returns false if the number of mallocs
  // and frees were the same. Exits and does NOT return if a full leak check was performed and a
  // leak was detected.
  //
  // See also libFuzzer's |Fuzzer::TryDetectingAMemoryLeak|.
  bool DetectLeak();

  // First call returns true if a sanitizer is present; all other calls return false.
  static bool AcquireCrashState();

  ExecutorPtr executor_;
  fidl::InterfaceRequestHandler<Instrumentation> handler_;
  InstrumentationPtr instrumentation_;
  AsyncEventPair eventpair_;

  // Options provided by the engine.
  Options options_;
  bool can_detect_leaks_ = false;  // Is LSan available and is options.deteck_leaks == true?
  size_t malloc_limit_ = 0;

  // Queues for adding modules.
  AsyncDeque<CountersInfo> counters_;
  AsyncDeque<PCsInfo> pcs_;

  // Module feedback.
  std::vector<Module> modules_;

  // Memory tracking.
  bool detecting_leaks_ = false;  // Was the current iteration started with |kStartLeakCheck|?
  std::atomic<uint64_t> num_mallocs_ = 0;
  std::atomic<uint64_t> num_frees_ = 0;
  zx::time next_purge_;

  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(Process);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TARGET_PROCESS_H_
