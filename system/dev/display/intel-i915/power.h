// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>

#include "registers-pipe.h"
#include "registers-ddi.h"

namespace i915 {

class Power;
class Controller;

enum PowerWell {
    PowerWell1 = 0,
    PowerWell2 = 1,
};

class PowerWellRef {
public:
    PowerWellRef();
    ~PowerWellRef();
    PowerWellRef(PowerWellRef&& o);
    PowerWellRef& operator=(PowerWellRef&& o);

private:
    PowerWellRef(Power* power, PowerWell power_well);

    Power* power_;
    PowerWell power_well_;

    friend Power;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(PowerWellRef);
};

class Power {
public:
    explicit Power(Controller* controller);
    void Resume();
    PowerWellRef GetCdClockPowerWellRef();
    PowerWellRef GetPipePowerWellRef(registers::Pipe pipe);
    PowerWellRef GetDdiPowerWellRef(registers::Ddi ddi);

private:
    void SetPowerWell1Enable(bool enable);
    void SetPowerWell2Enable(bool enable);

    int32_t power_well1_refs_;
    int32_t power_well2_refs_;
    Controller* controller_;

    friend PowerWellRef;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Power);
};

} // namespace i915
