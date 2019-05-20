// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl_fuchsia_bluetooth as bt,
    fidl_fuchsia_bluetooth_bredr::{
        DataElement, DataElementData, DataElementType, ProtocolDescriptor, ProtocolIdentifier,
        SecurityLevel, ServiceDefinition, PSM_AVDTP,
    },
    fuchsia_bluetooth::error::Error as BTError,
};

use crate::harness::profile::ProfileHarness;

/// This makes a custom BR/EDR service definition that runs over L2CAP.
fn test_service_def() -> ServiceDefinition {
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

async fn add_service(profile: &ProfileHarness) -> Result<(bt::Status, u64), failure::Error> {
    let mut service_def = test_service_def();
    await!(profile.aux().add_service(&mut service_def, SecurityLevel::EncryptionOptional, false))
        .map_err(Into::into)
}

pub async fn add_fake_profile(profile: ProfileHarness) -> Result<(), Error> {
    let (status, _) = await!(add_service(&profile))?;
    if let Some(e) = status.error {
        return Err(BTError::from(*e).into());
    }
    Ok(())
}

pub async fn same_psm_twice_fails(profile: ProfileHarness) -> Result<(), Error> {
    await!(add_fake_profile(profile.clone()))?;
    let err = await!(add_fake_profile(profile));
    if err.is_ok() {
        return Err(format_err!("Should not be able to add the same profile twice"));
    }
    Ok(())
}

pub async fn add_remove_profile(profile: ProfileHarness) -> Result<(), Error> {
    let (status, service_id) = await!(add_service(&profile))?;
    if let Some(e) = status.error {
        return Err(BTError::from(*e).into());
    }
    profile.aux().remove_service(service_id)?;
    await!(add_fake_profile(profile))
}

pub async fn connect_unknown_peer(profile: ProfileHarness) -> Result<(), Error> {
    let (status, socket) = await!(profile.aux().connect_l2cap("unknown_peer", PSM_AVDTP as u16))?;
    // Should be an error
    if status.error.is_none() {
        return Err(format_err!("Expected an error from connecting to an unknown peer"));
    }
    if socket.is_some() {
        return Err(format_err!("Should not have a socket when we don't connect"));
    }
    Ok(())
}

// TODO(BT-659): the rest of connect_l2cap tests (that acutally succeed)
// TODO(BT-759): add_search / on_service_found
