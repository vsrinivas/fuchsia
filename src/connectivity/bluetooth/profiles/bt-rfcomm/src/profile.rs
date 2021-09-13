// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use bt_rfcomm::profile::{build_rfcomm_protocol, is_rfcomm_protocol, server_channel_from_protocol};
use bt_rfcomm::ServerChannel;
use fuchsia_bluetooth::profile::{Psm, ServiceDefinition};
use std::collections::HashSet;

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

/// Returns true if the provided `service` is requesting RFCOMM.
pub fn is_rfcomm_service_definition(service: &ServiceDefinition) -> bool {
    is_rfcomm_protocol(&service.protocol_descriptor_list)
}

/// Returns true if any of the `services` request RFCOMM.
pub fn service_definitions_request_rfcomm(services: &Vec<ServiceDefinition>) -> bool {
    services.iter().map(is_rfcomm_service_definition).fold(false, |acc, is_rfcomm| acc || is_rfcomm)
}

/// Returns the server channels specified in `services`. It's possible that
/// none of the `services` request a ServerChannel in which case the returned set
/// will be empty.
pub fn server_channels_from_service_definitions(
    services: &Vec<ServiceDefinition>,
) -> HashSet<ServerChannel> {
    services
        .iter()
        .filter_map(|def| server_channel_from_protocol(&def.protocol_descriptor_list))
        .collect()
}

/// Returns a set of PSMs specified by a list of `services`.
pub fn psms_from_service_definitions(services: &Vec<ServiceDefinition>) -> HashSet<Psm> {
    services.iter().fold(HashSet::new(), |mut psms, service| {
        psms.extend(&service.psm_set());
        psms
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_bluetooth_bredr as bredr;
    use fuchsia_bluetooth::profile::ProtocolDescriptor;
    use std::convert::TryFrom;

    use crate::types::tests::rfcomm_protocol_descriptor_list;

    #[test]
    fn test_update_service_definition_with_rfcomm() {
        let server_channel = ServerChannel::try_from(10).unwrap();
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
}
