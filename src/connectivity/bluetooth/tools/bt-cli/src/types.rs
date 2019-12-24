// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_control as fidl_control,
};

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
            v => return Err(format_err!("Invalid Major Class value provided: '{}'", v)),
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
            return Err(format_err!("Invalid Minor Class value (number too large)"));
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
            return Err(format_err!(
                "Invalid Service Class value(s) provided: {:?}",
                invalid_inputs
            ));
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
