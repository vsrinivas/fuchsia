// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::{anyhow, Context, Error};
use fidl_fuchsia_vboot::{BootTarget, FirmwareResult, Key as FwKey};
use std::fmt::Display;

/// Parameter type. Used to parse values to integers.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum ParamType {
    /// Number in a range [min, max]. If |display_hex| is true, values will be displayed in hexadecimal
    /// format.
    NumInRange { display_hex: bool, min: u32, max: u32 },
    /// A boolean. 1/0/true/false.
    Bool,
    /// A (=0), B (=1).
    AB,
    /// BootTarget (usb, disk, altfw).
    BootTarget,
    /// Result of booting firmware (unknown, trying, failure, success).
    FirmwareResult,
}

impl Display for ParamType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ParamType::NumInRange { display_hex: false, min, max } => {
                write!(f, "integer [{}-{}]", min, max)
            }
            ParamType::NumInRange { display_hex: true, min, max } => {
                write!(f, "integer [0x{:x}-0x{:x}]", min, max)
            }
            ParamType::Bool => write!(f, "boolean"),
            ParamType::AB => write!(f, "A or B"),
            ParamType::BootTarget => write!(f, "'usb', 'disk', or 'altfw'"),
            ParamType::FirmwareResult => write!(f, "'unknown', 'trying', 'failure', 'success'"),
        }
    }
}

impl ParamType {
    pub fn parse(&self, value: &str) -> Result<u32, Error> {
        match self {
            ParamType::NumInRange { display_hex, min, max } => {
                let (base, to_parse) = if value.starts_with("0x") && value.len() > 2 {
                    (16, &value[2..])
                } else {
                    (10, value)
                };
                let value = u32::from_str_radix(to_parse, base).context("Parsing number")?;
                if value < *min || value > *max {
                    let range = if *display_hex {
                        format!("{:x}-{:x}", min, max)
                    } else {
                        format!("{}-{}", min, max)
                    };
                    return Err(anyhow!(format!("Value is outside of expected range {}", range)));
                }
                Ok(value)
            }
            ParamType::Bool => match value.to_lowercase().as_str() {
                "true" | "1" | "yes" => Ok(1),
                "false" | "0" | "no" => Ok(0),
                _ => Err(anyhow!("Expected a boolean value")),
            },
            ParamType::AB => match value {
                "A" | "a" => Ok(0),
                "B" | "b" => Ok(1),
                _ => Err(anyhow!("Expected A or B")),
            },
            ParamType::BootTarget => match value {
                "usb" | "external" => Ok(BootTarget::External.into_primitive()),
                "disk" | "internal" => Ok(BootTarget::Internal.into_primitive()),
                "altfw" => Ok(BootTarget::Altfw.into_primitive()),
                _ => Err(anyhow!("Expected 'usb', 'disk', or 'altfw'")),
            },
            ParamType::FirmwareResult => Err(anyhow!("Cannot set firmware result")),
        }
    }

    pub fn display(&self, value: u32) -> Result<String, Error> {
        match self {
            ParamType::NumInRange { display_hex: false, .. } => Ok(value.to_string()),
            ParamType::NumInRange { display_hex: true, .. } => Ok(format!("0x{:x}", value)),
            ParamType::Bool => match value {
                0 => Ok("false".to_owned()),
                1 => Ok("true".to_owned()),
                _ => Err(anyhow!("Invalid bool")),
            },
            ParamType::AB => match value {
                0 => Ok("A".to_owned()),
                1 => Ok("B".to_owned()),
                _ => Err(anyhow!("Invalid A/B")),
            },
            ParamType::BootTarget => match BootTarget::from_primitive(value) {
                Some(BootTarget::Internal) => Ok("disk".to_owned()),
                Some(BootTarget::External) => Ok("usb".to_owned()),
                Some(BootTarget::Altfw) => Ok("altfw".to_owned()),
                _ => Err(anyhow!("Invalid BootTarget")),
            },
            ParamType::FirmwareResult => match FirmwareResult::from_primitive(value) {
                Some(FirmwareResult::Unknown) => Ok("unknown".to_owned()),
                Some(FirmwareResult::Trying) => Ok("trying".to_owned()),
                Some(FirmwareResult::Failure) => Ok("failure".to_owned()),
                Some(FirmwareResult::Success) => Ok("success".to_owned()),
                _ => Err(anyhow!("Invalid FirmwareResult")),
            },
        }
    }
}

#[derive(Debug)]
pub struct Parameter {
    pub name: &'static str,
    pub desc: &'static str,
    pub key: FwKey,
    pub ty: ParamType,
}

pub const PARAMETERS: &[Parameter] = &[
    Parameter {
        name: "nvram_cleared",
        desc: "Have NV settings been lost? Write 0 to clear",
        key: FwKey::KernelSettingsReset,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "dbg_reset",
        desc: "Debug reset mode request",
        key: FwKey::DebugResetMode,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "fw_try_next",
        desc: "Firmware to try next",
        key: FwKey::TryNext,
        ty: ParamType::AB,
    },
    Parameter {
        name: "fw_try_count",
        desc: "Number of times to try fw_try_next",
        key: FwKey::TryCount,
        ty: ParamType::NumInRange { display_hex: false, min: 0, max: 15 },
    },
    Parameter {
        name: "recovery_request",
        desc: "Recovery mode request",
        key: FwKey::RecoveryRequest,
        ty: ParamType::NumInRange { display_hex: false, min: 0, max: std::u8::MAX as u32 },
    },
    Parameter {
        name: "loc_idx",
        desc: "Localisation index for firmware screens",
        key: FwKey::LocalizationIndex,
        ty: ParamType::NumInRange { display_hex: false, min: 0, max: std::u8::MAX as u32 },
    },
    Parameter {
        name: "kern_nv",
        desc: "Non-volatile storage for OS",
        key: FwKey::KernelField,
        ty: ParamType::NumInRange { display_hex: false, min: 0, max: std::u16::MAX as u32 },
    },
    Parameter {
        name: "dev_boot_usb",
        desc: "Enable developer mode boot from external disk (USB/SD)",
        key: FwKey::DevBootExternal,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "dev_boot_altfw",
        desc: "Enable developer mode boot using alternate bootloader (altfw)",
        key: FwKey::DevBootAltfw,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "dev_boot_signed_only",
        desc: "Enable developer mode boot only from official kernels",
        key: FwKey::DevBootSignedOnly,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "dev_default_boot",
        desc: "Default boot from 'disk', 'altfw', or 'usb'",
        key: FwKey::DevDefaultBoot,
        ty: ParamType::BootTarget,
    },
    Parameter {
        name: "dev_enable_udc",
        desc: "Enable USB Device Controller",
        key: FwKey::DevEnableUdc,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "disable_dev_request",
        desc: "Disable developer mode on next boot",
        key: FwKey::DisableDevRequest,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "display_request",
        desc: "Should we initialize the display at boot?",
        key: FwKey::DisplayRequest,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "clear_tpm_owner_request",
        desc: "Clear TPM owner on next boot",
        key: FwKey::ClearTpmOwnerRequest,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "clear_tpm_owner_done",
        desc: "Clear TPM owner done",
        key: FwKey::ClearTpmOwnerDone,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "tpm_rebooted",
        desc: "TPM requesting repeated reboot",
        key: FwKey::TpmRequestedReboot,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "recovery_subcode",
        desc: "Recovery reason subcode",
        key: FwKey::RecoverySubcode,
        ty: ParamType::NumInRange { display_hex: false, min: 0, max: std::u8::MAX as u32 },
    },
    Parameter {
        name: "backup_nvram_request",
        desc: "Backup the nvram somewhere at the next boot. Cleared on success.",
        key: FwKey::BackupNvramRequest,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "fw_tried",
        desc: "Firmware tried this boot.",
        key: FwKey::FwTried,
        ty: ParamType::AB,
    },
    Parameter {
        name: "fw_result",
        desc: "Firmware result this boot",
        key: FwKey::FwResult,
        ty: ParamType::FirmwareResult,
    },
    Parameter {
        name: "fw_prev_tried",
        desc: "Firmware tried previous boot.",
        key: FwKey::FwPrevTried,
        ty: ParamType::AB,
    },
    Parameter {
        name: "fw_prev_result",
        desc: "Firmware result previous boot",
        key: FwKey::FwPrevResult,
        ty: ParamType::FirmwareResult,
    },
    Parameter {
        name: "wipeout_request",
        desc: "Firmware requested factory reset (wipeout)",
        key: FwKey::ReqWipeout,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "try_ro_sync",
        desc: "Try EC read only software sync",
        key: FwKey::TryRoSync,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "battery_cutoff_request",
        desc: "Cut off battery and shut down on next boot",
        key: FwKey::BatteryCutoffRequest,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "kernel_max_rollforward",
        desc: "Max kernel version to store into TPM",
        key: FwKey::KernelMaxRollforward,
        ty: ParamType::NumInRange { display_hex: true, min: 0, max: std::u32::MAX },
    },
    Parameter {
        name: "diagnostic_request",
        desc: "Request diagnostic rom run on next boot",
        key: FwKey::DiagRequest,
        ty: ParamType::Bool,
    },
    Parameter {
        name: "minios_priority",
        desc: "miniOS image to try first",
        key: FwKey::MiniosPriority,
        ty: ParamType::AB,
    },
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_num() {
        let param = ParamType::NumInRange { display_hex: false, min: 10, max: 13 };
        assert_eq!(param.parse("27").is_err(), true);
        assert_eq!(param.parse("6").is_err(), true);
        assert_eq!(param.parse("10").unwrap(), 10);
        assert_eq!(param.parse("0xa").unwrap(), 10);
        assert_eq!(param.parse("13").unwrap(), 13);
        assert_eq!(param.parse("0xabc").is_err(), true);
    }

    #[test]
    fn parse_bool() {
        let param = ParamType::Bool;
        assert_eq!(param.parse("1").unwrap(), 1);
        assert_eq!(param.parse("1").unwrap(), 1);
        assert_eq!(param.parse("yes").unwrap(), 1);
        assert_eq!(param.parse("0").unwrap(), 0);
        assert_eq!(param.parse("0").unwrap(), 0);
        assert_eq!(param.parse("no").unwrap(), 0);
        assert_eq!(param.parse("27").is_err(), true);
        assert_eq!(param.parse("maybe").is_err(), true);
    }

    #[test]
    fn parse_ab() {
        let param = ParamType::AB;
        assert_eq!(param.parse("a").unwrap(), 0);
        assert_eq!(param.parse("A").unwrap(), 0);
        assert_eq!(param.parse("b").unwrap(), 1);
        assert_eq!(param.parse("B").unwrap(), 1);
        assert_eq!(param.parse("c").is_err(), true);
        assert_eq!(param.parse("0").is_err(), true);
    }

    #[test]
    fn parse_boot_target() {
        let param = ParamType::BootTarget;
        assert_eq!(param.parse("usb").unwrap(), BootTarget::External.into_primitive());
        assert_eq!(param.parse("disk").unwrap(), BootTarget::Internal.into_primitive());
        assert_eq!(param.parse("altfw").unwrap(), BootTarget::Altfw.into_primitive());
        assert_eq!(param.parse("alt").is_err(), true);
        assert_eq!(param.parse("0").is_err(), true);
        assert_eq!(param.parse("u").is_err(), true);
    }
}
