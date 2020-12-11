// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::default_audio_info;
use crate::audio::policy::{
    Request as AudioRequest, Response as AudioResponse, StateBuilder, TransformFlags,
};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::message::base::{Audience, MessengerType};
use crate::policy::base::{response::Payload, Request};
use crate::switchboard::base::{AudioStreamType, SettingType};
use crate::{internal, EnvironmentBuilder};
use fidl_fuchsia_settings_policy::{
    self as fidl, VolumePolicyControllerMarker, VolumePolicyControllerProxy,
};
use std::collections::HashSet;

const ENV_NAME: &str = "settings_service_privacy_test_environment";

/// Creates an environment for audio policy.
async fn create_test_environment() -> VolumePolicyControllerProxy {
    let storage_factory = InMemoryStorageFactory::create();

    let env = EnvironmentBuilder::new(storage_factory)
        .settings(&[SettingType::Audio])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    env.connect_to_service::<VolumePolicyControllerMarker>().unwrap()
}

// A simple validation test to ensure the policy message hub propagates messages
// properly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_message_hub() {
    let messenger_factory = internal::policy::message::create_hub();
    let policy_handler_address = internal::policy::Address::Policy(SettingType::Audio);

    // Create messenger to send request.
    let (messenger, receptor) = messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("unbound messenger should be present");

    // Create receptor to act as policy endpoint.
    let mut policy_receptor = messenger_factory
        .create(MessengerType::Addressable(policy_handler_address))
        .await
        .expect("addressable messenger should be present")
        .1;

    let request_payload = internal::policy::Payload::Request(Request::Audio(AudioRequest::Get));

    // Send request.
    let mut reply_receptor = messenger
        .message(request_payload.clone(), Audience::Address(policy_handler_address))
        .send();

    // Wait and verify request received.
    let (payload, client) = policy_receptor.next_payload().await.expect("should receive message");
    assert_eq!(payload, request_payload);
    assert_eq!(client.get_author(), receptor.get_signature());

    let state = StateBuilder::new()
        .add_property(AudioStreamType::Background, TransformFlags::TRANSFORM_MAX)
        .build();

    // Send response.
    let reply_payload =
        internal::policy::Payload::Response(Ok(Payload::Audio(AudioResponse::State(state))));
    client.reply(reply_payload.clone()).send().ack();

    // Verify response received.
    let (result_payload, _) = reply_receptor.next_payload().await.expect("should receive result");
    assert_eq!(result_payload, reply_payload);
}

// Tests that from a clean slate, the policy service returns the expected default property targets.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_get_default() {
    let expected_stream_types = default_audio_info()
        .streams
        .iter()
        .map(|stream| stream.stream_type)
        .collect::<HashSet<_>>();

    let policy_service = create_test_environment().await;

    // Attempt to read properties.
    let properties = policy_service.get_properties().await.expect("failed to get properties");

    // Verify that each expected stream type is contained in the property targets.
    assert_eq!(properties.len(), expected_stream_types.len());
    for property in properties {
        // Allow unreachable patterns so this test will still build if more Target enums are added.
        #[allow(unreachable_patterns)]
        match property.target.expect("no target found for property") {
            fidl::Target::Stream(stream) => {
                assert!(expected_stream_types.contains(&stream.into()));
            }
            _ => panic!("unexpected target type"),
        }
    }
}

// Tests that adding a new policy transform returns its policy ID and is reflected by the output
// from GetProperties.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_add_policy() {
    let expected_policy_target = AudioStreamType::Media;
    let expected_policy_parameters =
        fidl::PolicyParameters::Disable(fidl::Disable { ..fidl::Disable::EMPTY });

    let policy_service = create_test_environment().await;

    // Add a policy property and save the returned policy ID.
    let added_policy_id = policy_service
        .add_policy(
            &mut fidl::Target::Stream(expected_policy_target.into()),
            &mut expected_policy_parameters.clone(),
        )
        .await
        .expect("failed to add policy")
        .unwrap();

    // Read the policies.
    let properties = policy_service.get_properties().await.expect("failed to get properties");

    // Verify that the expected target has the given policy and that others are unchanged.
    for property in properties {
        // Allow unreachable patterns so this test will still build if more Target enums are added.
        #[allow(unreachable_patterns)]
        match property.target.expect("no target found for property") {
            fidl::Target::Stream(stream) => {
                if stream == expected_policy_target.into() {
                    // Chosen property should have a policy added.
                    let added_policies = property.active_policies.unwrap();
                    assert_eq!(added_policies.len(), 1);
                    let added_policy = added_policies.first().unwrap();
                    assert_eq!(added_policy.policy_id.unwrap(), added_policy_id);
                    assert_eq!(
                        *added_policy.parameters.as_ref().unwrap(),
                        expected_policy_parameters
                    );
                } else {
                    // Other properties shouldn't have any policies.
                    assert!(property.active_policies.unwrap().is_empty());
                }
            }
            _ => panic!("unexpected target type"),
        }
    }
}

// Tests that removing and added policy transform works and the removed policy is gone
// from GetProperties.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_remove_policy() {
    let expected_policy_target = AudioStreamType::Media;
    let expected_policy_parameters =
        fidl::PolicyParameters::Disable(fidl::Disable { ..fidl::Disable::EMPTY });

    let policy_service = create_test_environment().await;

    // Add a policy property and save the returned policy ID.
    let added_policy_id = policy_service
        .add_policy(
            &mut fidl::Target::Stream(expected_policy_target.into()),
            &mut expected_policy_parameters.clone(),
        )
        .await
        .expect("failed to add policy")
        .unwrap();

    // Remove the same policy using the returned ID.
    policy_service
        .remove_policy(added_policy_id)
        .await
        .expect("failed to remove policy")
        .expect("removal error");

    // Fetch the properties.
    let properties = policy_service.get_properties().await.expect("failed to get properties");

    // Verify that the expected target has the given policy and that others are unchanged.
    for property in properties {
        // No policies are active.
        assert!(property.active_policies.unwrap().is_empty());
    }
}
