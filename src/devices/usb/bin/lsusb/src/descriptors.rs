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

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct DeviceDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub bcd_usb: u16,
    pub b_device_class: u8,
    pub b_device_sub_class: u8,
    pub b_device_protocol: u8,
    pub b_max_packet_size0: u8,
    pub id_vendor: u16,
    pub id_product: u16,
    pub bcd_device: u16,
    pub i_manufacturer: u8,
    pub i_product: u8,
    pub i_serial_number: u8,
    pub b_num_configurations: u8,
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct ConfigurationDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub w_total_length: u16,
    pub b_num_interfaces: u8,
    pub b_configuration_value: u8,
    pub i_configuration: u8,
    pub bm_attributes: u8,
    pub b_max_power: u8,
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct InterfaceInfoDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub b_interface_number: u8,
    pub b_alternate_setting: u8,
    pub b_num_endpoints: u8,
    pub b_interface_class: u8,
    pub b_interface_sub_class: u8,
    pub b_interface_protocol: u8,
    pub i_interface: u8,
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct EndpointInfoDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub b_endpoint_address: u8,
    pub bm_attributes: u8,
    pub w_max_packet_size: u16,
    pub b_interval: u8,
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct HidDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub bcd_hid: u16,
    pub b_country_code: u8,
    pub b_num_descriptors: u8,
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct SsEpCompDescriptorInfo {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub b_max_burst: u8,
    pub bm_attributes: u8,
    pub w_bytes_per_interval: u8,
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct SsIsochEpCompDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub w_reserved: u16,
    pub dw_bytes_per_interval: u32,
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct InterfaceAssocDescriptor {
    pub b_length: u8,
    pub b_descriptor_type: u8,
    pub b_first_interface: u8,
    pub b_interface_count: u8,
    pub b_function_class: u8,
    pub b_function_sub_class: u8,
    pub b_function_protocol: u8,
    pub i_function: u8,
}

#[repr(C, packed)]
#[derive(AsBytes, FromBytes)]
pub struct HidDescriptorEntry {
    pub b_descriptor_type: u8,
    pub w_descriptor_length: u16,
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
