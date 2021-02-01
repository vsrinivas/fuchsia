// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::default_audio_info;
use crate::audio::policy::{PropertyTarget, StateBuilder, Transform, TransformFlags};
use crate::audio::types::{AudioSettingSource, AudioStream, AudioStreamType};
use crate::base::SettingType;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::message::base::{Audience, MessengerType};
use crate::policy::base::{response::Payload, PolicyInfo, PolicyType, Request};
use crate::tests::fakes::audio_core_service;
use crate::tests::fakes::service_registry::ServiceRegistry;
use crate::{internal, EnvironmentBuilder};
use fidl_fuchsia_settings::{self as audio_fidl, AudioMarker, AudioProxy};
use fidl_fuchsia_settings_policy::{
    self as policy_fidl, VolumePolicyControllerMarker, VolumePolicyControllerProxy,
};
use fuchsia_component::server::NestedEnvironment;
use std::collections::HashSet;

const ENV_NAME: &str = "settings_service_privacy_test_environment";

struct TestEnvironment {
    /// The nested environment itself.
    nested_environment: NestedEnvironment,

    /// Handle to the volume policy service.
    policy_service: VolumePolicyControllerProxy,

    /// Handle to the setui audio service.
    setui_audio_service: AudioProxy,
}

/// Creates an environment for audio policy.
async fn create_test_environment() -> TestEnvironment {
    let storage_factory = InMemoryStorageFactory::create();

    let service_registry = ServiceRegistry::create();

    let audio_core_service_handle = audio_core_service::Builder::new().build();
    service_registry.lock().await.register_service(audio_core_service_handle.clone());

    let env = EnvironmentBuilder::new(storage_factory)
        .service(ServiceRegistry::serve(service_registry))
        .settings(&[SettingType::Audio])
        .policies(&[PolicyType::Audio])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let policy_service = env.connect_to_service::<VolumePolicyControllerMarker>().unwrap();
    let setui_audio_service = env.connect_to_service::<AudioMarker>().unwrap();

    TestEnvironment { nested_environment: env, policy_service, setui_audio_service }
}

/// Sets the stream volume to the given volume level using the audio service.
async fn set_stream_volume(env: &TestEnvironment, stream: AudioStreamType, volume_level: f32) {
    let mut audio_settings = audio_fidl::AudioSettings::EMPTY;
    audio_settings.streams = Some(vec![AudioStream {
        stream_type: stream,
        source: AudioSettingSource::User,
        user_volume_level: volume_level,
        user_volume_muted: false,
    }
    .into()]);
    env.setui_audio_service
        .set(audio_settings)
        .await
        .expect("set call succeeded")
        .expect("set succeeded");
}

/// Get the volume of the given stream from the audio service in the given test environment.
///
/// Panics for an unexpected result, such as the stream or volume level not being present.
async fn get_stream_volume(env: &TestEnvironment, stream: AudioStreamType) -> f32 {
    get_stream_volume_from_proxy(&env.setui_audio_service, stream).await
}

/// Get the volume of the given stream from the given audio service proxy.
///
/// Panics for an unexpected result, such as the stream or volume level not being present.
async fn get_stream_volume_from_proxy(
    setui_audio_service: &AudioProxy,
    stream: AudioStreamType,
) -> f32 {
    let audio_settings = setui_audio_service.watch().await.expect("failed to watch audio settings");

    let stream = audio_settings
        .streams
        .expect("no streams in audio settings")
        .iter()
        .find(|stream_settings| stream_settings.stream == Some(stream.into()))
        .expect("failed to find audio stream")
        .clone();

    stream.user_volume.as_ref().expect("no user volume").level.expect("no volume level")
}

async fn add_policy(env: &TestEnvironment, target: PropertyTarget, transform: Transform) -> u32 {
    env.policy_service
        .add_policy(&mut policy_fidl::Target::Stream(target.into()), &mut transform.into())
        .await
        .expect("failed to add policy")
        .expect("no policy ID found")
}

async fn remove_policy(env: &TestEnvironment, policy_id: u32) {
    env.policy_service
        .remove_policy(policy_id)
        .await
        .expect("failed to remove policy")
        .expect("removal error");
}

// A simple validation test to ensure the policy message hub propagates messages
// properly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_message_hub() {
    let messenger_factory = internal::policy::message::create_hub();
    let policy_handler_address = internal::policy::Address::Policy(PolicyType::Audio);

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

    let request_payload = internal::policy::Payload::Request(Request::Get);

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
        internal::policy::Payload::Response(Ok(Payload::PolicyInfo(PolicyInfo::Audio(state))));
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

    let env = create_test_environment().await;

    // Attempt to read properties.
    let properties = env.policy_service.get_properties().await.expect("failed to get properties");

    // Verify that each expected stream type is contained in the property targets.
    assert_eq!(properties.len(), expected_stream_types.len());
    for property in properties {
        // Allow unreachable patterns so this test will still build if more Target enums are added.
        #[allow(unreachable_patterns)]
        match property.target.expect("no target found for property") {
            policy_fidl::Target::Stream(stream) => {
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
    let policy_transform = Transform::Disable;

    let env = create_test_environment().await;

    // Add a policy property and save the returned policy ID.
    let added_policy_id = add_policy(&env, expected_policy_target, policy_transform).await;

    // Read the policies.
    let properties = env.policy_service.get_properties().await.expect("failed to get properties");

    // Verify that the expected target has the given policy and that others are unchanged.
    for property in properties {
        // Allow unreachable patterns so this test will still build if more Target enums are added.
        #[allow(unreachable_patterns)]
        match property.target.expect("no target found for property") {
            policy_fidl::Target::Stream(stream) => {
                if stream == expected_policy_target.into() {
                    // Chosen property should have a policy added.
                    let added_policies = property.active_policies.unwrap();
                    assert_eq!(added_policies.len(), 1);
                    let added_policy = added_policies.first().unwrap();
                    assert_eq!(added_policy.policy_id.unwrap(), added_policy_id);
                    assert_eq!(*added_policy.parameters.as_ref().unwrap(), policy_transform.into());
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

    let env = create_test_environment().await;

    // Add a policy property and save the returned policy ID.
    let added_policy_id = add_policy(&env, expected_policy_target, Transform::Disable).await;

    // Remove the same policy using the returned ID.
    remove_policy(&env, added_policy_id).await;

    // Fetch the properties.
    let properties = env.policy_service.get_properties().await.expect("failed to get properties");

    // Verify that the expected target has the given policy and that others are unchanged.
    for property in properties {
        // No policies are active.
        assert!(property.active_policies.unwrap().is_empty());
    }
}

// Tests that adding a new min policy transform affects the audio output from the audio setting.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_add_min_policy_adjusts_volume() {
    let expected_policy_target = AudioStreamType::Media;
    let expected_user_volume: f32 = 0.8;

    let env = create_test_environment().await;

    // Set media volume level to 0 to start with.
    set_stream_volume(&env, expected_policy_target, 0.0).await;

    // Add a min policy transform.
    add_policy(&env, expected_policy_target, Transform::Min(expected_user_volume)).await;

    assert_eq!(expected_user_volume, get_stream_volume(&env, expected_policy_target).await);
}

// Tests that adding a new max policy transform does not initially affect the audio output from the
// audio setting when at max, but does take effect after removal.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_add_max_policy_adjusts_volume_after_removal() {
    let expected_policy_target = AudioStreamType::Media;
    let initial_volume: f32 = 1.0;
    let expected_user_volume: f32 = 0.8;

    let env = create_test_environment().await;

    // Set media volume level to max to start with.
    set_stream_volume(&env, expected_policy_target, initial_volume).await;

    // Add a max policy transform.
    let added_policy_id =
        add_policy(&env, expected_policy_target, Transform::Max(expected_user_volume)).await;

    // Volume should stay at max from the point of view of the client.
    assert_eq!(initial_volume, get_stream_volume(&env, expected_policy_target).await);

    // Remove the added policy.
    remove_policy(&env, added_policy_id).await;

    // After the policy is removed, the volume now reflects its clamped value.
    assert_eq!(expected_user_volume, get_stream_volume(&env, expected_policy_target).await);
}

// Tests that incoming sets are clamped when a min policy is present.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_min_policy_clamps_sets() {
    let expected_policy_target = AudioStreamType::Media;
    let expected_user_volume: f32 = 0.2;

    let env = create_test_environment().await;

    // Set media volume level to 50% to start with.
    set_stream_volume(&env, expected_policy_target, 0.5).await;

    // Add a min policy transform.
    let policy_id =
        add_policy(&env, expected_policy_target, Transform::Min(expected_user_volume)).await;

    // Attempt to set the volume to 0.
    set_stream_volume(&env, expected_policy_target, 0.0).await;

    // The volume remains at 20%, the minimum set by policy.
    assert_eq!(expected_user_volume, get_stream_volume(&env, expected_policy_target).await);

    // Remove the min volume policy.
    remove_policy(&env, policy_id).await;

    let audio_connection = env.nested_environment.connect_to_service::<AudioMarker>().unwrap();

    // The volume remains at 20% after the policy is removed.
    assert_eq!(
        expected_user_volume,
        get_stream_volume_from_proxy(&audio_connection, expected_policy_target).await
    );
}

// Tests that incoming sets are scaled when a max policy is present.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_max_policy_scales_sets() {
    let expected_policy_target = AudioStreamType::Media;
    let initial_volume: f32 = 0.4;
    let max_volume_limit: f32 = 0.8;
    let max_volume: f32 = 1.0;

    let env = create_test_environment().await;

    // Set media volume level to 40% to start with.
    set_stream_volume(&env, expected_policy_target, initial_volume).await;

    // Add a max policy transform.
    let policy_id =
        add_policy(&env, expected_policy_target, Transform::Max(max_volume_limit)).await;

    // The volume is 50% when retrieved, since the initial 40% is half of the max of 80%.
    assert_eq!(
        initial_volume / max_volume_limit,
        get_stream_volume(&env, expected_policy_target).await
    );

    // Attempt to set the volume to max.
    set_stream_volume(&env, expected_policy_target, max_volume).await;

    // The volume is 100% when retrieved since the max policy limit is transparent to clients.
    assert_eq!(max_volume, get_stream_volume(&env, expected_policy_target).await);

    // Remove the max volume policy.
    remove_policy(&env, policy_id).await;

    // The volume is 80% when retrieved after the policy is removed, since the internal volume was
    // 80% due to the policy.
    assert_eq!(max_volume_limit, get_stream_volume(&env, expected_policy_target).await);
}
