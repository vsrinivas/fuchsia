// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::{profile, util::CollectExt},
    std::{
        collections::HashSet,
        convert::{TryFrom, TryInto},
        iter::FromIterator,
    },
};

use crate::types::{Psm, ServiceRecord};

/// Builds the L2Cap Protocol Descriptor from the provided `psm`.
pub fn build_l2cap_descriptor(psm: Psm) -> Vec<bredr::ProtocolDescriptor> {
    vec![bredr::ProtocolDescriptor {
        protocol: bredr::ProtocolIdentifier::L2Cap,
        params: vec![bredr::DataElement::Uint16(psm.0)],
    }]
}

fn parse_service_definition(
    def: &bredr::ServiceDefinition,
) -> Result<ServiceRecord, anyhow::Error> {
    let definition: profile::ServiceDefinition = def.try_into()?;
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

    // Convert primary PSM into local Psm type.
    let primary_psm = definition.primary_psm().map(|psm| Psm(psm));

    // Convert (potential) additional PSMs into local Psm type.
    let additional_psms = definition.additional_psms().iter().map(|psm| Psm(*psm)).collect();

    Ok(ServiceRecord::new(
        svc_ids,
        primary_psm,
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

    use fidl::encoding::Decodable;
    use fidl_fuchsia_bluetooth_bredr::{ProfileDescriptor, PSM_AVCTP, PSM_AVCTP_BROWSE, PSM_AVDTP};
    use fuchsia_bluetooth::{profile, types::Uuid};

    const SDP_SUPPORTED_FEATURES: u16 = 0x0311;

    /// Builds the smallest service definition that is valid.
    /// Just includes the ServiceClassProfileIdentifiers.
    /// Returns the expected ServiceRecord as well.
    fn build_minimal_service_definition() -> (bredr::ServiceDefinition, ServiceRecord) {
        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![
                Uuid::new16(bredr::ServiceClassProfileIdentifier::Headset as u16).into(),
                Uuid::new16(bredr::ServiceClassProfileIdentifier::Handsfree as u16).into(),
            ]),
            ..bredr::ServiceDefinition::new_empty()
        };

        let mut ids = HashSet::new();
        ids.insert(bredr::ServiceClassProfileIdentifier::Headset);
        ids.insert(bredr::ServiceClassProfileIdentifier::Handsfree);
        let record = ServiceRecord::new(ids, None, HashSet::new(), vec![], vec![]);

        (def, record)
    }

    // Builds an A2DP Service Definition and also the expected parsed ServiceRecord.
    // This is done in the same function to maintain data consistency, such that if the
    // parsing implementation changes, only this builder will need to be updated.
    pub(crate) fn build_a2dp_service_definition() -> (bredr::ServiceDefinition, ServiceRecord) {
        let prof_descs = vec![ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        }];
        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![Uuid::new16(0x110B).into()]), // Audio Sink UUID
            protocol_descriptor_list: Some(vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![bredr::DataElement::Uint16(PSM_AVDTP)],
                },
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::Avdtp,
                    params: vec![bredr::DataElement::Uint16(0x0103)], // Indicate v1.3
                },
            ]),
            profile_descriptors: Some(prof_descs.clone()),
            additional_attributes: Some(vec![]),
            ..bredr::ServiceDefinition::new_empty()
        };

        let mut a2dp_ids = HashSet::new();
        a2dp_ids.insert(bredr::ServiceClassProfileIdentifier::AudioSink);
        let record =
            ServiceRecord::new(a2dp_ids, Some(Psm(25)), HashSet::new(), prof_descs, vec![]);

        (def, record)
    }

    /// Builds an example AVRCP Service Definition and also the expected parsed ServiceRecord.
    /// This is done in the same function to maintain data consistency, such that if the
    /// parsing implementation changes, only this builder will need to be updated.
    fn build_avrcp_service_definition() -> (bredr::ServiceDefinition, ServiceRecord) {
        use bredr::ServiceClassProfileIdentifier::AvRemoteControl;
        use bredr::ServiceClassProfileIdentifier::AvRemoteControlController;
        let prof_descs = vec![ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::AvRemoteControl,
            major_version: 1,
            minor_version: 6,
        }];
        let avrcp_attribute = profile::Attribute {
            id: SDP_SUPPORTED_FEATURES, // SDP Attribute "SUPPORTED FEATURES"
            element: profile::DataElement::Uint16(1),
        };

        let mut avrcp_ids = HashSet::new();
        avrcp_ids.insert(bredr::ServiceClassProfileIdentifier::AvRemoteControl);
        avrcp_ids.insert(bredr::ServiceClassProfileIdentifier::AvRemoteControlController);
        let mut additional_psms = HashSet::new();
        additional_psms.insert(Psm(27));

        let def = bredr::ServiceDefinition {
            service_class_uuids: Some(vec![
                Uuid::new16(AvRemoteControl as u16).into(),
                Uuid::new16(AvRemoteControlController as u16).into(),
            ]),
            protocol_descriptor_list: Some(vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![bredr::DataElement::Uint16(PSM_AVCTP as u16)],
                },
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::Avctp,
                    params: vec![bredr::DataElement::Uint16(0x0103)], // Indicate v1.3
                },
            ]),
            additional_protocol_descriptor_lists: Some(vec![vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![bredr::DataElement::Uint16(PSM_AVCTP_BROWSE as u16)],
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

        let record = ServiceRecord::new(
            avrcp_ids,
            Some(Psm(23)),
            additional_psms,
            prof_descs,
            vec![avrcp_attribute],
        );

        (def, record)
    }

    #[test]
    fn test_parse_service_definitions_success() {
        // Empty is ok.
        let empty = vec![];
        let parsed = parse_service_definitions(empty);
        assert_eq!(Ok(vec![]), parsed.map_err(|e| format!("{:?}", e)));

        // Bare minimum case. Only the ServiceClassProfileIdentifiers are provided in the service.
        let (id_only_def, expected_id_only_record) = build_minimal_service_definition();
        let parsed = parse_service_definitions(vec![id_only_def]);
        assert_eq!(Ok(vec![expected_id_only_record]), parsed.map_err(|e| format!("{:?}", e)));

        // Normal case, multiple services.
        let (a2dp_def, expected_a2dp_record) = build_a2dp_service_definition();
        let (avrcp_def, expected_avrcp_record) = build_avrcp_service_definition();
        let service_defs = vec![a2dp_def, avrcp_def];
        let parsed = parse_service_definitions(service_defs);
        assert_eq!(
            Ok(vec![expected_a2dp_record, expected_avrcp_record]),
            parsed.map_err(|e| format!("{:?}", e))
        );
    }
}
