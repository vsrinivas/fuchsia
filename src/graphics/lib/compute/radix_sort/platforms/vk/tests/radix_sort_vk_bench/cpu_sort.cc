// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// We need a stable sort
//

#include <chrono>
#include <cstdint>

//
// Multithreaded/vector or single-threaded?
//
#include <algorithm>

//
// Multithreaded/vector
//

#if defined(__cpp_lib_parallel_algorithm) && !defined(CPU_USE_STD_STABLE_SORT)

#define CPU_USE_PARALLEL_SORT
#undef CPU_USE_STD_STABLE_SORT
#include <execution>

//
// Single-threaded
//
#else  // CPU_USE_STD_STABLE_SORT

#define CPU_USE_STD_STABLE_SORT
#undef CPU_USE_PARALLEL_SORT

#endif

//
//
//

extern "C" char const *
cpu_sort_u32(uint32_t * a, uint32_t count, double * cpu_ns)
{
  using to_ns = std::chrono::duration<double, std::chrono::nanoseconds::period>;

  auto start = std::chrono::high_resolution_clock::now();

#if defined(CPU_USE_PARALLEL_SORT)
  std::stable_sort(std::execution::par_unseq, a, a + count);
  char const * const algo = "std::stable_sort(std::execution::par_unseq)()";
#elif defined(CPU_USE_STD_STABLE_SORT)
  std::stable_sort(a, a + count);
  char const * const algo = "std:stable_sort()";
#endif

  auto stop        = std::chrono::high_resolution_clock::now();
  auto duration_ns = to_ns(stop - start);

  *cpu_ns = duration_ns.count();

  return algo;
}

extern "C" char const *
cpu_sort_u64(uint64_t * a, uint32_t count, double * cpu_ns)
{
  using to_ns = std::chrono::duration<double, std::chrono::nanoseconds::period>;

  auto start = std::chrono::high_resolution_clock::now();

#if defined(CPU_USE_PARALLEL_SORT)
  std::stable_sort(std::execution::par_unseq, a, a + count);
  char const * const algo = "std::stable_sort(std::execution::par_unseq)()";
#elif defined(CPU_USE_STD_STABLE_SORT)
  std::stable_sort(a, a + count);
  char const * const algo = "std::stable_sort()";
#endif

  auto stop        = std::chrono::high_resolution_clock::now();
  auto duration_ns = to_ns(stop - start);

  *cpu_ns = duration_ns.count();

  return algo;
}

//
//
//
