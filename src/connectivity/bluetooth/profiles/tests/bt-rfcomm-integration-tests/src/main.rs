// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::types::{PeerId, Uuid},
    fuchsia_component_test::{builder::Capability, RealmInstance},
    futures::stream::StreamExt,
    mock_piconet_client::v2::{BtProfileComponent, PiconetHarness, PiconetMember},
    profile_client::ProfileClient,
};

/// RFCOMM component V2 URL.
/// The RFCOMM component is a unique component in that it functions as a proxy for the
/// `f.b.bredr.Profile` protocol. Consequently, it both connects to and provides the
/// `f.b.bredr.Profile` protocol.
/// It only manages Profile requests that require RFCOMM - any non-RFCOMM requests
/// are relayed to the upstream `f.b.bredr.Profile` provider.
const RFCOMM_URL_V2: &str =
    "fuchsia-pkg://fuchsia.com/bt-rfcomm-integration-tests#meta/bt-rfcomm.cm";

/// The moniker for the RFCOMM component under test.
const RFCOMM_MONIKER: &str = "bt-rfcomm";

/// The moniker for a mock piconet member.
const MOCK_PICONET_MEMBER_MONIKER: &str = "mock-peer";

/// The SppClient in these integration tests is just an alias for the `ProfileClient`.
type SppClient = ProfileClient;

/// Returns a valid SPP Service Definition.
/// See SPP V12 Table 6.1.
pub fn spp_service_definition() -> bredr::ServiceDefinition {
    bredr::ServiceDefinition {
        service_class_uuids: Some(vec![Uuid::new16(
            bredr::ServiceClassProfileIdentifier::SerialPort as u16,
        )
        .into()]),
        protocol_descriptor_list: Some(vec![
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::L2Cap,
                params: vec![], // Ignored for RFCOMM.
            },
            bredr::ProtocolDescriptor {
                protocol: bredr::ProtocolIdentifier::Rfcomm,
                params: vec![], // This will be populated by the RFCOMM component.
            },
        ]),
        profile_descriptors: Some(vec![bredr::ProfileDescriptor {
            profile_id: bredr::ServiceClassProfileIdentifier::SerialPort,
            major_version: 1,
            minor_version: 2,
        }]),
        ..bredr::ServiceDefinition::EMPTY
    }
}

/// Builds the test topology for the RFCOMM integration tests. Returns the test realm, an observer
/// for bt-rfcomm, and a piconet member which can be driven by the test.
#[track_caller]
async fn setup_test_topology() -> (RealmInstance, BtProfileComponent, PiconetMember) {
    let mut test_harness = PiconetHarness::new().await;

    // Add a mock piconet member.
    let spec = test_harness
        .add_mock_piconet_member(MOCK_PICONET_MEMBER_MONIKER.to_string(), None)
        .await
        .expect("failed to add mock piconet member");
    // Add bt-rfcomm which is under test.
    let expose = vec![Capability::protocol(bredr::ProfileMarker::PROTOCOL_NAME)];
    let rfcomm = test_harness
        .add_profile_with_capabilities(
            RFCOMM_MONIKER.to_string(),
            RFCOMM_URL_V2.to_string(),
            None,
            vec![],
            expose,
        )
        .await
        .expect("failed to add RFCOMM profile");
    let test_topology = test_harness.build().await.unwrap();

    // Once the test realm has been built, we can grab the piconet member to be driven
    // by this test.
    let test_driven_peer = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");

    (test_topology, rfcomm, test_driven_peer)
}

/// Returns an SppClient that uses the `Profile` capability exposed by the RFCOMM component in the
/// test `topology`.
/// For the purposes of the integration tests, the RFCOMM client advertises SPP.
#[track_caller]
async fn setup_spp_client(
    topology: &RealmInstance,
    rfcomm_component: &BtProfileComponent,
) -> SppClient {
    let profile = rfcomm_component
        .connect_to_protocol::<bredr::ProfileMarker>(topology)
        .expect("Profile should be available");
    let spp = spp_service_definition();
    ProfileClient::advertise(profile, &vec![spp], bredr::ChannelParameters::EMPTY).unwrap()
}

#[track_caller]
async fn expect_peer_advertising(
    search_results: &mut bredr::SearchResultsRequestStream,
    expected_id: PeerId,
) {
    let request = search_results.select_next_some().await.unwrap();
    let bredr::SearchResultsRequest::ServiceFound { peer_id, protocol, responder, .. } = request;
    assert_eq!(expected_id, peer_id.into());
    log::info!("Discovered service with protocol: {:?}", protocol);
    responder.send().unwrap();
}

/// Tests that an RFCOMM-requesting client can register a service advertisement
/// which is discoverable by others in the piconet. If the client re-connects and
/// decides to re-advertise, the service should be discoverable.
#[fuchsia::test]
async fn register_rfcomm_service_advertisement_is_discovered() {
    let (test_topology, rfcomm_under_test, test_driven_peer) = setup_test_topology().await;

    // Poke at `bt-rfcomm` by registering a fake SPP client that uses the Profile protocol
    // provided by it.
    let spp_client = setup_spp_client(&test_topology, &rfcomm_under_test).await;

    // Test driven member searches for SPP services.
    let mut search_results = test_driven_peer
        .register_service_search(bredr::ServiceClassProfileIdentifier::SerialPort, vec![])
        .unwrap();
    // We expect it to discover `spp_client's` service advertisement.
    expect_peer_advertising(&mut search_results, rfcomm_under_test.peer_id()).await;

    // For some reason, the SPP client disconnects (component termination, error, etc..).
    drop(spp_client);

    // Client coming back should be OK.
    let _spp_client2 = setup_spp_client(&test_topology, &rfcomm_under_test).await;

    // We expect test driven member to discover `spp_client2s` service advertisement.
    expect_peer_advertising(&mut search_results, rfcomm_under_test.peer_id()).await;
}

#[fuchsia::test]
async fn multiple_rfcomm_clients_can_register_advertisements() {
    let (test_topology, rfcomm_under_test, test_driven_peer) = setup_test_topology().await;

    // Test driven member searches for SPP services.
    let mut search_results = test_driven_peer
        .register_service_search(bredr::ServiceClassProfileIdentifier::SerialPort, vec![])
        .unwrap();

    let _spp_client1 = setup_spp_client(&test_topology, &rfcomm_under_test).await;
    // `spp_client1`'s advertisement should be discovered.
    expect_peer_advertising(&mut search_results, rfcomm_under_test.peer_id()).await;

    let _spp_client2 = setup_spp_client(&test_topology, &rfcomm_under_test).await;
    // Because `bt-rfcomm` manages multiple RFCOMM service advertisements, we expect _2_ search
    // result events. This is because `bt-rfcomm` unregisters `_spp_client1`'s advertisement, groups
    // `_spp_client2`'s advertisement with it, and re-registers them together. As such, the new
    // "unified" advertisement consists of two services.
    expect_peer_advertising(&mut search_results, rfcomm_under_test.peer_id()).await;
    expect_peer_advertising(&mut search_results, rfcomm_under_test.peer_id()).await;

    // There should be no more search result events.
    assert!(futures::poll!(&mut search_results.next()).is_pending());
}
