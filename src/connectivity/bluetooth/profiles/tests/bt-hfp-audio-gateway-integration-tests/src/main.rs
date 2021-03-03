// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    bitflags::bitflags,
    bt_profile_test_server_client::ProfileTestHarness,
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_bluetooth::types::{PeerId, Uuid},
    futures::{stream::StreamExt, TryFutureExt},
};

/// HFP component URL.
const HFP_URL: &str =
    "fuchsia-pkg://fuchsia.com/bt-hfp-audio-gateway-default#meta/bt-hfp-audio-gateway.cmx";

/// SDP Attribute ID for the Supported Features of HFP.
/// Defined in Assigned Numbers for SDP
/// https://www.bluetooth.com/specifications/assigned-numbers/service-discovery
const ATTR_ID_HFP_SUPPORTED_FEATURES: u16 = 0x0311;

bitflags! {
    /// Defined in HFP v1.8, Table 5.2
    struct HandsFreeFeaturesSdpAttribute: u16 {
        const ECHO_CANCELATION_AND_NOISE_REDUCTION = 0b0000_0001;
        const THREE_WAY_CALLING                    = 0b0000_0010;
        const CLI_PRESENTATION                     = 0b0000_0100;
        const VOICE_RECOGNITION                    = 0b0000_1000;
        const REMOTE_VOLUME_CONTROL                = 0b0001_0000;
        const WIDEBAND_SPEECH                      = 0b0010_0000;
        const ENHANCED_VOICE_RECOGNITION           = 0b0100_0000;
        const ENHANCED_VOICE_RECOGNITION_TEXT      = 0b1000_0000;
    }
}

fn hfp_launch_info() -> bredr::LaunchInfo {
    bredr::LaunchInfo { component_url: Some(HFP_URL.to_string()), ..bredr::LaunchInfo::EMPTY }
}

/// Make the SDP definition for an HFP Hands Free service.
fn hfp_hf_service_definition() -> bredr::ServiceDefinition {
    let supported_features = HandsFreeFeaturesSdpAttribute::empty().bits();

    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![
            Uuid::new16(bredr::ServiceClassProfileIdentifier::Handsfree as u16).into(),
            Uuid::new16(bredr::ServiceClassProfileIdentifier::GenericAudio as u16).into(),
        ]),
        protocol_descriptor_list: Some(vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![],
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![],
            },
        ]),
        profile_descriptors: Some(vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::Handsfree,
            major_version: 1,
            minor_version: 8,
        }]),
        additional_attributes: Some(vec![bredr::Attribute {
            id: ATTR_ID_HFP_SUPPORTED_FEATURES,
            element: bredr::DataElement::Uint16(supported_features),
        }]),
        ..bredr::ServiceDefinition::new_empty()
    }
}

/// Tests that HFP correctly advertises it's services and can be
/// discovered by another peer in the mock piconet.
#[fasync::run_singlethreaded(test)]
async fn test_hfp_ag_service_advertisement() {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // Create MockPeer #1 to be driven by the test.
    let id1 = PeerId(1);
    let mock_peer1 = test_harness.register_peer(id1).await.unwrap();

    // Peer #1 adds a search for HFP AG in the piconet.
    let mut results_requests = mock_peer1
        .register_service_search(
            bredr::ServiceClassProfileIdentifier::HandsfreeAudioGateway,
            vec![],
        )
        .await
        .unwrap();

    // MockPeer #2 is the profile-under-test: HFP.
    let id2 = PeerId(2);
    let mock_peer2 = test_harness.register_peer(id2).await.unwrap();
    mock_peer2.launch_profile(hfp_launch_info()).await.expect("launch profile should be ok");

    // We expect Peer #1 to discover HFP's service advertisement.
    let service_found_fut = results_requests.select_next_some().map_err(|e| format_err!("{:?}", e));
    let bredr::SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await.unwrap();
    assert_eq!(id2, peer_id.into());
    responder.send().unwrap();
}

/// Tests that HFP correctly searches for Handsfree and discovers a mock peer
/// providing it.
#[fasync::run_singlethreaded(test)]
async fn test_hfp_search_and_connect() {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // MockPeer #1 is driven by the test.
    let id1 = PeerId(0x1111);
    let mock_peer1 = test_harness.register_peer(id1).await.unwrap();

    // Peer #1 advertises an HFP HF service.
    let service_defs = vec![hfp_hf_service_definition()];
    let _connect_requests = mock_peer1.register_service_advertisement(service_defs).await.unwrap();

    // MockPeer #2 is the profile-under-test: HFP.
    let id2 = PeerId(0x2222);
    let mut mock_peer2 = test_harness.register_peer(id2).await.unwrap();
    mock_peer2.launch_profile(hfp_launch_info()).await.expect("launch profile should be ok");

    // We expect HFP to discover Peer #1's service advertisement.
    if let bredr::PeerObserverRequest::ServiceFound { peer_id, responder, .. } =
        mock_peer2.expect_observer_request().await.unwrap()
    {
        assert_eq!(id1, peer_id.into());
        responder.send().unwrap();
    }
}
