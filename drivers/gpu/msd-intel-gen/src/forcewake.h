// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FORCEWAKE_H
#define FORCEWAKE_H

#include "magma_util/macros.h"
#include "magma_util/sleep.h"
#include "register_io.h"
#include "registers.h"

class ForceWake {
public:
    static void reset(RegisterIo* reg_io) { registers::MultiForceWake::reset(reg_io); }

    static void request(RegisterIo* reg_io)
    {
        if (registers::MultiForceWake::read_status(reg_io) & (1 << kThreadShift))
            return;
        DLOG("forcewake request");
        registers::MultiForceWake::write(reg_io, 1 << kThreadShift, 1 << kThreadShift);
        wait(reg_io, true);
    }

    static void release(RegisterIo* reg_io)
    {
        if ((registers::MultiForceWake::read_status(reg_io) & (1 << kThreadShift)) == 0)
            return;
        DLOG("forcewake release");
        registers::MultiForceWake::write(reg_io, 1 << kThreadShift, 0);
        wait(reg_io, false);
    }

private:
    static void wait(RegisterIo* reg_io, bool set)
    {
        uint32_t status;
        for (unsigned int ms = 0; ms < kRetryMaxMs; ms++) {
            status = registers::MultiForceWake::read_status(reg_io);
            if (((status >> kThreadShift) & 1) == (set ? 1 : 0))
                return;
            magma::msleep(1);
            DLOG("forcewake wait retrying");
        }
        DLOG("timed out waiting for forcewake, status 0x%x", status);
    }

    static constexpr uint32_t kThreadShift = 0;
    static constexpr uint32_t kRetryMaxMs = 3;

    friend class TestForceWake;
};

#endif // FORCEWAKE_H
