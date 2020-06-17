// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_device_manager as fdevicemgr;
use fidl_fuchsia_hardware_power_statecontrol as fpower;
use std::convert::TryFrom;

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
    Mexec,
}

impl ShutdownRequest {
    /// Gets the fuchsia.hardware.power.statecontrol flag constant that this ShutdownRequest maps
    /// to.
    // TODO(pshickel): We only need this to support the deprecated shutdown path that is being used
    // by the SystemPowerManager node. We can safely remove this after we're using the new shutdown
    // path.
    pub fn to_driver_manager_flag(&self) -> u32 {
        match *self {
            ShutdownRequest::PowerOff => fdevicemgr::SUSPEND_FLAG_POWEROFF,
            ShutdownRequest::Reboot(_) => fdevicemgr::SUSPEND_FLAG_REBOOT,
            ShutdownRequest::RebootBootloader => fdevicemgr::SUSPEND_FLAG_REBOOT_BOOTLOADER,
            ShutdownRequest::RebootRecovery => fdevicemgr::SUSPEND_FLAG_REBOOT_RECOVERY,
            ShutdownRequest::Mexec => fdevicemgr::SUSPEND_FLAG_MEXEC,
        }
    }
}

// TODO(fxbug.dev/53962): This function only exists to help convert incoming old-style Suspend
// requests (which are deprecated) into the new style shutdown-type specific APIs. We can remove
// this after all clients are using the new APIs and the old-style Suspend API has been removed.
/// Converts a fuchsia.hardware.power.statecontrol.SystemPowerState into a ShutdownRequest.
impl TryFrom<fpower::SystemPowerState> for ShutdownRequest {
    type Error = anyhow::Error;
    fn try_from(state: fpower::SystemPowerState) -> Result<Self, Error> {
        Ok(match state {
            fpower::SystemPowerState::Poweroff => ShutdownRequest::PowerOff,
            fpower::SystemPowerState::Reboot => ShutdownRequest::Reboot(RebootReason::UserRequest),
            fpower::SystemPowerState::RebootBootloader => ShutdownRequest::RebootBootloader,
            fpower::SystemPowerState::RebootRecovery => ShutdownRequest::RebootRecovery,
            fpower::SystemPowerState::Mexec => ShutdownRequest::Mexec,
            _ => Err(format_err!("Invalid state: {:?}", state))?,
        })
    }
}

/// Converts a ShutdownRequest into a fuchsia.device.manager.SystemPowerState value.
impl Into<fdevicemgr::SystemPowerState> for ShutdownRequest {
    fn into(self) -> fdevicemgr::SystemPowerState {
        match self {
            ShutdownRequest::PowerOff => fdevicemgr::SystemPowerState::SystemPowerStatePoweroff,
            ShutdownRequest::Reboot(_) => fdevicemgr::SystemPowerState::SystemPowerStateReboot,
            ShutdownRequest::RebootBootloader => {
                fdevicemgr::SystemPowerState::SystemPowerStateRebootBootloader
            }
            ShutdownRequest::RebootRecovery => {
                fdevicemgr::SystemPowerState::SystemPowerStateRebootRecovery
            }
            ShutdownRequest::Mexec => fdevicemgr::SystemPowerState::SystemPowerStateMexec,
        }
    }
}
