// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_KSTRESS_STRESS_TEST_H_
#define SRC_ZIRCON_BIN_KSTRESS_STRESS_TEST_H_

#include <lib/zx/resource.h>
#include <stdarg.h>
#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <memory>
#include <random>

#include <fbl/macros.h>
#include <fbl/vector.h>

class StressTest {
 public:
  StressTest() {
    tests_.push_back(this);
    // Use hardware entropy (hopefully) to seed up our initial random generator that we will use to
    // produce all other generators.
    std::random_device rd;
    std::seed_seq seed{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
    rand_gen_.seed(seed);
  }

  virtual ~StressTest() = default;

  DISALLOW_COPY_ASSIGN_AND_MOVE(StressTest);

  // Called once before starting the test. Allocate resources needed for
  // the test here.
  //
  // If overridden in a subclass, call through to this version first.
  virtual zx_status_t Init(bool verbose, const zx_info_kmem_stats& stats,
                           zx::unowned_resource root_resource) {
    verbose_ = verbose;

    // gather some info about the system
    kmem_stats_ = stats;
    num_cpus_ = zx_system_get_num_cpus();
    root_resource_ = root_resource;
    return ZX_OK;
  }

  // Called once to start the test. Must return immediately.
  virtual zx_status_t Start() = 0;

  // Called to stop the individual test. Must wait until test has
  // been shut down.
  virtual zx_status_t Stop() = 0;

  // Return the name of the test in C string format
  virtual const char* name() const = 0;

  // get a ref to the master test list
  static fbl::Vector<StressTest*>& tests() { return tests_; }

  // wrapper around printf that enables/disables based on verbose flag
  void Printf(const char* fmt, ...) const {
    if (!verbose_) {
      return;
    }

    va_list ap;
    va_start(ap, fmt);

    vprintf(fmt, ap);

    va_end(ap);
  }

  void PrintfAlways(const char* fmt, ...) const {
    va_list ap;
    va_start(ap, fmt);

    vprintf(fmt, ap);

    va_end(ap);
  }

  using Rng = std::mt19937_64;
  Rng RngGen() {
    // Seed a new random generator from our initially seeded one.
    Rng rng;
    std::seed_seq seed{rand_gen_(), rand_gen_(), rand_gen_(), rand_gen_(),
                       rand_gen_(), rand_gen_(), rand_gen_(), rand_gen_()};
    rng.seed(seed);
    return rng;
  }

 protected:
  // global list of all the stress tests, registered at app start
  static fbl::Vector<StressTest*> tests_;

  Rng rand_gen_;
  bool verbose_{false};
  zx_info_kmem_stats_t kmem_stats_{};
  uint32_t num_cpus_{};
  // Optional root resource.
  zx::unowned_resource root_resource_;
};

// factories for local tests
std::unique_ptr<StressTest> CreateVmStressTest();

#endif  // SRC_ZIRCON_BIN_KSTRESS_STRESS_TEST_H_
