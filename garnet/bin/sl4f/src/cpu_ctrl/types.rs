use anyhow::Error;
use fidl_fuchsia_hardware_cpu_ctrl::CpuPerformanceStateInfo;
use serde::{Deserialize, Serialize};
use std::convert::TryFrom;
use std::string::String;

#[derive(Deserialize, Debug)]
pub struct GetPerformanceStateInfoRequest {
    pub device_number: String,
}

#[derive(Deserialize, Debug)]
pub struct GetNumLogicalCoresRequest {
    pub device_number: String,
}

#[derive(Deserialize, Debug)]
pub struct GetLogicalCoreIdRequest {
    pub device_number: String,
}

pub enum CpuCtrlMethod {
    GetPerformanceStateInfo(GetPerformanceStateInfoRequest),
    GetNumLogicalCores(GetNumLogicalCoresRequest),
    GetLogicalCoreId(GetLogicalCoreIdRequest),
    UndefinedFunc,
}

impl TryFrom<(&str, serde_json::value::Value)> for CpuCtrlMethod {
    type Error = Error;
    fn try_from(input: (&str, serde_json::value::Value)) -> Result<Self, Self::Error> {
        match input.0 {
            "GetPerformanceStateInfo" => {
                Ok(CpuCtrlMethod::GetPerformanceStateInfo(serde_json::from_value(input.1)?))
            }
            "GetNumLogicalCores" => {
                Ok(CpuCtrlMethod::GetNumLogicalCores(serde_json::from_value(input.1)?))
            }
            "GetLogicalCoreId" => {
                Ok(CpuCtrlMethod::GetLogicalCoreId(serde_json::from_value(input.1)?))
            }
            _ => Ok(CpuCtrlMethod::UndefinedFunc),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SerializableCpuPerformanceStateInfo {
    pub frequency_hz: i64,
    pub voltage_uv: i64,
}

impl std::convert::From<CpuPerformanceStateInfo> for SerializableCpuPerformanceStateInfo {
    fn from(cpu_performance_state_info: CpuPerformanceStateInfo) -> Self {
        SerializableCpuPerformanceStateInfo {
            frequency_hz: cpu_performance_state_info.frequency_hz,
            voltage_uv: cpu_performance_state_info.voltage_uv,
        }
    }
}

impl std::convert::From<SerializableCpuPerformanceStateInfo> for CpuPerformanceStateInfo {
    fn from(info: SerializableCpuPerformanceStateInfo) -> Self {
        Self { frequency_hz: info.frequency_hz, voltage_uv: info.voltage_uv }
    }
}
