// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_profile_test_server::ProfileTestHarness,
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_bluetooth::types::{Channel, Uuid},
    fuchsia_zircon as zx,
    futures::{stream::StreamExt, TryFutureExt},
    std::convert::TryInto,
};

/// AVRCP component URL.
const AVRCP_URL: &str = fuchsia_component::fuchsia_single_component_package_url!("bt-avrcp");

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
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // Create MockPeer #1 to be driven by the test.
    let id1 = PeerId(1);
    let mock_peer1 = test_harness.register_peer(id1).await?;

    // Peer #1 adds a search for AVRCP Target in the piconet.
    let mut results_requests = mock_peer1
        .register_service_search(
            bredr::ServiceClassProfileIdentifier::AvRemoteControlTarget,
            vec![],
        )
        .await?;

    // MockPeer #2 is the profile-under-test: AVRCP.
    let id2 = PeerId(2);
    let mock_peer2 = test_harness.register_peer(id2).await?;
    let launch_info = bredr::LaunchInfo {
        component_url: Some(AVRCP_URL.to_string()),
        ..bredr::LaunchInfo::EMPTY
    };
    mock_peer2.launch_profile(launch_info).await.expect("launch profile should be ok");

    // We expect Peer #1 to discover AVRCP's service advertisement.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let bredr::SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await?;
    assert_eq!(id2, peer_id.into());
    responder.send()?;

    Ok(())
}

/// Tests that AVRCP correctly advertises it's CT-related services and can be
/// discovered by another peer in the mock piconet.
#[fasync::run_singlethreaded(test)]
async fn test_avrcp_controller_service_advertisement() -> Result<(), Error> {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // Create MockPeer #1 to be driven by the test.
    let id1 = PeerId(3);
    let mock_peer1 = test_harness.register_peer(id1).await?;

    // Peer #1 adds a search for AVRCP Controllers in the piconet.
    let mut results_requests = mock_peer1
        .register_service_search(bredr::ServiceClassProfileIdentifier::AvRemoteControl, vec![])
        .await?;

    // MockPeer #2 is the profile-under-test: AVRCP.
    let id2 = PeerId(4);
    let mock_peer2 = test_harness.register_peer(id2).await?;
    let launch_info = bredr::LaunchInfo {
        component_url: Some(AVRCP_URL.to_string()),
        ..bredr::LaunchInfo::EMPTY
    };
    mock_peer2.launch_profile(launch_info).await.expect("launch profile should be ok");

    // We expect Peer #1 to discover AVRCP's service advertisement.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let bredr::SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await?;
    assert_eq!(id2, peer_id.into());
    responder.send()?;

    Ok(())
}

/// Tests that AVRCP correctly searches for AvRemoteControls, discovers a mock peer
/// providing it, and attempts to connect to the mock peer.
#[fasync::run_singlethreaded(test)]
async fn test_avrcp_search_and_connect() -> Result<(), Error> {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // MockPeer #1 is driven by the test.
    let id1 = PeerId(4532);
    let mut mock_peer1 = test_harness.register_peer(id1).await?;

    // Peer #1 advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = mock_peer1.register_service_advertisement(service_defs).await?;

    // MockPeer #2 is the profile-under-test: AVRCP.
    let id2 = PeerId(923445);
    let mut mock_peer2 = test_harness.register_peer(id2).await?;
    let launch_info = bredr::LaunchInfo {
        component_url: Some(AVRCP_URL.to_string()),
        ..bredr::LaunchInfo::EMPTY
    };
    mock_peer2.launch_profile(launch_info).await.expect("launch profile should be ok");

    // We expect AVRCP to discover Peer #1's service advertisement.
    if let bredr::PeerObserverRequest::ServiceFound { peer_id, responder, .. } =
        mock_peer2.expect_observer_request().await?
    {
        assert_eq!(id1, peer_id.into());
        responder.send()?;
    }

    // We then expect AVRCP to attempt to connect to Peer #1.
    let _channel = match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } => {
            assert_eq!(id2, peer_id.into());
            channel
        }
    };

    // The observer of Peer #1 should be relayed of the connection attempt.
    match mock_peer1.expect_observer_request().await? {
        bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(id2, peer_id.into());
            responder.send()?;
        }
        x => return Err(format_err!("Expected PeerConnected but got: {:?}", x)),
    }
    Ok(())
}

/// Tests the case of a remote peer initiating the AVCTP connection to AVRCP.
/// AVRCP should accept the connection (which should be relayed on the observer), and
/// sending/receiving data should be OK.
#[fasync::run_singlethreaded(test)]
async fn test_remote_initiates_connection_to_avrcp() -> Result<(), Error> {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // MockPeer #1 is driven by the test.
    let id1 = PeerId(89712);
    let mock_peer1 = test_harness.register_peer(id1).await?;

    // Peer #1 advertises an AVRCP CT service.
    let service_defs = vec![avrcp_controller_service_definition()];
    let mut connect_requests = mock_peer1.register_service_advertisement(service_defs).await?;

    // MockPeer #2 is the profile-under-test: AVRCP.
    let id2 = PeerId(23418);
    let mut mock_peer2 = test_harness.register_peer(id2).await?;
    let launch_info = bredr::LaunchInfo {
        component_url: Some(AVRCP_URL.to_string()),
        ..bredr::LaunchInfo::EMPTY
    };
    mock_peer2.launch_profile(launch_info).await.expect("launch profile should be ok");

    // We expect AVRCP to discover Peer #1's service advertisement.
    if let bredr::PeerObserverRequest::ServiceFound { peer_id, responder, .. } =
        mock_peer2.expect_observer_request().await?
    {
        assert_eq!(id1, peer_id.into());
        responder.send()?;
    }

    // We then expect AVRCP to attempt to connect to Peer #1.
    // Peer #1 rejects the connection as it will initiate a connection itself.
    match connect_requests.select_next_some().await? {
        bredr::ConnectionReceiverRequest::Connected { peer_id, .. } => {
            assert_eq!(id2, peer_id.into());
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
    let channel = mock_peer1.make_connection(id2, params).await?;
    let channel: Channel = channel.try_into()?;

    // The observer of bt-avrcp.cmx should be relayed of the connection attempt.
    match mock_peer2.expect_observer_request().await? {
        bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(id1, peer_id.into());
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
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // MockPeer #1 is driven by the test.
    let id1 = PeerId(138);
    let mock_peer1 = test_harness.register_peer(id1).await?;

    // Peer #1 searches for AVRCP CT services.
    let mut results_requests = mock_peer1
        .register_service_search(bredr::ServiceClassProfileIdentifier::AvRemoteControl, vec![])
        .await?;

    // MockPeer #2 is the profile-under-test: AVRCP.
    let id2 = PeerId(1076);
    let mut mock_peer2 = test_harness.register_peer(id2).await?;
    let launch_info = bredr::LaunchInfo {
        component_url: Some(AVRCP_URL.to_string()),
        ..bredr::LaunchInfo::EMPTY
    };
    mock_peer2.launch_profile(launch_info).await.expect("launch profile should be ok");

    // We expect Peer #1 to discover AVRCP's service advertisement.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let bredr::SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await?;
    assert_eq!(id2, peer_id.into());
    responder.send()?;

    // Peer #1 tries to initiate a browse channel connection.
    let l2cap = bredr::L2capParameters {
        psm: Some(bredr::PSM_AVCTP_BROWSE),
        ..bredr::L2capParameters::new_empty()
    };
    let params = bredr::ConnectParameters::L2cap(l2cap);
    let channel = mock_peer1.make_connection(id2, params).await?;
    let channel: Channel = channel.try_into()?;

    // The observer of AVRCP should be relayed of the connection attempt.
    match mock_peer2.expect_observer_request().await? {
        bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(id1, peer_id.into());
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
