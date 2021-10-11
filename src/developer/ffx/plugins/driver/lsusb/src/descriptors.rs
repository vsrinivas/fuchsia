// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use num_derive::FromPrimitive;

#[repr(u8)]
#[derive(FromPrimitive)]
pub enum DescriptorType {
    Device = 0x01,
    Config = 0x02,
    r#String = 0x03,
    Interface = 0x04,
    Endpoint = 0x05,
    DeviceQualifier = 0x06,
    OtherSpeedConfig = 0x07,
    InterfacePower = 0x08,
    InterfaceAssociation = 0x0b,
    Hid = 0x21,
    Hidreport = 0x22,
    Hidphysical = 0x23,
    CsInterface = 0x24,
    CsEndpoint = 0x25,
    SsEpCompanion = 0x30,
    SsIsochEpCompanion = 0x31,
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct DeviceDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bcdUSB: u16,
    pub bDeviceClass: u8,
    pub bDeviceSubClass: u8,
    pub bDeviceProtocol: u8,
    pub bMaxPacketSize0: u8,
    pub idVendor: u16,
    pub idProduct: u16,
    pub bcdDevice: u16,
    pub iManufacturer: u8,
    pub iProduct: u8,
    pub iSerialNumber: u8,
    pub bNumConfigurations: u8,
}

impl DeviceDescriptor {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }
    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct ConfigurationDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub wTotalLength: u16,
    pub bNumInterfaces: u8,
    pub bConfigurationValue: u8,
    pub iConfiguration: u8,
    pub bmAttributes: u8,
    pub bMaxPower: u8,
}

impl ConfigurationDescriptor {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }

    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct InterfaceInfoDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bInterfaceNumber: u8,
    pub bAlternateSetting: u8,
    pub bNumEndpoints: u8,
    pub bInterfaceClass: u8,
    pub bInterfaceSubClass: u8,
    pub bInterfaceProtocol: u8,
    pub iInterface: u8,
}

impl InterfaceInfoDescriptor {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }

    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct EndpointInfoDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bEndpointAddress: u8,
    pub bmAttributes: u8,
    pub wMaxPacketSize: u8,
    pub bInterval: u8,
}

impl EndpointInfoDescriptor {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }

    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct HidDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bcdHID: u16,
    pub bCountryCode: u8,
    pub bNumDescriptors: u8,
}

impl HidDescriptor {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }

    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct SsEpCompDescriptorInfo {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bMaxBurst: u8,
    pub bmAttributes: u8,
    pub wBytesPerInterval: u8,
}

impl SsEpCompDescriptorInfo {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }

    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct SsIsochEpCompDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub wReserved: u16,
    pub dwBytesPerInterval: u32,
}

impl SsIsochEpCompDescriptor {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }

    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct InterfaceAssocDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bFirstInterface: u8,
    pub bInterfaceCount: u8,
    pub bFunctionClass: u8,
    pub bFunctionSubClass: u8,
    pub bFunctionProtocol: u8,
    pub iFunction: u8,
}

impl InterfaceAssocDescriptor {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }

    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

#[allow(non_snake_case)]
#[repr(C)]
pub struct HidDescriptorEntry {
    pub bDescriptorType: u8,
    pub wDescriptorLength: u16,
}

impl HidDescriptorEntry {
    pub fn from_array(array: [u8; std::mem::size_of::<Self>()]) -> Self {
        unsafe { std::mem::transmute::<[u8; std::mem::size_of::<Self>()], Self>(array) }
    }

    pub fn to_array(self) -> [u8; std::mem::size_of::<Self>()] {
        unsafe { std::mem::transmute::<Self, [u8; std::mem::size_of::<Self>()]>(self) }
    }
}

pub struct UsbSpeed(pub u32);

impl std::fmt::Display for UsbSpeed {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            1 => write!(f, "FULL"),
            2 => write!(f, "LOW"),
            3 => write!(f, "HIGH"),
            4 => write!(f, "SUPER"),
            _ => write!(f, "<unknown>"),
        }
    }
}

pub const EN_US: u16 = 0x0409;
