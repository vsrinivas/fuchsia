// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use num_traits::FromPrimitive;

#[repr(u8)]
#[derive(num_derive::FromPrimitive)]
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
#[repr(C, packed)]
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
#[repr(C, packed)]
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
#[repr(C, packed)]
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
#[repr(C, packed)]
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
#[repr(C, packed)]
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
#[repr(C, packed)]
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
#[repr(C, packed)]
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
#[repr(C, packed)]
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
#[repr(C, packed)]
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

pub struct DescriptorIterator<'a> {
    offset: usize,
    buffer: &'a [u8],
}

impl<'a> DescriptorIterator<'a> {
    pub fn new(buffer: &'a [u8]) -> DescriptorIterator<'a> {
        DescriptorIterator { offset: 0, buffer }
    }
}

pub struct HidDescriptorIter<'a> {
    offset: usize,
    buffer: &'a [u8],
}

impl<'a> HidDescriptorIter<'a> {
    pub fn new(buffer: &'a [u8]) -> HidDescriptorIter<'a> {
        HidDescriptorIter { offset: std::mem::size_of::<HidDescriptor>(), buffer }
    }

    pub fn get(&self) -> HidDescriptor {
        let mut info = [0; std::mem::size_of::<HidDescriptor>()];
        info.copy_from_slice(&self.buffer[0..(std::mem::size_of::<HidDescriptor>())]);
        HidDescriptor::from_array(info)
    }
}

impl<'a> Iterator for HidDescriptorIter<'a> {
    type Item = HidDescriptorEntry;

    fn next(&mut self) -> Option<Self::Item> {
        if self.offset + std::mem::size_of::<HidDescriptorEntry>() > self.buffer.len() {
            return None;
        }

        let mut entry = [0; std::mem::size_of::<HidDescriptorEntry>()];
        entry.copy_from_slice(
            &self.buffer[self.offset..self.offset + std::mem::size_of::<HidDescriptorEntry>()],
        );
        Some(HidDescriptorEntry::from_array(entry))
    }
}

pub enum Descriptor<'a> {
    Config(ConfigurationDescriptor),
    Interface(InterfaceInfoDescriptor),
    InterfaceAssociation(InterfaceAssocDescriptor),
    Endpoint(EndpointInfoDescriptor),
    Hid(HidDescriptorIter<'a>),
    SsEpCompanion(SsEpCompDescriptorInfo),
    SsIsochEpCompanion(SsIsochEpCompDescriptor),
    Unknown(&'a [u8]),
}

impl<'a> Iterator for DescriptorIterator<'a> {
    type Item = Descriptor<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.offset + 2 > self.buffer.len() {
            return None;
        }

        let length = self.buffer[self.offset] as usize;
        let desc_type = self.buffer[self.offset + 1];

        if length == 0 || length > self.buffer.len() {
            return None;
        }

        let desc = match DescriptorType::from_u8(desc_type) {
            Some(DescriptorType::Config) => {
                if length != std::mem::size_of::<ConfigurationDescriptor>() {
                    return None;
                }
                let mut info = [0; std::mem::size_of::<ConfigurationDescriptor>()];
                info.copy_from_slice(&self.buffer[self.offset..self.offset + length]);
                Descriptor::Config(ConfigurationDescriptor::from_array(info))
            }
            Some(DescriptorType::Interface) => {
                if length != std::mem::size_of::<InterfaceInfoDescriptor>() {
                    return None;
                }
                let mut info = [0; std::mem::size_of::<InterfaceInfoDescriptor>()];
                info.copy_from_slice(&self.buffer[self.offset..self.offset + length]);
                Descriptor::Interface(InterfaceInfoDescriptor::from_array(info))
            }
            Some(DescriptorType::Endpoint) => {
                if length != std::mem::size_of::<EndpointInfoDescriptor>() {
                    return None;
                }
                let mut info = [0; std::mem::size_of::<EndpointInfoDescriptor>()];
                info.copy_from_slice(&self.buffer[self.offset..self.offset + length]);
                Descriptor::Endpoint(EndpointInfoDescriptor::from_array(info))
            }
            Some(DescriptorType::Hid) => {
                if length < std::mem::size_of::<HidDescriptor>() {
                    return None;
                }
                Descriptor::Hid(HidDescriptorIter::new(
                    &self.buffer[self.offset..self.offset + length],
                ))
            }
            Some(DescriptorType::SsEpCompanion) => {
                if length != std::mem::size_of::<SsEpCompDescriptorInfo>() {
                    return None;
                }
                let mut info = [0; std::mem::size_of::<SsEpCompDescriptorInfo>()];
                info.copy_from_slice(&self.buffer[self.offset..self.offset + length]);
                Descriptor::SsEpCompanion(SsEpCompDescriptorInfo::from_array(info))
            }
            Some(DescriptorType::SsIsochEpCompanion) => {
                if length != std::mem::size_of::<SsIsochEpCompDescriptor>() {
                    return None;
                }
                let mut info = [0; std::mem::size_of::<SsIsochEpCompDescriptor>()];
                info.copy_from_slice(&self.buffer[self.offset..self.offset + length]);
                Descriptor::SsIsochEpCompanion(SsIsochEpCompDescriptor::from_array(info))
            }
            Some(DescriptorType::InterfaceAssociation) => {
                if length < std::mem::size_of::<InterfaceAssocDescriptor>() {
                    return None;
                }
                let mut info = [0; std::mem::size_of::<InterfaceAssocDescriptor>()];
                info.copy_from_slice(&self.buffer[self.offset..self.offset + length]);
                Descriptor::InterfaceAssociation(InterfaceAssocDescriptor::from_array(info))
            }
            _ => Descriptor::Unknown(&self.buffer[self.offset..self.offset + length]),
        };

        self.offset += length;
        Some(desc)
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
