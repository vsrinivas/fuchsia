// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/fit/defer.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/sanitizer.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <new>
#include <thread>
#include <utility>

#include <explicit-memory/bytes.h>
#include <sanitizer/lsan_interface.h>
#include <zxtest/zxtest.h>

namespace {

// This uses template specialization to let LeakedAllocation<T>
// support a T that is either an array type or a singleton type.
template <typename T>
struct Allocator {
  static T* New() { return new T; }
  static void Delete(T* ptr) { delete ptr; }
};

template <typename T, size_t N>
struct Allocator<T[N]> {
  static T* New() { return new T[N]; }
  static void Delete(T* ptr) { delete[] ptr; }
};

// This works essentially like std::unique_ptr<T>, but it stores the pointer
// in an obfuscated fashion that hides it from the GC-style scan LSan does.
// If this object is the only place that pointer is held, it should be
// diagnosed as a leak.  The CallWith method provides a way to operate on
// the pointer without it implicitly appearing in any place like stacks or
// registers that LSan's scans could observe after CallWith returns.
template <typename T>
class LeakedAllocation {
 public:
  using pointer_type = decltype(Allocator<T>::New());

  LeakedAllocation() = default;

  LeakedAllocation(const LeakedAllocation&) = delete;

  LeakedAllocation(LeakedAllocation&& other) : obfuscated_ptr_(other.obfuscated_ptr_) {
    other.obfuscated_ptr_ = kCipher_;
  }

  void swap(LeakedAllocation&& other) { std::swap(obfuscated_ptr_, other.obfuscated_ptr_); }

  LeakedAllocation& operator=(LeakedAllocation&& other) {
    swap(other);
    return *this;
  }

  [[nodiscard, gnu::noinline]] bool Allocate() {
    // The real work is done in another call frame that won't be inlined.  That
    // means all the local state of the real function's call frame will be only
    // in the call-clobbered registers and/or the stack below this call frame.
    bool ok = DoAllocate();

    // This function is never inlined, and it clobbers all the call-clobbered
    // registers just in case so that the unobfuscated pointer value should
    // not appear in any registers or live stack after it returns.
    ClobberRegistersAndStack();

    return ok;
  }

  auto get() const { return reinterpret_cast<pointer_type>(obfuscated_ptr_ ^ kCipher_); }

  // This calls the function with the pointer as from get(), but then
  // scrubs registers so on return it's safe to assume that the pointer
  // value does not appear in registers or live stack.
  template <typename F>
  [[gnu::noinline]] void CallWith(F func) const {
    DoCallWith(std::move(func));
    ClobberRegistersAndStack();
  }

  ~LeakedAllocation() {
    auto ptr = get();
    if (ptr) {
      Allocator<T>::Delete(ptr);
    }
  }

 private:
  // This is a large enough size that it should be well more than whatever
  // was used in DoAllocate or DoCallWith.
  static constexpr size_t kClobberStackSize_ = 16384;

  static constexpr uintptr_t kCipher_ = 0xfeedfacedeadbeefUL;
  uintptr_t obfuscated_ptr_ = kCipher_;

  [[nodiscard, gnu::noinline]] bool DoAllocate() {
    auto ptr = Allocator<T>::New();
    if (!ptr) {
      return false;
    }
    this->~LeakedAllocation();
    obfuscated_ptr_ = reinterpret_cast<uintptr_t>(ptr) ^ kCipher_;
    return true;
  }

  template <typename F>
  [[gnu::noinline]] void DoCallWith(F func) const {
    func(get());
  }

  // Caller should use [[gnu::noinline]] too.
  [[gnu::noinline]] static void ClobberRegistersAndStack() {
    // Wipe out a sizable range in both the machine stack and unsafe stack,
    // just in case either or both is in use and gets a pointer value stored.
    ClobberUnsafeStack();
    ClobberMachineStack();

#ifdef __aarch64__
    __asm__ volatile("mov x0, xzr" ::: "x0");
    __asm__ volatile("mov x1, xzr" ::: "x1");
    __asm__ volatile("mov x2, xzr" ::: "x2");
    __asm__ volatile("mov x3, xzr" ::: "x3");
    __asm__ volatile("mov x4, xzr" ::: "x4");
    __asm__ volatile("mov x5, xzr" ::: "x5");
    __asm__ volatile("mov x6, xzr" ::: "x6");
    __asm__ volatile("mov x7, xzr" ::: "x7");
    __asm__ volatile("mov x8, xzr" ::: "x8");
    __asm__ volatile("mov x9, xzr" ::: "x9");
    __asm__ volatile("mov x10, xzr" ::: "x10");
    __asm__ volatile("mov x11, xzr" ::: "x11");
    __asm__ volatile("mov x12, xzr" ::: "x12");
    __asm__ volatile("mov x13, xzr" ::: "x13");
    __asm__ volatile("mov x14, xzr" ::: "x14");
    __asm__ volatile("mov x15, xzr" ::: "x15");
    __asm__ volatile("mov x16, xzr" ::: "x16");
    __asm__ volatile("mov x17, xzr" ::: "x17");
#elif defined(__x86_64__)
    __asm__ volatile("xor %%rax, %%rax" ::: "%rax");
    __asm__ volatile("xor %%rbx, %%rbx" ::: "%rbx");
    __asm__ volatile("xor %%rcx, %%rcx" ::: "%rcx");
    __asm__ volatile("xor %%rdx, %%rdx" ::: "%rdx");
    __asm__ volatile("xor %%rdi, %%rdi" ::: "%rdi");
    __asm__ volatile("xor %%rsi, %%rsi" ::: "%rsi");
    __asm__ volatile("xor %%r8, %%r8" ::: "%r8");
    __asm__ volatile("xor %%r9, %%r9" ::: "%r9");
    __asm__ volatile("xor %%r10, %%r10" ::: "%r10");
    __asm__ volatile("xor %%r11, %%r11" ::: "%r11");
#endif
  }

  [[gnu::noinline, clang::no_sanitize("safe-stack")]] static void ClobberMachineStack() {
    char array[kClobberStackSize_];
    mandatory_memset(array, 0, sizeof(array));
  }

  [[gnu::noinline]] static void ClobberUnsafeStack() {
#if __has_feature(safe_stack)
    char array[kClobberStackSize_];
    mandatory_memset(array, 0, sizeof(array));
#endif
  }
};

// This invokes the LeakSanitizer machinery that ordinarily runs at exit.
bool LsanDetectsLeaks() { return __lsan_do_recoverable_leak_check() != 0; }

// Send the scare warnings via the sanitizer logging so they line up with the
// following LSan messages they're warning about.
void SanLog(std::string_view s) { __sanitizer_log_write(s.data(), s.size()); }

// Invoke LSan check and wrap output with tefmocheck ignore marker blocks.
bool HasLeaks() {
  // tefmocheck will ignore LeakSanitizer warnings emitted within this block
  // of text. Don't change this output without also changing the ExceptBlock in
  // tefmocheck.
  // See //tools/testing/tefmocheck/string_in_log_check.go
  SanLog("[===LSAN EXCEPT BLOCK START===]");

  SanLog("[===NOTE===] A scary-looking message with lots of logging");
  SanLog("[===NOTE===] and LSan detected memory leaks");
  SanLog("[===NOTE===] is expected now!  Do not be alarmed.");
  bool leaks_detected = LsanDetectsLeaks();
  SanLog("[===LSAN EXCEPT BLOCK END===]");

  return leaks_detected;
}

class LeakSanitizerTest : public zxtest::Test {
 protected:
  void SetUp() override {
    // The test is meaningless if there are leaks on entry.
    ASSERT_FALSE(LsanDetectsLeaks());
  }

  void TearDown() override {
    // The test pollutes other cases if there are leaks on exit.
    ASSERT_FALSE(LsanDetectsLeaks());
  }
};

TEST(LeakSanitizerTest, NoLeaks) {
  // The default state should be no leaks detected.
  EXPECT_FALSE(LsanDetectsLeaks());
}

TEST_F(LeakSanitizerTest, Leak) {
  // Make a known "leaked" allocation.  The pointer is obfuscated so the
  // LSan sweep should declare it leaked.  But the LeakedAllocation dtor
  // actually de-obfuscates and cleans it up afterwards.
  LeakedAllocation<int> leak;
  ASSERT_TRUE(leak.Allocate());
  EXPECT_TRUE(HasLeaks());
}

TEST_F(LeakSanitizerTest, Disable) {
  {
    // An allocation made after __lsan_disable() should not count.
    __lsan::ScopedDisabler disable;
    LeakedAllocation<int> leak;
    ASSERT_TRUE(leak.Allocate());
    EXPECT_FALSE(LsanDetectsLeaks());
  }

  // Make sure it's back to normal after __lsan_enable().
  {
    ASSERT_FALSE(LsanDetectsLeaks());
    LeakedAllocation<int> leak;
    ASSERT_TRUE(leak.Allocate());
    EXPECT_TRUE(HasLeaks());
  }
}

TEST_F(LeakSanitizerTest, IgnoreObject) {
  LeakedAllocation<int> leak;
  ASSERT_TRUE(leak.Allocate());
  // It counts as a leak now, but should not after this call.
  leak.CallWith([](int* ptr) { __lsan_ignore_object(ptr); });
  EXPECT_FALSE(LsanDetectsLeaks());
}

class ScopedRootRegionRegistration {
 public:
  ScopedRootRegionRegistration() = delete;
  ScopedRootRegionRegistration(const ScopedRootRegionRegistration&) = delete;
  ScopedRootRegionRegistration(const void* ptr, size_t size) : ptr_(ptr), size_(size) {
    __lsan_register_root_region(ptr_, size_);
  }
  ~ScopedRootRegionRegistration() { __lsan_unregister_root_region(ptr_, size_); }

 private:
  const void* ptr_;
  size_t size_;
};

class ScopedVmar {
 public:
  zx_status_t Allocate(size_t size) {
    return zx::vmar::root_self()->allocate(
        ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, /*offset=*/0, size,
        &vmar_, &address_);
  }

  const zx::vmar& get() const { return vmar_; }

  ~ScopedVmar() {
    if (vmar_) {
      EXPECT_OK(vmar_.destroy());
    }
  }

 private:
  zx::vmar vmar_;
  uintptr_t address_ = 0;
};

TEST_F(LeakSanitizerTest, RegisterRoot) {
  // This should be detected as a leak.
  LeakedAllocation<int> leak;
  ASSERT_TRUE(leak.Allocate());
  ASSERT_TRUE(HasLeaks());

  // Set up a VMAR with two special pages.
  // The first is allocated and the second is not.
  ScopedVmar vmar;
  ASSERT_OK(vmar.Allocate(ZX_PAGE_SIZE * 2));
  zx::vmo vmo;
  uintptr_t root_page;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmar.get().map(ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, /*vmar_offset=*/0,
                           vmo, /*vmo_offset=*/0, ZX_PAGE_SIZE, &root_page));
  const uintptr_t bad_page = root_page + ZX_PAGE_SIZE;

  // Make the root page contain the only pointer to the leaked item.
  leak.CallWith([root_page](int* ptr) { *reinterpret_cast<int**>(root_page) = ptr; });

  // That pointer should not be observed by LSan yet.
  ASSERT_TRUE(HasLeaks());

  // Now register both regions as LSan roots.
  // The good one should lead LSan to find the pointer.
  // The bad one should be detected and ignored by LSan.
  ScopedRootRegionRegistration good_root(reinterpret_cast<void*>(root_page), ZX_PAGE_SIZE);
  ScopedRootRegionRegistration bad_root(reinterpret_cast<void*>(bad_page), ZX_PAGE_SIZE);

  EXPECT_FALSE(HasLeaks());
}

class ThreadsForTest {
 public:
  // This must be called first via ASSERT_NO_FATAL_FAILURE.
  void Allocate() {
    for (auto& t : threads_) {
      ASSERT_TRUE(t.leak.Allocate());
    }
  }

  // This must follow Allocate and be called via ASSERT_NO_FATAL_FAILURE.
  template <typename F>
  void Launch(const F get_ready) {
    for (auto& t : threads_) {
      ASSERT_FALSE(t.launched);
      t.thread = std::thread(
          [&](Thread& self) {
            void* volatile stack_slot = nullptr;
            get_ready(self.leak, &stack_slot);
            std::unique_lock<std::mutex> lock(mutex_);
            self.ready = true;
            ready_.notify_all();
            finish_.wait(lock, [this]() { return time_to_die_; });
          },
          std::ref(t));
      t.launched = true;
    }

    // Wait for all the threads to be ready.
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this]() {
      return std::all_of(threads_.begin(), threads_.end(), [](Thread& t) { return t.ready; });
    });
  }

  // At the end, wake the threads up and wait for them to die.
  ~ThreadsForTest() {
    {
      std::lock_guard<std::mutex> locked(mutex_);
      time_to_die_ = true;
      finish_.notify_all();
    }
    for (auto& t : threads_) {
      if (t.launched) {
        t.thread.join();
      }
    }
  }

 private:
  static constexpr int kThreadCount_ = 10;
  struct Thread {
    LeakedAllocation<int> leak;
    std::thread thread;
    bool launched = false;
    bool ready = false;
  };
  std::array<Thread, kThreadCount_> threads_;
  std::mutex mutex_;
  std::condition_variable ready_, finish_;
  bool time_to_die_ = false;
};

TEST_F(LeakSanitizerTest, ThreadStackReference) {
  ThreadsForTest threads;

  ASSERT_NO_FATAL_FAILURE(threads.Allocate());

  ASSERT_NO_FATAL_FAILURE(
      threads.Launch([](const auto& leak, void* volatile* stack) { *stack = leak.get(); }));

  // Now those threads' stacks should be the only place holding those pointers.
  EXPECT_FALSE(LsanDetectsLeaks());
}
TEST_F(LeakSanitizerTest, TlsReference) {
  thread_local int* tls_reference = nullptr;

  {
    // Test the only reference being in TLS in the main thread.
    LeakedAllocation<int> leak;
    ASSERT_TRUE(leak.Allocate());
    auto cleanup = fit::defer([]() { tls_reference = nullptr; });
    leak.CallWith([](int* ptr) { tls_reference = ptr; });
    EXPECT_FALSE(LsanDetectsLeaks());
  }

  {
    ASSERT_FALSE(LsanDetectsLeaks());

    ThreadsForTest threads;

    ASSERT_NO_FATAL_FAILURE(threads.Allocate());

    ASSERT_NO_FATAL_FAILURE(threads.Launch([](const auto& leak, void* volatile*) {
      EXPECT_NULL(tls_reference);
      leak.CallWith([](int* ptr) { tls_reference = ptr; });
    }));

    // Now those threads' TLS should be the only place holding those pointers.
    EXPECT_FALSE(LsanDetectsLeaks());
  }
}

// This is the regression test for ensuring the issue described in fxbug.dev/66819 is fixed. The
// issue was that lsan would report leaks in libc++'s std::thread that weren't actual leaks. This
// was because it was possible for the newly spawned thread to be suspended before actually
// running any user code, meaning the memory snapshot would occur while the std::thread
// allocations were accessible via the new thread's pthread arguments, but not through the thread
// register. The fix ensures that the start_arg of all pthread structs are checked, so this should
// no longer leak.
//
// Below is a minimal reproducer for this issue. As a final test, to ensure this is fixed, we'll
// rerun the test a large number of times such that we have enough confidence the bug is fixed.
TEST_F(LeakSanitizerTest, LeakedThreadFix) {
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (!root_dir) {
    root_dir = "";
  }
  std::string file(root_dir);
  file += "/bin/lsan-thread-race-test";
  const char* argv[] = {file.c_str(), nullptr};

  // Before, it was almost guaranteed the issue would reporoduce a couple dozen times in 100 runs.
  // This takes roughly 2-3 seconds to run in an uninstrumented debug build on x64 and arm64.
  constexpr size_t kTestRuns = 100;
  for (size_t i = 0; i < kTestRuns; ++i) {
    zx::process child;
    ASSERT_OK(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                         child.reset_and_get_address()));

    zx_signals_t signals;
    ASSERT_OK(child.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), &signals));
    ASSERT_TRUE(signals & ZX_PROCESS_TERMINATED);

    zx_info_process_t info;
    ASSERT_OK(child.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr));

    EXPECT_EQ(info.return_code, 0, "Expected the thread race test to exit successfully");
  }
}

}  // namespace
