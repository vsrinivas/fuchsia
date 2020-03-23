// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::encoding::Decodable,
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_bluetooth_bredr::{
        ChannelParameters, ConnectionReceiverRequestStream, DataElement, ProtocolDescriptor,
        ProtocolIdentifier, SecurityRequirements, ServiceDefinition, PSM_AVDTP,
    },
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::types::{PeerId, Uuid},
    futures::{FutureExt, StreamExt},
};

use crate::{harness::profile::ProfileHarness, tests::timeout_duration};

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

    let _ = profile.aux().advertise(
        &mut service_defs.into_iter(),
        SecurityRequirements::new_empty(),
        ChannelParameters::new_empty(),
        connect_client,
    )?;
    Ok(connect_requests)
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
    let fut = profile.aux().connect(
        &mut PeerId(0xDEAD).into(),
        PSM_AVDTP,
        ChannelParameters::new_empty(),
    );
    match fut.await {
        Ok(Err(_)) => Ok(()),
        x => Err(format_err!("Expected error from connecting to an unknown peer, got {:?}", x)),
    }
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
            test_connect_unknown_peer
        ]
    )
}
