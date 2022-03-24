// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enum for supported Device commands.
pub enum DeviceMethod {
    GetDeviceName,
    GetProduct,
    GetVersion,
}

impl std::str::FromStr for DeviceMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetDeviceName" => Ok(DeviceMethod::GetDeviceName),
            "GetProduct" => Ok(DeviceMethod::GetProduct),
            "GetVersion" => Ok(DeviceMethod::GetVersion),
            _ => return Err(format_err!("invalid Device Facade method: {}", method)),
        }
    }
}
