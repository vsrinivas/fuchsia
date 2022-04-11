// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::format_err, argh::FromArgs};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "device",
    description = "Performs specified subcommand on device",
    example = "To unbind a driver

    $ driver device unbind 'sys/platform/pci:00:01.6'",
    error_code(1, "Failed to open device")
)]
pub struct DeviceCommand {
    /// the subcommand to run.
    #[argh(subcommand)]
    pub subcommand: DeviceSubcommand,

    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, short = 's', long = "select")]
    pub select: bool,
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
#[argh(subcommand)]
pub enum DeviceSubcommand {
    Bind(BindCommand),
    Unbind(UnbindCommand),
    LogLevel(LogLevelCommand),
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Binds the driver specified to the specified device.
#[argh(subcommand, name = "bind")]
pub struct BindCommand {
    // TODO(surajmalhotra): Make this a URL once drivers are components.
    /// the path of the driver to debug, e.g. "/system/driver/usb_video.so"
    #[argh(positional)]
    pub driver_path: String,

    /// the path of the device to unbind, relative to the /dev directory.
    /// E.g. "sys/platform/pci/00:1f.6" or "class/usb-device/000"
    #[argh(positional)]
    pub device_path: String,
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Unbinds the driver bound to the specified device.
#[argh(subcommand, name = "unbind")]
pub struct UnbindCommand {
    /// the path of the device to unbind, relative to the /dev directory.
    /// E.g. "sys/platform/pci/00:1f.6" or "class/usb-device/000"
    #[argh(positional)]
    pub device_path: String,
}

#[derive(Clone, PartialEq, Debug)]
pub enum LogLevel {
    Error,
    Warning,
    Info,
    Debug,
    Trace,
    Serial,
}

impl std::str::FromStr for LogLevel {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let lower = s.to_ascii_lowercase();
        match lower.as_str() {
            "error" | "e" => Ok(LogLevel::Error),
            "warning" | "w" => Ok(LogLevel::Warning),
            "info" | "i" => Ok(LogLevel::Info),
            "debug" | "d" => Ok(LogLevel::Debug),
            "trace" | "t" => Ok(LogLevel::Trace),
            "serial" | "s" => Ok(LogLevel::Serial),
            _ => Err(format!(
                "'{}' is not a valid value: must be one of 'error', 'warning', 'info', 'debug', 'trace', or 'serial'",
                s
            )),
        }
    }
}

impl Into<u32> for LogLevel {
    fn into(self) -> u32 {
        match self {
            LogLevel::Error => 0x50,
            LogLevel::Warning => 0x40,
            LogLevel::Info => 0x30,
            LogLevel::Debug => 0x20,
            LogLevel::Trace => 0x10,
            LogLevel::Serial => std::u32::MAX,
        }
    }
}

impl std::convert::TryFrom<u32> for LogLevel {
    type Error = anyhow::Error;
    fn try_from(flags: u32) -> Result<Self, anyhow::Error> {
        match flags {
            0x50 => Ok(LogLevel::Error),
            0x40 => Ok(LogLevel::Warning),
            0x30 => Ok(LogLevel::Info),
            0x20 => Ok(LogLevel::Debug),
            0x10 => Ok(LogLevel::Trace),
            std::u32::MAX => Ok(LogLevel::Serial),
            _ => Err(format_err!("'{}' is not a valid value.", flags)),
        }
    }
}

impl std::fmt::Display for LogLevel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            LogLevel::Error => write!(f, "error"),
            LogLevel::Warning => write!(f, "warning"),
            LogLevel::Info => write!(f, "info"),
            LogLevel::Debug => write!(f, "debug"),
            LogLevel::Trace => write!(f, "trace"),
            LogLevel::Serial => write!(f, "serial"),
        }
    }
}

#[derive(FromArgs, Clone, PartialEq, Debug)]
/// Sets or prints the log level for the specified device. If log_level is not specified, will
/// print current log level.
#[argh(subcommand, name = "log-level")]
pub struct LogLevelCommand {
    /// the path of the device to unbind, relative to the /dev directory.
    /// E.g. "sys/platform/pci/00:1f.6" or "class/usb-device/000"
    #[argh(positional)]
    pub device_path: String,

    /// the log level to set
    #[argh(positional)]
    pub log_level: Option<LogLevel>,
}
