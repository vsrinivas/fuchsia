// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <perftest/perftest.h>

namespace {

template <typename T>
void DoNotOptimize(const T& var) {
  // The compiler will avoid optimizations that entirely remove the values
  // passed to an asm statement.
  asm volatile("" : "+m"(const_cast<T&>(var)));
}

template <size_t N>
bool BenchmarkHeapAllocate(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    auto allocation = new uint8_t[N];
    DoNotOptimize(allocation);
    delete[] allocation;
  }
  return true;
}

template <size_t N>
bool BenchmarkHeapAllocateZero(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    auto allocation = new uint8_t[N]{};
    DoNotOptimize(allocation);
    delete[] allocation;
  }
  return true;
}

template <size_t N>
bool BenchmarkHeapAllocateTouchPages(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    auto allocation = new uint8_t[N]{};
    DoNotOptimize(allocation);
    for (size_t i = 0; i < N; i += 4096) {
      reinterpret_cast<uint8_t*>(allocation)[i] = 0;
    }
    delete[] allocation;
  }
  return true;
}

template <size_t N, bool TouchPages>
void Alloca(perftest::RepeatState* state) {
  state->NextStep();

  {
    auto v = alloca(N);
    DoNotOptimize(v);
    if (TouchPages) {
      for (size_t i = 0; i < N; i += 4096) {
        reinterpret_cast<uint8_t*>(v)[i] = 0;
      }
    }
  }

  state->NextStep();
}

template <size_t N, bool TouchPages>
bool BenchmarkAllocaSameThread(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Teardown");
  while (state->KeepRunning()) {
    Alloca<N, TouchPages>(state);
  }
  return true;
}

// A new thread has a new stack, which is more likely to need to expand.
template <size_t N, bool TouchPages>
bool BenchmarkAllocaNewThread(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Teardown");
  while (state->KeepRunning()) {
    std::thread th(Alloca<N, TouchPages>, state);
    th.join();
  }
  return true;
}

template <size_t N>
bool BenchmarkVectorInitialAllocation(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    std::vector<uint8_t> vec(N);
    DoNotOptimize(vec);
  }
  return true;
}

template <size_t Initial, size_t Final>
bool BenchmarkVectorResize(perftest::RepeatState* state) {
  while (state->KeepRunning()) {
    std::vector<uint8_t> vec(Initial);
    vec.resize(Final);
    DoNotOptimize(vec);
  }
  return true;
}

uint8_t* AllocateThreadLocalStorage() {
  thread_local uint8_t buf[65336];
  return buf;
}

uint8_t* AllocateThreadLocalStorageHeap() {
  thread_local uint8_t* buf = nullptr;
  if (buf == nullptr) {
    buf = new uint8_t[65536];
  }
  return buf;
}

template <size_t NTouchPages>
void AllocateThreadLocalStorageWrapper(perftest::RepeatState* state, uint8_t* (*allocator)()) {
  state->NextStep();

  uint8_t* buffer = allocator();
  DoNotOptimize(buffer);
  for (size_t i = 0; i < NTouchPages; i += 4096) {
    reinterpret_cast<uint8_t*>(buffer)[i] = 0;
  }

  state->NextStep();
}

template <size_t NTouchPages>
bool BenchmarkThreadLocalStorageSameThread(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Teardown");
  while (state->KeepRunning()) {
    AllocateThreadLocalStorageWrapper<NTouchPages>(state, AllocateThreadLocalStorage);
  }
  return true;
}

// A new thread has a new stack, which should need to create new TLS.
template <size_t NTouchPages>
bool BenchmarkThreadLocalStorageNewThread(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Teardown");
  while (state->KeepRunning()) {
    std::thread th(AllocateThreadLocalStorageWrapper<NTouchPages>, state,
                   AllocateThreadLocalStorage);
    th.join();
  }
  return true;
}

template <size_t NTouchPages>
bool BenchmarkThreadLocalStorageHeapSameThread(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Teardown");
  while (state->KeepRunning()) {
    AllocateThreadLocalStorageWrapper<NTouchPages>(state, AllocateThreadLocalStorageHeap);
  }
  return true;
}

// A new thread has a new stack, which should need to create new TLS.
template <size_t NTouchPages>
bool BenchmarkThreadLocalStorageHeapNewThread(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Teardown");
  while (state->KeepRunning()) {
    std::thread th(AllocateThreadLocalStorageWrapper<NTouchPages>, state,
                   AllocateThreadLocalStorageHeap);
    th.join();
  }
  return true;
}

template <typename PoolType>
class PoolBuffer {
 public:
  PoolBuffer(PoolType* pool, std::unique_ptr<uint8_t[]> buffer)
      : pool_(pool), buffer_(std::move(buffer)) {}
  ~PoolBuffer() { pool_->put(std::move(buffer_)); }
  uint8_t* get() { return buffer_.get(); }

 private:
  PoolType* pool_;
  std::unique_ptr<uint8_t[]> buffer_;
};

class LockedBufferPool {
 public:
  PoolBuffer<LockedBufferPool> acquire() {
    mu_.lock();
    std::unique_ptr<uint8_t[]> buffer;
    if (!buffers_.empty()) {
      buffer = std::move(buffers_.back());
      buffers_.pop_back();
    } else {
      buffer = std::unique_ptr<uint8_t[]>(new uint8_t[65536]);
    }
    mu_.unlock();
    return PoolBuffer<LockedBufferPool>(this, std::move(buffer));
  }
  void put(std::unique_ptr<uint8_t[]> buffer) {
    mu_.lock();
    buffers_.emplace_back(std::move(buffer));
    mu_.unlock();
  }

 private:
  std::vector<std::unique_ptr<uint8_t[]>> buffers_;
  std::mutex mu_;
};

// Reuse a fixed number of buffers by storing them with atomics in an array.
// Search for available buffer with linear search.
// If the array is full, fall back to allocating / deallocating a new buffer.
template <size_t PoolSize>
class FixedAtomicSwapBufferPool {
 public:
  ~FixedAtomicSwapBufferPool() {
    for (size_t i = 0; i < PoolSize; i++) {
      uint8_t* buffer = buffers_[i].exchange(nullptr, std::memory_order_relaxed);
      delete[] buffer;
    }
  }
  PoolBuffer<FixedAtomicSwapBufferPool<PoolSize>> acquire() {
    // Potential optimization: scan buffers before using exchange to find ones worth testing.
    for (size_t i = 0; i < PoolSize; i++) {
      uint8_t* buffer = buffers_[i].exchange(nullptr, std::memory_order_relaxed);
      if (buffer != nullptr) {
        return PoolBuffer<FixedAtomicSwapBufferPool<PoolSize>>(this,
                                                               std::unique_ptr<uint8_t[]>(buffer));
      }
    }
    return PoolBuffer<FixedAtomicSwapBufferPool<PoolSize>>(
        this, std::unique_ptr<uint8_t[]>(new uint8_t[65536]));
  }

  void put(std::unique_ptr<uint8_t[]> buffer) {
    uint8_t* buf = buffer.release();
    for (size_t i = 0; i < PoolSize; i++) {
      uint8_t* expected = nullptr;
      if (buffers_[i].compare_exchange_weak(expected, buf, std::memory_order_relaxed)) {
        return;
      }
    }
    delete[] buf;
  }

 private:
  std::atomic<uint8_t*> buffers_[PoolSize] = {};
};

template <typename PoolType, size_t NOtherThreads>
bool BenchmarkPool(perftest::RepeatState* state) {
  PoolType pool;
  std::atomic_flag stop_threads;
  std::thread other_threads[NOtherThreads];
  for (size_t i = 0; i < NOtherThreads; i++) {
    other_threads[i] = std::thread([&pool, &stop_threads]() {
      while (!stop_threads.test()) {
        pool.acquire();
      }
    });
  }
  while (state->KeepRunning()) {
    pool.acquire();
  }
  stop_threads.test_and_set();
  for (size_t i = 0; i < NOtherThreads; i++) {
    other_threads[i].join();
  }
  return true;
}

template <typename PoolType, bool TouchPages>
bool BenchmarkPoolFirstUse(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Teardown");

  while (state->KeepRunning()) {
    PoolType pool;
    state->NextStep();
    auto buffer = pool.acquire();
    if (TouchPages) {
      for (size_t i = 0; i < 65536; i += 4096) {
        buffer.get()[i] = 0;
      }
    }
    state->NextStep();
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/16", BenchmarkHeapAllocate<16>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/256", BenchmarkHeapAllocate<256>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/4096", BenchmarkHeapAllocate<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/65536", BenchmarkHeapAllocate<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/eZero/16", BenchmarkHeapAllocateZero<16>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/Zero/256", BenchmarkHeapAllocateZero<256>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/Zero/4096", BenchmarkHeapAllocateZero<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/Zero/65536",
                         BenchmarkHeapAllocateZero<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/TouchPages/16",
                         BenchmarkHeapAllocateTouchPages<16>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/TouchPages/256",
                         BenchmarkHeapAllocateTouchPages<256>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/TouchPages/4096",
                         BenchmarkHeapAllocateTouchPages<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/TouchPages/65536",
                         BenchmarkHeapAllocateTouchPages<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/16",
                         BenchmarkAllocaSameThread<16, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/256",
                         BenchmarkAllocaSameThread<256, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/4096",
                         BenchmarkAllocaSameThread<4096, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/65536",
                         BenchmarkAllocaSameThread<65536, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/TouchPages/16",
                         BenchmarkAllocaSameThread<16, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/TouchPages/256",
                         BenchmarkAllocaSameThread<256, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/TouchPages/4096",
                         BenchmarkAllocaSameThread<4096, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/TouchPages/65536",
                         BenchmarkAllocaSameThread<65536, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/16",
                         BenchmarkAllocaNewThread<16, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/256",
                         BenchmarkAllocaNewThread<256, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/4096",
                         BenchmarkAllocaNewThread<4096, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/65536",
                         BenchmarkAllocaNewThread<65536, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/TouchPages/16",
                         BenchmarkAllocaNewThread<16, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/TouchPages/256",
                         BenchmarkAllocaNewThread<256, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/TouchPages/4096",
                         BenchmarkAllocaNewThread<4096, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/TouchPages/65536",
                         BenchmarkAllocaNewThread<65536, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorage/SameThread",
                         BenchmarkThreadLocalStorageSameThread<0>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorage/NewThread",
                         BenchmarkThreadLocalStorageNewThread<0>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorage/SameThread/TouchPages/4096",
                         BenchmarkThreadLocalStorageSameThread<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorage/NewThread/TouchPages/4096",
                         BenchmarkThreadLocalStorageNewThread<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorage/SameThread/TouchPages/65536",
                         BenchmarkThreadLocalStorageSameThread<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorage/NewThread/TouchPages/65536",
                         BenchmarkThreadLocalStorageNewThread<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorageHeap/SameThread",
                         BenchmarkThreadLocalStorageHeapSameThread<0>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorageHeap/NewThread",
                         BenchmarkThreadLocalStorageHeapNewThread<0>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorageHeap/SameThread/TouchPages/4096",
                         BenchmarkThreadLocalStorageHeapSameThread<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorageHeap/NewThread/TouchPages/4096",
                         BenchmarkThreadLocalStorageHeapNewThread<4096>);
  perftest::RegisterTest(
      "CPP/AllocationStrategy/ThreadLocalStorageHeap/SameThread/TouchPages/65536",
      BenchmarkThreadLocalStorageHeapSameThread<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/ThreadLocalStorageHeap/NewThread/TouchPages/65536",
                         BenchmarkThreadLocalStorageHeapNewThread<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialAllocation/16",
                         BenchmarkVectorInitialAllocation<16>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialAllocation/256",
                         BenchmarkVectorInitialAllocation<256>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialAllocation/4096",
                         BenchmarkVectorInitialAllocation<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialAllocation/65536",
                         BenchmarkVectorInitialAllocation<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialThenResize/16_to_256",
                         BenchmarkVectorResize<16, 256>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialThenResize/16_to_4096",
                         BenchmarkVectorResize<16, 4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialThenResize/16_to_65536",
                         BenchmarkVectorResize<16, 65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialThenResize/256_to_4096",
                         BenchmarkVectorResize<256, 4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialThenResize/256_to_65536",
                         BenchmarkVectorResize<256, 65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/StdVector/InitialThenResize/4096_to_65536",
                         BenchmarkVectorResize<4096, 65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/LockedBufferPool/1Thread",
                         BenchmarkPool<LockedBufferPool, 0>);
  perftest::RegisterTest("CPP/AllocationStrategy/LockedBufferPool/4Thread",
                         BenchmarkPool<LockedBufferPool, 3>);
  perftest::RegisterTest("CPP/AllocationStrategy/LockedBufferPool/16Thread",
                         BenchmarkPool<LockedBufferPool, 15>);
  perftest::RegisterTest("CPP/AllocationStrategy/LockedBufferPool/64Thread",
                         BenchmarkPool<LockedBufferPool, 63>);
  perftest::RegisterTest("CPP/AllocationStrategy/LockedBufferPool/FirstUse",
                         BenchmarkPoolFirstUse<LockedBufferPool, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/LockedBufferPool/FirstUse/TouchPages",
                         BenchmarkPoolFirstUse<LockedBufferPool, true>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/1Buffer/1Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<1>, 0>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/1Buffer/4Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<1>, 3>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/1Buffer/16Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<1>, 15>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/1Buffer/64Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<1>, 63>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/4Buffer/1Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<4>, 0>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/4Buffer/4Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<4>, 3>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/4Buffer/16Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<4>, 15>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/4Buffer/64Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<4>, 63>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/16Buffer/1Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<16>, 0>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/16Buffer/4Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<16>, 3>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/16Buffer/16Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<16>, 15>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/16Buffer/64Thread",
                         BenchmarkPool<FixedAtomicSwapBufferPool<16>, 63>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/1Buffer/FirstUse",
                         BenchmarkPoolFirstUse<FixedAtomicSwapBufferPool<1>, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/4Buffer/FirstUse",
                         BenchmarkPoolFirstUse<FixedAtomicSwapBufferPool<4>, false>);
  perftest::RegisterTest("CPP/AllocationStrategy/FixedAtomicSwapBufferPool/16Buffer/FirstUse",
                         BenchmarkPoolFirstUse<FixedAtomicSwapBufferPool<16>, false>);
  perftest::RegisterTest(
      "CPP/AllocationStrategy/FixedAtomicSwapBufferPool/1Buffer/FirstUse/TouchPages",
      BenchmarkPoolFirstUse<FixedAtomicSwapBufferPool<1>, true>);
  perftest::RegisterTest(
      "CPP/AllocationStrategy/FixedAtomicSwapBufferPool/4Buffer/FirstUse/TouchPages",
      BenchmarkPoolFirstUse<FixedAtomicSwapBufferPool<4>, true>);
  perftest::RegisterTest(
      "CPP/AllocationStrategy/FixedAtomicSwapBufferPool/16Buffer/FirstUse/TouchPages",
      BenchmarkPoolFirstUse<FixedAtomicSwapBufferPool<16>, true>);
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
