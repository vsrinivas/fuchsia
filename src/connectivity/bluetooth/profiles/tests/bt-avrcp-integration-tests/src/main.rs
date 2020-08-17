// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_profile_test_server::{Profile, ProfileTestHarness},
    fidl_fuchsia_bluetooth_bredr::*,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    futures::{stream::StreamExt, TryFutureExt},
    matches::assert_matches,
};

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
        .register_service_search(ServiceClassProfileIdentifier::AvRemoteControlTarget, vec![])
        .await?;

    // MockPeer #2 is the profile-under-test: AVRCP.
    let id2 = PeerId(2);
    let mock_peer2 = test_harness.register_peer(id2).await?;
    assert_matches!(mock_peer2.launch_profile(Profile::Avrcp).await, Ok(true));

    // We expect Peer #1 to discover AVRCP's service advertisement.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let SearchResultsRequest::ServiceFound { peer_id, responder, .. } = service_found_fut.await?;
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
        .register_service_search(ServiceClassProfileIdentifier::AvRemoteControl, vec![])
        .await?;

    // MockPeer #2 is the profile-under-test: AVRCP.
    let id2 = PeerId(4);
    let mock_peer2 = test_harness.register_peer(id2).await?;
    assert_matches!(mock_peer2.launch_profile(Profile::Avrcp).await, Ok(true));

    // We expect Peer #1 to discover AVRCP's service advertisement.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let SearchResultsRequest::ServiceFound { peer_id, responder, .. } = service_found_fut.await?;
    assert_eq!(id2, peer_id.into());
    responder.send()?;

    Ok(())
}
