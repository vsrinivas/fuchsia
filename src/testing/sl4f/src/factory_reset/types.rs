// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enum for supported Factory reset commands.
pub enum FactoryResetMethod {
    FactoryReset,
}

impl std::str::FromStr for FactoryResetMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "FactoryReset" => Ok(FactoryResetMethod::FactoryReset),
            _ => return Err(format_err!("invalid Factory Reset Facade method: {}", method)),
        }
    }
}
