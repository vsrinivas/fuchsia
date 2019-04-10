// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{bail, Error},
    fidl_fuchsia_bluetooth_control as fidl_control,
    fuchsia_bluetooth::{
        assigned_numbers::find_service_uuid,
        types::Bool,
        util::{clone_bt_fidl_bool, clone_host_state},
    },
    std::fmt::{self, Write},
};

pub struct AdapterInfo(fidl_control::AdapterInfo);

impl From<fidl_control::AdapterInfo> for AdapterInfo {
    fn from(b: fidl_control::AdapterInfo) -> AdapterInfo {
        AdapterInfo(b)
    }
}
impl Into<fidl_control::AdapterInfo> for AdapterInfo {
    fn into(self) -> fidl_control::AdapterInfo {
        self.0
    }
}

impl fmt::Display for AdapterInfo {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        writeln!(fmt, "Adapter:")?;
        writeln!(fmt, "\tIdentifier:\t{}", self.0.identifier)?;
        writeln!(fmt, "\tAddress:\t{}", self.0.address)?;
        writeln!(fmt, "\tTechnology:\t{:?}", self.0.technology)?;
        if let Some(ref state) = self.0.state {
            for line in AdapterState::from(clone_host_state(state)).to_string().lines() {
                writeln!(fmt, "\t{}", line)?;
            }
        }
        Ok(())
    }
}

pub struct AdapterState(fidl_control::AdapterState);

impl From<fidl_control::AdapterState> for AdapterState {
    fn from(b: fidl_control::AdapterState) -> AdapterState {
        AdapterState(b)
    }
}
impl Into<fidl_control::AdapterState> for AdapterState {
    fn into(self) -> fidl_control::AdapterState {
        self.0
    }
}

impl fmt::Display for AdapterState {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        if let Some(ref local_name) = self.0.local_name {
            writeln!(fmt, "Local Name:\t{}", local_name)?;
        }
        if let Some(ref discoverable) = self.0.discoverable {
            writeln!(fmt, "Discoverable:\t{}", Bool::from(clone_bt_fidl_bool(discoverable)))?;
        }
        if let Some(ref discovering) = self.0.discovering {
            writeln!(fmt, "Discovering:\t{}", Bool::from(clone_bt_fidl_bool(discovering)))?;
        }
        writeln!(fmt, "Local UUIDs:\t{:#?}", self.0.local_service_uuids)
    }
}

pub struct RemoteDevice(pub fidl_control::RemoteDevice);

impl RemoteDevice {
    /// Construct a concise summary of a `RemoteDevice`'s information.
    pub fn summary(&self) -> String {
        let mut msg = String::new();
        write!(msg, "Device {}", self.0.address).expect("Error occurred writing to String");
        if let Some(rssi) = &self.0.rssi {
            write!(msg, ", RSSI {}", rssi.value).expect("Error occurred writing to String");
        }
        if let Some(name) = &self.0.name {
            write!(msg, ", Name {}", name).expect("Error occurred writing to String");
        }
        if self.0.bonded {
            write!(msg, " [bonded]").expect("Error occurred writing to String");
        }
        if self.0.connected {
            write!(msg, " [connected]").expect("Error occurred writing to String");
        }
        msg
    }
}

impl From<fidl_control::RemoteDevice> for RemoteDevice {
    fn from(b: fidl_control::RemoteDevice) -> RemoteDevice {
        RemoteDevice(b)
    }
}
impl Into<fidl_control::RemoteDevice> for RemoteDevice {
    fn into(self) -> fidl_control::RemoteDevice {
        self.0
    }
}

impl fmt::Display for RemoteDevice {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        writeln!(fmt, "Remote Device:")?;
        writeln!(fmt, "\tIdentifier:\t{}", self.0.identifier)?;
        writeln!(fmt, "\tAddress:\t{}", self.0.address)?;
        writeln!(fmt, "\tTechnology:\t{:?}", self.0.technology)?;
        if let Some(name) = &self.0.name {
            writeln!(fmt, "\tName:\t\t{}", name)?;
        }
        writeln!(fmt, "\tAppearance:\t{:?}", self.0.appearance)?;
        if let Some(rssi) = &self.0.rssi {
            writeln!(fmt, "\tRSSI:\t\t{}", rssi.value)?;
        }
        if let Some(tx_power) = &self.0.tx_power {
            writeln!(fmt, "\tTX Power:\t{}", tx_power.value)?;
        }
        writeln!(fmt, "\tConnected:\t{}", self.0.connected)?;
        writeln!(fmt, "\tBonded:\t\t{}", self.0.bonded)?;
        writeln!(
            fmt,
            "\tServices:\t{:?}",
            self.0
                .service_uuids
                .iter()
                .map(|uuid| find_service_uuid(uuid).map(|an| an.name).unwrap_or(uuid))
                .collect::<Vec<_>>(),
        )?;
        Ok(())
    }
}

pub trait TryInto<T> {
    fn try_into(self) -> Result<T, Error>;
}

#[repr(u32)]
/// Represents the major class of a device as defined by the Bluetooth specification:
/// https://www.bluetooth.com/specifications/assigned-numbers/baseband
pub enum MajorClass {
    Miscellaneous = 0b00000,
    Computer = 0b00001,
    Phone = 0b00010,
    Lan = 0b00011,
    AudioVideo = 0b00100,
    Peripheral = 0b00101,
    Imaging = 0b00110,
    Wearable = 0b00111,
    Toy = 0b01000,
    Health = 0b01001,
    Uncategorized = 0b11111,
}

impl TryInto<MajorClass> for &str {
    fn try_into(self) -> Result<MajorClass, Error> {
        Ok(match &*self.to_uppercase() {
            "MISCELLANEOUS" => MajorClass::Miscellaneous,
            "COMPUTER" => MajorClass::Computer,
            "PHONE" => MajorClass::Phone,
            "LAN" => MajorClass::Lan,
            "AUDIOVIDEO" => MajorClass::AudioVideo,
            "PERIPHERAL" => MajorClass::Peripheral,
            "IMAGING" => MajorClass::Imaging,
            "WEARABLE" => MajorClass::Wearable,
            "TOY" => MajorClass::Toy,
            "HEALTH" => MajorClass::Health,
            "UNCATEGORIZED" => MajorClass::Uncategorized,
            v => bail!("Invalid Major Class value provided: '{}'", v),
        })
    }
}

/// Represents the minor class associated with a device. The meaning of the minor class value
/// depends on the major class of the device. Values defined in the Bluetooth specification:
/// https://www.bluetooth.com/specifications/assigned-numbers/baseband
pub struct MinorClass(u32);

impl MinorClass {
    pub fn not_set() -> MinorClass {
        MinorClass(0)
    }
}

impl TryInto<MinorClass> for &str {
    fn try_into(self) -> Result<MinorClass, Error> {
        let value = self.parse()?;
        if value > 0b11_1111 {
            bail!("Invalid Minor Class value (number too large)")
        }
        Ok(MinorClass(value))
    }
}

/// Represents the complete list of service classes assigned to a device. Values defined in the
/// Bluetooth specification: https://www.bluetooth.com/specifications/assigned-numbers/baseband
pub struct ServiceClass(u32);

impl ServiceClass {
    const LIMITED_DISCOVERABLE_MODE: u32 = 1;
    const POSITIONING: u32 = 1 << 3;
    const NETWORKING: u32 = 1 << 4;
    const RENDERING: u32 = 1 << 5;
    const CAPTURING: u32 = 1 << 6;
    const OBJECT_TRANSFER: u32 = 1 << 7;
    const AUDIO: u32 = 1 << 8;
    const TELEPHONY: u32 = 1 << 9;
    const INFORMATION: u32 = 1 << 10;
}

// Construct a `ServiceClass` from an `Iterator` over `&str` values
impl<'a, I> TryInto<ServiceClass> for I
where
    I: Iterator<Item = &'a &'a str>,
{
    fn try_into(self) -> Result<ServiceClass, Error> {
        let mut invalid_inputs = vec![];
        let mut class_value = 0;
        for input in self {
            let value = match &*input.to_uppercase() {
                "LIMITED_DISCOVERABLE_MODE" => ServiceClass::LIMITED_DISCOVERABLE_MODE,
                "POSITIONING" => ServiceClass::POSITIONING,
                "NETWORKING" => ServiceClass::NETWORKING,
                "RENDERING" => ServiceClass::RENDERING,
                "CAPTURING" => ServiceClass::CAPTURING,
                "OBJECT_TRANSFER" => ServiceClass::OBJECT_TRANSFER,
                "AUDIO" => ServiceClass::AUDIO,
                "TELEPHONY" => ServiceClass::TELEPHONY,
                "INFORMATION" => ServiceClass::INFORMATION,
                _ => {
                    invalid_inputs.push(input);
                    continue;
                }
            };
            class_value |= value;
        }
        if !invalid_inputs.is_empty() {
            bail!("Invalid Service Class value(s) provided: {:?}", invalid_inputs);
        }
        Ok(ServiceClass(class_value))
    }
}

pub struct DeviceClass {
    pub major: MajorClass,
    pub minor: MinorClass,
    pub service: ServiceClass,
}

impl From<DeviceClass> for fidl_control::DeviceClass {
    fn from(cod: DeviceClass) -> Self {
        let value = (cod.minor.0 & 0b11_1111) << 2 // bits [2,7] represent the minor class
            | (((cod.major as u32) & 0b1_1111) << 8) // bits [8,12] represent the major class
            | (cod.service.0 & 0b111_1111_1111) << 13; // bits [13,23] represent the service class
        fidl_control::DeviceClass { value }
    }
}
