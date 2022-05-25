// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    fidl_fuchsia_input_report as fir,
    std::{path::PathBuf, str::FromStr},
};

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "get",
    description = "Gets and prints an input report from an input device",
    example = "Get and print the touch input report of input device 001:

    $ driver print-input-report get class/input-report/001 touch"
)]
pub struct GetCommand {
    /// get and print the input report of only the input device specified by
    /// this device path which is relative to the /dev directory.
    #[argh(positional)]
    pub device_path: PathBuf,

    /// mouse, sensor, touch, keyboard, or consumer_control.
    #[argh(positional)]
    pub device_type: DeviceType,
}

#[derive(Debug, PartialEq)]
pub enum DeviceType {
    Mouse,
    Sensor,
    Touch,
    Keyboard,
    ConsumerControl,
}

impl FromStr for DeviceType {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "mouse" => Ok(Self::Mouse),
            "sensor" => Ok(Self::Sensor),
            "touch" => Ok(Self::Touch),
            "keyboard" => Ok(Self::Keyboard),
            "consumer_control" => Ok(Self::ConsumerControl),
            _ => Err(format!("'{}' is not a valid value for DeviceType", s)),
        }
    }
}

impl DeviceType {
    pub fn get_fidl(&self) -> fir::DeviceType {
        match self {
            Self::Mouse => fir::DeviceType::Mouse,
            Self::Sensor => fir::DeviceType::Sensor,
            Self::Touch => fir::DeviceType::Touch,
            Self::Keyboard => fir::DeviceType::Keyboard,
            Self::ConsumerControl => fir::DeviceType::ConsumerControl,
        }
    }
}
