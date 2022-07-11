// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <lib/fit/defer.h>
#include <lib/zx/process.h>
#include <lib/zx/suspend_token.h>
#include <lib/zx/task.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <pthread.h>
#include <threads.h>
#include <zircon/sanitizer.h>
#include <zircon/threads.h>

#include <array>
#include <condition_variable>
#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

#include "asan_impl.h"
#include "sanitizer-memory-snapshot-test-dso.h"

// Use the GNU global register variable extension to steal an available
// (i.e. usually call-saved and not otherwise special) register to hold a
// probably-unique bit pattern that the test can verify is reported.  It
// shouldn't really matter that the register is only resreved in functions
// compiled in this translation unit, because it's only set and sampled when
// blocked in functions here.  However, "blocking" actually involves calling
// into vDSO code that doesn't keep this register fixed, so pick the
// least-likely call-saved register to be used and hope that the vDSO paths
// used have little enough register pressure never to touch it.
//
// N.B. For GCC to honor it, the register declaration must be at top-level
// and not inside the namespace below.  For register, constexpr, and extern
// (used here) it makes no difference anyway.
#ifdef __aarch64__
// Note!  Clang requires passed -ffixed-REG to use this declaration,
// though the GNU specification for the feature does not require that.
register uintptr_t gSpecialRegister __asm__("x28");
constexpr bool kHaveSpecialRegister = true;
#else
// Unfortunately there really is no such register on x86, since there is
// often enough register pressure to use everything.  Anyway LLVM can't
// handle arbitrary fixed registers on x86, though GCC can.
constexpr bool kHaveSpecialRegister = false;
extern uintptr_t gSpecialRegister;  // Only used in discarded if constexpr else.
#endif

// For some tests, it would be easier to take advantage of the sanitizer hooks. Unfortunately,
// defining hooks here will take precedence over the definitions in sanitizer runtimes. For those
// tests, we can just check if the hooks are available to use.
#if __has_feature(address_sanitizer) || __has_feature(hwaddress_sanitizer) || \
    __has_feature(leak_sanitizer)
#define USES_SANITIZER_HOOKS 1
#else
#define USES_SANITIZER_HOOKS 0
#endif

namespace {

constexpr int kThreadCount = 10;

// These should be bit patterns that are unlikely to appear in nature.
constexpr uintptr_t kRegisterPattern = 0xfeedfacedeadbeefUL;
constexpr uintptr_t kTssPattern = 0xf00d4c11abbababaUL;
constexpr uintptr_t kPthreadspecificPattern = 0xf00d43051215abedUL;
constexpr uintptr_t kDeadThreadReturnPattern = 0xdeadbeef3e34a100UL;

class DlopenAuto {
 public:
  DlopenAuto() : handle_(dlopen("libsanitizer-memory-snapshot-test-dlopen-dso.so", RTLD_LOCAL)) {}
  ~DlopenAuto() { dlclose(handle_); }

  bool Ok() const { return handle_; }

  const void* operator()(const char* name) {
    return reinterpret_cast<const void* (*)()>(reinterpret_cast<uintptr_t>(dlsym(handle_, name)))();
  }

 private:
  void* handle_;
};

struct MemoryChunk {
  void* mem;
  size_t len;
};

using MemoryChunks = std::vector<MemoryChunk>;

// A new pthread that immediately dies and returns Cookie().
// It's joined for cleanup on destruction.
class ScopedPthread {
 public:
  ScopedPthread() {
    pthread_create(
        &thread_, nullptr, [](void* ptr) { return ptr; }, reinterpret_cast<void*>(Cookie()));
  }

  ~ScopedPthread() {
    void* value;
    pthread_join(thread_, &value);
  }

  void Check(uintptr_t value) {
    if (value == Cookie()) {
      seen_ = true;
    }
  }

  bool seen() const { return seen_; }

 private:
  pthread_t thread_;
  bool seen_ = false;

  uintptr_t Cookie() const { return reinterpret_cast<uintptr_t>(this) ^ kDeadThreadReturnPattern; }
};

struct SnapshotResult {
  std::array<ScopedPthread, kThreadCount> dead_threads{};
  MemoryChunks globals, stacks, tls;

  zx_status_t status = ZX_ERR_BAD_STATE;
  bool ran_callback = false;

  bool saw_main_tss = false;
  std::array<bool, kThreadCount> saw_thread_tss{};

  bool saw_main_specific = false;
  std::array<bool, kThreadCount> saw_thread_specific{};

  // Note we don't try to detect the special register value in the main thread
  // because the __sanitizer_memory_snapshot code path can't reasonably be
  // expected not to touch that register (it's sketchy enough to assume the
  // vDSO code path doesn't touch it).
  std::array<bool, kThreadCount> saw_thread_special_registers{};
};

bool ChunksCover(const MemoryChunks& chunks, const void* ptr) {
  auto addr = reinterpret_cast<uintptr_t>(ptr);
  // When hwasan is enabled, `ptr` can be tagged if it points to a static local variable. However,
  // globals received here from __sanitizer_memory_snapshot will not be tagged since we currently
  // disable tagging on globals. We can safely strip the tag here because the actual static data
  // will be within expected memory chunks, but the tag is added to the C ptr afterwards due to how
  // hwasan instruments local variables.
  addr &= ADDR_MASK;
  for (const auto& chunk : chunks) {
    const auto start = reinterpret_cast<uintptr_t>(chunk.mem);
    if (addr >= start && (addr - start < chunk.len)) {
      return true;
    }
  }
  return false;
}

void SnapshotDoneCallback(zx_status_t status, void* arg) {
  auto result = static_cast<SnapshotResult*>(arg);
  result->ran_callback = true;
  result->status = status;
}

// It's technically not kosher under the API documentation to stash the ranges
// like this and check them later, but it makes the testing much easier.  The
// registers are by definition a copy and the tss_set (pthread_setspecific)
// data address ranges are not knowable outside libc, so those get tested by
// value (kosher) rather than by address range (easy).

void GlobalsCallback(void* mem, size_t len, void* arg) {
  auto result = static_cast<SnapshotResult*>(arg);
  result->globals.push_back({mem, len});
}

void StacksCallback(void* mem, size_t len, void* arg) {
  auto result = static_cast<SnapshotResult*>(arg);
  result->stacks.push_back({mem, len});
}

void TlsCallback(void* mem, size_t len, void* arg) {
  auto result = static_cast<SnapshotResult*>(arg);
  result->tls.push_back({mem, len});

  // Currently, the TLS callback receives two kinds of buffers: (1) an actual TLS
  // segment which may or may not be 8-byte aligned and (2) libc internals (start_arg,
  // tsd, etc.) which will always be 8-byte aligned. The checks bellow are for
  // asserting we found known words in (2). We check that known TLS segments are
  // found by storing them for checking later after the snapshot. If a buffer we
  // receive is not aligned, we know it is from TLS.
  if (reinterpret_cast<uintptr_t>(mem) % alignof(uintptr_t) != 0)
    return;

  const auto* words = reinterpret_cast<uintptr_t*>(mem);
  for (size_t i = 0; i < len / sizeof(words[0]); ++i) {
    if (words[i] == kTssPattern) {
      result->saw_main_tss = true;
    }
    for (bool& seen : result->saw_thread_tss) {
      if (words[i] == (kTssPattern ^ reinterpret_cast<uintptr_t>(&seen))) {
        seen = true;
      }
    }
    if (words[i] == kPthreadspecificPattern) {
      result->saw_main_specific = true;
    }
    for (bool& seen : result->saw_thread_specific) {
      if (words[i] == (kPthreadspecificPattern ^ reinterpret_cast<uintptr_t>(&seen))) {
        seen = true;
      }
    }
    for (auto& t : result->dead_threads) {
      t.Check(words[i]);
    }
  }
}

void RegistersCallback(void* mem, size_t len, void* arg) {
  auto result = static_cast<SnapshotResult*>(arg);
  const auto* regs = reinterpret_cast<uintptr_t*>(mem);
  for (size_t i = 0; i < len / sizeof(regs[0]); ++i) {
    for (bool& seen : result->saw_thread_special_registers) {
      if (regs[i] == (kRegisterPattern ^ reinterpret_cast<uintptr_t>(&seen))) {
        seen = true;
      }
    }
  }
}

// This is the least-demanding possible smoke test.
TEST(SanitizerUtilsTest, MemorySnapshotNoReportsOneThread) {
  SnapshotResult result;

  __sanitizer_memory_snapshot(nullptr, nullptr, nullptr, nullptr, SnapshotDoneCallback,
                              static_cast<void*>(&result));

  EXPECT_TRUE(result.ran_callback);
  EXPECT_OK(result.status);
}

// This tests just the stop-the-world machinery, without verifying
// that it did anything other than not crash or wedge or report error.
TEST(SanitizerUtilsTest, MemorySnapshotNoReportsWithThreads) {
  SnapshotResult result;

  std::mutex mutex;
  std::condition_variable cond;
  bool time_to_die = false;

  // Start some threads that just sit around.
  std::array<std::thread, kThreadCount> threads{};
  for (auto& t : threads) {
    t = std::thread([&]() {
      std::unique_lock<std::mutex> lock(mutex);
      cond.wait(lock, [&]() { return time_to_die; });
    });
  }

  // At the end, wake the threads up and wait for them to die.
  auto cleanup = fit::defer([&]() {
    {
      std::lock_guard<std::mutex> locked(mutex);
      time_to_die = true;
      cond.notify_all();
    }
    for (auto& t : threads) {
      t.join();
    }
  });

  __sanitizer_memory_snapshot(nullptr, nullptr, nullptr, nullptr, SnapshotDoneCallback,
                              static_cast<void*>(&result));

  EXPECT_TRUE(result.ran_callback);
  EXPECT_OK(result.status);
}

// This tests the enumeration of globals without anything using thread state.
TEST(SanitizerUtilsTest, MemorySnapshotGlobalsOnly) {
  DlopenAuto loaded;
  ASSERT_TRUE(loaded.Ok(), "dlopen: %s", dlerror());

  SnapshotResult result;

  __sanitizer_memory_snapshot(GlobalsCallback, nullptr, nullptr, nullptr, SnapshotDoneCallback,
                              static_cast<void*>(&result));

  ASSERT_TRUE(result.ran_callback);
  ASSERT_OK(result.status);

  static int local_data = 23;
  static int local_bss;
  static const int local_rodata = 17;
  static int* const local_relro = &local_data;
  EXPECT_TRUE(ChunksCover(result.globals, &local_data));
  EXPECT_TRUE(ChunksCover(result.globals, &local_bss));
  EXPECT_FALSE(ChunksCover(result.globals, &local_rodata));
  EXPECT_FALSE(ChunksCover(result.globals, &local_relro));

  EXPECT_TRUE(ChunksCover(result.globals, NeededDsoDataPointer()));
  EXPECT_TRUE(ChunksCover(result.globals, NeededDsoBssPointer()));
  EXPECT_FALSE(ChunksCover(result.globals, NeededDsoRodataPointer()));
  EXPECT_FALSE(ChunksCover(result.globals, NeededDsoRelroPointer()));

  EXPECT_TRUE(ChunksCover(result.globals, loaded("DlopenDsoDataPointer")));
  EXPECT_TRUE(ChunksCover(result.globals, loaded("DlopenDsoBssPointer")));
  EXPECT_FALSE(ChunksCover(result.globals, loaded("DlopenDsoRodataPointer")));
  EXPECT_FALSE(ChunksCover(result.globals, loaded("DlopenDsoRelroPointer")));
}

thread_local int gTdata = 42;
thread_local int gTbss;

template <typename Key, auto Create, auto Destroy, auto Store>
class ScopedTlsKey {
 public:
  ScopedTlsKey() { Create(&key_, nullptr); }
  ~ScopedTlsKey() { Destroy(key_); }

  void Set(uintptr_t x) const { Store(key_, reinterpret_cast<void*>(x)); }

 private:
  Key key_;
};

// This is the kitchen-sink test of the real-world case of collecting
// everything.  It seems more useful to test this case as one than to
// separately test stacks, regs, and tls, separately for this thread and
// other threads, etc.  This is the way the interface is really used for
// leak-checking or conservative GC.
TEST(SanitizerUtilsTest, MemorySnapshotFull) {
  DlopenAuto loaded;
  ASSERT_TRUE(loaded.Ok(), "dlopen: %s", dlerror());

  // Check how many threads exist now (probably just one).
  size_t nthreads;
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_PROCESS_THREADS, nullptr, 0, nullptr, &nthreads));
  const auto quiescent_nthreads = nthreads;

  // The constructor (ScopedPthread) creates threads that immediately exit
  // just so their return values are stored but exist nowhere else.
  SnapshotResult result;

  // Now wait until all those threads have finished dying.
  do {
    std::this_thread::yield();
    ASSERT_OK(
        zx::process::self()->get_info(ZX_INFO_PROCESS_THREADS, nullptr, 0, nullptr, &nthreads));
  } while (nthreads > quiescent_nthreads);

  ScopedTlsKey<tss_t, tss_create, tss_delete, tss_set> tss;
  tss.Set(kTssPattern);

  ScopedTlsKey<pthread_key_t, pthread_key_create, pthread_key_delete, pthread_setspecific> specific;
  specific.Set(kPthreadspecificPattern);

  // "Pre-fault" the TLS accesses so that not only this thread but all the
  // threads created later will definitely have them in their DTVs.  The
  // implementation handles the lazy DTV update case by not reporting the
  // not-yet-used thread DTV entries, but it's not an API requirement that
  // they *not* be reported so we don't separately test for that.
  ASSERT_NOT_NULL(NeededDsoThreadLocalDataPointer());
  ASSERT_NOT_NULL(NeededDsoThreadLocalBssPointer());
  ASSERT_NOT_NULL(loaded("DlopenDsoThreadLocalDataPointer"));
  ASSERT_NOT_NULL(loaded("DlopenDsoThreadLocalBssPointer"));

  // Use a raw futex rather than std::condition_variable here so that the test
  // threads can use only code in this translation unit and the vDSO.  It's so
  // far reasonable to expect gSpecialRegister not to be clobbered by the
  // zx_futex_wait code in the vDSO, but not reasonable to expect that from
  // the libc++ and libc code involved in using std::condition_variable.
  std::atomic_int ready = 0;
  std::atomic_int finished = 0;
  static_assert(sizeof(std::atomic_int) == sizeof(zx_futex_t));

  // Start some threads that report their addresses and then just block.
  struct Thread {
    std::thread thread;
    const void* safe_stack = nullptr;
    const void* unsafe_stack = nullptr;
    const void* tdata = nullptr;
    const void* tbss = nullptr;
    const void* needed_dso_tdata = nullptr;
    const void* needed_dso_tbss = nullptr;
    const void* dlopen_dso_tdata = nullptr;
    const void* dlopen_dso_tbss = nullptr;
    bool ready = false;
  };
  std::array<Thread, kThreadCount> threads{};
  for (auto& t : threads) {
    t.thread = std::thread(
        [&](Thread& self) {
          int stack_local = 42;

          self.safe_stack = __builtin_frame_address(0);
          self.unsafe_stack = &stack_local;

          self.tdata = &gTdata;
          self.tbss = &gTbss;
          self.needed_dso_tdata = NeededDsoThreadLocalDataPointer();
          self.needed_dso_tbss = NeededDsoThreadLocalBssPointer();
          self.dlopen_dso_tdata = loaded("DlopenDsoThreadLocalDataPointer");
          self.dlopen_dso_tbss = loaded("DlopenDsoThreadLocalBssPointer");

          const size_t self_idx = &self - threads.begin();
          const auto* ptr = &result.saw_thread_tss[self_idx];
          tss.Set(kTssPattern ^ reinterpret_cast<uintptr_t>(ptr));
          ptr = &result.saw_thread_specific[self_idx];
          specific.Set(kPthreadspecificPattern ^ reinterpret_cast<uintptr_t>(ptr));

          if constexpr (kHaveSpecialRegister) {
            ptr = &result.saw_thread_special_registers[self_idx];
            gSpecialRegister = kRegisterPattern ^ reinterpret_cast<uintptr_t>(ptr);
          }

          ready.fetch_add(1);
          zx_status_t status = zx_futex_wake(reinterpret_cast<zx_futex_t*>(&ready), 1);
          if (status != ZX_OK) {
            __builtin_trap();
          }

          status = zx_futex_wait(reinterpret_cast<zx_futex_t*>(&finished), 0, ZX_HANDLE_INVALID,
                                 ZX_TIME_INFINITE);
          if (status != ZX_OK && status != ZX_ERR_BAD_STATE) {
            __builtin_trap();
          }
        },
        std::ref(t));
  }

  // At the end, wake the threads up and wait for them to die.
  auto cleanup = fit::defer([&]() {
    finished.store(1);
    EXPECT_OK(zx_futex_wake(reinterpret_cast<zx_futex_t*>(&finished), -1));
    for (auto& t : threads) {
      t.thread.join();
    }
  });

  // Now wait for all the threads to be ready.
  while (true) {
    zx_futex_t count = ready.load();
    ASSERT_LE(count, kThreadCount);
    if (count == kThreadCount) {
      break;
    }
    zx_status_t status = zx_futex_wait(reinterpret_cast<zx_futex_t*>(&ready), count,
                                       ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
    if (status != ZX_ERR_BAD_STATE) {  // Normal race condition case: retry.
      ASSERT_OK(status, "zx_futex_wait failed");
    }
  }

  // Sanity-check the setup work.
  for (auto& t : threads) {
    EXPECT_NOT_NULL(t.safe_stack);
    EXPECT_NOT_NULL(t.unsafe_stack);
    EXPECT_NOT_NULL(t.tdata);
    EXPECT_NOT_NULL(t.tbss);
    EXPECT_NOT_NULL(t.needed_dso_tdata);
    EXPECT_NOT_NULL(t.needed_dso_tbss);
    EXPECT_NOT_NULL(t.dlopen_dso_tdata);
    EXPECT_NOT_NULL(t.dlopen_dso_tbss);
  }

  // Now do the actual thing.
  __sanitizer_memory_snapshot(GlobalsCallback, StacksCallback, RegistersCallback, TlsCallback,
                              SnapshotDoneCallback, static_cast<void*>(&result));

  EXPECT_TRUE(result.ran_callback);
  EXPECT_OK(result.status);

  static int local_data = 23;
  static int local_bss;
  static const int local_rodata = 17;
  static int* const local_relro = &local_data;
  EXPECT_TRUE(ChunksCover(result.globals, &local_data));
  EXPECT_TRUE(ChunksCover(result.globals, &local_bss));
  EXPECT_FALSE(ChunksCover(result.globals, &local_rodata));
  EXPECT_FALSE(ChunksCover(result.globals, &local_relro));

  EXPECT_TRUE(ChunksCover(result.globals, NeededDsoDataPointer()));
  EXPECT_TRUE(ChunksCover(result.globals, NeededDsoBssPointer()));
  EXPECT_FALSE(ChunksCover(result.globals, NeededDsoRodataPointer()));
  EXPECT_FALSE(ChunksCover(result.globals, NeededDsoRelroPointer()));

  EXPECT_TRUE(ChunksCover(result.globals, loaded("DlopenDsoDataPointer")));
  EXPECT_TRUE(ChunksCover(result.globals, loaded("DlopenDsoBssPointer")));
  EXPECT_FALSE(ChunksCover(result.globals, loaded("DlopenDsoRodataPointer")));
  EXPECT_FALSE(ChunksCover(result.globals, loaded("DlopenDsoRelroPointer")));

  int stack_local = 42;
  EXPECT_TRUE(ChunksCover(result.stacks, __builtin_frame_address(0)));
  EXPECT_TRUE(ChunksCover(result.stacks, &stack_local));

  for (auto& t : threads) {
    EXPECT_TRUE(ChunksCover(result.stacks, t.safe_stack));
    EXPECT_TRUE(ChunksCover(result.stacks, t.unsafe_stack));
  }

  EXPECT_TRUE(ChunksCover(result.tls, &gTdata));
  EXPECT_TRUE(ChunksCover(result.tls, &gTbss));
  EXPECT_TRUE(ChunksCover(result.tls, NeededDsoThreadLocalDataPointer()));
  EXPECT_TRUE(ChunksCover(result.tls, NeededDsoThreadLocalBssPointer()));
  EXPECT_TRUE(ChunksCover(result.tls, loaded("DlopenDsoThreadLocalDataPointer")));
  EXPECT_TRUE(ChunksCover(result.tls, loaded("DlopenDsoThreadLocalBssPointer")));

  for (auto& t : threads) {
    EXPECT_TRUE(ChunksCover(result.tls, t.tdata));
    EXPECT_TRUE(ChunksCover(result.tls, t.tbss));
    EXPECT_TRUE(ChunksCover(result.tls, t.needed_dso_tdata));
    EXPECT_TRUE(ChunksCover(result.tls, t.needed_dso_tbss));
    EXPECT_TRUE(ChunksCover(result.tls, t.dlopen_dso_tdata));
    EXPECT_TRUE(ChunksCover(result.tls, t.dlopen_dso_tbss));
  }

  EXPECT_TRUE(result.saw_main_tss);
  for (bool& seen : result.saw_thread_tss) {
    EXPECT_TRUE(seen, "saw_thread_tss[%zu]", &seen - result.saw_thread_tss.begin());
  }

  EXPECT_TRUE(result.saw_main_specific);
  for (bool& seen : result.saw_thread_specific) {
    EXPECT_TRUE(seen, "saw_thread_specific[%zu]", &seen - result.saw_thread_specific.begin());
  }

  if constexpr (kHaveSpecialRegister) {
    for (bool& seen : result.saw_thread_special_registers) {
      EXPECT_TRUE(seen, "saw_thread_special_registers[%zu]",
                  &seen - result.saw_thread_special_registers.begin());
    }
  }

  for (const auto& t : result.dead_threads) {
    EXPECT_TRUE(t.seen(), "dead thread %tu not seen", &t - result.dead_threads.begin());
  }
}

enum StartArgClearedThreadState {
  kWaitingThreadStart,
  kThreadRunning,
  kFinishedSnapshot,
};

struct ThreadArgs {
  std::mutex* mutex;
  std::condition_variable* cv;
  StartArgClearedThreadState* state;
};

struct CallbackArgs {
  void* data_ptr;
  bool found_in_tls;
  bool found_in_stack;
  bool found_in_regs;
};

// This is the type we pass as an argument to the thread in the `StartArgCleared` test. It's useful
// to know the type alignment when iterating over raw data.
using StartArgClearedSearchType = ThreadArgs;

// For the `StartArgCleared`, we want to iterate over the stack to search for a specific pointer. If
// this code was ASan-instrumented, then it's possible for this to iterate over redzones which ASan
// will report. We can ignore these reports while searching for the pointer.
#ifdef __clang__
[[clang::no_sanitize("address")]]
#endif
void StartArgClearedUnsanitizedStackCallback(void* mem, size_t len, void* arg) {
  auto args = static_cast<CallbackArgs*>(arg);
  if (args->found_in_stack)
    return;

  // See if the data we're looking for points anywhere into this stack.
  uintptr_t data_ptr = reinterpret_cast<uintptr_t>(args->data_ptr);
  uintptr_t stack_begin = reinterpret_cast<uintptr_t>(mem);
  uintptr_t stack_end = reinterpret_cast<uintptr_t>(stack_begin + len);
  // When HWASan is enabled, `data_ptr` can be tagged since it points to a local variable in the
  // `StartArgCleared` test. However, the underlying stack base will not be tagged if came from
  // regions allocated by syscalls (zx_vmar_allocate + zx_vmar_map). Even if the pointer is
  // instrumented to include a tag, the addressing bits should still point to something on this stack
  // if the thing it points to is actually on this stack.
  data_ptr &= ADDR_MASK;
  args->found_in_stack = (stack_begin <= data_ptr && data_ptr < stack_end);
}

// If we take a snapshot now, we should not find the argument in tls callbacks because it was
// cleared before we enter the thread. It should instead be in either the stack or registers.
void StartArgClearedTlsCallback(void* mem, size_t len, void* arg) {
  auto args = static_cast<CallbackArgs*>(arg);
  if (args->found_in_tls)
    return;

  // The tls callback iterates over two things: (1) the TLS region that contains actual thread-local
  // data, or (2) pointers to data pointed to by internal pthread machinery. For (1), we can see if
  // the pointer we're looking for points into this TLS region.
  uintptr_t data_ptr = reinterpret_cast<uintptr_t>(args->data_ptr);
  uintptr_t tls_begin = reinterpret_cast<uintptr_t>(mem);
  uintptr_t tls_end = reinterpret_cast<uintptr_t>(tls_begin + len);
  if (tls_begin <= data_ptr && data_ptr < tls_end) {
    args->found_in_tls = true;
    return;
  }

  // For (2), we're iterating over an array of pointers. This should also be pointer-aligned, but if
  // `mem` happens to point to 4-byte aligned data, then it might not.
  if (reinterpret_cast<uintptr_t>(mem) % alignof(uintptr_t) == 0) {
    for (const uintptr_t& val :
         cpp20::span{reinterpret_cast<const uintptr_t*>(mem), len / sizeof(uintptr_t)}) {
      if (val == reinterpret_cast<uintptr_t>(args->data_ptr)) {
        args->found_in_tls = true;
        return;
      }
    }
  }
}

void StartArgClearedRegsCallback(void* mem, size_t len, void* arg) {
  auto args = static_cast<CallbackArgs*>(arg);
  if (args->found_in_regs)
    return;

  // The regs callback is passed a pointer to an array of registers (specifically
  // `zx_thread_state_general_regs_t`), so we'll be iterating over an array of pointers. Check if
  // any of them match the thread argument.
  ZX_ASSERT_MSG(reinterpret_cast<uintptr_t>(mem) % alignof(uintptr_t) == 0,
                "`mem` does not point to an array of register values.");
  for (const uintptr_t& reg :
       cpp20::span{reinterpret_cast<const uintptr_t*>(mem), len / sizeof(uintptr_t)}) {
    if (reg == reinterpret_cast<uintptr_t>(args->data_ptr)) {
      args->found_in_regs = true;
      return;
    }
  }
}

TEST(SanitizerUtilsTest, StartArgCleared) {
  std::mutex mutex;
  std::condition_variable cv;
  StartArgClearedThreadState state;

  thrd_t thread;

  auto cleanup = fit::defer([&]() {
    // Finally allow the thread to finish.
    {
      std::unique_lock<std::mutex> lock(mutex);
      state = kFinishedSnapshot;
    }
    cv.notify_one();

    int result;
    EXPECT_EQ(thrd_join(thread, &result), thrd_success);
    EXPECT_EQ(result, 0);
  });

  auto thread_entry = [](void* arg) {
    auto* thread_args = reinterpret_cast<ThreadArgs*>(arg);
    std::mutex& mutex = *thread_args->mutex;
    std::condition_variable& cv = *thread_args->cv;
    StartArgClearedThreadState& state = *thread_args->state;

    // Notify the main thread that we have entered this thread.
    std::unique_lock<std::mutex> lock(mutex);
    state = kThreadRunning;
    cv.notify_one();

    // Wait shortly after entering this thread. At this point, the start_arg field of the pthread
    // struct should be cleared and inaccessible from the tls callback. We can continue from here
    // once we are in `cleanup` and have finished the scan.
    cv.wait(lock, [&state]() { return state == kFinishedSnapshot; });

    return 0;
  };

  ThreadArgs thread_args = {
      .mutex = &mutex,
      .cv = &cv,
      .state = &state,
  };
  state = kWaitingThreadStart;
  ASSERT_EQ(thrd_create(&thread, thread_entry, &thread_args), thrd_success);

  // Wait here until we ensure the new thread has started.
  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&state]() { return state == kThreadRunning; });
  }

  CallbackArgs callback_args = {
      // Try to look for our thread argument.
      .data_ptr = &thread_args,
  };

  __sanitizer_memory_snapshot(/*globals=*/nullptr,
                              /*stacks=*/StartArgClearedUnsanitizedStackCallback,
                              /*regs=*/StartArgClearedRegsCallback,
                              /*tls=*/StartArgClearedTlsCallback, /*done=*/nullptr, &callback_args);

  EXPECT_TRUE(callback_args.found_in_stack || callback_args.found_in_regs);
  EXPECT_FALSE(callback_args.found_in_tls);
}

// NOTE: We can't use sanitizers for this specific test because we want to be able to suspend the
// thread after its creation, but before it starts. The easiest way we can do that is through
// sanitizer hooks. Unfortunately, defining a hook here will supersede corresponding hooks in the
// actual sanitizer and can cause other tests to fail. We can guarantee this hook will be free if no
// sanitizers are enabled. We could implement chained hooks using dlsym(RTLD_NEXT,"...") but that
// could be fragile and it doesn't seem crucial to test these cases especially under sanitizer
// builds.
#if !USES_SANITIZER_HOOKS

class SuspendedThreadTest : public ::zxtest::Test {
 public:
  // We only want to run the before_thread_create hook if this is the thread we see.
  // This way, we don't mix in what we want to happen for the
  // MemorySnapshotStartArgOnSuspendedThread with other tests.
  static thread_local zx::suspend_token* gSuspendToken;

 protected:
  void SetUp() override {
    // The sanitizer hooks will only work with this test since it will be the only test where
    // `gSuspendToken` has a non-zero value.
    gSuspendToken = &suspend_;
    ASSERT_NE(gSuspendToken, nullptr);
  }

  void TearDown() override {
    // Resume the thread which will clear up any allocated data.
    EXPECT_EQ(gSuspendToken, &suspend_);
    gSuspendToken->reset();
    gSuspendToken = nullptr;

    int result;
    EXPECT_EQ(thrd_join(thread_, &result), thrd_success);
    EXPECT_EQ(result, 0);
  }

  thrd_t thread_;

 private:
  zx::suspend_token suspend_;
};

thread_local zx::suspend_token* SuspendedThreadTest::gSuspendToken = nullptr;

// This tests the snapshot covers arguments passed to the pthread machinery.
// In particular, if we suspend a thread that hasn't started yet, it's possible
// its thread register hasn't been setup yet, so memory_snapshot can't access
// internal pthread data structures through it. This ensures that the thread
// argument is covered even before the thread register has been set up yet.
TEST_F(SuspendedThreadTest, MemorySnapshotStartArgOnSuspendedThread) {
  // Create a new pthread, but ensure that the thread is suspended before it starts. That is, we
  // want the pthread machinery for the thread to be setup, but we do not want to execute any code
  // in the new thread. We can do this via the before_thread_create hook which runs after the thread
  // is created, but before the thread actually starts.
  constexpr int kTransferData = 42;
  std::unique_ptr<int> transfer_ptr(new int(kTransferData));
  auto thread_entry = [](void* arg) -> int {
    std::unique_ptr<int> transfer_ptr(reinterpret_cast<int*>(arg));
    ZX_ASSERT(*transfer_ptr == kTransferData && "Failed to get the expected data");
    return 0;
  };
  ASSERT_EQ(thrd_create(&thread_, thread_entry, transfer_ptr.get()), thrd_success);

  // At this point, the pthread structure should be setup. At any point in between now and when we
  // take the memory snapshot, the thread may start, but will be immediately suspended via the
  // sanitizer hook. The memory snapshot machinery should ensure it's suspended before it does its
  // scan.
  int* data_ptr = transfer_ptr.release();

  struct CallbackResult {
    const void* data_ptr;
    bool found_data;
  };
  CallbackResult result = {
      .data_ptr = data_ptr,
      .found_data = false,
  };

  // The callback will update the result if we find the pointer we're looking for. Note that
  // technically, the pointer also exists in this thread's stack, but we just want to ensure it's
  // accessible in the other thread's TCB.
  auto tls_callback = [](void* mem, size_t len, void* arg) -> void {
    auto result = static_cast<CallbackResult*>(arg);

    // We already found the pointer we're looking for.
    if (result->found_data)
      return;

    for (const void* ptr : cpp20::span{reinterpret_cast<void* const*>(mem), len / sizeof(void*)}) {
      if (ptr == result->data_ptr) {
        result->found_data = true;
        return;
      }
    }
  };

  __sanitizer_memory_snapshot(/*globals=*/nullptr,
                              /*stacks=*/nullptr,
                              /*regs=*/nullptr,
                              /*tls=*/tls_callback, /*done=*/nullptr, static_cast<void*>(&result));

  EXPECT_TRUE(result.found_data);
}

#endif  // !USES_SANITIZER_HOOKS

}  // namespace

#if !USES_SANITIZER_HOOKS

// Attempt to suspend the newly created thread. Propagate the suspend token so we can close it later
// to startup the thread.
void* __sanitizer_before_thread_create_hook(thrd_t thread, bool /*detached*/, const char* /*name*/,
                                            void* /*stack_base*/, size_t /*stack_size*/) {
  // Do not allow this to run for anything other than the MemorySnapshotStartArgOnSuspendedThread
  // test. This token pointer is only set as non-zero for this test.
  if (!SuspendedThreadTest::gSuspendToken)
    return nullptr;

  // Use a plain handle here rather than initializing a zx::task so we don't close the initialized
  // task on its destructor.
  zx_handle_t task = thrd_get_zx_handle(thread);
  zx_status_t status =
      zx_task_suspend_token(task, SuspendedThreadTest::gSuspendToken->reset_and_get_address());
  ZX_ASSERT(status == ZX_OK && "Failed to suspend new thread.");
  return SuspendedThreadTest::gSuspendToken;
}

void __sanitizer_thread_create_hook(void* hook, thrd_t th, int error) {
  // Either `hook` and `gSuspendHook` are both nullptr because we are not running the
  // MemorySnapshotStartArgOnSuspendedThread test, or they are both the same non-zero value since we
  // are running the MemorySnapshotStartArgOnSuspendedThread test.
  ZX_ASSERT(hook == SuspendedThreadTest::gSuspendToken && "Thread was not suspended correctly");
  ZX_ASSERT(error == thrd_success && "Thread was not created correctly");
}

// Override this definition because the default one will check that `hook` is `null`, which it won't
// be for MemorySnapshotStartArgOnSuspendedThread.
void __sanitizer_thread_start_hook(void* hook, thrd_t self) {}
void __sanitizer_thread_exit_hook(void* hook, thrd_t self) {}

#endif  // !USES_SANITIZER_HOOKS
