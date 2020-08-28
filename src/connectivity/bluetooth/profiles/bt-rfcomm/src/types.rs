// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth::PeerId,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::profile::{
        combine_channel_parameters, ChannelParameters, ServiceDefinition,
    },
    slab::Slab,
    std::collections::HashSet,
};

use crate::profile::{psms_from_service_definitions, server_channels_from_service_definitions};
use crate::rfcomm::ServerChannel;

/// Every group of registered services will be assigned a ServiceGroupHandle to track
/// relevant information about the advertisement. There can be multiple `ServiceGroupHandle`s
/// per profile client. A unique handle is assigned per Profile.Advertise() call.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct ServiceGroupHandle(usize);

/// A collection of `ServiceGroups` which are indexed by a unique `ServiceGroupHandle`.
pub struct Services(Slab<ServiceGroup>);

impl Services {
    pub fn new() -> Self {
        Self(Slab::new())
    }

    pub fn contains(&self, handle: ServiceGroupHandle) -> bool {
        self.0.contains(handle.0)
    }

    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    pub fn get_mut(&mut self, handle: ServiceGroupHandle) -> Option<&mut ServiceGroup> {
        self.0.get_mut(handle.0)
    }

    pub fn remove(&mut self, handle: ServiceGroupHandle) -> ServiceGroup {
        self.0.remove(handle.0)
    }

    pub fn insert(&mut self, service: ServiceGroup) -> ServiceGroupHandle {
        ServiceGroupHandle(self.0.insert(service))
    }

    pub fn iter(&self) -> impl Iterator<Item = (ServiceGroupHandle, &ServiceGroup)> {
        self.0.iter().map(|(id, data)| (ServiceGroupHandle(id), data))
    }

    /// Returns currently registered PSMs.
    pub fn psms(&self) -> HashSet<u16> {
        self.iter().map(|(_, data)| data.allocated_psms()).fold(
            HashSet::new(),
            |mut psms, current| {
                psms.extend(current);
                psms
            },
        )
    }

    /// Attempts to build a set of AdvertiseParams from the services in the group.
    /// Returns None if there are no services to be advertised.
    pub fn build_registration(&self) -> Option<AdvertiseParams> {
        if self.is_empty() {
            return None;
        }

        let mut services = Vec::new();
        let mut parameters = ChannelParameters::default();

        for (_, data) in self.iter() {
            services.extend(data.service_defs().clone());
            parameters = combine_channel_parameters(&parameters, data.channel_parameters());
        }

        Some(AdvertiseParams { services, parameters })
    }
}

/// Parameters needed to advertise a service.
/// This type is used to reduce verbosity of passing around service advertisements.
#[derive(Clone, Debug, PartialEq)]
pub struct AdvertiseParams {
    pub services: Vec<ServiceDefinition>,
    pub parameters: ChannelParameters,
}

/// Relevant information associated with a group of registered services.
#[derive(Debug)]
pub struct ServiceGroup {
    /// Client associated with this group.
    receiver: bredr::ConnectionReceiverProxy,

    /// The ChannelParameters for this group.
    channel_parameters: ChannelParameters,

    /// The client's Responder for this group. When the services are
    /// unregistered with the `ProfileRegistrar`, the hanging-get responder
    /// will be notified.
    responder: Option<bredr::ProfileAdvertiseResponder>,

    /// The services definitions for this group.
    service_defs: Vec<ServiceDefinition>,

    /// The allocated PSMs for this group.
    allocated_psms: HashSet<u16>,

    /// The allocated server channels for this group.
    allocated_server_channels: HashSet<ServerChannel>,
}

impl ServiceGroup {
    pub fn new(
        receiver: bredr::ConnectionReceiverProxy,
        channel_parameters: ChannelParameters,
    ) -> Self {
        Self {
            receiver,
            channel_parameters,
            responder: None,
            service_defs: vec![],
            allocated_psms: HashSet::new(),
            allocated_server_channels: HashSet::new(),
        }
    }

    pub fn service_defs(&self) -> &Vec<ServiceDefinition> {
        &self.service_defs
    }

    pub fn channel_parameters(&self) -> &ChannelParameters {
        &self.channel_parameters
    }

    pub fn allocated_psms(&self) -> &HashSet<u16> {
        &self.allocated_psms
    }

    /// Returns the Server Channels that were allocated to this group of services.
    pub fn allocated_server_channels(&self) -> &HashSet<ServerChannel> {
        &self.allocated_server_channels
    }

    /// Returns true if the `psm` is requested by this service group.s
    pub fn contains_psm(&self, psm: u16) -> bool {
        self.allocated_psms.contains(&psm)
    }

    /// Relays the connection parameters to the client.
    pub fn relay_connected(
        &self,
        mut peer_id: PeerId,
        channel: bredr::Channel,
        mut protocol: Vec<bredr::ProtocolDescriptor>,
    ) -> Result<(), Error> {
        self.receiver
            .connected(&mut peer_id, channel, &mut protocol.iter_mut())
            .map_err(|e| e.into())
    }

    pub fn set_responder(&mut self, responder: bredr::ProfileAdvertiseResponder) {
        self.responder = Some(responder);
    }

    /// Sets the ServiceDefinitions for this group.
    pub fn set_service_defs(&mut self, defs: Vec<ServiceDefinition>) {
        self.allocated_psms = psms_from_service_definitions(&defs);
        self.allocated_server_channels = server_channels_from_service_definitions(&defs);
        self.service_defs = defs;
    }
}

impl Drop for ServiceGroup {
    fn drop(&mut self) {
        if let Some(responder) = self.responder.take() {
            let _ = responder.send(&mut Ok(()));
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use fidl::encoding::Decodable;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::{
        Channel, ProfileDescriptor, ProtocolIdentifier, ServiceClassProfileIdentifier,
    };
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::{
        profile::{DataElement, ProtocolDescriptor},
        types::{PeerId, Uuid},
    };
    use futures::{stream::StreamExt, task::Poll};
    use matches::assert_matches;

    /// Defines a Protocol requesting RFCOMM with the provided server `channel`.
    pub fn rfcomm_protocol_descriptor_list(
        channel: Option<ServerChannel>,
    ) -> Vec<ProtocolDescriptor> {
        let params = channel.map(|c| vec![DataElement::Uint8(c.0)]).unwrap_or(vec![]);
        vec![
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::L2Cap, params: vec![] },
            ProtocolDescriptor { protocol: bredr::ProtocolIdentifier::Rfcomm, params: params },
        ]
    }

    /// Defines the SPP Service Definition, which requests RFCOMM.
    /// An optional `channel` can be provided to specify the Server Channel.
    pub fn rfcomm_service_definition(channel: Option<ServerChannel>) -> ServiceDefinition {
        ServiceDefinition {
            service_class_uuids: vec![Uuid::new16(0x1101).into()], // SPP UUID
            protocol_descriptor_list: rfcomm_protocol_descriptor_list(channel),
            additional_protocol_descriptor_lists: vec![],
            profile_descriptors: vec![ProfileDescriptor {
                profile_id: ServiceClassProfileIdentifier::SerialPort,
                major_version: 1,
                minor_version: 2,
            }],
            information: vec![],
            additional_attributes: vec![],
        }
    }

    /// Defines a sample ServiceDefinition with the provided `psm`.
    pub fn other_service_definition(psm: u16) -> ServiceDefinition {
        ServiceDefinition {
            service_class_uuids: vec![Uuid::new16(0x110A).into()], // A2DP
            protocol_descriptor_list: vec![
                ProtocolDescriptor {
                    protocol: ProtocolIdentifier::L2Cap,
                    params: vec![DataElement::Uint16(psm)],
                },
                ProtocolDescriptor {
                    protocol: ProtocolIdentifier::Avdtp,
                    params: vec![DataElement::Uint16(0x0103)], // Indicate v1.3
                },
            ],
            additional_protocol_descriptor_lists: vec![],
            profile_descriptors: vec![ProfileDescriptor {
                profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
                major_version: 1,
                minor_version: 2,
            }],
            information: vec![],
            additional_attributes: vec![],
        }
    }

    fn build_service_group() -> (ServiceGroup, bredr::ConnectionReceiverRequestStream) {
        let (client, server) =
            create_proxy_and_stream::<bredr::ConnectionReceiverMarker>().unwrap();
        let params = Default::default();

        (ServiceGroup::new(client, params), server)
    }

    #[test]
    fn test_services_collection() {
        let _exec = fasync::Executor::new().unwrap();
        let mut services = Services::new();

        let mut expected_psms = HashSet::new();
        let mut expected_defs = vec![];
        let mut expected_adv_params =
            AdvertiseParams { services: vec![], parameters: Default::default() };

        // Empty collection of Services has no associated PSMs and shouldn't build
        // into any registration data.
        assert_eq!(services.psms(), expected_psms);
        assert_eq!(services.build_registration(), None);

        // Insert a new group.
        let (mut group1, _server1) = build_service_group();
        let defs1 = vec![rfcomm_service_definition(None)];
        group1.set_service_defs(defs1.clone());
        let _handle1 = services.insert(group1);

        expected_defs.extend(defs1.clone());
        expected_adv_params.services = expected_defs.clone();
        assert_eq!(services.build_registration(), Some(expected_adv_params.clone()));

        // Build a new ServiceGroup with RFCOMM and non-RFCOMM services and custom
        // SecurityRequirements/ChannelParameters.
        let (mut group2, _server2) = build_service_group();
        let psm = 6;
        let defs2 =
            vec![other_service_definition(psm), rfcomm_service_definition(Some(ServerChannel(1)))];
        group2.set_service_defs(defs2.clone());
        let new_chan_params = ChannelParameters {
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_rx_sdu_size: None,
            security_requirements: None,
        };
        group2.channel_parameters = new_chan_params.clone();
        let handle2 = services.insert(group2);

        // We expect the advertisement parameters to include the stricter security
        // requirements, channel parameters, and both handle1 and handle2 ServiceDefinitions.
        expected_psms.insert(6);
        expected_defs.extend(defs2);
        expected_adv_params.services = expected_defs.clone();
        expected_adv_params.parameters = new_chan_params;
        assert_eq!(services.psms(), expected_psms);
        assert_eq!(services.build_registration(), Some(expected_adv_params.clone()));

        // Removing group2 should result in the new registration parameters to only
        // include group1's parameters.
        let _ = services.remove(handle2);
        expected_adv_params.services = defs1;
        expected_adv_params.parameters = ChannelParameters::default();
        assert_eq!(services.build_registration(), Some(expected_adv_params));
    }

    #[test]
    fn test_service_group() {
        let _exec = fasync::Executor::new().unwrap();

        let (mut service_group, _server) = build_service_group();

        let mut expected_server_channels = HashSet::new();
        let mut expected_psms = HashSet::new();

        assert_eq!(service_group.service_defs(), &vec![]);
        assert_eq!(service_group.allocated_server_channels(), &expected_server_channels);
        assert_eq!(service_group.allocated_psms(), &expected_psms);

        let psm = 20;
        let other_def = other_service_definition(psm);
        service_group.set_service_defs(vec![other_def.clone()]);
        expected_psms.insert(psm);
        assert_eq!(service_group.allocated_server_channels(), &expected_server_channels);
        assert_eq!(service_group.allocated_psms(), &expected_psms);

        let sc = ServerChannel(10);
        let rfcomm_def = rfcomm_service_definition(Some(sc));
        service_group.set_service_defs(vec![rfcomm_def, other_def]);

        expected_server_channels.insert(sc);
        assert_eq!(service_group.allocated_server_channels(), &expected_server_channels);
        assert_eq!(service_group.allocated_psms(), &expected_psms);
    }

    #[test]
    fn test_service_group_relay_connected() {
        let mut exec = fasync::Executor::new().unwrap();

        let (mut service_group, mut server) = build_service_group();

        let defs =
            vec![other_service_definition(6), rfcomm_service_definition(Some(ServerChannel(1)))];
        service_group.set_service_defs(defs);

        let id = PeerId(123);
        let channel = Channel::new_empty();
        let protocol = vec![];
        let res = service_group.relay_connected(id.into(), channel, protocol);
        assert_matches!(res, Ok(()));

        // We expect the connected request to be relayed to the client.
        match exec.run_until_stalled(&mut server.next()) {
            Poll::Ready(Some(Ok(bredr::ConnectionReceiverRequest::Connected {
                peer_id, ..
            }))) => {
                assert_eq!(peer_id, id.into());
            }
            x => panic!("Expected ready but got: {:?}", x),
        }
    }
}
