// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr::{
        ChannelParameters, DataElement, DataElementData, DataElementType, ProtocolDescriptor,
        ProtocolIdentifier, SecurityLevel, ServiceDefinition, PSM_AVDTP,
    },
    fuchsia_bluetooth::error::Error as BTError,
};

use crate::harness::profile::ProfileHarness;

/// This makes a custom BR/EDR service definition that runs over L2CAP.
fn service_definition_for_testing() -> ServiceDefinition {
    ServiceDefinition {
        service_class_uuids: vec![String::from("f0c451a0-7e57-1111-2222-123456789ABC")],
        protocol_descriptors: vec![ProtocolDescriptor {
            protocol: ProtocolIdentifier::L2Cap,
            params: vec![DataElement {
                type_: DataElementType::UnsignedInteger,
                size: 2,
                data: DataElementData::Integer(0x100f), // In the "dynamically-assigned" range
            }],
        }],
        profile_descriptors: vec![],
        additional_attributes: None,
        additional_protocol_descriptors: None,
        information: vec![],
    }
}

async fn add_service(profile: &ProfileHarness) -> Result<u64, anyhow::Error> {
    let mut service_def = service_definition_for_testing();
    let fut = profile.aux().add_service(
        &mut service_def,
        SecurityLevel::EncryptionOptional,
        ChannelParameters::new_empty(),
    );
    let (status, id) = fut.await?;
    if let Some(e) = status.error {
        return Err(BTError::from(*e).into());
    }
    Ok(id)
}

async fn test_add_profile(profile: ProfileHarness) -> Result<(), Error> {
    let _ = add_service(&profile).await?;
    Ok(())
}

async fn test_same_psm_twice_fails(profile: ProfileHarness) -> Result<(), Error> {
    let _ = add_service(&profile).await?;
    let result = add_service(&profile).await;
    if result.is_ok() {
        return Err(format_err!("Should not be able to add the same profile twice"));
    }
    Ok(())
}

async fn test_add_and_remove_profile(profile: ProfileHarness) -> Result<(), Error> {
    let id = add_service(&profile).await?;
    profile.aux().remove_service(id)?;

    // Adding the profile a second time after removing it should succeed.
    let _ = add_service(&profile).await;
    Ok(())
}

async fn test_connect_unknown_peer(profile: ProfileHarness) -> Result<(), Error> {
    let fut = profile.aux().connect_l2cap(
        "unknown_peer",
        PSM_AVDTP as u16,
        ChannelParameters::new_empty(),
    );
    let (status, channel) = fut.await?;
    // Should be an error
    if status.error.is_none() {
        return Err(format_err!("Expected an error from connecting to an unknown peer"));
    }
    if channel.socket.is_some() {
        return Err(format_err!("Should not have a socket when we don't connect"));
    }
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
            test_connect_unknown_peer
        ]
    )
}
