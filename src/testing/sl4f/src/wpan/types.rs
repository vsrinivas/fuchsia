// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Supported Wpan commands.
pub enum WpanMethod {
    GetIsCommissioned,
    GetMacAddressFilterSettings,
    GetNcpChannel,
    GetNcpMacAddress,
    GetNcpRssi,
    GetNcpState,
    GetNetworkName,
    GetPanId,
    GetPartitionId,
    GetThreadRloc16,
    GetThreadRouterId,
    GetWeaveNodeId,
    InitializeProxies,
}

impl std::str::FromStr for WpanMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetIsCommissioned" => Ok(WpanMethod::GetIsCommissioned),
            "GetMacAddressFilterSettings" => Ok(WpanMethod::GetMacAddressFilterSettings),
            "GetNcpChannel" => Ok(WpanMethod::GetNcpChannel),
            "GetNcpMacAddress" => Ok(WpanMethod::GetNcpMacAddress),
            "GetNcpRssi" => Ok(WpanMethod::GetNcpRssi),
            "GetNcpState" => Ok(WpanMethod::GetNcpState),
            "GetNetworkName" => Ok(WpanMethod::GetNetworkName),
            "GetPanId" => Ok(WpanMethod::GetPanId),
            "GetPartitionId" => Ok(WpanMethod::GetPartitionId),
            "GetThreadRloc16" => Ok(WpanMethod::GetThreadRloc16),
            "GetThreadRouterId" => Ok(WpanMethod::GetThreadRouterId),
            "GetWeaveNodeId" => Ok(WpanMethod::GetWeaveNodeId),
            "InitializeProxies" => Ok(WpanMethod::InitializeProxies),
            _ => return Err(format_err!("invalid Wpan FIDL method: {}", method)),
        }
    }
}
