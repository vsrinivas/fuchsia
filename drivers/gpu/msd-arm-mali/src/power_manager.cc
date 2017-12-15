// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager.h"

#include "registers.h"

PowerManager::PowerManager(RegisterIo* io)
{
    // Initialize current set of running cores.
    ReceivedPowerInterrupt(io);
}

void PowerManager::EnableCores(RegisterIo* io, uint64_t shader_bitmask)
{
    registers::CoreReadyState::WriteState(io, registers::CoreReadyState::CoreType::kShader,
                                          registers::CoreReadyState::ActionType::kActionPowerOn,
                                          shader_bitmask);
    registers::CoreReadyState::WriteState(io, registers::CoreReadyState::CoreType::kL2,
                                          registers::CoreReadyState::ActionType::kActionPowerOn, 1);
    registers::CoreReadyState::WriteState(io, registers::CoreReadyState::CoreType::kTiler,
                                          registers::CoreReadyState::ActionType::kActionPowerOn, 1);
}

void PowerManager::ReceivedPowerInterrupt(RegisterIo* io)
{
    tiler_ready_status_ =
        registers::CoreReadyState::ReadBitmask(io, registers::CoreReadyState::CoreType::kTiler,
                                               registers::CoreReadyState::StatusType::kReady);
    l2_ready_status_ =
        registers::CoreReadyState::ReadBitmask(io, registers::CoreReadyState::CoreType::kL2,
                                               registers::CoreReadyState::StatusType::kReady);
    shader_ready_status_ =
        registers::CoreReadyState::ReadBitmask(io, registers::CoreReadyState::CoreType::kShader,
                                               registers::CoreReadyState::StatusType::kReady);
}
