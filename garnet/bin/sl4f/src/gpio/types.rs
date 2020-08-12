// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_hardware_gpio::GpioFlags;
use serde::Deserialize;
use std::convert::TryFrom;

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub enum SerializableGpioFlags {
    PullDown,
    PullUp,
    NoPull,
    PullMask,
}

impl From<GpioFlags> for SerializableGpioFlags {
    fn from(x: GpioFlags) -> Self {
        match x {
            GpioFlags::PullDown => SerializableGpioFlags::PullDown,
            GpioFlags::PullUp => SerializableGpioFlags::PullUp,
            GpioFlags::NoPull => SerializableGpioFlags::NoPull,
            GpioFlags::PullMask => SerializableGpioFlags::PullMask,
        }
    }
}

impl From<SerializableGpioFlags> for GpioFlags {
    fn from(x: SerializableGpioFlags) -> Self {
        match x {
            SerializableGpioFlags::PullDown => GpioFlags::PullDown,
            SerializableGpioFlags::PullUp => GpioFlags::PullUp,
            SerializableGpioFlags::NoPull => GpioFlags::NoPull,
            SerializableGpioFlags::PullMask => GpioFlags::PullMask,
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct ConfigInRequest {
    pub pin: u32,
    pub flags: SerializableGpioFlags,
}

#[derive(Deserialize, Debug)]
pub struct ConfigOutRequest {
    pub pin: u32,
    pub value: u8,
}

#[derive(Deserialize, Debug)]
pub struct ReadRequest {
    pub pin: u32,
}

#[derive(Deserialize, Debug)]
pub struct WriteRequest {
    pub pin: u32,
    pub value: u8,
}

#[derive(Deserialize, Debug)]
pub struct SetDriveStrengthRequest {
    pub pin: u32,
    pub ds_ua: u64,
}

pub enum GpioMethod {
    ConfigIn(ConfigInRequest),
    ConfigOut(ConfigOutRequest),
    Read(ReadRequest),
    Write(WriteRequest),
    SetDriveStrength(SetDriveStrengthRequest),
    UndefinedFunc,
}

impl TryFrom<(&str, serde_json::value::Value)> for GpioMethod {
    type Error = Error;
    fn try_from(input: (&str, serde_json::value::Value)) -> Result<Self, Self::Error> {
        match input.0 {
            "ConfigIn" => Ok(GpioMethod::ConfigIn(serde_json::from_value(input.1)?)),
            "ConfigOut" => Ok(GpioMethod::ConfigOut(serde_json::from_value(input.1)?)),
            "Read" => Ok(GpioMethod::Read(serde_json::from_value(input.1)?)),
            "Write" => Ok(GpioMethod::Write(serde_json::from_value(input.1)?)),
            "SetDriveStrength" => {
                Ok(GpioMethod::SetDriveStrength(serde_json::from_value(input.1)?))
            }
            _ => Ok(GpioMethod::UndefinedFunc),
        }
    }
}
