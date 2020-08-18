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
#define AssertThat(workload, condition, ...)                          \
  do {                                                                \
    if (unlikely(!(condition))) {                                     \
      fprintf(stderr,                                                 \
              "\n"                                                    \
              "*** FAILURE ***\n"                                     \
              "\n"                                                    \
              "Workload '%s' CPU calculation failed:\n\n",            \
              workload);                                              \
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
void AssertEqual(const char* workload, double expected, double actual, double epsilon = 0.0) {
  AssertThat(workload, std::abs(expected - actual) <= epsilon,
             "      Expected: %.17g (%s)\n"
             "        Actual: %.17g (%s)\n"
             "    Difference: %.17g > %.17g (***)\n",
             expected, DoubleAsHex(expected).c_str(), actual, DoubleAsHex(actual).c_str(),
             std::abs(expected - actual), epsilon);
}

// Assert that the given uint64_t's are equal.
void AssertEqual(const char* workload, uint64_t expected, uint64_t actual) {
  AssertThat(workload, expected == actual,
             "      Expected: %20ld (%#016lx)\n"
             "        Actual: %20ld (%#016lx)\n",
             expected, expected, actual, actual);
}

// Workload interface.
class Workload {
 public:
  virtual ~Workload() = default;
  virtual void DoWork() = 0;
};

//
// The actual workloads.
//

// |memset| a small amount of memory.
//
// |memset| tends to be highly optimised for using all available memory
// bandwidth. We use a small enough buffer size to avoid spilling out
// of L1 cache.
class MemsetWorkload final : public Workload {
 public:
  MemsetWorkload() : memory_(std::make_unique<uint8_t[]>(kBufferSize)) {}

  void DoWork() final {
    memset(memory_.get(), 0xaa, kBufferSize);
    HideMemoryFromCompiler(memory_.get());
    memset(memory_.get(), 0x55, kBufferSize);
    HideMemoryFromCompiler(memory_.get());
  }

 private:
  std::unique_ptr<uint8_t[]> memory_;
  static constexpr int kBufferSize = 8192;
};

// Calculate the trigonometric identity sin(x)**2 + cos(x)**2 == 1
// in a tight loop.
//
// Exercises floating point operations on the CPU, though mostly within
// the |sin| and |cos| functions.
class SinCosWorkload final : public Workload {
 public:
  void DoWork() final {
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

    AssertEqual("trigonometry", kIterations, result, DBL_EPSILON * kIterations);
  }
};

// Calculate the n'th Fibonacci number using inefficient recursion.
uint64_t Fibonacci(uint64_t n) {
  if (n <= 1)
    return n;
  return Fibonacci(n - 1) + Fibonacci(n - 2);
}

// Calculate the Fibonacci sequence using recursion.
//
// Exercises call/return control flow.
class FibonacciWorkload final : public Workload {
 public:
  void DoWork() final {
    uint64_t result = Fibonacci(HideFromCompiler(30));
    AssertEqual("fibonacci", 832040, result);
  }
};

// Perform a 16*16 matrix multiplication using floats.
//
// Exercises floating point operations.
class MatrixMultiplicationWorkload final : public Workload {
 public:
  MatrixMultiplicationWorkload() {
    // Create a permutation matrix.
    for (int i = 0; i < kSize; i++) {
      permutation_.m[i][kSize - i - 1] = 1.0;
    }

    // Create a random matrix.
    std::mt19937_64 generator{};
    std::uniform_real_distribution<> dist(-1.0, 1.0);
    for (int x = 0; x < kSize; x++) {
      for (int y = 0; y < kSize; y++) {
        random_.m[x][y] = dist(generator);
      }
    }
  }

  void DoWork() final {
    // Multiply it 1000 times.
    Matrix active = random_;
    for (int n = 0; n < 1'000; n++) {
      // Naïve matrix multiplication algorithm.
      Matrix prev = active;
      for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
          float r = 0;
          for (int i = 0; i < kSize; i++) {
            r += prev.m[i][y] * permutation_.m[x][i];
          }
          active.m[x][y] = r;
        }
      }
    }

    // Ensure the final result matches our random_ matrix.
    for (int x = 0; x < kSize; x++) {
      for (int y = 0; y < kSize; y++) {
        AssertEqual("matrix", active.m[x][y], random_.m[x][y], /*epsilon=*/0.0);
      }
    }
  }

 private:
  static constexpr int kSize = 16;
  struct Matrix {
    float m[kSize][kSize];
  };
  Matrix permutation_{};
  Matrix random_{};
};

// Run the Mersenne Twister random number generator algorithm.
//
// This exercises integer bitwise operation and multiplication.
class MersenneTwisterWorkload final : public Workload {
 public:
  void DoWork() final {
    std::mt19937_64 generator{};

    // Iterate the generator.
    uint64_t v;
    UNROLL_LOOP_4
    for (int i = 0; i < 10'000; i++) {
      v = generator();
    }

    // The C++11 standard states that the 10,000th consecutive
    // invocation of the mt19937_64 should be the following value.
    AssertEqual("mersenne", v, 0x8a85'92f5'817e'd872);
  }
};

// Run a mixed of the other workloads.
class MixedWorkload final : public Workload {
 public:
  void DoWork() final {
    fibonacci_.DoWork();
    matrix_.DoWork();
    memset_.DoWork();
    mersenee_.DoWork();
    trigonometry_.DoWork();
  }

 private:
  FibonacciWorkload fibonacci_;
  MatrixMultiplicationWorkload matrix_;
  MemsetWorkload memset_;
  MersenneTwisterWorkload mersenee_;
  SinCosWorkload trigonometry_;
};

// Convert the given Workload into a CpuWorkload.
template <typename W>
auto IterateWorkload() -> auto {
  return [](WorkIndicator indicator) {
    W workload{};
    do {
      workload.DoWork();
    } while (!indicator.ShouldStop());
  };
}

}  // namespace

std::vector<CpuWorkload> GetCpuWorkloads() {
  return std::vector<CpuWorkload>{{"fibonacci", IterateWorkload<FibonacciWorkload>()},
                                  {"matrix", IterateWorkload<MatrixMultiplicationWorkload>()},
                                  {"memset", IterateWorkload<MemsetWorkload>()},
                                  {"mersenne", IterateWorkload<MersenneTwisterWorkload>()},
                                  {"trigonometry", IterateWorkload<SinCosWorkload>()},
                                  {"mixed", IterateWorkload<MixedWorkload>()}};
}

}  // namespace hwstress
