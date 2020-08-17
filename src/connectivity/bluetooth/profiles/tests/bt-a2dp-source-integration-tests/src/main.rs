// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_profile_test_server::{Profile, ProfileTestHarness},
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr::*,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::{PeerId, Uuid},
    futures::{stream::StreamExt, TryFutureExt},
    matches::assert_matches,
};

/// Make the SDP definition for an A2DP sink service.
fn a2dp_sink_service_definition() -> ServiceDefinition {
    ServiceDefinition {
        service_class_uuids: Some(vec![Uuid::new16(0x110B).into()]), // Audio Sink UUID
        protocol_descriptor_list: Some(vec![
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::L2Cap,
                params: vec![DataElement::Uint16(PSM_AVDTP)],
            },
            ProtocolDescriptor {
                protocol: ProtocolIdentifier::Avdtp,
                params: vec![DataElement::Uint16(0x0103)], // Indicate v1.3
            },
        ]),
        profile_descriptors: Some(vec![ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AdvancedAudioDistribution,
            major_version: 1,
            minor_version: 2,
        }]),
        ..ServiceDefinition::new_empty()
    }
}

/// Tests that A2DP source correctly advertises it's services and can be
/// discovered by another peer in the mock piconet.
#[fasync::run_singlethreaded(test)]
async fn test_a2dp_source_service_advertisement() -> Result<(), Error> {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // Create MockPeer #1 to be driven by the test.
    let id1 = PeerId(1);
    let mock_peer1 = test_harness.register_peer(id1).await?;

    // Peer #1 adds a search for Audio Sources in the piconet.
    let mut results_requests = mock_peer1
        .register_service_search(ServiceClassProfileIdentifier::AudioSource, vec![])
        .await?;

    // MockPeer #2 is the profile-under-test: A2DP Source.
    let id2 = PeerId(8);
    let mock_peer2 = test_harness.register_peer(id2).await?;
    assert_matches!(mock_peer2.launch_profile(Profile::AudioSource).await, Ok(true));

    // We expect Peer #1 to discover A2DP Source's service advertisement.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let SearchResultsRequest::ServiceFound { peer_id, responder, .. } = service_found_fut.await?;
    assert_eq!(id2, peer_id.into());
    responder.send()?;

    Ok(())
}

/// Tests that A2DP source correctly searches for Audio Sinks, discovers a mock peer providing
/// Audio Sink, and attempts to connect to it.
#[fasync::run_singlethreaded(test)]
async fn test_a2dp_source_search_and_connect() -> Result<(), Error> {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // MockPeer #1 is driven by the test.
    let id1 = PeerId(10);
    let mut mock_peer1 = test_harness.register_peer(id1).await?;

    // Peer #1 advertises an Audio Sink service.
    let service_defs = vec![a2dp_sink_service_definition()];
    let mut connect_requests = mock_peer1.register_service_advertisement(service_defs).await?;

    // MockPeer #2 is the profile-under-test: A2DP Source.
    let id2 = PeerId(20);
    let mut mock_peer2 = test_harness.register_peer(id2).await?;
    assert_matches!(mock_peer2.launch_profile(Profile::AudioSource).await, Ok(true));

    // We expect A2DP Source to discover Peer #1's service advertisement.
    if let PeerObserverRequest::ServiceFound { peer_id, responder, .. } =
        mock_peer2.expect_observer_request().await?
    {
        assert_eq!(id1, peer_id.into());
        responder.send()?;
    }

    // We then expect A2DP Source to attempt to connect to Peer #1.
    match connect_requests.select_next_some().await? {
        ConnectionReceiverRequest::Connected { peer_id, .. } => {
            assert_eq!(id2, peer_id.into());
        }
    }

    // The observer of Peer #1 should be relayed of the connection attempt.
    match mock_peer1.expect_observer_request().await? {
        PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(id2, peer_id.into());
            responder.send()?;
        }
        x => return Err(format_err!("Expected PeerConnected but got: {:?}", x)),
    }
    Ok(())
}
