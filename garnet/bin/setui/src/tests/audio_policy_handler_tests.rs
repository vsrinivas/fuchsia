// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::default_audio_info;
use crate::audio::policy::audio_policy_handler::{AudioPolicyHandler, ARG_POLICY_ID};
use crate::audio::policy::{
    self as audio, PolicyId, PropertyTarget, Response, State, StateBuilder, Transform,
    TransformFlags,
};
use crate::audio::types::AudioStreamType;
use crate::audio::types::{AudioInfo, AudioSettingSource, AudioStream};
use crate::audio::utils::round_volume_level;
use crate::base::{SettingInfo, SettingType};
use crate::handler::base::Request as SettingRequest;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::device_storage::{
    DeviceStorage, DeviceStorageCompatible, DeviceStorageFactory,
};
use crate::internal::core;
use crate::internal::core::message::Receptor;
use crate::message::base::MessengerType;
use crate::policy::base::response::Error as PolicyError;
use crate::policy::base::{response::Payload, Request};
use crate::policy::policy_handler::{ClientProxy, Create, PolicyHandler, RequestTransform};
use crate::switchboard::base::{SettingAction, SettingActionData, SettingEvent};
use crate::tests::message_utils::verify_payload;
use fuchsia_async::Task;
use futures::lock::Mutex;
use matches::assert_matches;
use std::borrow::BorrowMut;
use std::sync::Arc;

const CONTEXT_ID: u64 = 0;

struct TestEnvironment {
    /// Device storage handle.
    store: Arc<Mutex<DeviceStorage<State>>>,

    /// Receptor representing the setting proxy.
    setting_proxy_receptor: Arc<Mutex<Receptor>>,

    /// Receptor representing the switchboard.
    switchboard_receptor: Arc<Mutex<Receptor>>,

    /// A newly created AudioPolicyHandler.
    handler: AudioPolicyHandler,
}

/// Creates a state containing all of the default audio streams.
fn get_default_state() -> State {
    let audio_info = default_audio_info();
    let mut state_builder = StateBuilder::new();
    for stream in audio_info.streams.iter() {
        state_builder = state_builder.add_property(stream.stream_type, TransformFlags::all());
    }
    state_builder.build()
}

/// Verifies that a given state contains only the expected transform in the given target.
///
/// Since property IDs are globally unique and increasing, this is necessary since otherwise, two
/// state objects with transforms that were created separately cannot be equal.
fn verify_state(state: &State, target: PropertyTarget, transform: Transform) {
    for property in state.get_properties() {
        if property.target == target {
            // One property was added.
            assert_eq!(property.active_policies.len(), 1);
            // The expected transform was added.
            assert_eq!(
                property.active_policies.first().expect("should have policies").transform,
                transform
            );
        } else {
            // Other properties have no policies.
            assert_eq!(property.active_policies.len(), 0);
        }
    }
}

async fn create_handler_test_environment() -> TestEnvironment {
    let core_messenger_factory = core::message::create_hub();
    let (core_messenger, _) = core_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("core messenger created");
    let (_, setting_proxy_receptor) = core_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("setting proxy messenger created");
    let (_, switchboard_receptor) = core_messenger_factory
        .create(MessengerType::Addressable(core::Address::Switchboard))
        .await
        .expect("switchboard messenger created");
    let storage_factory = InMemoryStorageFactory::create();
    let store = storage_factory.lock().await.get_store::<State>(CONTEXT_ID);
    let client_proxy = ClientProxy::new(
        core_messenger,
        setting_proxy_receptor.get_signature(),
        store.clone(),
        SettingType::Audio,
    );

    let handler =
        AudioPolicyHandler::create(client_proxy.clone()).await.expect("failed to create handler");

    TestEnvironment {
        store,
        setting_proxy_receptor: Arc::new(Mutex::new(setting_proxy_receptor)),
        switchboard_receptor: Arc::new(Mutex::new(switchboard_receptor)),
        handler,
    }
}

/// Starts a task that continuously watches for get requests on the given receptor and replies with
/// the given audio info.
fn serve_audio_info(setting_proxy_receptor: Arc<Mutex<Receptor>>, audio_info: AudioInfo) {
    Task::spawn(async move {
        match setting_proxy_receptor.lock().await.next_payload().await {
            Ok((
                core::Payload::Action(SettingAction {
                    id: request_id,
                    setting_type: SettingType::Audio,
                    data: SettingActionData::Request(SettingRequest::Get),
                }),
                message_client,
            )) => {
                message_client
                    .reply(core::Payload::Event(SettingEvent::Response(
                        request_id,
                        Ok(Some(SettingInfo::Audio(audio_info))),
                    )))
                    .send();
            }
            _ => {}
        }
    })
    .detach();
}

/// Adds a media volume limit to the handler in the test environment. Automatically handles a Get
/// request for the current audio info, which happens whenever policies are modified.
async fn set_media_volume_limit(
    env: &mut TestEnvironment,
    transform: Transform,
    audio_info: AudioInfo,
) -> PolicyId {
    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    serve_audio_info(env.setting_proxy_receptor.clone(), audio_info);

    // Add the policy to the handler.
    let policy_response = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::AddPolicy(
            AudioStreamType::Media,
            transform,
        )))
        .await
        .expect("add policy succeeds");

    if let Payload::Audio(Response::Policy(policy_id)) = policy_response {
        policy_id
    } else {
        panic!("Policy ID not returned from set");
    }
}

/// Verifies that when the setting proxy returns the given internal volume level, that the setting
/// handler modifies the volume level and returns a result with the given expected volume level.
async fn get_and_verify_media_volume(
    env: &mut TestEnvironment,
    internal_volume_level: f32,
    expected_volume_level: f32,
) {
    // Start task to provide audio info to the handler, which it requests when a get request is
    // received.
    let mut audio_info = default_audio_info();
    audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: internal_volume_level,
        user_volume_muted: false,
    });
    serve_audio_info(env.setting_proxy_receptor.clone(), audio_info.clone());

    // Send the get request to the handler.
    let request_transform = env.handler.handle_setting_request(SettingRequest::Get).await;

    // Modify the audio info to contain the expected volume level.
    audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: expected_volume_level,
        user_volume_muted: false,
    });
    assert_eq!(
        request_transform,
        Some(RequestTransform::Result(Ok(Some(SettingInfo::Audio(audio_info)))))
    );
}

/// Asks the handler in the environment to handle a set request and verifies that the transformed
/// request matches the given volume level.
async fn set_and_verify_media_volume(
    env: &mut TestEnvironment,
    volume_level: f32,
    expected_volume_level: f32,
) {
    let mut stream = AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: volume_level,
        user_volume_muted: false,
    };

    let request_transform =
        env.handler.handle_setting_request(SettingRequest::SetVolume(vec![stream.clone()])).await;

    stream.user_volume_level = expected_volume_level;
    assert_eq!(
        request_transform,
        Some(RequestTransform::Request(SettingRequest::SetVolume(vec![stream])))
    );
}

/// Verifies that the setting proxy in the test environment received a set request for media volume.
async fn verify_media_volume_set(env: &mut TestEnvironment, volume_level: f32) {
    verify_payload(
        core::Payload::Action(SettingAction {
            id: 0,
            setting_type: SettingType::Audio,
            data: SettingActionData::Request(SettingRequest::SetVolume(vec![AudioStream {
                stream_type: AudioStreamType::Media,
                source: AudioSettingSource::User,
                user_volume_level: volume_level,
                user_volume_muted: false,
            }])),
        }),
        env.setting_proxy_receptor.lock().await.borrow_mut(),
        None,
    )
    .await;
}

/// Verifies that the switchboard in the test environment received a changed notification with the
/// given audio info.
async fn verify_media_volume_changed(env: &mut TestEnvironment, audio_info: AudioInfo) {
    verify_payload(
        core::Payload::Event(SettingEvent::Changed(SettingInfo::Audio(audio_info))),
        env.switchboard_receptor.lock().await.borrow_mut(),
        None,
    )
    .await;
}

/// Verifies that the audio policy handler restores to a default state with all stream types when no
/// persisted value is found.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_no_persisted_state() {
    let expected_value = get_default_state();

    let mut env = create_handler_test_environment().await;

    // Request the policy state from the handler.
    let payload = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::Get))
        .await
        .expect("get failed");

    // The state response matches the expected value.
    assert_eq!(payload, Payload::Audio(Response::State(expected_value)));

    // Verify that nothing was written to storage.
    assert_eq!(env.store.lock().await.get().await, State::default_value());
}

/// Verifies that the audio policy handler reads the persisted state and restores it.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_restore_persisted_state() {
    let modified_property = AudioStreamType::Media;
    let expected_transform = Transform::Max(1.0f32);

    // Persisted state with only one stream and transform.
    let mut persisted_state =
        StateBuilder::new().add_property(AudioStreamType::Media, TransformFlags::all()).build();
    persisted_state
        .properties()
        .get_mut(&AudioStreamType::Media)
        .expect("failed to get property")
        .add_transform(expected_transform);

    let core_messenger_factory = core::message::create_hub();
    let (core_messenger, _) = core_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("core messenger created");
    let (_, setting_proxy_receptor) = core_messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("setting proxy messenger created");
    let storage_factory = InMemoryStorageFactory::create();
    let store = storage_factory.lock().await.get_store::<State>(CONTEXT_ID);
    let client_proxy = ClientProxy::new(
        core_messenger,
        setting_proxy_receptor.get_signature(),
        store.clone(),
        SettingType::Audio,
    );

    // Write the "persisted" value to storage for the handler to read on start.
    store.lock().await.write(&persisted_state, false).await.expect("write failed");

    // Start task to provide audio info to the handler, which it requests at startup.
    serve_audio_info(Arc::new(Mutex::new(setting_proxy_receptor)), default_audio_info());

    let mut handler =
        AudioPolicyHandler::create(client_proxy.clone()).await.expect("failed to create handler");

    // Request the policy state from the handler.
    let payload = handler
        .handle_policy_request(Request::Audio(audio::Request::Get))
        .await
        .expect("get failed");

    // The state response matches the expected value.
    if let Payload::Audio(Response::State(mut state)) = payload {
        // The persisted transform was found in the returned state.
        verify_state(&state, modified_property, expected_transform);

        // The returned state has the full list of properties from configuration, not just the
        // single persisted property.
        assert_eq!(state.properties().len(), get_default_state().properties().len());
    } else {
        panic!("unexpected response")
    }
}

/// Tests adding and reading policies.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_policy() {
    let expected_transform = Transform::Mute(false);
    let modified_property = AudioStreamType::Media;

    let mut env = create_handler_test_environment().await;

    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    serve_audio_info(env.setting_proxy_receptor.clone(), default_audio_info());

    // Add a policy transform.
    let payload = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::AddPolicy(
            modified_property,
            expected_transform,
        )))
        .await
        .expect("get failed");

    // Handler returns a response containing a policy ID.
    assert_matches!(payload, Payload::Audio(Response::Policy(_)));

    // Verify that the expected transform was written to storage.
    let stored_value = env.store.lock().await.get().await;
    verify_state(&stored_value, modified_property, expected_transform);

    // Request the policy state from the handler and verify that it matches the stored value.
    let payload = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::Get))
        .await
        .expect("get failed");
    assert_eq!(payload, Payload::Audio(Response::State(stored_value)));
}

/// Tests that attempting to removing an unknown policy returns an appropriate error.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_remove_unknown_policy() {
    let mut env = create_handler_test_environment().await;
    let invalid_policy_id = PolicyId::create(42);

    // Attempt to remove a policy, even though none have been added.
    let response = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::RemovePolicy(invalid_policy_id)))
        .await;

    // The response is an appropriate error.
    assert_eq!(
        response,
        Err(PolicyError::InvalidArgument(
            SettingType::Audio,
            ARG_POLICY_ID.into(),
            format!("{:?}", invalid_policy_id).into()
        ))
    );
}

/// Tests removing added policies.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_remove_policy() {
    let expected_value = get_default_state();

    let mut env = create_handler_test_environment().await;

    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    serve_audio_info(env.setting_proxy_receptor.clone(), default_audio_info());

    // Add a policy transform.
    let payload = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::AddPolicy(
            AudioStreamType::Media,
            Transform::Mute(false),
        )))
        .await
        .expect("get failed");
    let policy_id = match payload {
        Payload::Audio(Response::Policy(policy_id)) => policy_id,
        _ => panic!("unexpected response"),
    };

    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    serve_audio_info(env.setting_proxy_receptor.clone(), default_audio_info());

    // Remove the added transform
    let payload = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::RemovePolicy(policy_id)))
        .await
        .expect("get failed");

    // The returned ID should match the original ID.
    assert_eq!(payload, Payload::Audio(Response::Policy(policy_id)));

    // Request the policy state from the handler.
    let payload = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::Get))
        .await
        .expect("get failed");

    // The state response matches the expected value.
    assert_eq!(payload, Payload::Audio(Response::State(expected_value.clone())));

    // Verify that the expected value is persisted to storage.
    assert_eq!(env.store.lock().await.get().await, expected_value);
}

/// Verifies that adding a max volume policy adjusts the internal volume level if it's above the
/// new max.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_policy_modifies_internal_volume_above_max() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at max volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 1.0,
        user_volume_muted: false,
    });

    // Set the max volume limit to 60%.
    let max_volume = 0.6;
    set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info).await;

    // Handler requests that the setting handler set the volume to 60% (the max set by policy).
    verify_media_volume_set(&mut env, max_volume).await;
}

/// Verifies that adding a min volume policy adjusts the internal volume level if it's below the
/// new min.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_policy_modifies_internal_volume_below_min() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 0%.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 0.0,
        user_volume_muted: false,
    });

    // Set the min volume limit to 40%.
    let min_volume = 0.4;
    set_media_volume_limit(&mut env, Transform::Min(min_volume), starting_audio_info).await;

    // Handler requests that the setting handler set the volume to 40% (the min set by policy).
    verify_media_volume_set(&mut env, min_volume).await;
}

/// Verifies that the internal volume will not be adjusted when it's already within policy limits.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_policy_does_not_modify_internal_volume_within_limits() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50%.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 0.5,
        user_volume_muted: false,
    });

    // Set the min volume limit to 40%.
    set_media_volume_limit(&mut env, Transform::Min(0.4), starting_audio_info.clone()).await;

    // Set the max volume limit to 60%.
    set_media_volume_limit(&mut env, Transform::Max(0.6), starting_audio_info.clone()).await;

    // So far, no set requests should have been sent to the setting proxy, set the min volume limit
    // to 55% to ensure that the first set request the setting proxy receives is this one.
    set_media_volume_limit(&mut env, Transform::Min(0.55), starting_audio_info).await;

    // Handler requests that the setting handler set the volume to 55% (the min set by policy).
    verify_media_volume_set(&mut env, 0.55).await;
}

/// Verifies that when a max volume policy is removed, the switchboard will receive a changed event
/// with the new external volume.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_remove_policy_notifies_switchboard() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    let max_volume = 0.6;

    // Starting audio info has media volume at 60% volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: max_volume,
        user_volume_muted: false,
    });

    // Set the max volume limit to 60%.
    let policy_id =
        set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info.clone())
            .await;

    // Since the max matches the starting volume, the new external volume should be 100%.
    let mut changed_audio_info = starting_audio_info.clone();
    changed_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 1.0,
        user_volume_muted: false,
    });

    // The internal volume didn't change but the external volume did, so the handler manually sends
    // a changed even to the switchboard.
    verify_media_volume_changed(&mut env, changed_audio_info.clone()).await;

    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    serve_audio_info(env.setting_proxy_receptor.clone(), starting_audio_info.clone());

    // Remove the policy from the handler.
    env.handler
        .handle_policy_request(Request::Audio(audio::Request::RemovePolicy(policy_id)))
        .await
        .expect("remove policy succeeds");

    // The internal volume didn't change but the external volume should be back at the original
    // values.
    verify_media_volume_changed(&mut env, starting_audio_info).await;
}

/// Verifies that when multiple max volume policies are in place, that the most strict one applies.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_lowest_max_limit_applies_internally() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at max volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 1.0,
        user_volume_muted: false,
    });

    // Add a policy transform to limit the max media volume to 60%.
    let max_volume = 0.6;
    set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info.clone()).await;

    // Handler requests that the setting handler set the volume to 60% (the max set by policy).
    verify_media_volume_set(&mut env, max_volume).await;

    // Internal audio level should now be at 60%.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: max_volume,
        user_volume_muted: false,
    });

    // Add a higher limit, which won't result in a new set request.
    set_media_volume_limit(&mut env, Transform::Max(0.7), starting_audio_info.clone()).await;

    // Add a lower limit, which will cause a new set request.
    let lower_max_volume = 0.5;
    set_media_volume_limit(&mut env, Transform::Max(lower_max_volume), starting_audio_info.clone())
        .await;

    // Handler requests that the setting handler set the volume to 50% (the lowest max).
    verify_media_volume_set(&mut env, lower_max_volume).await;
}

/// Verifies that when multiple min volume policies are in place, that the most strict one applies.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_highest_min_limit_applies_internally() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 0% volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 0.0,
        user_volume_muted: false,
    });

    // Add a policy transform to limit the min media volume to 30%.
    let min_volume = 0.3;
    set_media_volume_limit(&mut env, Transform::Min(min_volume), starting_audio_info.clone()).await;

    // Handler requests that the setting handler set the volume to 30% (the max set by policy).
    verify_media_volume_set(&mut env, min_volume).await;

    // Internal audio level should now be at 30%.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: min_volume,
        user_volume_muted: false,
    });

    // Add a lower limit, which won't result in a new set request.
    set_media_volume_limit(&mut env, Transform::Min(0.2), starting_audio_info.clone()).await;

    // Add a higher limit, which will cause a new set request.
    let higher_min_volume = 0.4;
    set_media_volume_limit(
        &mut env,
        Transform::Min(higher_min_volume),
        starting_audio_info.clone(),
    )
    .await;

    // Handler requests that the setting handler set the volume to 40% (the highest min).
    verify_media_volume_set(&mut env, higher_min_volume).await;
}

/// Verifies that when no policies are in place that external set requests are not modified.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_no_policy_external_sets_not_modified() {
    let mut env = create_handler_test_environment().await;

    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let external_volume_level = i as f32 / 10.0;

        // Set requests have the same volume level before and after passing through the policy
        // handler.
        set_and_verify_media_volume(&mut env, external_volume_level, external_volume_level).await;
    }
}

/// Verifies that adding a max volume policy scales external set requests to an appropriate internal
/// volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_max_volume_policy_scales_external_sets() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at max volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 1.0,
        user_volume_muted: false,
    });

    // Set the max volume limit to 60%.
    let max_volume = 0.6;
    set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info).await;

    // Test set requests with input volumes from 0.0 to 1.0.
    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let external_volume_level = i as f32 / 10.0;

        // The volume in the set request will be a proportional percentage of the max volume. For
        // example, 25% in the original set request would result in .60 * .25 = 15% internal volume.
        let expected_volume_level = external_volume_level * max_volume;

        // Set requests have the expected volume level after passing through the policy handler.
        set_and_verify_media_volume(&mut env, external_volume_level, expected_volume_level).await;
    }
}

/// Verifies that adding a min volume policy scales external set requests to an appropriate internal
/// volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_volume_policy_scales_external_sets() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 0% volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 0.0,
        user_volume_muted: false,
    });

    // Set the min volume limit to 60%.
    let min_volume = 0.2;
    set_media_volume_limit(&mut env, Transform::Min(min_volume), starting_audio_info).await;

    // Test set requests with input volumes from 0.0 to 1.0.
    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let external_volume_level = i as f32 / 10.0;

        // With only a min volume in place, the transformed internal volume will be the same as the
        // external volume in the set request unless it's below the min volume.
        let expected_volume_level = external_volume_level.max(min_volume);

        // Set requests have the expected volume level after passing through the policy handler.
        set_and_verify_media_volume(&mut env, external_volume_level, expected_volume_level).await;
    }
}

/// Verifies that adding both a max and a min volume policy scales external set requests to an
/// appropriate internal volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_and_max_volume_policy_scales_external_volume() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50% volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 0.5,
        user_volume_muted: false,
    });

    // Set the max volume limit to 80%.
    let max_volume = 0.8;
    set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info.clone()).await;

    // Set the min volume limit to 20%.
    let min_volume = 0.2;
    set_media_volume_limit(&mut env, Transform::Min(min_volume), starting_audio_info).await;

    // Test set requests with input volumes from 0.0 to 1.0.
    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let external_volume_level = i as f32 / 10.0;

        // The volume in the set request will be a proportional percentage of the max volume, unless
        // the result is below the min volume, in which case it is kept at the min volume. For
        // example, 10% in the original set request would result in .80 * .10 = 8% internal volume.
        // However, this is below the min of 20% so the resulting volume level is still 20%.
        let expected_volume_level = (external_volume_level * max_volume).max(min_volume);

        // Set requests have the expected volume level after passing through the policy handler.
        set_and_verify_media_volume(&mut env, external_volume_level, expected_volume_level).await;
    }
}

/// Verifies that when no policies are in place that external get requests return the same
/// level as the internal volume levels.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_no_policy_gets_not_modified() {
    let mut env = create_handler_test_environment().await;

    // Test get requests with internal volumes from 0.0 to 1.0.
    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let internal_volume_level = i as f32 / 10.0;

        // Get requests have the same volume level before and after passing through the policy
        // handler.
        get_and_verify_media_volume(&mut env, internal_volume_level, internal_volume_level).await;
    }
}

/// Verifies that a max volume policy scales the result of get requests to the correct external
/// volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_max_volume_policy_scales_gets() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50% volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 0.5,
        user_volume_muted: false,
    });

    // Set the max volume limit to 60%.
    let max_volume = 0.6;
    set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info).await;

    // Test get requests with internal volumes from 0.0 to 1.0.
    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let internal_volume_level = i as f32 / 10.0;

        // Since the internal volume is already scaled by the max volume limit, the transformed
        // output of the get request should undo this scaling. For example, 30% internal volume
        // should result in 0.30 / 0.60 (max volume limit) = 50% external volume.
        let expected_volume_level = round_volume_level(internal_volume_level / max_volume);

        // Get requests have the expected volume level after passing through the policy handler.
        get_and_verify_media_volume(&mut env, internal_volume_level, expected_volume_level).await;
    }
}

/// Verifies that a min volume policy alone won't result in any scaling of the external volume
/// levels.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_volume_policy_scales_gets() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50% volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 0.5,
        user_volume_muted: false,
    });

    // Set the max volume limit to 60%.
    set_media_volume_limit(&mut env, Transform::Min(0.2), starting_audio_info).await;

    for i in 2..=10 {
        // 0.2 to 1 inclusive, with steps of 0.1. Starting at 20% since the min limit clamps the
        // internal volume at 20%.
        let internal_volume_level = i as f32 / 10.0;

        // When only a min limit is present, the output volume does not need to be scaled.
        get_and_verify_media_volume(&mut env, internal_volume_level, internal_volume_level).await;
    }
}

/// Verifies that the external volume returned by a get request is properly scaled when both a min
/// and a max volume policy are in place.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_and_max_volume_policy_scales_gets() {
    let mut env = create_handler_test_environment().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50% volume.
    starting_audio_info.replace_stream(AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: 0.5,
        user_volume_muted: false,
    });

    // Set the max volume limit to 80%.
    let max_volume = 0.8;
    set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info.clone()).await;

    // Set the min volume limit to 20%.
    // Since internal volumes shouldn't be below the min volume anyways, the min volume level has no
    // effect here.
    set_media_volume_limit(&mut env, Transform::Min(0.2), starting_audio_info).await;

    // Test get requests with internal volumes from 0.0 to 1.0.
    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let internal_volume_level = i as f32 / 10.0;

        // Since the internal volume is already scaled by the max volume limit, the transformed
        // output of the get request should undo this scaling. For example, 30% internal volume
        // should result in 0.30 / 0.60 (max volume limit) = 50% external volume.
        let expected_volume_level = round_volume_level(internal_volume_level / max_volume);

        // Get requests have the expected volume level after passing through the policy handler.
        get_and_verify_media_volume(&mut env, internal_volume_level, expected_volume_level).await;
    }
}
