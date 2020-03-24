// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Supported Weave commands.
pub enum FactoryDataManagerMethod {
    GetPairingCode,
}

impl std::str::FromStr for FactoryDataManagerMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetPairingCode" => Ok(FactoryDataManagerMethod::GetPairingCode),
            _ => return Err(format_err!("invalid Weave FIDL method: {}", method)),
        }
    }
}
