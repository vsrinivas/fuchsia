// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_workloads.h"

#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cfloat>
#include <random>
#include <vector>

#include "compiler.h"
#include "cpu_stressor.h"
#include "util.h"

namespace hwstress {
namespace {

// Assert that the given condition is true.
//
// The error string should express the error.
//
// We use a macro to avoid having to evaluate the arguments in the
// (expected case) that the condition is true.
#define AssertThat(condition, ...)                                    \
  do {                                                                \
    if (unlikely(!(condition))) {                                     \
      fprintf(stderr,                                                 \
              "\n"                                                    \
              "*** FAILURE ***\n"                                     \
              "\n"                                                    \
              "CPU calculation failed:\n\n");                         \
      fprintf(stderr, __VA_ARGS__);                                   \
      fprintf(stderr,                                                 \
              "\n"                                                    \
              "This failure may indicate faulty hardware.\n\n"        \
              "\n");                                                  \
      fflush(stderr);                                                 \
      ZX_PANIC("CPU calculation failed: possible hardware fault.\n"); \
    }                                                                 \
  } while (false)

// Assert that the given values are equal, within an |epsilon| error.
void AssertEqual(double expected, double actual, double epsilon = 0.0) {
  AssertThat(std::abs(expected - actual) <= epsilon,
             "      Expected: %.17g (%s)\n"
             "        Actual: %.17g (%s)\n"
             "    Difference: %.17g > %.17g (***)\n",
             expected, DoubleAsHex(expected).c_str(), actual, DoubleAsHex(actual).c_str(),
             std::abs(expected - actual), epsilon);
}

// Assert that the given uint64_t's are equal.
void AssertEqual(uint64_t expected, uint64_t actual) {
  AssertThat(expected == actual,
             "      Expected: %20ld (%#016lx)\n"
             "        Actual: %20ld (%#016lx)\n",
             expected, expected, actual, actual);
}

//
// The actual workloads.
//

// |memset| a small amount of memory.
//
// |memset| tends to be highly optimised for using all available memory
// bandwidth. We use a small enough buffer size to avoid spilling out
// of L1 cache.
void MemsetWorkload(const StopIndicator& indicator) {
  constexpr int kBufferSize = 8192;
  auto memory = std::make_unique<uint8_t[]>(kBufferSize);
  do {
    memset(memory.get(), 0xaa, kBufferSize);
    HideMemoryFromCompiler(memory.get());
    memset(memory.get(), 0x55, kBufferSize);
    HideMemoryFromCompiler(memory.get());
  } while (!indicator.ShouldStop());
}

// Calculate the trigonometric identity sin(x)**2 + cos(x)**2 == 1
// in a tight loop.
//
// Exercises floating point operations on the CPU, though mostly within
// the |sin| and |cos| functions.
void SinCosWorkload(const StopIndicator& indicator) {
  do {
    constexpr int kIterations = 10000;
    double result = 0;

    UNROLL_LOOP_4
    for (int i = 0; i < kIterations; i++) {
      // Calculate "sin(x)**2 + cos(x)**2", which is always "1.0". Hide
      // the input from the compiler to prevent it pre-calculating
      // anything.
      double input = HideFromCompiler(i);
      double a = sin(input);
      double b = cos(input);
      result += a * a + b * b;
    }

    AssertEqual(kIterations, result, DBL_EPSILON * kIterations);
  } while (!indicator.ShouldStop());
}

// Calculate the n'th Fibonacci number using inefficient recursion.
uint64_t Fibonacci(uint64_t n) {
  if (n <= 1)
    return n;
  return Fibonacci(n - 1) + Fibonacci(n - 2);
}

// Calculate the Fibonacci sequence using recursion.
//
// Exercises call/return control flow.
void FibonacciWorkload(const StopIndicator& indicator) {
  do {
    uint64_t result = Fibonacci(HideFromCompiler(30));
    AssertEqual(832040, result);
  } while (!indicator.ShouldStop());
}

// Perform a 16*16 matrix multiplication using floats.
//
// Exercises floating point operations.
void MatrixMultiplicationWorkload(const StopIndicator& indicator) {
  constexpr int kSize = 16;
  struct Matrix {
    float m[kSize][kSize];
  };

  // Create a matrix that permutes the input matrix columns.
  Matrix p = {};
  for (int i = 0; i < kSize; i++) {
    p.m[i][kSize - i - 1] = 1.0;
  }

  // Create an initial random matrix.
  std::mt19937_64 generator{};
  std::uniform_real_distribution<> dist(-1.0, 1.0);
  Matrix initial = {};
  for (int x = 0; x < kSize; x++) {
    for (int y = 0; y < kSize; y++) {
      initial.m[x][y] = dist(generator);
    }
  }

  do {
    // Multiply it 1000 times.
    Matrix active = initial;
    for (int n = 0; n < 1'000; n++) {
      // Naïve matrix multiplication algorithm.
      Matrix prev = active;
      for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
          float r = 0;
          for (int i = 0; i < kSize; i++) {
            r += prev.m[i][y] * p.m[x][i];
          }
          active.m[x][y] = r;
        }
      }
    }

    // Ensure the final result matches our initial matrix.
    for (int x = 0; x < kSize; x++) {
      for (int y = 0; y < kSize; y++) {
        AssertEqual(active.m[x][y], initial.m[x][y], /*epsilon=*/0.0);
      }
    }
  } while (!indicator.ShouldStop());
}

// Calculate the square root of "v" using Newton's Method [1].
//
// [1]: https://en.wikipedia.org/wiki/Newton%27s_method
inline double NewtonsMethodSqrt(double f) {
  double v = f;
  for (int n = 0; n < 32; n++) {
    v = v - ((v * v) - f) / (2.0 * v);
  }
  return v;
}

// Manually calculate square roots on random numbers, using Newton's method.
//
// Exercises floating point.
void NewtonsMethodWorkload(const StopIndicator& indicator) {
  do {
    std::mt19937_64 generator{};
    std::uniform_real_distribution<> dist(1.0, 2'000'000'000);

    for (int i = 0; i < 10'000; i++) {
      // Calculate the square root.
      double target = dist(generator);
      double v = NewtonsMethodSqrt(target);

      // We don't have any guarantee that we can get an exact square
      // root. Ensure that we got a pretty good value: in particular,
      // make sure that (v - epsilon)**2 < target < (v + epsilon)**2.
      double v_plus_epsilon = std::nextafter(v, std::numeric_limits<double>::infinity());
      double v_minus_epsilon = std::nextafter(v, -std::numeric_limits<double>::infinity());
      AssertThat(
          v_minus_epsilon * v_minus_epsilon < target && target < v_plus_epsilon * v_plus_epsilon,
          "Attempted to calculate v = sqrt(target):\n"
          "\n"
          "  target        :  %.19g\n"
          "  v²            :  %.19g\n"
          "\n"
          "  sqrt(target)  :  %.19g\n"
          "  v             :  %.19g\n"
          "\n"
          "  Expected that:\n"
          "      (v - ε)²  :  %.19g\n"
          "   <  target    :  %.19g\n"
          "   <  (v + ε)²  :  %.19g\n",
          std::sqrt(target), v, target, v * v, v_minus_epsilon * v_minus_epsilon, target,
          v_plus_epsilon * v_plus_epsilon);
    }
  } while (!indicator.ShouldStop());
}

// Run the Mersenne Twister random number generator algorithm.
//
// This exercises integer bitwise operation and multiplication.
void MersenneTwisterWorkload(const StopIndicator& indicator) {
  do {
    std::mt19937_64 generator{};

    // Iterate the generator.
    uint64_t v;
    UNROLL_LOOP_4
    for (int i = 0; i < 10'000; i++) {
      v = generator();
    }

    // The C++11 standard states that the 10,000th consecutive
    // invocation of the mt19937_64 should be the following value.
    AssertEqual(v, 0x8a85'92f5'817e'd872);
  } while (!indicator.ShouldStop());
}

}  // namespace

std::vector<Workload> GetWorkloads() {
  return std::vector<Workload>{
      {"fibonacci", FibonacciWorkload},  {"matrix", MatrixMultiplicationWorkload},
      {"memset", MemsetWorkload},        {"mersenne", MersenneTwisterWorkload},
      {"newton", NewtonsMethodWorkload}, {"trigonometry", SinCosWorkload}};
}

}  // namespace hwstress
