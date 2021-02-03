// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    bt_rfcomm::profile::is_rfcomm_protocol,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::{profile::*, util::CollectExt},
    std::{
        collections::HashSet,
        convert::{TryFrom, TryInto},
        iter::FromIterator,
    },
};

use crate::types::{Connection, ServiceRecord};

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
            .collect_results()?;
        HashSet::from_iter(uuids_vec)
    };
    if svc_ids.is_empty() {
        return Err(format_err!("There must be at least one service class UUID"));
    };

    // Parse the primary connection type. If RFCOMM, use a placeholder empty server channel
    // number until it is assigned.
    let primary_connection = if is_rfcomm_protocol(&definition.protocol_descriptor_list) {
        Some(Connection::Rfcomm(None))
    } else {
        definition.primary_psm().map(|psm| Connection::L2cap(psm))
    };

    // Convert (potential) additional PSMs into local Psm type.
    let additional_psms = definition.additional_psms();
    // TODO(fxbug.dev/66007): Check the additional protocols for RFCOMM.

    Ok(ServiceRecord::new(
        svc_ids,
        primary_connection,
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
    use {fidl::encoding::Decodable, fuchsia_bluetooth::types::Uuid};

    const SDP_SUPPORTED_FEATURES: u16 = 0x0311;

    /// Builds the smallest service definition that is valid - only the Service Class IDs.
    /// Returns the definition and the expected parsed ServiceRecord.
    fn build_minimal_service_definition() -> (bredr::ServiceDefinition, ServiceRecord) {
        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![
                Uuid::new16(bredr::ServiceClassProfileIdentifier::Headset as u16).into(),
                Uuid::new16(bredr::ServiceClassProfileIdentifier::Handsfree as u16).into(),
            ]),
            ..bredr::ServiceDefinition::EMPTY
        };

        let mut ids = HashSet::new();
        ids.insert(bredr::ServiceClassProfileIdentifier::Headset);
        ids.insert(bredr::ServiceClassProfileIdentifier::Handsfree);
        let record = ServiceRecord::new(ids, None, HashSet::new(), vec![], vec![]);

        (def, record)
    }

    /// Builds an A2DP Sink Service Definition and the expected parsed ServiceRecord.
    /// Uses the provided `psm` for the service.
    pub(crate) fn build_a2dp_service_definition(
        psm: Psm,
    ) -> (bredr::ServiceDefinition, ServiceRecord) {
        let prof_descs = vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        }];
        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![Uuid::new16(0x110B).into()]), // Audio Sink UUID
            protocol_descriptor_list: Some(vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![bredr::DataElement::Uint16(psm.into())],
                },
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::Avdtp,
                    params: vec![bredr::DataElement::Uint16(0x0103)], // Indicate v1.3
                },
            ]),
            profile_descriptors: Some(prof_descs.clone()),
            additional_attributes: Some(vec![]),
            ..bredr::ServiceDefinition::EMPTY
        };

        let mut a2dp_ids = HashSet::new();
        a2dp_ids.insert(bredr::ServiceClassProfileIdentifier::AudioSink);
        let connection = Some(Connection::L2cap(psm));
        let record = ServiceRecord::new(a2dp_ids, connection, HashSet::new(), prof_descs, vec![]);

        (def, record)
    }

    /// Builds an example RFCOMM Service Definition and returns the expected parsed ServiceRecord.
    /// Uses an optional `additional_psm` for an additional service.
    pub(crate) fn build_rfcomm_service_definition(
        additional_psm: Option<Psm>,
    ) -> (bredr::ServiceDefinition, ServiceRecord) {
        let prof_descs = vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::SerialPort,
            major_version: 1,
            minor_version: 2,
        }];
        let mut def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![Uuid::new16(
                bredr::ServiceClassProfileIdentifier::SerialPort as u16,
            )
            .into()]),
            protocol_descriptor_list: Some(vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![],
                },
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::Rfcomm,
                    params: vec![], // This will be assigned.
                },
            ]),
            profile_descriptors: Some(prof_descs.clone()),
            ..bredr::ServiceDefinition::EMPTY
        };
        let mut addl_psms = HashSet::new();
        if let Some(psm) = additional_psm {
            def.additional_protocol_descriptor_lists =
                Some(vec![vec![bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![bredr::DataElement::Uint16(psm.into())],
                }]]);
            addl_psms.insert(psm);
        }
        let mut spp_ids = HashSet::new();
        spp_ids.insert(bredr::ServiceClassProfileIdentifier::SerialPort);
        let connection = Some(Connection::Rfcomm(None));
        let record = ServiceRecord::new(spp_ids, connection, addl_psms, prof_descs, vec![]);
        (def, record)
    }

    /// Builds an example AVRCP Service Definition and the expected parsed ServiceRecord.
    /// This is done in the same function to maintain data consistency, such that if the
    /// parsing implementation changes, only this builder will need to be updated.
    fn build_avrcp_service_definition() -> (bredr::ServiceDefinition, ServiceRecord) {
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

        let mut avrcp_ids = HashSet::new();
        avrcp_ids.insert(bredr::ServiceClassProfileIdentifier::AvRemoteControl);
        avrcp_ids.insert(bredr::ServiceClassProfileIdentifier::AvRemoteControlController);
        let mut additional_psms = HashSet::new();
        additional_psms.insert(Psm::new(27));

        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![
                Uuid::new16(AvRemoteControl as u16).into(),
                Uuid::new16(AvRemoteControlController as u16).into(),
            ]),
            protocol_descriptor_list: Some(vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![bredr::DataElement::Uint16(bredr::PSM_AVCTP as u16)],
                },
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::Avctp,
                    params: vec![bredr::DataElement::Uint16(0x0103)], // Indicate v1.3
                },
            ]),
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
        let connection = Some(Connection::L2cap(Psm::new(23)));
        let record = ServiceRecord::new(
            avrcp_ids,
            connection,
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
        let (id_only_def, expected_id_only_record) = build_minimal_service_definition();
        let parsed = parse_service_definitions(vec![id_only_def]);
        assert_eq!(Ok(vec![expected_id_only_record]), parsed.map_err(|e| format!("{:?}", e)));

        // Normal case, multiple services.
        let (a2dp_def, expected_a2dp_record) = build_a2dp_service_definition(Psm::new(25));
        let (avrcp_def, expected_avrcp_record) = build_avrcp_service_definition();
        let service_defs = vec![a2dp_def, avrcp_def];
        let parsed = parse_service_definitions(service_defs);
        assert_eq!(
            Ok(vec![expected_a2dp_record, expected_avrcp_record]),
            parsed.map_err(|e| format!("{:?}", e))
        );
    }

    #[test]
    fn parse_rfcomm_service_definitions_success() {
        let addl_psm = Some(Psm::new(19)); // Random L2CAP service.
        let (rfcomm_def, expected_rfcomm_record) = build_rfcomm_service_definition(addl_psm);
        let parsed = parse_service_definitions(vec![rfcomm_def]);
        assert_eq!(Ok(vec![expected_rfcomm_record]), parsed.map_err(|e| format!("{:?}", e)));
    }
}
