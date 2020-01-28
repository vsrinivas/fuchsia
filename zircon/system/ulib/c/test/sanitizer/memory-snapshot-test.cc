// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <pthread.h>
#include <threads.h>
#include <zircon/sanitizer.h>

#include <array>
#include <thread>
#include <vector>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

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
  const auto addr = reinterpret_cast<uintptr_t>(ptr);
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
  auto cleanup = fbl::MakeAutoCall([&]() {
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

          const size_t self_idx = &self - &threads[0];
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
  auto cleanup = fbl::MakeAutoCall([&]() {
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
    ASSERT_OK(zx_futex_wait(reinterpret_cast<zx_futex_t*>(&ready), count, ZX_HANDLE_INVALID,
                            ZX_TIME_INFINITE));
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
    EXPECT_TRUE(seen, "saw_thread_tss[%zu]", &seen - &result.saw_thread_tss[0]);
  }

  EXPECT_TRUE(result.saw_main_specific);
  for (bool& seen : result.saw_thread_specific) {
    EXPECT_TRUE(seen, "saw_thread_specific[%zu]", &seen - &result.saw_thread_specific[0]);
  }

  if constexpr (kHaveSpecialRegister) {
    for (bool& seen : result.saw_thread_special_registers) {
      EXPECT_TRUE(seen, "saw_thread_special_registers[%zu]",
                  &seen - &result.saw_thread_special_registers[0]);
    }
  }

  for (const auto& t : result.dead_threads) {
    EXPECT_TRUE(t.seen(), "dead thread %tu not seen", &t - &result.dead_threads[0]);
  }
}

}  // namespace
