// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_bredr as bredr;
use fidl_fuchsia_bluetooth_deviceid::*;
use fuchsia_bluetooth::types::PeerId;
use fuchsia_component_test::new::{Capability, RealmInstance};
use fuchsia_zircon as zx;
use futures::{future::Either, pin_mut, Future, FutureExt, StreamExt};
use mock_piconet_client_v2::{BtProfileComponent, PiconetHarness, PiconetMember};
use tracing::info;

const DEVICE_ID_URL_V2: &str =
    "fuchsia-pkg://fuchsia.com/bt-device-id-integration-tests#meta/bt-device-id.cm";

/// The moniker for the DeviceID component under test.
const DEVICE_ID_MONIKER: &str = "bt-device-id";
/// The moniker for a mock piconet member.
const MOCK_PICONET_MEMBER_MONIKER: &str = "mock-peer";

/// Builds the test topology for the DI integration tests. Returns the test realm, an observer
/// for bt-device-id, and a piconet member which can be driven by the test.
async fn setup_test_topology() -> (RealmInstance, BtProfileComponent, PiconetMember) {
    let mut test_harness = PiconetHarness::new().await;

    // Add a mock piconet member.
    let spec = test_harness
        .add_mock_piconet_member(MOCK_PICONET_MEMBER_MONIKER.to_string(), None)
        .await
        .expect("failed to add mock piconet member");

    // Add bt-device-id which is under test.
    let expose = vec![Capability::protocol::<DeviceIdentificationMarker>().into()];
    let di_profile = test_harness
        .add_profile_with_capabilities(
            DEVICE_ID_MONIKER.to_string(),
            DEVICE_ID_URL_V2.to_string(),
            None,
            vec![],
            expose,
        )
        .await
        .expect("failed to add DI profile");

    let test_topology = test_harness.build().await.unwrap();

    // Once the test realm has been built, we can grab the piconet member to be driven
    // by this test.
    let test_driven_peer = PiconetMember::new_from_spec(spec, &test_topology)
        .expect("failed to create piconet member from spec");

    (test_topology, di_profile, test_driven_peer)
}

fn record(primary: bool) -> DeviceIdentificationRecord {
    DeviceIdentificationRecord {
        vendor_id: Some(VendorId::BluetoothSigId(9)),
        product_id: Some(1),
        version: Some(DeviceReleaseNumber {
            major: Some(2),
            minor: Some(0),
            subminor: Some(6),
            ..DeviceReleaseNumber::EMPTY
        }),
        // Primary is optional.
        primary: Some(primary),
        // Missing optional `service_description`.
        ..DeviceIdentificationRecord::EMPTY
    }
}

/// Connects to the `DeviceIdentification` capability exposed by the DI Profile component and makes
/// a request to set the device identification.
/// Returns a future representing the set request - this should be polled to correctly detect when
/// the advertisement request has terminated.
/// Returns the Proxy associated with the request which should be kept alive until the client no
/// longer wants to advertise.
fn connect_di_client(
    topology: &RealmInstance,
    di_component: &BtProfileComponent,
    primary: bool,
) -> (impl Future<Output = Result<(), i32>>, DeviceIdentificationHandleProxy) {
    let di_svc = di_component
        .connect_to_protocol::<DeviceIdentificationMarker>(topology)
        .expect("DeviceIdentification should be available");

    let records = vec![record(primary)];
    let (token_client, token_server) =
        fidl::endpoints::create_proxy::<DeviceIdentificationHandleMarker>().unwrap();
    let request = di_svc
        .set_device_identification(&mut records.into_iter(), token_server)
        .check()
        .expect("can register");
    let fut = request.map(|r| r.expect("fidl call is ok"));

    (fut, token_client)
}

#[track_caller]
async fn expect_di_service_found(
    results_requests: &mut bredr::SearchResultsRequestStream,
    expected_id: PeerId,
) -> Vec<bredr::Attribute> {
    let service_found_fut = results_requests.select_next_some();
    let bredr::SearchResultsRequest::ServiceFound {
        peer_id, protocol, responder, attributes, ..
    } = service_found_fut.await.expect("should discover service");
    info!("Test driven peer found DI advertisement: {:?}", attributes);
    let _ = responder.send().expect("can respond to search result");
    assert_eq!(peer_id, expected_id.into());
    // Device Identification specifies no protocol - just information via attributes (it is valid
    // for it to be omitted entirely or empty).
    if let Some(p) = &protocol {
        assert!(p.is_empty());
    }
    attributes
}

#[fuchsia::test]
async fn piconet_member_discovers_di_component_advertisement() {
    let (test_topology, di_component, test_driven_peer) = setup_test_topology().await;

    // Set up a DI client that wants to set the device information.
    let (di_client_fut, di_client_token) = connect_di_client(&test_topology, &di_component, false);

    // Test driven peer adds a search for DeviceIdentification in the piconet.
    let mut results_requests = test_driven_peer
        .register_service_search(bredr::ServiceClassProfileIdentifier::PnpInformation, vec![])
        .expect("can register service search");

    // We expect it to discover the DI service advertisement.
    let attributes = expect_di_service_found(&mut results_requests, di_component.peer_id()).await;

    // All the mandatory DI attributes should exist (6).
    assert_eq!(attributes.iter().filter(|a| a.id == 0x200).count(), 1);
    assert_eq!(attributes.iter().filter(|a| a.id == 0x201).count(), 1);
    assert_eq!(attributes.iter().filter(|a| a.id == 0x202).count(), 1);
    assert_eq!(attributes.iter().filter(|a| a.id == 0x203).count(), 1);
    assert_eq!(attributes.iter().filter(|a| a.id == 0x204).count(), 1);
    assert_eq!(attributes.iter().filter(|a| a.id == 0x205).count(), 1);
    // We do expect the (optional) documentation attribute.
    assert_eq!(attributes.iter().filter(|a| a.id == 0x000A).count(), 1);

    // TODO(fxbug.dev/88050): Add a service description and verify the attribute once the MPS
    // supports parsing the `information` field of a bredr.ServiceDefinition.

    // DI client is done - DI component should process request and resolve it.
    drop(di_client_token);
    let result = di_client_fut.await;
    assert_eq!(result, Ok(()));
}

#[fuchsia::test]
async fn multiple_di_clients_can_register_advertisements() {
    let (test_topology, di_component, test_driven_peer) = setup_test_topology().await;

    // Two DI clients want to advertise.
    let (_di_client_fut1, _di_client_token1) =
        connect_di_client(&test_topology, &di_component, false);
    let (_di_client_fut2, _di_client_token2) =
        connect_di_client(&test_topology, &di_component, false);

    let mut results_requests = test_driven_peer
        .register_service_search(bredr::ServiceClassProfileIdentifier::PnpInformation, vec![])
        .expect("can register service search");

    // We expect test driven peer to discover both.
    let _attributes1 = expect_di_service_found(&mut results_requests, di_component.peer_id()).await;
    let _attributes2 = expect_di_service_found(&mut results_requests, di_component.peer_id()).await;
}

#[fuchsia::test]
async fn second_primary_di_client_is_rejected() {
    let (test_topology, di_component, test_driven_peer) = setup_test_topology().await;

    // Two DI clients want to advertise - both as primary. First one wins.
    let (di_client_fut1, _di_client_token1) =
        connect_di_client(&test_topology, &di_component, true);
    let (di_client_fut2, _di_client_token2) =
        connect_di_client(&test_topology, &di_component, true);
    pin_mut!(di_client_fut1);
    pin_mut!(di_client_fut2);

    // The first client that registers will be successful - the failed client's request will resolve
    // with error.
    let (res, _fut) = match futures::future::select(di_client_fut1, di_client_fut2).await {
        Either::Right(f) => f,
        Either::Left(f) => f,
    };
    assert_eq!(res, Err(zx::Status::ALREADY_EXISTS.into_raw()));

    // Test driven peer adds a search for DeviceIdentification in the piconet.
    let mut results_requests = test_driven_peer
        .register_service_search(bredr::ServiceClassProfileIdentifier::PnpInformation, vec![])
        .expect("can register service search");

    // We expect it to discover the DI service advertisement.
    let _attributes = expect_di_service_found(&mut results_requests, di_component.peer_id()).await;
}
