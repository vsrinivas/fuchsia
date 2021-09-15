// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::Decodable;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_bluetooth_bredr::*;
use fidl_fuchsia_cobalt::LoggerFactoryMarker;
use fidl_fuchsia_mediacodec::CodecFactoryMarker;
use fidl_fuchsia_sysmem::AllocatorMarker;
use fidl_fuchsia_tracing_provider::RegistryMarker;
use fixture::fixture;
use fuchsia_bluetooth::types::Uuid;
use fuchsia_component_test::{builder::Capability, RealmInstance};
use futures::stream::StreamExt;
use mock_piconet_client::v2::{BtProfileComponent, PiconetHarness, PiconetMember};

/// A2DP Source component V2 URL.
const A2DP_URL_V2: &str =
    "fuchsia-pkg://fuchsia.com/bt-a2dp-source-integration-tests#meta/bt-a2dp-source.cm";
const A2DP_SOURCE_MONIKER: &str = "bt-a2dp-source-profile";
/// Name prefix for mock peers
const MOCK_PEER_NAME: &str = "mock-peer";

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

struct A2dpSourceIntegrationTest {
    test_realm: RealmInstance,
    a2dp_src_under_test: BtProfileComponent,
    test_driven_peer: PiconetMember,
}

async fn setup_piconet_with_a2dp_source_and_mock_peer() -> A2dpSourceIntegrationTest {
    let mut test_harness = PiconetHarness::new().await;

    // Piconet member to be driven by the test.
    let spec = test_harness
        .add_mock_piconet_member(MOCK_PEER_NAME.to_string(), None)
        .await
        .expect("can add member");

    // Piconet member representing the profile-under-test: A2DP Source.
    // Note: Only a subset of capabilities are provided to A2DP Source; this is the minimum set of
    // capabilities sufficient for the scope of these integration tests. This list can be expanded
    // when the test code becomes more complex.
    let use_capabilities = vec![
        Capability::protocol(CodecFactoryMarker::PROTOCOL_NAME),
        Capability::protocol(LoggerFactoryMarker::PROTOCOL_NAME),
        Capability::protocol(AllocatorMarker::PROTOCOL_NAME),
        Capability::protocol(RegistryMarker::PROTOCOL_NAME),
    ];
    let a2dp_src_under_test = test_harness
        .add_profile_with_capabilities(
            A2DP_SOURCE_MONIKER.to_string(),
            A2DP_URL_V2.to_string(),
            None,
            use_capabilities,
            vec![],
        )
        .await
        .expect("can add profile");

    let test_realm = test_harness.build().await.expect("test topology should build");
    let test_driven_peer =
        PiconetMember::new_from_spec(spec, &test_realm).expect("can build member");

    A2dpSourceIntegrationTest { test_realm, a2dp_src_under_test, test_driven_peer }
}

async fn setup_piconet<F, Fut>(_name: &str, test: F)
where
    F: FnOnce(A2dpSourceIntegrationTest) -> Fut,
    Fut: futures::Future<Output = ()>,
{
    let test_fixture = setup_piconet_with_a2dp_source_and_mock_peer().await;
    test(test_fixture).await
}

/// Tests that A2DP source correctly advertises it's services and can be discovered by another peer
/// in the mock piconet.
#[fixture(setup_piconet)]
#[fuchsia::test]
async fn a2dp_source_service_advertisement_discovered_by_peer(tf: A2dpSourceIntegrationTest) {
    let A2dpSourceIntegrationTest { test_realm: _tr, a2dp_src_under_test, test_driven_peer } = tf;

    // Test driven peer adds a search for Audio Sources in the piconet.
    let mut results_requests = test_driven_peer
        .register_service_search(ServiceClassProfileIdentifier::AudioSource, vec![])
        .expect("can register service search");

    // We expect it to discover A2DP Source component's service advertisement.
    let service_found_fut = results_requests.select_next_some();
    let SearchResultsRequest::ServiceFound { peer_id, responder, .. } =
        service_found_fut.await.expect("should discover service");
    assert_eq!(peer_id, a2dp_src_under_test.peer_id().into());
    let _ = responder.send().unwrap();
}

/// Tests that A2DP source correctly searches for Audio Sinks, discovers a mock peer providing
/// Audio Sink, and attempts to connect to it.
#[fixture(setup_piconet)]
#[fuchsia::test]
async fn a2dp_source_search_and_connect(tf: A2dpSourceIntegrationTest) {
    let A2dpSourceIntegrationTest { test_realm: _tr, mut a2dp_src_under_test, test_driven_peer } =
        tf;

    // Test-driven peer advertises an AudioSink service.
    let service_defs = vec![a2dp_sink_service_definition()];
    let mut connect_requests = test_driven_peer
        .register_service_advertisement(service_defs)
        .expect("can register advertisement");

    // We expect A2DP Source to discover the service advertisement.
    a2dp_src_under_test
        .expect_observer_service_found_request(test_driven_peer.peer_id())
        .await
        .expect("should observe service found");

    // We then expect A2DP Source to attempt to connect to the peer.
    match connect_requests.select_next_some().await.expect("connection request") {
        ConnectionReceiverRequest::Connected { peer_id, .. } => {
            assert_eq!(peer_id, a2dp_src_under_test.peer_id().into());
        }
    }
}
