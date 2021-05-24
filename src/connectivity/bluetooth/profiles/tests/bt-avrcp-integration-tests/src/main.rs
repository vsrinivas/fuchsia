// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_profile_test_server_client::v2::{PiconetMember, ProfileTestHarnessV2},
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::types::{Channel, Uuid},
    fuchsia_zircon as zx,
    futures::{stream::StreamExt, TryFutureExt},
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

/// Tests that AVRCP correctly advertises it's TG-related services and can be
/// discovered by another peer in the mock piconet.
#[fasync::run_singlethreaded(test)]
async fn test_avrcp_target_service_advertisement() -> Result<(), Error> {
    let mut test_harness = ProfileTestHarnessV2::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let peer1 = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");
    let mut results_requests = peer1.register_service_search(
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
async fn test_avrcp_controller_service_advertisement() -> Result<(), Error> {
    let mut test_harness = ProfileTestHarnessV2::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let peer1 = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");
    let mut results_requests = peer1
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
async fn test_avrcp_search_and_connect() -> Result<(), Error> {
    let mut test_harness = ProfileTestHarnessV2::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let mut profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let (peer1_id, peer1) = (
        spec.id.0,
        PiconetMember::new_from_spec(spec, &test_topology)
            .expect("failed to create piconet member from spec"),
    );

    // Peer #1 advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = peer1
        .register_service_advertisement(service_defs)
        .expect("Mock peer failed to register service advertisement");

    if let bredr::PeerObserverRequest::ServiceFound { peer_id, responder, .. } =
        profile_observer.expect_observer_request().await?
    {
        assert_eq!(peer1_id, peer_id.value);
        responder.send()?;
    }

    // We then expect AVRCP to attempt to connect to Peer #1.
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
async fn test_remote_initiates_connection_to_avrcp() -> Result<(), Error> {
    let mut test_harness = ProfileTestHarnessV2::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let mut profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let (peer1_id, peer1) = (
        spec.id.0,
        PiconetMember::new_from_spec(spec, &test_topology)
            .expect("failed to create piconet member from spec"),
    );

    // Peer #1 advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = peer1
        .register_service_advertisement(service_defs)
        .expect("Mock peer failed to register service advertisement");

    if let bredr::PeerObserverRequest::ServiceFound { peer_id, responder, .. } =
        profile_observer.expect_observer_request().await?
    {
        assert_eq!(peer1_id, peer_id.value);
        responder.send()?;
    }

    // We then expect AVRCP to attempt to connect to Peer #1.
    match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, .. } => {
            assert_eq!(profile_observer.peer_id(), peer_id.into());
        }
    };

    // We wait `MAX_AVRCP_CONNECTION_ESTABLISHMENT` millis before attempting to connect, as per
    // AVRCP 1.6.2, Section 4.1.1, to avoid conflict.
    const MAX_AVRCP_CONNECTION_ESTABLISHMENT: zx::Duration = zx::Duration::from_millis(1000);
    fasync::Timer::new(fasync::Time::after(MAX_AVRCP_CONNECTION_ESTABLISHMENT)).await;

    // Peer #1 attempts to connect to AVRCP.
    let l2cap = bredr::L2capParameters {
        psm: Some(bredr::PSM_AVCTP),
        ..bredr::L2capParameters::new_empty()
    };
    let params = bredr::ConnectParameters::L2cap(l2cap);
    let channel = peer1.make_connection(profile_observer.peer_id(), params).await?;
    let channel: Channel = channel.try_into()?;

    // The observer of bt-avrcp.cm should be notified of the connection attempt.
    match profile_observer.expect_observer_request().await? {
        bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(peer1_id, peer_id.value);
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
async fn test_remote_initiates_browse_channel_before_control() -> Result<(), Error> {
    let mut test_harness = ProfileTestHarnessV2::new().await;
    let spec = test_harness.add_mock_piconet_member("mock-peer".to_string()).await?;
    let mut profile_observer =
        test_harness.add_profile("bt-avrcp-profile".to_string(), AVRCP_URL_V2.to_string()).await?;
    let test_topology = test_harness.build().await?;

    let (peer1_id, peer1) = (
        spec.id.0,
        PiconetMember::new_from_spec(spec, &test_topology)
            .expect("failed to create piconet member from spec"),
    );

    // Peer #1 advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = peer1
        .register_service_advertisement(service_defs)
        .expect("Mock peer failed to register service advertisement");

    if let bredr::PeerObserverRequest::ServiceFound { peer_id, responder, .. } =
        profile_observer.expect_observer_request().await?
    {
        assert_eq!(peer1_id, peer_id.value);
        responder.send()?;
    }

    // We then expect AVRCP to attempt to connect to Peer #1.
    match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, .. } => {
            assert_eq!(profile_observer.peer_id(), peer_id.into());
        }
    };
    // Peer #1 tries to initiate a browse channel connection.
    let l2cap = bredr::L2capParameters {
        psm: Some(bredr::PSM_AVCTP_BROWSE),
        ..bredr::L2capParameters::new_empty()
    };
    let params = bredr::ConnectParameters::L2cap(l2cap);
    let channel = peer1.make_connection(profile_observer.peer_id(), params).await?;
    let channel: Channel = channel.try_into()?;

    // The observer of bt-avrcp.cm should be notified of the connection attempt.
    match profile_observer.expect_observer_request().await? {
        bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(peer1_id, peer_id.value);
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
