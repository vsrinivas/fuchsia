// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Supported Wpan commands.
pub enum WpanMethod {
    GetNcpChannel,
    GetNcpMacAddress,
    GetNcpRssi,
    GetNetworkName,
    GetThreadRloc16,
    GetWeaveNodeId,
    InitializeProxies
}

impl std::str::FromStr for WpanMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetNcpChannel" => Ok(WpanMethod::GetNcpChannel),
            "GetNcpMacAddress" => Ok(WpanMethod::GetNcpMacAddress),
            "GetNcpRssi" => Ok(WpanMethod::GetNcpRssi),
            "GetNetworkName" => Ok(WpanMethod::GetNetworkName),
            "GetThreadRloc16" => Ok(WpanMethod::GetThreadRloc16),
            "GetWeaveNodeId" => Ok(WpanMethod::GetWeaveNodeId),
            "InitializeProxies" => Ok(WpanMethod::InitializeProxies),
            _ => return Err(format_err!("invalid Wpan FIDL method: {}", method)),
        }
    }
}
