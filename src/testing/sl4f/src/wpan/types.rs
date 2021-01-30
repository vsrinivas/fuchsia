// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use fidl_fuchsia_lowpan_test::{
    MacAddressFilterItem, MacAddressFilterMode, MacAddressFilterSettings, NeighborInfo,
};
use serde::{Deserialize, Serialize};
/// Supported Wpan commands.
pub enum WpanMethod {
    GetIsCommissioned,
    GetMacAddressFilterSettings,
    GetNcpChannel,
    GetNcpMacAddress,
    GetNcpRssi,
    GetNcpState,
    GetNetworkName,
    GetNeighborTable,
    GetPanId,
    GetPartitionId,
    GetThreadRloc16,
    GetThreadRouterId,
    GetWeaveNodeId,
    InitializeProxies,
    ReplaceMacAddressFilterSettings,
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
            "GetNeighborTable" => Ok(WpanMethod::GetNeighborTable),
            "GetNetworkName" => Ok(WpanMethod::GetNetworkName),
            "GetPanId" => Ok(WpanMethod::GetPanId),
            "GetPartitionId" => Ok(WpanMethod::GetPartitionId),
            "GetThreadRloc16" => Ok(WpanMethod::GetThreadRloc16),
            "GetThreadRouterId" => Ok(WpanMethod::GetThreadRouterId),
            "GetWeaveNodeId" => Ok(WpanMethod::GetWeaveNodeId),
            "InitializeProxies" => Ok(WpanMethod::InitializeProxies),
            "ReplaceMacAddressFilterSettings" => Ok(WpanMethod::ReplaceMacAddressFilterSettings),
            _ => return Err(format_err!("invalid Wpan FIDL method: {}", method)),
        }
    }
}

#[derive(Serialize)]
pub enum ConnectivityState {
    Inactive,
    Ready,
    Offline,
    Attaching,
    Attached,
    Isolated,
    Commissioning,
}

#[derive(Serialize, Deserialize)]
pub struct MacAddressFilterItemDto {
    pub mac_address: Option<Vec<u8>>,
    pub rssi: Option<i8>,
}

#[derive(Serialize, Deserialize)]
pub struct MacAddressFilterSettingsDto {
    pub items: Option<Vec<MacAddressFilterItemDto>>,
    pub mode: Option<MacAddressFilterModeDto>,
}

#[derive(Serialize, Deserialize)]
pub enum MacAddressFilterModeDto {
    Disabled = 0,
    Allow = 1,
    Deny = 2,
}

#[derive(Serialize, Deserialize)]
pub struct NeighborInfoDto {
    pub mac_address: Option<Vec<u8>>,
    pub short_address: Option<u16>,
    pub age: Option<i64>,
    pub is_child: Option<bool>,
    pub link_frame_count: Option<u32>,
    pub mgmt_frame_count: Option<u32>,
    pub last_rssi_in: Option<i32>,
    pub avg_rssi_in: Option<i8>,
    pub lqi_in: Option<u8>,
    pub thread_mode: Option<u8>,
}

impl Into<MacAddressFilterItemDto> for MacAddressFilterItem {
    fn into(self) -> MacAddressFilterItemDto {
        MacAddressFilterItemDto { mac_address: self.mac_address, rssi: self.rssi }
    }
}

impl Into<MacAddressFilterItem> for MacAddressFilterItemDto {
    fn into(self) -> MacAddressFilterItem {
        MacAddressFilterItem {
            mac_address: self.mac_address,
            rssi: self.rssi,
            ..MacAddressFilterItem::EMPTY
        }
    }
}

impl Into<MacAddressFilterSettings> for MacAddressFilterSettingsDto {
    fn into(self) -> MacAddressFilterSettings {
        MacAddressFilterSettings {
            mode: match self.mode {
                Some(mode) => Some(mode.into()),
                None => None,
            },
            items: match self.items {
                Some(items) => Some(items.into_iter().map(|x| x.into()).collect()),
                None => None,
            },
            ..MacAddressFilterSettings::EMPTY
        }
    }
}

impl Into<MacAddressFilterSettingsDto> for MacAddressFilterSettings {
    fn into(self) -> MacAddressFilterSettingsDto {
        MacAddressFilterSettingsDto {
            mode: match self.mode {
                Some(mode) => Some(mode.into()),
                None => None,
            },
            items: match self.items {
                Some(items) => Some(items.into_iter().map(|x| x.into()).collect()),
                None => None,
            },
        }
    }
}

impl Into<MacAddressFilterModeDto> for MacAddressFilterMode {
    fn into(self) -> MacAddressFilterModeDto {
        match self {
            MacAddressFilterMode::Disabled => MacAddressFilterModeDto::Disabled,
            MacAddressFilterMode::Allow => MacAddressFilterModeDto::Allow,
            MacAddressFilterMode::Deny => MacAddressFilterModeDto::Deny,
        }
    }
}

impl Into<MacAddressFilterMode> for MacAddressFilterModeDto {
    fn into(self) -> MacAddressFilterMode {
        match self {
            MacAddressFilterModeDto::Disabled => MacAddressFilterMode::Disabled,
            MacAddressFilterModeDto::Allow => MacAddressFilterMode::Allow,
            MacAddressFilterModeDto::Deny => MacAddressFilterMode::Deny,
        }
    }
}

impl Into<NeighborInfoDto> for NeighborInfo {
    fn into(self) -> NeighborInfoDto {
        NeighborInfoDto {
            mac_address: self.mac_address,
            short_address: self.short_address,
            age: self.age,
            is_child: self.is_child,
            link_frame_count: self.link_frame_count,
            mgmt_frame_count: self.mgmt_frame_count,
            last_rssi_in: self.last_rssi_in,
            avg_rssi_in: self.avg_rssi_in,
            lqi_in: self.lqi_in,
            thread_mode: self.thread_mode,
        }
    }
}

impl Into<NeighborInfo> for NeighborInfoDto {
    fn into(self) -> NeighborInfo {
        NeighborInfo {
            mac_address: self.mac_address,
            short_address: self.short_address,
            age: self.age,
            is_child: self.is_child,
            link_frame_count: self.link_frame_count,
            mgmt_frame_count: self.mgmt_frame_count,
            last_rssi_in: self.last_rssi_in,
            avg_rssi_in: self.avg_rssi_in,
            lqi_in: self.lqi_in,
            thread_mode: self.thread_mode,
            ..NeighborInfo::EMPTY
        }
    }
}
