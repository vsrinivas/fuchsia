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
void Alloca(perftest::RepeatState* state) {
  state->NextStep();

  {
    auto v = alloca(N);
    DoNotOptimize(v);
  }

  state->NextStep();
}

template <size_t N>
bool BenchmarkAllocaSameThread(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Cleanup");
  while (state->KeepRunning()) {
    Alloca<N>(state);
  }
  return true;
}

// A new thread has a new stack, which is more likely to need to expand.
template <size_t N>
bool BenchmarkAllocaNewThread(perftest::RepeatState* state) {
  state->DeclareStep("Setup");
  state->DeclareStep("Allocate");
  state->DeclareStep("Cleanup");
  while (state->KeepRunning()) {
    std::thread th(Alloca<N>, state);
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

void RegisterTests() {
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/Allocate/16", BenchmarkHeapAllocate<16>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/Allocate/256", BenchmarkHeapAllocate<256>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/Allocate/4096", BenchmarkHeapAllocate<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/Allocate/65536",
                         BenchmarkHeapAllocate<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/AllocateZero/16",
                         BenchmarkHeapAllocateZero<16>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/AllocateZero/256",
                         BenchmarkHeapAllocateZero<256>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/AllocateZero/4096",
                         BenchmarkHeapAllocateZero<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/Heap/AllocateZero/65536",
                         BenchmarkHeapAllocateZero<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/16",
                         BenchmarkAllocaSameThread<16>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/256",
                         BenchmarkAllocaSameThread<256>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/4096",
                         BenchmarkAllocaSameThread<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/SameThread/65536",
                         BenchmarkAllocaSameThread<65536>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/16",
                         BenchmarkAllocaNewThread<16>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/256",
                         BenchmarkAllocaNewThread<256>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/4096",
                         BenchmarkAllocaNewThread<4096>);
  perftest::RegisterTest("CPP/AllocationStrategy/Alloca/NewThread/65536",
                         BenchmarkAllocaNewThread<65536>);
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
}
PERFTEST_CTOR(RegisterTests)

}  // namespace
