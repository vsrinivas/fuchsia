// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_ram_metrics::{
    BandwidthInfo, BandwidthMeasurementConfig, GrantedCyclesResult,
};
use fuchsia_zircon as zx;
use serde::{Deserialize, Serialize};

pub enum RamMethod {
    MeasureBandwidth,
    GetDdrWindowingResults,
    UndefinedFunc,
}

impl From<&str> for RamMethod {
    fn from(method: &str) -> RamMethod {
        match method {
            "MeasureBandwidth" => RamMethod::MeasureBandwidth,
            "GetDdrWindowingResults" => RamMethod::GetDdrWindowingResults,
            _ => RamMethod::UndefinedFunc,
        }
    }
}

/// BandwidthMeasurementConfig object is not serializable so we create
/// SerializableBandwidthMeasurementConfig which is serializable.
#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
pub struct SerializableBandwidthMeasurementConfig {
    pub cycles_to_measure: u64,
    pub channels: [u64; 8],
}

impl From<SerializableBandwidthMeasurementConfig> for BandwidthMeasurementConfig {
    fn from(bandwidth_measurement_config: SerializableBandwidthMeasurementConfig) -> Self {
        BandwidthMeasurementConfig {
            cycles_to_measure: bandwidth_measurement_config.cycles_to_measure,
            channels: bandwidth_measurement_config.channels,
        }
    }
}

/// GrantedCyclesResult object is not serializable so we create SerializableGrantedCyclesResult
/// which is serializable.
#[derive(Clone, Debug, Serialize, PartialEq, Eq, Copy)]
pub struct SerializableGrantedCyclesResult {
    pub read_cycles: u64,
    pub write_cycles: u64,
    pub readwrite_cycles: u64,
}

impl From<GrantedCyclesResult> for SerializableGrantedCyclesResult {
    fn from(granted_cycles_result: GrantedCyclesResult) -> Self {
        SerializableGrantedCyclesResult {
            read_cycles: granted_cycles_result.read_cycles,
            write_cycles: granted_cycles_result.write_cycles,
            readwrite_cycles: granted_cycles_result.readwrite_cycles,
        }
    }
}
impl From<SerializableGrantedCyclesResult> for GrantedCyclesResult {
    fn from(grantedcyclesresult: SerializableGrantedCyclesResult) -> Self {
        GrantedCyclesResult {
            read_cycles: grantedcyclesresult.read_cycles,
            write_cycles: grantedcyclesresult.write_cycles,
            readwrite_cycles: grantedcyclesresult.readwrite_cycles,
        }
    }
}

/// BandwidthInfo object is not serializable so we create SerializableBandwidthInfo
/// which is serializable.
#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableBandwidthInfo {
    pub timestamp: zx::sys::zx_time_t,
    pub frequency: u64,
    pub bytes_per_cycle: u64,
    pub channels: [SerializableGrantedCyclesResult; 8],
    pub total: SerializableGrantedCyclesResult,
}

impl From<BandwidthInfo> for SerializableBandwidthInfo {
    fn from(bandwidth_info: BandwidthInfo) -> Self {
        let mut temp_channels = [SerializableGrantedCyclesResult {
            read_cycles: 0,
            write_cycles: 0,
            readwrite_cycles: 0,
        }; 8];
        for (serialized_channel, channel) in
            temp_channels.iter_mut().zip(bandwidth_info.channels.iter())
        {
            *serialized_channel = SerializableGrantedCyclesResult::from(*channel);
        }

        SerializableBandwidthInfo {
            timestamp: bandwidth_info.timestamp,
            frequency: bandwidth_info.frequency,
            bytes_per_cycle: bandwidth_info.bytes_per_cycle,
            channels: temp_channels,
            total: SerializableGrantedCyclesResult::from(bandwidth_info.total),
        }
    }
}

impl From<SerializableBandwidthInfo> for BandwidthInfo {
    fn from(bandwidthinfo: SerializableBandwidthInfo) -> Self {
        let mut temp_channels =
            [GrantedCyclesResult { read_cycles: 0, write_cycles: 0, readwrite_cycles: 0 }; 8];
        for (c, from_c) in temp_channels.iter_mut().zip(bandwidthinfo.channels.iter()) {
            *c = GrantedCyclesResult::from(*from_c);
        }

        BandwidthInfo {
            timestamp: bandwidthinfo.timestamp,
            frequency: bandwidthinfo.frequency,
            bytes_per_cycle: bandwidthinfo.bytes_per_cycle,
            channels: temp_channels,
            total: GrantedCyclesResult::from(bandwidthinfo.total),
        }
    }
}
