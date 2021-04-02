// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bitflags::bitflags,
    bt_profile_test_server_client::{MockPeer, ProfileTestHarness},
    fidl::{
        encoding::Decodable,
        endpoints::{ServerEnd, ServiceMarker},
    },
    fidl_fuchsia_bluetooth_bredr as bredr,
    fidl_fuchsia_bluetooth_hfp::HfpMarker,
    fidl_fuchsia_sys::EnvironmentOptions,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::{Channel, PeerId, Uuid},
    fuchsia_component::server::{NestedEnvironment, ServiceFs, ServiceObj},
    futures::{stream::StreamExt, TryFutureExt},
    std::convert::TryInto,
    test_call_manager::{TestCallManager, HFP_AG_URL},
};

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
    let url = Some(HFP_AG_URL.to_string());
    bredr::LaunchInfo { component_url: url.clone(), ..bredr::LaunchInfo::EMPTY }
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

fn launch_hfp(
    url: &str,
) -> (ServiceFs<ServiceObj<'_, fidl::Channel>>, NestedEnvironment, fuchsia_component::client::App) {
    let mut fs = ServiceFs::new();
    fs.add_service_at(bredr::ProfileMarker::NAME, |chan| Some(chan));
    let options = EnvironmentOptions {
        inherit_parent_services: true,
        use_parent_runners: false,
        kill_on_oom: false,
        delete_storage_on_death: false,
    };
    let env = fs.create_salted_nested_environment_with_options(&"hfp", options).unwrap();
    let app = fuchsia_component::client::launch(env.launcher(), url.to_string(), None).unwrap();
    (fs, env, app)
}

async fn connect_profile_to_peer(
    peer: &mut MockPeer,
    fs: &mut ServiceFs<ServiceObj<'_, fidl::Channel>>,
) -> Result<(), Error> {
    let channel = fs.next().await.ok_or(format_err!("Connection expected from hfp component"))?;
    let server_end = ServerEnd::<bredr::ProfileMarker>::new(channel);
    peer.connect_proxy(server_end).await?;
    Ok(())
}

/// Tests that HFP correctly searches for Handsfree, discovers a mock peer
/// providing it, and attempts to connect to the mock peer.
#[fasync::run_singlethreaded(test)]
async fn test_hfp_search_and_connect() {
    let test_harness = ProfileTestHarness::new().expect("Failed to create profile test harness");

    // MockPeer #1 is driven by the test.
    let id1 = PeerId(0x1111);
    let mut mock_peer1 = test_harness.register_peer(id1).await.unwrap();

    // Peer #1 advertises an HFP HF service.
    let service_defs = vec![hfp_hf_service_definition()];
    let mut connect_requests =
        mock_peer1.register_service_advertisement(service_defs).await.unwrap();

    // MockPeer #2 is the profile-under-test: HFP.
    let id2 = PeerId(0x2222);
    let mut mock_peer2 = test_harness.register_peer(id2).await.unwrap();

    // Launch hfp component and wire it up to MockPeer #2
    let (mut fs, _env, app) = launch_hfp(HFP_AG_URL);
    connect_profile_to_peer(&mut mock_peer2, &mut fs).await.unwrap();

    let proxy = app.connect_to_service::<HfpMarker>().unwrap();
    let facade = TestCallManager::new();
    facade.register_manager(proxy).await.unwrap();

    // We expect HFP to discover Peer #1's service advertisement.
    if let bredr::PeerObserverRequest::ServiceFound { peer_id, responder, .. } =
        mock_peer2.expect_observer_request().await.unwrap()
    {
        assert_eq!(id1, peer_id.into());
        responder.send().unwrap();
    }

    // We then expect HFP to attempt to connect to Peer #1.
    let _channel: Channel = match connect_requests.select_next_some().await.unwrap() {
        bredr::ConnectionReceiverRequest::Connected { peer_id, channel, .. } => {
            assert_eq!(id2, peer_id.into());
            channel.try_into().unwrap()
        }
    };

    // The observer of Peer #1 should be relayed of the connection attempt.
    match mock_peer1.expect_observer_request().await.unwrap() {
        bredr::PeerObserverRequest::PeerConnected { peer_id, responder, .. } => {
            assert_eq!(id2, peer_id.into());
            responder.send().unwrap();
        }
        x => panic!("Expected PeerConnected but got: {:?}", x),
    }
}
