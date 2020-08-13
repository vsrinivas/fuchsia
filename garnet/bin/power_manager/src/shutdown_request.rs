// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_power_statecontrol as fpower;

/// Type alias for the reboot reasons defined in fuchsia.hardware.power.statecontrol.RebootReason.
pub type RebootReason = fpower::RebootReason;

/// Represents the available shutdown types of the system. These are intended to mirror the
/// supported shutdown APIs of fuchsia.hardware.power.statecontrol.Admin.
#[derive(Debug, PartialEq, PartialOrd, Copy, Clone)]
pub enum ShutdownRequest {
    PowerOff,
    Reboot(RebootReason),
    RebootBootloader,
    RebootRecovery,
}

/// Converts a ShutdownRequest into a fuchsia.hardare.power.statecontrol.SystemPowerState value.
impl Into<fpower::SystemPowerState> for ShutdownRequest {
    fn into(self) -> fpower::SystemPowerState {
        match self {
            ShutdownRequest::PowerOff => fpower::SystemPowerState::Poweroff,
            ShutdownRequest::Reboot(_) => fpower::SystemPowerState::Reboot,
            ShutdownRequest::RebootBootloader => fpower::SystemPowerState::RebootBootloader,
            ShutdownRequest::RebootRecovery => fpower::SystemPowerState::RebootRecovery,
        }
    }
}
