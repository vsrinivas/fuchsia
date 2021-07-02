// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::encoding::Decodable,
    fidl::endpoints::{create_endpoints, create_proxy, create_request_stream, DiscoverableService},
    fidl_fuchsia_bluetooth_avrcp::{
        AbsoluteVolumeHandlerMarker, AbsoluteVolumeHandlerRequest,
        AbsoluteVolumeHandlerRequestStream, ControllerMarker, PeerManagerMarker,
        TargetHandlerMarker,
    },
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::types::{Channel, Uuid},
    fuchsia_component_test::builder::Capability,
    fuchsia_zircon as zx,
    futures::{pin_mut, select, stream::StreamExt, FutureExt, TryFutureExt},
    mock_piconet_client::v2::{PiconetHarness, PiconetMember},
    std::convert::TryInto,
};

/// AVRCP component V2 URL.
const AVRCP_URL_V2: &str = "fuchsia-pkg://fuchsia.com/bt-avrcp-integration-tests#meta/bt-avrcp.cm";
/// Attribute ID for SDP Support Features.
const SDP_SUPPORTED_FEATURES: u16 = 0x0311;

/// Make the SDP definition for an AVRCP Controller service.
fn avrcp_controller_service_definition() -> bredr::ServiceDefinition {
    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![Uuid::new16(0x110E).into(), Uuid::new16(0x110F).into()]),
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
        profile_descriptors: Some(vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::AvRemoteControl,
            major_version: 1,
            minor_version: 6,
        }]),
        additional_attributes: Some(vec![bredr::Attribute {
            id: SDP_SUPPORTED_FEATURES, // SDP Attribute "SUPPORTED FEATURES"
            element: bredr::DataElement::Uint16(0x0003), // CATEGORY 1 and 2.
        }]),
        ..bredr::ServiceDefinition::new_empty()
    }
}

/// Stub absolute volume handler which accepts any requested volume
async fn run_absolute_volume_handler(stream: AbsoluteVolumeHandlerRequestStream) {
    stream
        .for_each(|request| async move {
            match request {
                Ok(AbsoluteVolumeHandlerRequest::SetVolume { requested_volume, responder }) => {
                    responder
                        .send(requested_volume)
                        .expect("Error forwarding absolute volume request");
                }
                _ => {}
            }
        })
        .await;
}

/// Tests that AVRCP correctly advertises it's TG-related services and can be
/// discovered by another peer in the mock piconet.
#[fasync::run_singlethreaded(test)]
async fn avrcp_target_service_advertisement() -> Result<(), Error> {
    let mut test_harness = PiconetHarness::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let mock_peer = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");
    let mut results_requests = mock_peer.register_service_search(
        bredr::ServiceClassProfileIdentifier::AvRemoteControlTarget,
        vec![],
    )?;

    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let bredr::SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await?;
    assert_eq!(profile_observer.peer_id(), peer_id.into());
    responder.send()?;

    Ok(())
}

/// Tests that AVRCP correctly advertises it's CT-related services and can be
/// discovered by another peer in the mock piconet.
#[fasync::run_singlethreaded(test)]
async fn avrcp_controller_service_advertisement() -> Result<(), Error> {
    let mut test_harness = PiconetHarness::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let mock_peer = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");
    let mut results_requests = mock_peer
        .register_service_search(bredr::ServiceClassProfileIdentifier::AvRemoteControl, vec![])?;
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let bredr::SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await?;
    assert_eq!(profile_observer.peer_id(), peer_id.into());
    responder.send()?;

    Ok(())
}

/// Tests that AVRCP correctly searches for AvRemoteControls, discovers a mock peer
/// providing it, and attempts to connect to the mock peer.
#[fasync::run_singlethreaded(test)]
async fn avrcp_search_and_connect() -> Result<(), Error> {
    let mut test_harness = PiconetHarness::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let mut profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let mock_peer = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");

    // Mock peer advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = mock_peer
        .register_service_advertisement(service_defs)
        .expect("Mock peer failed to register service advertisement");

    // We then expect AVRCP to discover the mock peer and attempt to connect to it
    profile_observer.expect_observer_service_found_request(mock_peer.peer_id()).await?;
    let _channel = match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } => {
            assert_eq!(profile_observer.peer_id(), peer_id.into());
            channel
        }
    };
    Ok(())
}

/// Tests the case of a remote peer initiating the AVCTP connection to AVRCP.
/// AVRCP should accept the connection (which should be relayed on the observer), and
/// sending/receiving data should be OK.
#[fasync::run_singlethreaded(test)]
async fn remote_initiates_connection_to_avrcp() -> Result<(), Error> {
    let mut test_harness = PiconetHarness::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let mut profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let (mock_peer_id, mock_peer) = (
        spec.id.0,
        PiconetMember::new_from_spec(spec, &test_topology)
            .expect("failed to create piconet member from spec"),
    );

    // Mock peer advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = mock_peer
        .register_service_advertisement(service_defs)
        .expect("Mock peer failed to register service advertisement");

    // We then expect AVRCP to discover the mock peer and attempt to connect to it
    profile_observer.expect_observer_service_found_request(mock_peer.peer_id()).await?;
    match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, .. } => {
            assert_eq!(profile_observer.peer_id(), peer_id.into());
        }
    };

    // We wait `MAX_AVRCP_CONNECTION_ESTABLISHMENT` millis before attempting to connect, as per
    // AVRCP 1.6.2, Section 4.1.1, to avoid conflict.
    const MAX_AVRCP_CONNECTION_ESTABLISHMENT: zx::Duration = zx::Duration::from_millis(1000);
    fasync::Timer::new(fasync::Time::after(MAX_AVRCP_CONNECTION_ESTABLISHMENT)).await;

    // Mock peer attempts to connect to AVRCP.
    let l2cap = bredr::L2capParameters {
        psm: Some(bredr::PSM_AVCTP),
        ..bredr::L2capParameters::new_empty()
    };
    let params = bredr::ConnectParameters::L2cap(l2cap);
    let channel = mock_peer.make_connection(profile_observer.peer_id(), params).await?;
    let channel: Channel = channel.try_into()?;

    // The observer of bt-avrcp.cm should be notified of the connection attempt.
    match profile_observer.expect_observer_request().await? {
        bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(mock_peer_id, peer_id.value);
            responder.send()?;
        }
        x => return Err(format_err!("Expected PeerConnected but got: {:?}", x)),
    }
    let mut vec = Vec::new();
    let read_fut = channel.read_datagram(&mut vec);

    // Sending a random non-AVRCP data packet should be OK.
    let write_result = channel.as_ref().write(&[0x00, 0x00, 0x00]);
    assert_eq!(write_result, Ok(3));

    // We expect a general reject response, since the sent packet is malformed.
    let expected_read_result_packet = &[
        0x03, // Bit 0 indicates invalid profile id, Bit 1 indicates CommandType::Response.
        0x00, // Profile ID
        0x00, // Profile ID
    ];
    let read_result = read_fut.await;
    assert_eq!(read_result, Ok(3));
    assert_eq!(vec.as_slice(), expected_read_result_packet);

    Ok(())
}

/// Tests the case of a remote device attempting to connect over PSM_BROWSE
/// before PSM_AVCTP. AVRCP 1.6 states that the Browse channel may only be established
/// after the control channel. We expect AVRCP to drop the incoming channel.
#[fasync::run_singlethreaded(test)]
async fn remote_initiates_browse_channel_before_control() -> Result<(), Error> {
    let mut test_harness = PiconetHarness::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let mut profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let (mock_peer_id, mock_peer) = (
        spec.id.0,
        PiconetMember::new_from_spec(spec, &test_topology)
            .expect("failed to create piconet member from spec"),
    );

    // Mock peer advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = mock_peer
        .register_service_advertisement(service_defs)
        .expect("Mock peer failed to register service advertisement");

    // We then expect AVRCP to discover the mock peer and attempt to connect to it
    profile_observer.expect_observer_service_found_request(mock_peer.peer_id()).await?;
    match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, .. } => {
            assert_eq!(profile_observer.peer_id(), peer_id.into());
        }
    };
    // Mock peer tries to initiate a browse channel connection.
    let l2cap = bredr::L2capParameters {
        psm: Some(bredr::PSM_AVCTP_BROWSE),
        ..bredr::L2capParameters::new_empty()
    };
    let params = bredr::ConnectParameters::L2cap(l2cap);
    let channel = mock_peer.make_connection(profile_observer.peer_id(), params).await?;
    let channel: Channel = channel.try_into()?;

    // The observer of bt-avrcp.cm should be notified of the connection attempt.
    match profile_observer.expect_observer_request().await? {
        bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(mock_peer_id, peer_id.value);
            responder.send()?;
        }
        x => return Err(format_err!("Expected PeerConnected but got: {:?}", x)),
    }

    // However, AVRCP should immediately close the `channel` because the Browse channel
    // cannot be established before the Control channel.
    let closed = channel.closed().await;
    assert_eq!(closed, Ok(()));

    Ok(())
}

/// Tests that AVRCP's absolute volume handler and target handler can each be set once and only once
#[fasync::run_singlethreaded(test)]
async fn avrcp_disallows_handler_double_sets() -> Result<(), Error> {
    let mut test_harness = PiconetHarness::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let mut profile_observer = test_harness
        .add_profile_with_capabilities(
            "bt-avrcp-profile".to_string(),
            AVRCP_URL_V2.to_string(),
            vec![],
            vec![Capability::protocol(PeerManagerMarker::SERVICE_NAME)],
        )
        .await?;
    let test_topology = test_harness.build().await?;

    let mock_peer = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");

    // Mock peer advertises service & bt-avrcp discovers it
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = mock_peer
        .register_service_advertisement(service_defs)
        .expect("Mock peer failed to register service advertisement");

    // We then expect AVRCP to discover the mock peer and attempt to connect to it
    profile_observer.expect_observer_service_found_request(mock_peer.peer_id()).await?;
    let _channel = match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } => {
            assert_eq!(profile_observer.peer_id(), peer_id.into());
            channel
        }
    };

    // Connect to bt-avrcp.cm's controller service
    let avrcp_svc = test_topology
        .root
        .connect_to_protocol_at_exposed_dir::<PeerManagerMarker>()
        .context("Failed to connect to Bluetooth AVRCP interface")?;

    // Get controller for mock peer
    let (_c_client, c_server) = create_endpoints::<ControllerMarker>()?;
    let _ = avrcp_svc.get_controller_for_target(&mut mock_peer.peer_id().into(), c_server).await?;

    // Create absolute volume handler, register with AVRCP
    let (avh_client, avh_stream) = create_request_stream::<AbsoluteVolumeHandlerMarker>()?;
    let avh_fut = run_absolute_volume_handler(avh_stream);
    fasync::Task::spawn(avh_fut).detach();
    avrcp_svc
        .set_absolute_volume_handler(avh_client)
        .await?
        .expect("AVRCP should accept first absolute volume handler");

    // Register with AVRCP again
    // Request should be rejected, so no handler needs to be instantiated
    let (avh_client, _avh_server) = create_endpoints::<AbsoluteVolumeHandlerMarker>()?;
    let _ = avrcp_svc
        .set_absolute_volume_handler(avh_client)
        .await?
        .expect_err("AVRCP should reject absolute volume handler double sets");

    // Create target handler, register with AVRCP
    let (t_client, t_stream) = create_request_stream::<TargetHandlerMarker>()?;
    let t_fut = t_stream.for_each(|_request| async move {});
    fasync::Task::spawn(t_fut).detach();
    avrcp_svc
        .register_target_handler(t_client)
        .await?
        .expect("AVRCP should accept first target handler");

    // Register with AVRCP again
    // Request should be rejected, so no handler needs to be instantiated
    let (t_client, _t_server) = create_endpoints::<TargetHandlerMarker>()?;
    let _ = avrcp_svc
        .register_target_handler(t_client)
        .await?
        .expect_err("AVRCP should reject target handler double sets");

    Ok(())
}

/// Confirm that peers can have their absolute volume set
/// through the registered absolute volume handler
#[fasync::run_singlethreaded(test)]
async fn avrcp_remote_receives_set_absolute_volume_request() -> Result<(), Error> {
    let mut test_harness = PiconetHarness::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let mut profile_observer = test_harness
        .add_profile_with_capabilities(
            "bt-avrcp-profile".to_string(),
            AVRCP_URL_V2.to_string(),
            vec![],
            vec![Capability::protocol(PeerManagerMarker::SERVICE_NAME)],
        )
        .await?;
    let test_topology = test_harness.build().await?;

    let mock_peer = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");

    // Mock peer advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = mock_peer
        .register_service_advertisement(service_defs)
        .expect("Mock peer failed to register service advertisement");

    // We then expect AVRCP to discover the mock peer and attempt to connect to it
    profile_observer.expect_observer_service_found_request(mock_peer.peer_id()).await?;
    let channel: Channel = match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } => {
            assert_eq!(profile_observer.peer_id(), peer_id.into());
            channel.try_into()?
        }
    };

    // Connect to avrcp controller service.
    let avrcp_svc = test_topology
        .root
        .connect_to_protocol_at_exposed_dir::<PeerManagerMarker>()
        .context("Failed to connect to Bluetooth AVRCP interface")?;

    // Create absolute volume handler server which simply forwards the desired volume unmodified
    let (avh_client, avh_server) = create_request_stream::<AbsoluteVolumeHandlerMarker>()?;
    let avh_fut = run_absolute_volume_handler(avh_server);
    avrcp_svc.set_absolute_volume_handler(avh_client).await?.expect("Error registering AVH");
    fasync::Task::spawn(avh_fut).detach();

    // Get controller for mock peer
    let (c_proxy, c_server) = create_proxy::<ControllerMarker>()?;
    let _ = avrcp_svc.get_controller_for_target(&mut mock_peer.peer_id().into(), c_server).await?;

    // Expect new SetAbsoluteVolume request on mock peer
    let mut vec = Vec::new();
    let desired_vol: u8 = 0x7F; // Valid volume range is 0x00 - 0x7F
    let datagram_len = async {
        let set_av_fut = c_proxy.set_absolute_volume(desired_vol).fuse();
        let receive_av_fut = channel.read_datagram(&mut vec).fuse();
        pin_mut!(set_av_fut, receive_av_fut);
        loop {
            select! {
                ret = set_av_fut => { let _ = ret.expect("SetAbsoluteVolume failed"); },
                ret = receive_av_fut => return ret.expect("Mock peer failed to receive command"),
            }
        }
    }
    .await;

    // TODO(fxbug.dev/79580): Find more idiomatic way of unpacking request datagrams
    // Expecting 3 octet AVC header (AVCTP spec 6.1.1)
    // followed by 11 octet SetAbsoluteVolume command (AVRCP spec 25.16)
    assert_eq!(datagram_len, 14);

    // Confirm AVC header's PID is A/V remote control (Assigned numbers, 16-bit UUIDs)
    assert_eq!(vec[1..3], [0x11, 0x0e]);

    // PDU ID is stored in 9th octet
    assert_eq!(vec[9], 0x50);

    // Confirm the requested volume the peer received is the same as what the AVH sent
    assert_eq!(vec[datagram_len - 1], desired_vol);

    Ok(())
}
