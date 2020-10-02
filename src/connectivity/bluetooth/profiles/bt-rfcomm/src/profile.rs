// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::profile::{DataElement, ProtocolDescriptor, ServiceDefinition},
    std::collections::HashSet,
};

use crate::rfcomm::ServerChannel;

/// Returns a ProtocolDescriptorList for an RFCOMM service with the provided `server_channel`.
pub fn build_rfcomm_protocol(server_channel: ServerChannel) -> Vec<ProtocolDescriptor> {
    // The PSM for L2CAP is empty when RFCOMM is used.
    vec![
        ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
        ProtocolDescriptor {
            protocol: bredr::ProtocolIdentifier::Rfcomm,
            params: vec![DataElement::Uint8(server_channel.0)],
        },
    ]
}

/// Updates the provided `service` with the assigned `server_channel` if
/// the service is requesting RFCOMM.
/// Updates the primary protocol descriptor only. SDP records for profiles
/// usually have the RFCOMM descriptor in the primary protocol.
///
/// Returns Ok() if the `service` was updated.
pub fn update_svc_def_with_server_channel(
    service: &mut ServiceDefinition,
    server_channel: ServerChannel,
) -> Result<(), Error> {
    // If the service definition is not requesting RFCOMM, there is no need to update
    // with the server channel.
    if !is_rfcomm_service_definition(&service) {
        return Err(format_err!("Non-RFCOMM service definition provided"));
    }

    service.protocol_descriptor_list = build_rfcomm_protocol(server_channel);
    Ok(())
}

/// Returns true if the provided `protocol` is RFCOMM.
///
/// Protocols are generally specified by a vector of ProtocolDescriptors which
/// are ordered from lowest level (typically L2CAP) to highest.
fn is_rfcomm_protocol(protocol: &Vec<ProtocolDescriptor>) -> bool {
    protocol.iter().any(|descriptor| descriptor.protocol == bredr::ProtocolIdentifier::Rfcomm)
}

/// Returns true if the provided `service` is requesting RFCOMM.
pub fn is_rfcomm_service_definition(service: &ServiceDefinition) -> bool {
    is_rfcomm_protocol(&service.protocol_descriptor_list)
}

/// Returns true if any of the `services` request RFCOMM.
pub fn service_definitions_request_rfcomm(services: &Vec<ServiceDefinition>) -> bool {
    services.iter().map(is_rfcomm_service_definition).fold(false, |acc, is_rfcomm| acc || is_rfcomm)
}

/// Returns the Server Channel from the provided `service` or None if the service
/// is not RFCOMM or is invalidly formatted.
pub fn server_channel_from_service_definition(
    service: &ServiceDefinition,
) -> Option<ServerChannel> {
    for descriptor in &service.protocol_descriptor_list {
        if descriptor.protocol == bredr::ProtocolIdentifier::Rfcomm {
            // If the Protocol is RFCOMM, there should be one element with the Server Channel.
            if descriptor.params.len() != 1 {
                return None;
            }

            if let DataElement::Uint8(sc) = descriptor.params[0] {
                return Some(ServerChannel(sc));
            }
            return None;
        }
    }
    None
}

/// Returns the server channels specified in `services`. It's possible that
/// none of the `services` request a ServerChannel in which case the returned set
/// will be empty.
pub fn server_channels_from_service_definitions(
    services: &Vec<ServiceDefinition>,
) -> HashSet<ServerChannel> {
    services.iter().filter_map(server_channel_from_service_definition).collect()
}

/// Returns a set of PSMs specified by a list of `services`.
pub fn psms_from_service_definitions(services: &Vec<ServiceDefinition>) -> HashSet<u16> {
    services.iter().fold(HashSet::new(), |mut psms, service| {
        psms.extend(&service.psm_set());
        psms
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::types::tests::rfcomm_protocol_descriptor_list;

    #[test]
    fn test_is_rfcomm_service_definition() {
        let mut def = ServiceDefinition::default();

        let empty_res = is_rfcomm_service_definition(&def);
        assert!(!empty_res);

        // Updated definition with L2CAP.
        def.protocol_descriptor_list =
            vec![ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] }];
        let l2cap_res = is_rfcomm_service_definition(&def);
        assert!(!l2cap_res);

        // Updated definition with L2CAP and RFCOMM.
        def.protocol_descriptor_list = rfcomm_protocol_descriptor_list(None);
        let rfcomm_res = is_rfcomm_service_definition(&def);
        assert!(rfcomm_res);
    }

    #[test]
    fn test_update_service_definition_with_rfcomm() {
        let server_channel = ServerChannel(10);
        let mut def = ServiceDefinition::default();
        let mut expected = def.clone();

        // Empty definition doesn't request RFCOMM - shouldn't be updated.
        let updated = update_svc_def_with_server_channel(&mut def, server_channel);
        assert!(updated.is_err());
        assert_eq!(expected, def);

        // Normal case - definition is requesting RFCOMM. It should be updated with the
        // server channel.
        def.protocol_descriptor_list = vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::Rfcomm, params: vec![] },
        ];
        expected.protocol_descriptor_list = rfcomm_protocol_descriptor_list(Some(server_channel));

        let updated = update_svc_def_with_server_channel(&mut def, server_channel);
        assert!(updated.is_ok());
        assert_eq!(expected, def);
    }

    #[test]
    fn test_server_channel_from_empty_service_definition_is_none() {
        assert_eq!(None, server_channel_from_service_definition(&ServiceDefinition::default()));
    }

    #[test]
    fn test_server_channel_from_l2cap_is_none() {
        let mut def = ServiceDefinition::default();
        // Just L2CAP - should be no server channel.
        def.protocol_descriptor_list = vec![ProtocolDescriptor {
            protocol: bredr::ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(bredr::PSM_AVDTP)],
        }];
        assert_eq!(None, server_channel_from_service_definition(&def));
    }

    #[test]
    fn test_server_channel_from_empty_rfcomm_is_none() {
        let mut def = ServiceDefinition::default();

        // RFCOMM but un-allocated Server Channel.
        def.protocol_descriptor_list = vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::Rfcomm, params: vec![] },
        ];
        assert_eq!(None, server_channel_from_service_definition(&def));
    }

    #[test]
    fn test_server_channel_from_invalid_rfcomm_is_none() {
        let mut def = ServiceDefinition::default();
        // RFCOMM but invalidly formatted.
        def.protocol_descriptor_list = vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![DataElement::Uint16(100)],
            },
        ];
        assert_eq!(None, server_channel_from_service_definition(&def));
    }

    #[test]
    fn test_server_channel_from_rfcomm_is_present() {
        let mut def = ServiceDefinition::default();

        // RFCOMM service with assigned server channel.
        let sc = 10;
        def.protocol_descriptor_list = vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![DataElement::Uint8(sc)],
            },
        ];
        assert_eq!(Some(ServerChannel(sc)), server_channel_from_service_definition(&def));
    }
}
