// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    bt_rfcomm::profile::{is_rfcomm_protocol, server_channel_from_protocol},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::profile::*,
    std::{
        collections::HashSet,
        convert::{TryFrom, TryInto},
        iter::FromIterator,
    },
};

use crate::types::ServiceRecord;

/// Builds the L2Cap Protocol Descriptor from the provided `psm`.
pub fn build_l2cap_descriptor(psm: Psm) -> Vec<bredr::ProtocolDescriptor> {
    vec![bredr::ProtocolDescriptor {
        protocol: bredr::ProtocolIdentifier::L2Cap,
        params: vec![bredr::DataElement::Uint16(psm.into())],
    }]
}

fn parse_service_definition(
    def: &bredr::ServiceDefinition,
) -> Result<ServiceRecord, anyhow::Error> {
    let definition: ServiceDefinition = def.try_into()?;
    // Parse the service class UUIDs into ServiceClassProfileIdentifiers.
    let svc_ids = {
        let uuids_vec = definition
            .service_class_uuids
            .iter()
            .map(|uuid| bredr::ServiceClassProfileIdentifier::try_from(uuid.clone()))
            .collect::<Result<Vec<_>, _>>()?;
        HashSet::from_iter(uuids_vec)
    };
    if svc_ids.is_empty() {
        return Err(format_err!("There must be at least one service class UUID"));
    };

    // The primary protocol may be empty and not specify a PSM. However, in the case of RFCOMM, it
    // must be fully populated.
    let primary_protocol = definition.protocol_descriptor_list.clone();
    if is_rfcomm_protocol(&primary_protocol) {
        let _channel_number = server_channel_from_protocol(&primary_protocol)
            .ok_or(format_err!("Invalid RFCOMM descriptor"))?;
    }

    // Convert (potential) additional PSMs into local Psm type.
    let additional_psms = definition.additional_psms();

    Ok(ServiceRecord::new(
        svc_ids,
        primary_protocol,
        additional_psms,
        definition.profile_descriptors,
        definition.additional_attributes,
    ))
}

pub fn parse_service_definitions(
    svc_defs: Vec<bredr::ServiceDefinition>,
) -> Result<Vec<ServiceRecord>, anyhow::Error> {
    let mut parsed_svc_defs = vec![];

    for def in &svc_defs {
        let parsed_def = parse_service_definition(def)?;
        parsed_svc_defs.push(parsed_def);
    }

    Ok(parsed_svc_defs)
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use {bt_rfcomm::ServerChannel, fidl::encoding::Decodable, fuchsia_bluetooth::types::Uuid};

    const SDP_SUPPORTED_FEATURES: u16 = 0x0311;

    /// Builds the smallest service definition that is valid - only the Service Class IDs.
    /// Returns the definition and the expected parsed ServiceRecord.
    fn minimal_service_definition() -> (bredr::ServiceDefinition, ServiceRecord) {
        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![
                Uuid::new16(bredr::ServiceClassProfileIdentifier::Headset as u16).into(),
                Uuid::new16(bredr::ServiceClassProfileIdentifier::Handsfree as u16).into(),
            ]),
            ..bredr::ServiceDefinition::EMPTY
        };

        let ids = vec![
            bredr::ServiceClassProfileIdentifier::Headset,
            bredr::ServiceClassProfileIdentifier::Handsfree,
        ]
        .into_iter()
        .collect();
        let record = ServiceRecord::new(ids, vec![], HashSet::new(), vec![], vec![]);

        (def, record)
    }

    /// Builds an A2DP Sink Service Definition and the expected parsed ServiceRecord.
    /// Uses the provided `psm` for the service.
    pub(crate) fn a2dp_service_definition(psm: Psm) -> (bredr::ServiceDefinition, ServiceRecord) {
        let prof_descs = vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        }];
        let protocol_descriptor_list = vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![bredr::DataElement::Uint16(psm.into())],
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Avdtp,
                params: vec![bredr::DataElement::Uint16(0x0103)], // Indicate v1.3
            },
        ];
        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![Uuid::new16(0x110B).into()]), // Audio Sink UUID
            protocol_descriptor_list: Some(protocol_descriptor_list.clone()),
            profile_descriptors: Some(prof_descs.clone()),
            additional_attributes: Some(vec![]),
            ..bredr::ServiceDefinition::EMPTY
        };

        let a2dp_ids = vec![bredr::ServiceClassProfileIdentifier::AudioSink].into_iter().collect();
        let primary_protocol = protocol_descriptor_list.iter().map(Into::into).collect();
        let record =
            ServiceRecord::new(a2dp_ids, primary_protocol, HashSet::new(), prof_descs, vec![]);

        (def, record)
    }

    /// Builds an example AVRCP Service Definition and the expected parsed ServiceRecord.
    /// This is done in the same function to maintain data consistency, such that if the
    /// parsing implementation changes, only this builder will need to be updated.
    fn avrcp_service_definition() -> (bredr::ServiceDefinition, ServiceRecord) {
        use bredr::ServiceClassProfileIdentifier::AvRemoteControl;
        use bredr::ServiceClassProfileIdentifier::AvRemoteControlController;
        let prof_descs = vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::AvRemoteControl,
            major_version: 1,
            minor_version: 6,
        }];
        let avrcp_attribute = Attribute {
            id: SDP_SUPPORTED_FEATURES, // SDP Attribute "SUPPORTED FEATURES"
            element: DataElement::Uint16(1),
        };

        let avrcp_ids = vec![
            bredr::ServiceClassProfileIdentifier::AvRemoteControl,
            bredr::ServiceClassProfileIdentifier::AvRemoteControlController,
        ]
        .into_iter()
        .collect();
        let additional_psms = vec![Psm::AVCTP_BROWSE].into_iter().collect();
        let protocol_descriptor_list = vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![bredr::DataElement::Uint16(bredr::PSM_AVCTP as u16)],
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Avctp,
                params: vec![bredr::DataElement::Uint16(0x0103)], // Indicate v1.3
            },
        ];
        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![
                Uuid::new16(AvRemoteControl as u16).into(),
                Uuid::new16(AvRemoteControlController as u16).into(),
            ]),
            protocol_descriptor_list: Some(protocol_descriptor_list.clone()),
            additional_protocol_descriptor_lists: Some(vec![vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![bredr::DataElement::Uint16(bredr::PSM_AVCTP_BROWSE as u16)],
                },
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::Avctp,
                    params: vec![bredr::DataElement::Uint16(0x0103)],
                },
            ]]),
            profile_descriptors: Some(prof_descs.clone()),
            additional_attributes: Some(vec![(&avrcp_attribute).into()]),
            ..bredr::ServiceDefinition::new_empty()
        };
        let primary_protocol = protocol_descriptor_list.iter().map(Into::into).collect();
        let record = ServiceRecord::new(
            avrcp_ids,
            primary_protocol,
            additional_psms,
            prof_descs,
            vec![avrcp_attribute],
        );

        (def, record)
    }

    #[test]
    fn parse_l2cap_service_definitions_success() {
        // Empty is ok.
        let empty = vec![];
        let parsed = parse_service_definitions(empty);
        assert_eq!(Ok(vec![]), parsed.map_err(|e| format!("{:?}", e)));

        // Bare minimum case. Only the ServiceClassProfileIdentifiers are provided in the service.
        let (id_only_def, expected_id_only_record) = minimal_service_definition();
        let parsed = parse_service_definitions(vec![id_only_def]);
        assert_eq!(Ok(vec![expected_id_only_record]), parsed.map_err(|e| format!("{:?}", e)));

        // Normal case, multiple services.
        let (a2dp_def, expected_a2dp_record) = a2dp_service_definition(Psm::new(25));
        let (avrcp_def, expected_avrcp_record) = avrcp_service_definition();
        let service_defs = vec![a2dp_def, avrcp_def];
        let parsed = parse_service_definitions(service_defs);
        assert_eq!(
            Ok(vec![expected_a2dp_record, expected_avrcp_record]),
            parsed.map_err(|e| format!("{:?}", e))
        );
    }

    /// Builds an example RFCOMM-requesting SPP service and returns the expected parsed
    /// ServiceRecord.
    pub(crate) fn rfcomm_service_definition(
        rfcomm_channel: ServerChannel,
    ) -> (bredr::ServiceDefinition, ServiceRecord) {
        let prof_descs = vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::SerialPort,
            major_version: 1,
            minor_version: 2,
        }];
        let protocol_descriptor_list = vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![], // For RFCOMM services, the PSM is omitted.
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![bredr::DataElement::Uint8(rfcomm_channel.into())],
            },
        ];
        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![Uuid::new16(
                bredr::ServiceClassProfileIdentifier::SerialPort as u16,
            )
            .into()]),
            protocol_descriptor_list: Some(protocol_descriptor_list.clone()),
            profile_descriptors: Some(prof_descs.clone()),
            ..bredr::ServiceDefinition::EMPTY
        };
        let spp_ids = vec![bredr::ServiceClassProfileIdentifier::SerialPort].into_iter().collect();
        let primary_protocol = protocol_descriptor_list.iter().map(Into::into).collect();
        let record =
            ServiceRecord::new(spp_ids, primary_protocol, HashSet::new(), prof_descs, vec![]);
        (def, record)
    }

    #[test]
    fn parse_rfcomm_service_definitions_success() {
        let (spp_def, expected_record) =
            rfcomm_service_definition(ServerChannel::try_from(3).expect("valid"));
        let parsed = parse_service_definitions(vec![spp_def]);
        assert_eq!(Ok(vec![expected_record]), parsed.map_err(|e| format!("{:?}", e)));
    }
}
