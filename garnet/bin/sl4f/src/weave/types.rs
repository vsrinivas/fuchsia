// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Supported Weave commands.
pub enum WeaveMethod {
    GetPairingCode,
    GetQrCode,
}

impl std::str::FromStr for WeaveMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetPairingCode" => Ok(WeaveMethod::GetPairingCode),
            "GetQrCode" => Ok(WeaveMethod::GetQrCode),
            _ => return Err(format_err!("invalid Weave FIDL method: {}", method)),
        }
    }
}
