// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::encoding::Decodable,
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_bluetooth::{DeviceClass, MAJOR_DEVICE_CLASS_MISCELLANEOUS},
    fidl_fuchsia_bluetooth_bredr::{
        ChannelParameters, ConnectParameters, ConnectionReceiverRequestStream, DataElement,
        L2capParameters, ProtocolDescriptor, ProtocolIdentifier, SearchResultsRequestStream,
        ServiceClassProfileIdentifier, ServiceDefinition, PSM_AVDTP,
    },
    fidl_fuchsia_bluetooth_sys::ProcedureTokenProxy,
    fidl_fuchsia_bluetooth_test::{BredrPeerParameters, HciEmulatorProxy, PeerProxy},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::{
        expectation::asynchronous::ExpectableStateExt,
        types::Address,
        types::{PeerId, Uuid},
    },
    futures::{FutureExt, StreamExt},
};

use crate::{
    harness::{
        access::{expectation, AccessHarness},
        profile::ProfileHarness,
    },
    tests::timeout_duration,
};

/// This makes a custom BR/EDR service definition that runs over L2CAP.
fn service_definition_for_testing() -> ServiceDefinition {
    let test_uuid: Uuid = "f0c451a0-7e57-1111-2222-123456789ABC".parse().expect("UUID to parse");
    ServiceDefinition {
        service_class_uuids: Some(vec![test_uuid.into()]),
        protocol_descriptor_list: Some(vec![ProtocolDescriptor {
            protocol: ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(0x100f)], // In the "dynamically-assigned" range
        }]),
        ..ServiceDefinition::new_empty()
    }
}

fn add_service(profile: &ProfileHarness) -> Result<ConnectionReceiverRequestStream, anyhow::Error> {
    let service_defs = vec![service_definition_for_testing()];
    let (connect_client, connect_requests) =
        create_request_stream().context("ConnectionReceiver creation")?;

    let _ = profile.aux().proxy().advertise(
        &mut service_defs.into_iter(),
        ChannelParameters::new_empty(),
        connect_client,
    );
    Ok(connect_requests)
}

async fn create_bredr_peer(proxy: &HciEmulatorProxy, address: Address) -> Result<PeerProxy, Error> {
    let peer_params = BredrPeerParameters {
        address: Some(address.into()),
        connectable: Some(true),
        device_class: Some(DeviceClass { value: MAJOR_DEVICE_CLASS_MISCELLANEOUS + 0 }),
        service_definition: None,
    };

    let (peer, remote) = fidl::endpoints::create_proxy()?;
    let _ = proxy
        .add_bredr_peer(peer_params, remote)
        .await?
        .map_err(|e| format_err!("Failed to register fake peer: {:#?}", e))?;
    Ok(peer)
}

async fn start_discovery(access: &AccessHarness) -> Result<ProcedureTokenProxy, Error> {
    // We create a capability to capture the discovery token, and pass it to the profile provider
    // Discovery will stop once we drop this token
    let (token, token_server) = fidl::endpoints::create_proxy()?;
    let fidl_response = access.aux().start_discovery(token_server);
    fidl_response
        .await?
        .map_err(|sys_err| format_err!("Error calling StartDiscovery(): {:?}", sys_err))?;
    Ok(token)
}

async fn add_search(profile: &ProfileHarness) -> Result<SearchResultsRequestStream, Error> {
    let (results_client, results_stream) =
        create_request_stream().context("SearchResults creation")?;
    profile.aux().proxy().search(
        ServiceClassProfileIdentifier::AudioSource,
        &[],
        results_client,
    )?;
    Ok(results_stream)
}

fn default_address() -> Address {
    Address::Public([1, 0, 0, 0, 0, 0])
}

async fn test_add_profile(profile: ProfileHarness) -> Result<(), Error> {
    let _ = add_service(&profile)?;
    Ok(())
}

async fn test_same_psm_twice_fails(profile: ProfileHarness) -> Result<(), Error> {
    let _request_stream = add_service(&profile)?;
    let mut second_request_stream = add_service(&profile)?;
    // Second request should have a closed stream
    match second_request_stream
        .next()
        .on_timeout(timeout_duration().after_now(), || {
            Some(Err(fidl::Error::UnexpectedSyncResponse))
        })
        .await
    {
        None => Ok(()),
        x => Err(format_err!("Expected client to close, but instead got {:?}", x)),
    }
}

async fn test_add_and_remove_profile(profile: ProfileHarness) -> Result<(), Error> {
    let request_stream = add_service(&profile)?;

    drop(request_stream);

    // Adding the profile a second time after removing it should succeed.
    let mut request_stream = add_service(&profile)?;

    // Request stream should be pending (otherwise it is an error)
    if request_stream.next().now_or_never().is_some() {
        return Err(format_err!("Should not have an error on re-adding the service"));
    }

    Ok(())
}

async fn test_connect_unknown_peer(profile: ProfileHarness) -> Result<(), Error> {
    let fut = profile.aux().proxy().connect(
        &mut PeerId(0xDEAD).into(),
        &mut ConnectParameters::L2cap(L2capParameters {
            psm: Some(PSM_AVDTP),
            ..L2capParameters::new_empty()
        }),
    );
    match fut.await {
        Ok(Err(_)) => Ok(()),
        x => Err(format_err!("Expected error from connecting to an unknown peer, got {:?}", x)),
    }
}

async fn test_connect_peer(
    (access, profile): (AccessHarness, ProfileHarness),
) -> Result<(), Error> {
    let emulator = profile.aux().emulator().clone();
    let peer_address = default_address();
    let _search_result = add_search(&profile).await?;
    let _discovery_result = start_discovery(&access).await?;
    let _peer = create_bredr_peer(&emulator, peer_address).await?;

    let state = access
        .when_satisfied(expectation::peer_with_address(peer_address), timeout_duration())
        .await?;

    // We can safely unwrap here as this is guarded by the previous expectation
    let peer_id = state.peers.values().find(|p| p.address == peer_address).unwrap().id;

    let fidl_response = access.aux().connect(&mut peer_id.into());
    fidl_response
        .await?
        .map_err(|sys_err| format_err!("Error calling Connect(): {:?}", sys_err))?;
    access.when_satisfied(expectation::peer_connected(peer_id, true), timeout_duration()).await?;
    Ok(())
}

// TODO(BT-659): the rest of connect_l2cap tests (that acutally succeed)
// TODO(BT-759): add_search / on_service_found

/// Run all test cases.
pub fn run_all() -> Result<(), Error> {
    run_suite!(
        "bredr.Profile",
        [
            test_add_profile,
            test_same_psm_twice_fails,
            test_add_and_remove_profile,
            test_connect_unknown_peer,
            test_connect_peer
        ]
    )
}
