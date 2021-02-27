// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::format_err, fidl_fuchsia_bluetooth_sys as sys, std::str::FromStr};

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum InputCapability {
    None,
    Confirmation,
    Keyboard,
}

impl FromStr for InputCapability {
    type Err = anyhow::Error;

    fn from_str(src: &str) -> Result<Self, Self::Err> {
        match src {
            "none" => Ok(InputCapability::None),
            "confirmation" => Ok(InputCapability::Confirmation),
            "keyboard" => Ok(InputCapability::Keyboard),
            _ => Err(format_err!("Invalid input capability")),
        }
    }
}

impl Into<sys::InputCapability> for InputCapability {
    fn into(self) -> sys::InputCapability {
        match self {
            InputCapability::None => sys::InputCapability::None,
            InputCapability::Confirmation => sys::InputCapability::Confirmation,
            InputCapability::Keyboard => sys::InputCapability::Keyboard,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OutputCapability {
    None,
    Display,
}

impl FromStr for OutputCapability {
    type Err = anyhow::Error;

    fn from_str(src: &str) -> Result<Self, Self::Err> {
        match src {
            "none" => Ok(OutputCapability::None),
            "display" => Ok(OutputCapability::Display),
            _ => Err(format_err!("Invalid output capability")),
        }
    }
}

impl Into<sys::OutputCapability> for OutputCapability {
    fn into(self) -> sys::OutputCapability {
        match self {
            OutputCapability::None => sys::OutputCapability::None,
            OutputCapability::Display => sys::OutputCapability::Display,
        }
    }
}
