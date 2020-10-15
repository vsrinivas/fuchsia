// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use std::convert::TryFrom;

pub enum SysInfoMethod {
    GetBoardName,
    GetBoardRevision,
    GetBootloaderVendor,
    UndefinedFunc,
}

impl TryFrom<(&str, serde_json::value::Value)> for SysInfoMethod {
    type Error = Error;
    fn try_from(input: (&str, serde_json::value::Value)) -> Result<Self, Self::Error> {
        match input.0 {
            "GetBoardName" => Ok(SysInfoMethod::GetBoardName),
            "GetBoardRevision" => Ok(SysInfoMethod::GetBoardRevision),
            "GetBootloaderVendor" => Ok(SysInfoMethod::GetBootloaderVendor),
            _ => Ok(SysInfoMethod::UndefinedFunc),
        }
    }
}
