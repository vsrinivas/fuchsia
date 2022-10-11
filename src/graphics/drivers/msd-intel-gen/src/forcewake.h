// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FORCEWAKE_H
#define FORCEWAKE_H

#include <thread>

#include "magma_util/macros.h"
#include "magma_util/register_io.h"
#include "registers.h"

class ForceWake {
 public:
  static void reset(magma::RegisterIo* reg_io, registers::ForceWake::Domain domain) {
    registers::ForceWake::reset(reg_io, domain);
  }

  static void request(magma::RegisterIo* reg_io, registers::ForceWake::Domain domain) {
    if (registers::ForceWake::read_status(reg_io, domain) & (1 << kThreadShift))
      return;
    DLOG("forcewake request");
    registers::ForceWake::write(reg_io, domain, 1 << kThreadShift, 1 << kThreadShift);
    wait(reg_io, domain, true);
  }

  static void release(magma::RegisterIo* reg_io, registers::ForceWake::Domain domain) {
    if ((registers::ForceWake::read_status(reg_io, domain) & (1 << kThreadShift)) == 0)
      return;
    DLOG("forcewake release");
    registers::ForceWake::write(reg_io, domain, 1 << kThreadShift, 0);
    wait(reg_io, domain, false);
  }

  static constexpr uint32_t kThreadShift = 0;
  static constexpr uint32_t kRetryMaxMs = 3;

 private:
  static void wait(magma::RegisterIo* reg_io, registers::ForceWake::Domain domain, bool set) {
    uint32_t status;
    for (unsigned int ms = 0; ms < kRetryMaxMs; ms++) {
      status = registers::ForceWake::read_status(reg_io, domain);
      if (((status >> kThreadShift) & 1) == (set ? 1 : 0))
        return;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      DLOG("forcewake wait retrying");
    }
    DLOG("timed out waiting for forcewake, status 0x%x", status);
  }
};

#endif  // FORCEWAKE_H
