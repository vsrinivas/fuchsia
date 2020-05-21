// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use serde::Deserialize;
use std::convert::TryFrom;

#[derive(Deserialize, Debug)]
pub struct ConfigInRequest {
    pub pin: u32,
    pub flags: u32,
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

pub enum GpioMethod {
    ConfigIn(ConfigInRequest),
    ConfigOut(ConfigOutRequest),
    Read(ReadRequest),
    Write(WriteRequest),
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
            _ => Ok(GpioMethod::UndefinedFunc),
        }
    }
}
