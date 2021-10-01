// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    bitflags::bitflags,
    fidl_fuchsia_bluetooth_bredr::*,
    fuchsia_bluetooth::{
        profile::{elem_to_profile_descriptor, psm_from_protocol, Psm},
        types::Uuid,
    },
    profile_client::ProfileClient,
    std::fmt::Debug,
    tracing::info,
};

bitflags! {
    pub struct AvrcpTargetFeatures: u16 {
        const CATEGORY1         = 1 << 0;
        const CATEGORY2         = 1 << 1;
        const CATEGORY3         = 1 << 2;
        const CATEGORY4         = 1 << 3;
        const PLAYERSETTINGS    = 1 << 4;
        const GROUPNAVIGATION   = 1 << 5;
        const SUPPORTSBROWSING  = 1 << 6;
        const SUPPORTSMULTIPLEMEDIAPLAYERS = 1 << 7;
        const SUPPORTSCOVERART  = 1 << 8;
        // 9-15 Reserved
    }
}

bitflags! {
    pub struct AvrcpControllerFeatures: u16 {
        const CATEGORY1         = 1 << 0;
        const CATEGORY2         = 1 << 1;
        const CATEGORY3         = 1 << 2;
        const CATEGORY4         = 1 << 3;
        // 4-5 RESERVED
        const SUPPORTSBROWSING  = 1 << 6;
        const SUPPORTSCOVERARTGETIMAGEPROPERTIES = 1 << 7;
        const SUPPORTSCOVERARTGETIMAGE = 1 << 8;
        const SUPPORTSCOVERARTGETLINKEDTHUMBNAIL = 1 << 9;
        // 10-15 RESERVED
    }
}

impl AvrcpControllerFeatures {
    pub fn supports_cover_art(&self) -> bool {
        self.contains(
            AvrcpControllerFeatures::SUPPORTSCOVERARTGETIMAGE
                | AvrcpControllerFeatures::SUPPORTSCOVERARTGETIMAGEPROPERTIES
                | AvrcpControllerFeatures::SUPPORTSCOVERARTGETLINKEDTHUMBNAIL,
        )
    }
}

const SDP_SUPPORTED_FEATURES: u16 = 0x0311;

const AV_REMOTE_TARGET_CLASS: u16 = 0x110c;
const AV_REMOTE_CLASS: u16 = 0x110e;
const AV_REMOTE_CONTROLLER_CLASS: u16 = 0x110f;

/// The common service definition for AVRCP Target and Controller.
/// AVRCP 1.6, Section 8.
fn build_common_service_definition() -> ServiceDefinition {
    ServiceDefinition {
        protocol_descriptor_list: Some(vec![
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::L2Cap,
                params: vec![DataElement::Uint16(PSM_AVCTP as u16)],
            },
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::Avctp,
                params: vec![DataElement::Uint16(0x0103)], // Indicate v1.3
            },
        ]),
        profile_descriptors: Some(vec![ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AvRemoteControl,
            major_version: 1,
            minor_version: 6,
        }]),
        ..ServiceDefinition::EMPTY
    }
}

/// Make the SDP definition for the AVRCP Controller service.
/// AVRCP 1.6, Section 8, Table 8.1.
fn make_controller_service_definition() -> ServiceDefinition {
    let mut service = build_common_service_definition();

    let service_class_uuids: Vec<fidl_fuchsia_bluetooth::Uuid> =
        vec![Uuid::new16(AV_REMOTE_CLASS).into(), Uuid::new16(AV_REMOTE_CONTROLLER_CLASS).into()];
    service.service_class_uuids = Some(service_class_uuids);

    service.additional_attributes = Some(vec![Attribute {
        id: SDP_SUPPORTED_FEATURES, // SDP Attribute "SUPPORTED FEATURES"
        element: DataElement::Uint16(
            AvrcpControllerFeatures::CATEGORY1.bits() | AvrcpControllerFeatures::CATEGORY2.bits(),
        ),
    }]);

    service
}

/// Make the SDP definition for the AVRCP Target service.
/// AVRCP 1.6, Section 8, Table 8.2.
fn make_target_service_definition() -> ServiceDefinition {
    let mut service = build_common_service_definition();

    let service_class_uuids: Vec<fidl_fuchsia_bluetooth::Uuid> =
        vec![Uuid::new16(AV_REMOTE_TARGET_CLASS).into()];
    service.service_class_uuids = Some(service_class_uuids);

    service.additional_attributes = Some(vec![Attribute {
        id: SDP_SUPPORTED_FEATURES, // SDP Attribute "SUPPORTED FEATURES"
        element: DataElement::Uint16(
            AvrcpTargetFeatures::CATEGORY1.bits()
                | AvrcpTargetFeatures::CATEGORY2.bits()
                | AvrcpTargetFeatures::SUPPORTSBROWSING.bits(),
        ),
    }]);

    service.additional_protocol_descriptor_lists = Some(vec![vec![
        ProtocolDescriptor {
            protocol: ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(PSM_AVCTP_BROWSE as u16)],
        },
        ProtocolDescriptor {
            protocol: ProtocolIdentifier::Avctp,
            params: vec![DataElement::Uint16(0x0103)],
        },
    ]]);

    service
}

#[derive(PartialEq, Hash, Clone, Copy)]
pub struct AvrcpProtocolVersion(pub u8, pub u8);

impl std::fmt::Debug for AvrcpProtocolVersion {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}.{}", self.0, self.1)
    }
}

#[derive(Debug, PartialEq, Clone, Copy)]
pub enum AvrcpService {
    Target {
        features: AvrcpTargetFeatures,
        psm: Psm,
        protocol_version: AvrcpProtocolVersion,
    },
    Controller {
        features: AvrcpControllerFeatures,
        psm: Psm,
        protocol_version: AvrcpProtocolVersion,
    },
}

impl AvrcpService {
    pub fn from_search_result(
        protocol: Vec<ProtocolDescriptor>,
        attributes: Vec<Attribute>,
    ) -> Result<AvrcpService, Error> {
        let mut features: Option<u16> = None;
        let mut service_uuids: Option<Vec<Uuid>> = None;
        let mut profile: Option<ProfileDescriptor> = None;

        // Both the `protocol` and `attributes` should contain the primary protocol descriptor. It
        // is simpler to parse the former.
        let protocol = protocol.iter().map(Into::into).collect();
        let psm =
            psm_from_protocol(&protocol).ok_or(format_err!("AVRCP Service with no L2CAP PSM"))?;

        for attr in attributes {
            match attr.id {
                ATTR_SERVICE_CLASS_ID_LIST => {
                    if let DataElement::Sequence(seq) = attr.element {
                        let uuids: Vec<Uuid> = seq
                            .into_iter()
                            .flatten()
                            .filter_map(|item| match *item {
                                DataElement::Uuid(uuid) => Some(uuid.into()),
                                _ => None,
                            })
                            .collect();
                        if uuids.len() > 0 {
                            service_uuids = Some(uuids);
                        }
                    }
                }
                ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST => {
                    if let DataElement::Sequence(profiles) = attr.element {
                        for elem in profiles {
                            let elem = elem.expect("DataElement sequence elements should exist");
                            profile = elem_to_profile_descriptor(&*elem);
                        }
                    }
                }
                SDP_SUPPORTED_FEATURES => {
                    if let DataElement::Uint16(value) = attr.element {
                        features = Some(value);
                    }
                }
                _ => {}
            }
        }

        let (service_uuids, features, profile) = match (service_uuids, features, profile) {
            (Some(s), Some(f), Some(p)) => (s, f, p),
            (s, f, p) => {
                let err = format_err!(
                    "{}{}{}missing in service attrs",
                    if s.is_some() { "" } else { "Class UUIDs " },
                    if f.is_some() { "" } else { "Features " },
                    if p.is_some() { "" } else { "Profile " }
                );
                return Err(err);
            }
        };

        // The L2CAP PSM should always be PSM_AVCTP. However, in unexpected cases, the peer may try
        // to advertise a different PSM for its AVRCP service.
        if psm != Psm::AVCTP {
            info!("Found AVRCP Service with non standard PSM: {:?}", psm);
        }

        let protocol_version = AvrcpProtocolVersion(profile.major_version, profile.minor_version);

        if service_uuids.contains(&Uuid::new16(AV_REMOTE_TARGET_CLASS)) {
            let features = AvrcpTargetFeatures::from_bits_truncate(features);
            return Ok(AvrcpService::Target { features, psm, protocol_version });
        } else if service_uuids.contains(&Uuid::new16(AV_REMOTE_CLASS))
            || service_uuids.contains(&Uuid::new16(AV_REMOTE_CONTROLLER_CLASS))
        {
            let features = AvrcpControllerFeatures::from_bits_truncate(features);
            return Ok(AvrcpService::Controller { features, psm, protocol_version });
        }
        Err(format_err!("Failed to find any applicable services for AVRCP"))
    }
}

pub fn connect_and_advertise() -> Result<(ProfileProxy, ProfileClient), Error> {
    let profile_svc = fuchsia_component::client::connect_to_protocol::<ProfileMarker>()
        .context("Failed to connect to Bluetooth profile service")?;

    const SEARCH_ATTRIBUTES: [u16; 5] = [
        ATTR_SERVICE_CLASS_ID_LIST,
        ATTR_PROTOCOL_DESCRIPTOR_LIST,
        ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
        ATTR_ADDITIONAL_PROTOCOL_DESCRIPTOR_LIST,
        SDP_SUPPORTED_FEATURES,
    ];

    let service_defs = vec![make_controller_service_definition(), make_target_service_definition()];
    let channel_parameters = ChannelParameters {
        channel_mode: Some(ChannelMode::EnhancedRetransmission),
        ..ChannelParameters::EMPTY
    };
    let mut profile_client =
        ProfileClient::advertise(profile_svc.clone(), &service_defs, channel_parameters)?;

    profile_client
        .add_search(ServiceClassProfileIdentifier::AvRemoteControl, &SEARCH_ATTRIBUTES)?;

    info!("Registered service search & advertisement");

    Ok((profile_svc, profile_client))
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    fn build_attributes(
        service_class: bool,
        profile_descriptor: bool,
        sdp_features: bool,
    ) -> Vec<Attribute> {
        let mut attrs = Vec::new();
        if service_class {
            attrs.push(Attribute {
                id: ATTR_SERVICE_CLASS_ID_LIST,
                element: DataElement::Sequence(vec![Some(Box::new(DataElement::Uuid(
                    Uuid::new16(AV_REMOTE_TARGET_CLASS).into(),
                )))]),
            });
        }
        if profile_descriptor {
            attrs.push(Attribute {
                id: ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
                element: DataElement::Sequence(vec![Some(Box::new(DataElement::Sequence(vec![
                    Some(Box::new(DataElement::Uuid(Uuid::new16(4366).into()))),
                    Some(Box::new(DataElement::Uint16(0xffff))),
                ])))]),
            });
        }

        if sdp_features {
            attrs.push(Attribute {
                id: SDP_SUPPORTED_FEATURES, // SDP Attribute "SUPPORTED FEATURES"
                element: DataElement::Uint16(0xffff),
            });
        }
        attrs
    }

    #[fuchsia::test]
    fn service_from_search_result() {
        let attributes = build_attributes(true, true, true);
        let protocol = vec![ProtocolDescriptor {
            protocol: ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(20)], // Random PSM is still OK.
        }];
        let service = AvrcpService::from_search_result(protocol, attributes);
        assert_matches!(service, Ok(_));
    }

    #[fuchsia::test]
    fn service_with_missing_features_returns_none() {
        let no_service_class = build_attributes(false, true, true);
        let protocol = vec![ProtocolDescriptor {
            protocol: ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(20)], // Random PSM is still OK.
        }];
        let service = AvrcpService::from_search_result(protocol, no_service_class);
        assert_matches!(service, Err(_));

        let no_profile_descriptor = build_attributes(true, false, true);
        let protocol = vec![ProtocolDescriptor {
            protocol: ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(20)], // Random PSM is still OK.
        }];
        let service = AvrcpService::from_search_result(protocol, no_profile_descriptor);
        assert_matches!(service, Err(_));

        let no_sdp_features = build_attributes(true, true, false);
        let protocol = vec![ProtocolDescriptor {
            protocol: ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(20)], // Random PSM is still OK.
        }];
        let service = AvrcpService::from_search_result(protocol, no_sdp_features);
        assert_matches!(service, Err(_));
    }

    #[test]
    fn service_with_missing_protocol_returns_none() {
        let attributes = build_attributes(true, true, true);
        let protocol = vec![];
        let service = AvrcpService::from_search_result(protocol, attributes);
        assert_matches!(service, Err(_));
    }
}
