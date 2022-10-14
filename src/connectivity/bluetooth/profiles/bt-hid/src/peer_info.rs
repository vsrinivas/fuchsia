// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::profile::{Attribute, DataElementConversionError},
    fuchsia_bluetooth::types::PeerId,
    num_derive::FromPrimitive,
    num_traits::FromPrimitive,
    tracing::{debug, info, warn},
};

use crate::{
    descriptor::{DescriptorList, DescriptorReadError},
    sdp_data,
};

#[derive(Debug)]
enum HidDataElementError {
    // The general error for an inability to convert a data element to the expected native type.
    DataElementConversion(DataElementConversionError),
    // A more specific error describing a failure to read or parse a HID descriptor.
    DescriptorRead(DescriptorReadError),
}

impl From<DataElementConversionError> for HidDataElementError {
    fn from(error: DataElementConversionError) -> Self {
        Self::DataElementConversion(error)
    }
}

impl From<DescriptorReadError> for HidDataElementError {
    fn from(error: DescriptorReadError) -> Self {
        Self::DescriptorRead(error)
    }
}

#[derive(Debug, PartialEq)]
pub enum PeerInfoError {
    UnexpectedProtocol {
        protocols: Option<Vec<bredr::ProtocolDescriptor>>,
        attributes: Vec<bredr::Attribute>,
    },
    UnparseableSdpRecord {
        protocols: Option<Vec<bredr::ProtocolDescriptor>>,
        attributes: Vec<bredr::Attribute>,
    },
}

#[derive(Clone, Copy, Debug, FromPrimitive)]
enum HidAttribute {
    // TODO(fxb/97186) Parse the rest of the SDP record if necessary.
    ParserVersion = 0x201,
    DeviceSubclass = 0x202,
    CountryCode = 0x203,
    VirtualCable = 0x204,
    ReconnectInitiate = 0x205,
    DescriptorList = 0x206,
    // TODO(fxb/96992) implement HIDLangIDBase
    BatteryPower = 0x209,
    RemoteWake = 0x20A,
    SupervisionTimeout = 0x20C,
    NormallyConnectable = 0x20D,
    BootDevice = 0x20E,
    SsrHostMaxLatency = 0x20F,
    SsrHostMinTimeout = 0x210,
}

/// Info parsed from the SDP record for the peer.
#[derive(Debug, Default, PartialEq)]
pub struct PeerInfo {
    // TODO(fxb/97186) Parse the rest of the SDP record if necessary.
    pub parser_version: Option<u16>,
    pub device_subclass: Option<u8>,
    pub country_code: Option<u8>,
    pub virtual_cable: Option<bool>,
    pub reconnect_initiate: Option<bool>,
    pub descriptor_list: Option<DescriptorList>,
    //TODO(fxb/96992) implement HIDLangIDBase
    pub battery_power: Option<bool>,
    pub remote_wake: Option<bool>,
    pub supervision_timeout: Option<u16>,
    pub normally_connectable: Option<bool>,
    pub boot_device: Option<bool>,
    pub ssr_host_max_latency: Option<u16>,
    pub ssr_host_min_timeout: Option<u16>,
}

impl PeerInfo {
    fn is_complete(&self) -> bool {
        let complete =
            // TODO(fxb/97186) Parse the rest of the SDP record if necessary.
            self.parser_version.is_some()
            && self.device_subclass.is_some()
            && self.country_code.is_some()
            && self.virtual_cable.is_some()
            && self.reconnect_initiate.is_some()
            && self.descriptor_list.is_some()
            // TODO(fxb/96992) implement HIDLangIDBase
            // battery_power is optional
            // remote_wake is optional
            // supervision_timeout is optional
            // normally_connectable is optional
            && self.boot_device.is_some()
            // ssr_host_max_latency is optional
            // ssr_host_min_timeout is optional
            ;

        complete
    }

    fn set_attribute(
        &mut self,
        attribute_id: HidAttribute,
        attribute: Attribute,
    ) -> Result<(), HidDataElementError> {
        match attribute_id {
            HidAttribute::ParserVersion => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.parser_version = Some(value);
            }
            HidAttribute::DeviceSubclass => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.device_subclass = Some(value);
            }
            HidAttribute::CountryCode => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.country_code = Some(value);
            }
            HidAttribute::VirtualCable => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.virtual_cable = Some(value);
            }
            HidAttribute::ReconnectInitiate => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.reconnect_initiate = Some(value);
            }
            HidAttribute::DescriptorList => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.descriptor_list = Some(value);
            }
            HidAttribute::BatteryPower => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.battery_power = Some(value);
            }
            HidAttribute::RemoteWake => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.remote_wake = Some(value);
            }
            HidAttribute::SupervisionTimeout => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.supervision_timeout = Some(value);
            }
            HidAttribute::NormallyConnectable => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.normally_connectable = Some(value);
            }
            HidAttribute::BootDevice => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.boot_device = Some(value);
            }
            HidAttribute::SsrHostMaxLatency => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.ssr_host_max_latency = Some(value);
            }
            HidAttribute::SsrHostMinTimeout => {
                let value = attribute.element.try_into().map_err(HidDataElementError::from)?;
                self.ssr_host_min_timeout = Some(value);
            }
        }

        Ok(())
    }

    fn get_attribute_id_and_set_attribute(&mut self, attribute: Attribute, peer_id: PeerId) {
        // TODO(fxb/97186) Parse the rest of the SDP record if necessary.
        let attribute_id = match HidAttribute::from_u16(attribute.id) {
            Some(attr) => attr,
            None => {
                info!(
                    "Got unknown attribute ID {:} while reading SDP record from peer {:}.",
                    attribute.id, peer_id
                );
                return;
            }
        };

        if let Err(err) = self.set_attribute(attribute_id, attribute) {
            warn!(
                "Unable to read SDP attribute with ID {:?} from peer {:} with error {:?}",
                attribute_id, peer_id, err
            );
        }
    }

    pub fn new_from_sdp_record(
        protocols: &Option<Vec<bredr::ProtocolDescriptor>>,
        attributes: &Vec<bredr::Attribute>,
        peer_id: PeerId,
    ) -> Result<Self, PeerInfoError> {
        debug!("Parsing SDP record {:?}, {:?} for peer {:}", protocols, attributes, peer_id);

        if protocols != &sdp_data::expected_protocols() {
            warn!("Got unexpected protocol stack {:?} for peer {:?}", protocols, peer_id);
            return Err(PeerInfoError::UnexpectedProtocol {
                protocols: protocols.clone(),
                attributes: attributes.clone(),
            });
        }

        let mut peer_info: PeerInfo = Default::default();
        for attribute in attributes {
            peer_info.get_attribute_id_and_set_attribute(attribute.into(), peer_id);
        }

        if !peer_info.is_complete() {
            warn!(
                "Got incomplete or unparseable SDP record {:?} parsed as {:?} for peer {:}",
                attributes, peer_info, peer_id
            );
            return Err(PeerInfoError::UnparseableSdpRecord {
                protocols: protocols.clone(),
                attributes: attributes.clone(),
            });
        };

        debug!(
            "Parsed SDP record {:?}, {:?} into {:?} for peer {:}",
            protocols, attributes, peer_info, peer_id
        );

        Ok(peer_info)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use sdp_data::test::*;
    use sdp_data::*;

    #[fuchsia::test]
    fn success() {
        let peer_info =
            PeerInfo::new_from_sdp_record(&expected_protocols(), &attributes(), PeerId(1));
        assert_eq!(peer_info, Ok(expected_peer_info()));
    }

    #[fuchsia::test]
    fn success_with_reordered_attributes() {
        let mut reordered_attributes = attributes();
        reordered_attributes.reverse();

        let peer_info =
            PeerInfo::new_from_sdp_record(&expected_protocols(), &reordered_attributes, PeerId(1));
        assert_eq!(peer_info, Ok(expected_peer_info()));
    }

    #[fuchsia::test]
    fn success_with_missing_optional_attribute() {
        let mut expected_peer_info = expected_peer_info();
        expected_peer_info.ssr_host_min_timeout = None;

        let peer_info = PeerInfo::new_from_sdp_record(
            &expected_protocols(),
            &attributes_missing_optional(),
            PeerId(1),
        );
        assert_eq!(peer_info, Ok(expected_peer_info));
    }

    #[fuchsia::test]
    fn failure_with_missing_required_attribute() {
        let peer_info = PeerInfo::new_from_sdp_record(
            &expected_protocols(),
            &attributes_missing_required(),
            PeerId(1),
        );
        assert_eq!(
            peer_info,
            Err(PeerInfoError::UnparseableSdpRecord {
                protocols: expected_protocols(),
                attributes: attributes_missing_required(),
            })
        );
    }

    #[fuchsia::test]
    fn failure_with_unexpected_protocols() {
        let mut expected_protocols_internal = expected_protocols().unwrap();
        let _ = expected_protocols_internal.pop();
        let unexpected_protocols = Some(expected_protocols_internal);

        let peer_info = PeerInfo::new_from_sdp_record(
            &unexpected_protocols,
            &attributes_missing_required(),
            PeerId(1),
        );
        assert_eq!(
            peer_info,
            Err(PeerInfoError::UnexpectedProtocol {
                protocols: unexpected_protocols,
                attributes: attributes_missing_required(),
            })
        );
    }
}
