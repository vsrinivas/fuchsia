// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_input_report::{
    Axis, ContactInputDescriptor, ContactInputReport, DeviceDescriptor, DeviceInfo, FeatureReport,
    InputReport, SensorAxis, SensorDescriptor, SensorFeatureDescriptor, SensorFeatureReport,
    SensorInputDescriptor, SensorInputReport, SensorReportingState, TouchDescriptor,
    TouchInputDescriptor, TouchInputReport,
};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::vec::Vec;

pub enum InputReportMethod {
    GetReports,
    GetDescriptor,
    SendOutputReport,
    GetFeatureReport,
    SetFeatureReport,
    UndefinedFunc,
}

impl std::str::FromStr for InputReportMethod {
    type Err = ();
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(match s.as_ref() {
            "GetReports" => InputReportMethod::GetReports,
            "GetDescriptor" => InputReportMethod::GetDescriptor,
            "SendOutputReport" => InputReportMethod::SendOutputReport,
            "GetFeatureReport" => InputReportMethod::GetFeatureReport,
            "SetFeatureReport" => InputReportMethod::SetFeatureReport,
            _ => InputReportMethod::UndefinedFunc,
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct InputDeviceMatchArgs {
    pub vendor_id: Option<u32>,
    pub product_id: Option<u32>,
    pub version: Option<u32>,
}

impl InputDeviceMatchArgs {
    #[cfg(test)]
    pub fn default() -> Self {
        InputDeviceMatchArgs { vendor_id: None, product_id: None, version: None }
    }

    fn get_u32_arg(args: &Value, name: &str) -> Option<u32> {
        args.get(name).and_then(Value::as_u64).and_then(|num| Some(num as u32))
    }
}

impl std::convert::From<&Value> for InputDeviceMatchArgs {
    fn from(value: &Value) -> Self {
        InputDeviceMatchArgs {
            vendor_id: InputDeviceMatchArgs::get_u32_arg(value, "vendor_id"),
            product_id: InputDeviceMatchArgs::get_u32_arg(value, "product_id"),
            version: InputDeviceMatchArgs::get_u32_arg(value, "version"),
        }
    }
}

#[derive(Clone, Debug, Serialize, Eq, PartialEq)]
pub struct SerializableDeviceInfo {
    pub vendor_id: u32,
    pub product_id: u32,
    pub version: u32,
}

impl SerializableDeviceInfo {
    pub fn new(device_info: &DeviceInfo) -> Self {
        SerializableDeviceInfo {
            vendor_id: device_info.vendor_id,
            product_id: device_info.product_id,
            version: device_info.version,
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableUnit {
    #[serde(rename = "type")]
    pub type_: u32,
    pub exponent: i32,
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableRange {
    pub min: i64,
    pub max: i64,
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableAxis {
    pub range: SerializableRange,
    pub unit: SerializableUnit,
}

impl SerializableAxis {
    pub fn new(axis: &Axis) -> Self {
        SerializableAxis {
            range: SerializableRange { min: axis.range.min, max: axis.range.max },
            unit: SerializableUnit {
                type_: axis.unit.type_.into_primitive(),
                exponent: axis.unit.exponent,
            },
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableSensorAxis {
    pub axis: SerializableAxis,
    #[serde(rename = "type")]
    pub type_: u32,
}

impl SerializableSensorAxis {
    pub fn new(sensor_axis: &SensorAxis) -> Self {
        SerializableSensorAxis {
            axis: SerializableAxis::new(&sensor_axis.axis),
            type_: sensor_axis.type_.into_primitive(),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableSensorInputDescriptor {
    pub values: Option<Vec<SerializableSensorAxis>>,
}

impl SerializableSensorInputDescriptor {
    pub fn new(sensor_input: &SensorInputDescriptor) -> Self {
        SerializableSensorInputDescriptor {
            values: sensor_input
                .values
                .as_ref()
                .map(|values| values.iter().map(SerializableSensorAxis::new).collect()),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableSensorFeatureDescriptor {
    pub report_interval: Option<SerializableAxis>,
    pub supports_reporting_state: Option<bool>,
    pub sensitivity: Option<Vec<SerializableSensorAxis>>,
    pub threshold_high: Option<Vec<SerializableSensorAxis>>,
    pub threshold_low: Option<Vec<SerializableSensorAxis>>,
}

impl SerializableSensorFeatureDescriptor {
    pub fn new(sensor_feature: &SensorFeatureDescriptor) -> Self {
        SerializableSensorFeatureDescriptor {
            report_interval: sensor_feature.report_interval.as_ref().map(SerializableAxis::new),
            supports_reporting_state: sensor_feature.supports_reporting_state.clone(),
            sensitivity: sensor_feature
                .sensitivity
                .as_ref()
                .map(|sensitivity| sensitivity.iter().map(SerializableSensorAxis::new).collect()),
            threshold_high: sensor_feature.threshold_high.as_ref().map(|threshold_high| {
                threshold_high.iter().map(SerializableSensorAxis::new).collect()
            }),
            threshold_low: sensor_feature.threshold_low.as_ref().map(|threshold_low| {
                threshold_low.iter().map(SerializableSensorAxis::new).collect()
            }),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableSensorDescriptor {
    pub input: Option<SerializableSensorInputDescriptor>,
    pub feature: Option<SerializableSensorFeatureDescriptor>,
}

impl SerializableSensorDescriptor {
    pub fn new(sensor: &SensorDescriptor) -> Self {
        SerializableSensorDescriptor {
            input: sensor.input.as_ref().map(SerializableSensorInputDescriptor::new),
            feature: sensor.feature.as_ref().map(SerializableSensorFeatureDescriptor::new),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableContactInputDescriptor {
    pub position_x: Option<SerializableAxis>,
    pub position_y: Option<SerializableAxis>,
    pub pressure: Option<SerializableAxis>,
    pub contact_width: Option<SerializableAxis>,
    pub contact_height: Option<SerializableAxis>,
}

impl SerializableContactInputDescriptor {
    pub fn new(contact_input: &ContactInputDescriptor) -> Self {
        SerializableContactInputDescriptor {
            position_x: contact_input.position_x.as_ref().map(SerializableAxis::new),
            position_y: contact_input.position_y.as_ref().map(SerializableAxis::new),
            pressure: contact_input.pressure.as_ref().map(SerializableAxis::new),
            contact_width: contact_input.contact_width.as_ref().map(SerializableAxis::new),
            contact_height: contact_input.contact_height.as_ref().map(SerializableAxis::new),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableTouchInputDescriptor {
    pub contacts: Option<Vec<SerializableContactInputDescriptor>>,
    pub max_contacts: Option<u32>,
    pub touch_type: Option<u32>,
    pub buttons: Option<Vec<u8>>,
}

impl SerializableTouchInputDescriptor {
    pub fn new(touch_input: &TouchInputDescriptor) -> Self {
        SerializableTouchInputDescriptor {
            contacts: touch_input
                .contacts
                .as_ref()
                .map(|values| values.iter().map(SerializableContactInputDescriptor::new).collect()),
            max_contacts: touch_input.max_contacts,
            touch_type: touch_input.touch_type.map(|touch_type| touch_type.into_primitive()),
            buttons: touch_input.buttons.clone(),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableTouchDescriptor {
    pub input: Option<SerializableTouchInputDescriptor>,
}

impl SerializableTouchDescriptor {
    pub fn new(touch: &TouchDescriptor) -> Self {
        SerializableTouchDescriptor {
            input: touch.input.as_ref().map(SerializableTouchInputDescriptor::new),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableDeviceDescriptor {
    pub device_info: Option<SerializableDeviceInfo>,
    pub sensor: Option<SerializableSensorDescriptor>,
    pub touch: Option<SerializableTouchDescriptor>,
}

impl SerializableDeviceDescriptor {
    pub fn new(descriptor: &DeviceDescriptor) -> Self {
        SerializableDeviceDescriptor {
            device_info: descriptor.device_info.as_ref().map(SerializableDeviceInfo::new),
            sensor: descriptor.sensor.as_ref().map(SerializableSensorDescriptor::new),
            touch: descriptor.touch.as_ref().map(SerializableTouchDescriptor::new),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableSensorInputReport {
    pub values: Option<Vec<i64>>,
}

impl SerializableSensorInputReport {
    pub fn new(sensor_report: &SensorInputReport) -> Self {
        SerializableSensorInputReport { values: sensor_report.values.clone() }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SerializableSensorFeatureReport {
    pub report_interval: Option<i64>,
    pub reporting_state: Option<u32>,
    pub sensitivity: Option<Vec<i64>>,
    pub threshold_high: Option<Vec<i64>>,
    pub threshold_low: Option<Vec<i64>>,
}

impl SerializableSensorFeatureReport {
    pub fn new(sensor_report: &SensorFeatureReport) -> Self {
        SerializableSensorFeatureReport {
            report_interval: sensor_report.report_interval.clone(),
            reporting_state: sensor_report.reporting_state.map(|state| state.into_primitive()),
            sensitivity: sensor_report.sensitivity.clone(),
            threshold_high: sensor_report.threshold_high.clone(),
            threshold_low: sensor_report.threshold_low.clone(),
        }
    }
}

impl std::convert::TryInto<SensorFeatureReport> for SerializableSensorFeatureReport {
    type Error = anyhow::Error;

    fn try_into(self) -> Result<SensorFeatureReport, Self::Error> {
        Ok(SensorFeatureReport {
            report_interval: self.report_interval.clone(),
            reporting_state: match self.reporting_state {
                Some(r) => {
                    Some(SensorReportingState::from_primitive(r).ok_or(anyhow!(
                        "Invalid reporting_state value: {:?}",
                        self.reporting_state
                    ))?)
                }
                None => None,
            },
            sensitivity: self.sensitivity.clone(),
            threshold_high: self.threshold_high.clone(),
            threshold_low: self.threshold_low.clone(),
        })
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SerializableFeatureReport {
    pub sensor: Option<SerializableSensorFeatureReport>,
}

impl SerializableFeatureReport {
    pub fn new(report: &FeatureReport) -> Self {
        SerializableFeatureReport {
            sensor: report.sensor.as_ref().map(SerializableSensorFeatureReport::new),
        }
    }
}

impl std::convert::TryInto<FeatureReport> for SerializableFeatureReport {
    type Error = anyhow::Error;

    fn try_into(self) -> Result<FeatureReport, Self::Error> {
        Ok(FeatureReport {
            sensor: match self.sensor {
                Some(s) => Some(s.try_into()?),
                None => None,
            },
        })
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableContactInputReport {
    pub contact_id: Option<u32>,
    pub position_x: Option<i64>,
    pub position_y: Option<i64>,
    pub pressure: Option<i64>,
    pub contact_width: Option<i64>,
    pub contact_height: Option<i64>,
}

impl SerializableContactInputReport {
    pub fn new(contact_report: &ContactInputReport) -> Self {
        SerializableContactInputReport {
            contact_id: contact_report.contact_id,
            position_x: contact_report.position_x,
            position_y: contact_report.position_y,
            pressure: contact_report.pressure,
            contact_width: contact_report.contact_width,
            contact_height: contact_report.contact_height,
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableTouchInputReport {
    pub contacts: Option<Vec<SerializableContactInputReport>>,
    pub pressed_buttons: Option<Vec<u8>>,
}

impl SerializableTouchInputReport {
    pub fn new(touch_report: &TouchInputReport) -> Self {
        SerializableTouchInputReport {
            contacts: touch_report
                .contacts
                .as_ref()
                .map(|values| values.iter().map(SerializableContactInputReport::new).collect()),
            pressed_buttons: touch_report.pressed_buttons.clone(),
        }
    }
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct SerializableInputReport {
    pub event_time: Option<i64>,
    pub sensor: Option<SerializableSensorInputReport>,
    pub touch: Option<SerializableTouchInputReport>,
    pub trace_id: Option<u64>,
}

impl SerializableInputReport {
    pub fn new(report: &InputReport) -> Self {
        SerializableInputReport {
            event_time: report.event_time,
            sensor: report.sensor.as_ref().map(SerializableSensorInputReport::new),
            touch: report.touch.as_ref().map(SerializableTouchInputReport::new),
            trace_id: report.trace_id,
        }
    }
}
