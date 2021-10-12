// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    num_traits::FromPrimitive,
    zerocopy::{AsBytes, FromBytes, LayoutVerified},
};

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
#[derive(AsBytes, FromBytes)]
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

#[allow(non_snake_case)]
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
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

#[allow(non_snake_case)]
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
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

#[allow(non_snake_case)]
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct EndpointInfoDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bEndpointAddress: u8,
    pub bmAttributes: u8,
    pub wMaxPacketSize: u8,
    pub bInterval: u8,
}

#[allow(non_snake_case)]
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct HidDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bcdHID: u16,
    pub bCountryCode: u8,
    pub bNumDescriptors: u8,
}

#[allow(non_snake_case)]
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct SsEpCompDescriptorInfo {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub bMaxBurst: u8,
    pub bmAttributes: u8,
    pub wBytesPerInterval: u8,
}

#[allow(non_snake_case)]
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct SsIsochEpCompDescriptor {
    pub bLength: u8,
    pub bDescriptorType: u8,
    pub wReserved: u16,
    pub dwBytesPerInterval: u32,
}

#[allow(non_snake_case)]
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
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

#[allow(non_snake_case)]
#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct HidDescriptorEntry {
    pub bDescriptorType: u8,
    pub wDescriptorLength: u16,
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
    fn new(buffer: &'a [u8]) -> HidDescriptorIter<'a> {
        HidDescriptorIter { offset: std::mem::size_of::<HidDescriptor>(), buffer }
    }

    pub fn get(&self) -> LayoutVerified<&[u8], HidDescriptor> {
        LayoutVerified::new(&self.buffer[0..(std::mem::size_of::<HidDescriptor>())]).unwrap()
    }
}

impl<'a> Iterator for HidDescriptorIter<'a> {
    type Item = LayoutVerified<&'a [u8], HidDescriptorEntry>;

    fn next(&mut self) -> Option<Self::Item> {
        let len = std::mem::size_of::<HidDescriptorEntry>();
        if self.offset + len > self.buffer.len() {
            return None;
        }
        let slice = &self.buffer[self.offset..self.offset + len];
        self.offset += len;

        LayoutVerified::new(slice)
    }
}

pub enum Descriptor<'a> {
    Config(LayoutVerified<&'a [u8], ConfigurationDescriptor>),
    Interface(LayoutVerified<&'a [u8], InterfaceInfoDescriptor>),
    InterfaceAssociation(LayoutVerified<&'a [u8], InterfaceAssocDescriptor>),
    Endpoint(LayoutVerified<&'a [u8], EndpointInfoDescriptor>),
    Hid(HidDescriptorIter<'a>),
    SsEpCompanion(LayoutVerified<&'a [u8], SsEpCompDescriptorInfo>),
    SsIsochEpCompanion(LayoutVerified<&'a [u8], SsIsochEpCompDescriptor>),
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

        let slice = &self.buffer[self.offset..self.offset + length];

        let desc = match DescriptorType::from_u8(desc_type) {
            Some(DescriptorType::Config) => {
                LayoutVerified::new(slice).map(|d| Descriptor::Config(d))
            }
            Some(DescriptorType::Interface) => {
                LayoutVerified::new(slice).map(|d| Descriptor::Interface(d))
            }
            Some(DescriptorType::Endpoint) => {
                LayoutVerified::new(slice).map(|d| Descriptor::Endpoint(d))
            }
            Some(DescriptorType::Hid) => {
                if length < std::mem::size_of::<HidDescriptor>() {
                    None
                } else {
                    Some(Descriptor::Hid(HidDescriptorIter::new(
                        &self.buffer[self.offset..self.offset + length],
                    )))
                }
            }
            Some(DescriptorType::SsEpCompanion) => {
                LayoutVerified::new(slice).map(|d| Descriptor::SsEpCompanion(d))
            }
            Some(DescriptorType::SsIsochEpCompanion) => {
                LayoutVerified::new(slice).map(|d| Descriptor::SsIsochEpCompanion(d))
            }
            Some(DescriptorType::InterfaceAssociation) => {
                LayoutVerified::new(slice).map(|d| Descriptor::InterfaceAssociation(d))
            }
            _ => Some(Descriptor::Unknown(slice)),
        };

        self.offset += length;
        desc
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
