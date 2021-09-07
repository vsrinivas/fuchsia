// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Services;
use crate::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings_policy::{
    PolicyParameters, Property, Target, Transform, Volume as PolicyVolume,
    VolumePolicyControllerMarker, VolumePolicyControllerRequest,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use setui_client_lib::{volume_policy, VolumePolicyCommands, VolumePolicyOptions};

// Verifies that invoking a volume policy command with no arguments fetches the policy properties.
pub(crate) async fn validate_volume_policy_get() -> Result<(), Error> {
    // Create a fake volume policy service that responds to GetProperties with a single property.
    // Any other calls will cause the test to fail.
    let env = create_service!(
        Services::VolumePolicy,
        VolumePolicyControllerRequest::GetProperties { responder } => {
            let mut properties = Vec::new();
            properties.push(Property {
                target: Some(Target::Stream(AudioRenderUsage::Background)),
                active_policies: Some(vec![]),
                available_transforms: Some(vec![Transform::Max]),
                ..Property::EMPTY
            });
            responder.send(&mut properties.into_iter())?;
        }
    );

    let volume_policy_service = env
        .connect_to_protocol::<VolumePolicyControllerMarker>()
        .context("Failed to connect to volume policy service")?;

    let output = assert_get!(volume_policy::command(volume_policy_service, None, None));
    // Spot-check that the output contains the available transform in the data returned from the
    // fake service.
    assert!(output.contains("Max"));
    Ok(())
}

// Verifies that adding a new policy works and prints out the resulting policy ID.
pub(crate) async fn validate_volume_policy_add() -> Result<(), Error> {
    let expected_target = AudioRenderUsage::Background;
    let expected_volume: f32 = 1.0;
    let expected_policy_id = 42;
    let add_options = VolumePolicyCommands::AddPolicy(VolumePolicyOptions {
        target: expected_target,
        min: None,
        max: Some(expected_volume),
    });

    // Create a fake volume policy service that responds to AddPolicy and verifies that the inputs
    // are the same as expected, then return the expected policy ID. Any other calls will cause the
    // test to fail.
    let env = create_service!(
        Services::VolumePolicy,
        VolumePolicyControllerRequest::AddPolicy { target, parameters, responder } => {
            assert_eq!(target, Target::Stream(expected_target));
            assert_eq!(
                parameters,
                PolicyParameters::Max(PolicyVolume {
                    volume: Some(expected_volume),
                    ..PolicyVolume::EMPTY
                })
            );
            responder.send(&mut Ok(expected_policy_id))?;
        }
    );

    let volume_policy_service = env
        .connect_to_protocol::<VolumePolicyControllerMarker>()
        .context("Failed to connect to volume policy service")?;

    // Make the add call.
    let output =
        assert_set!(volume_policy::command(volume_policy_service, Some(add_options), None));
    // Verify that the output contains the policy ID returned from the fake service.
    assert!(output.contains(expected_policy_id.to_string().as_str()));
    Ok(())
}

// Verifies that removing a policy sends the proper call to the volume policy API.
pub(crate) async fn validate_volume_policy_remove() -> Result<(), Error> {
    let expected_policy_id = 42;
    // Create a fake volume policy service that verifies the removed policy ID matches the expected
    // value. Any other calls will cause the
    // test to fail.
    let env = create_service!(
        Services::VolumePolicy, VolumePolicyControllerRequest::RemovePolicy { policy_id, responder } => {
            assert_eq!(policy_id, expected_policy_id);
            responder.send(&mut Ok(()))?;
        }
    );

    let volume_policy_service = env
        .connect_to_protocol::<VolumePolicyControllerMarker>()
        .context("Failed to connect to volume policy service")?;

    // Attempt to remove the given policy ID.
    assert_set!(volume_policy::command(volume_policy_service, None, Some(expected_policy_id)));
    Ok(())
}
