// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::default_audio_info;
use crate::audio::policy::audio_policy_handler::{AudioPolicyHandler, ARG_POLICY_ID};
use crate::audio::policy::{
    self as audio, PolicyId, Property, PropertyTarget, Request as AudioRequest,
    Response as AudioResponse, Response, State, StateBuilder, Transform, TransformFlags,
};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::device_storage::{
    DeviceStorage, DeviceStorageCompatible, DeviceStorageFactory,
};
use crate::internal;
use crate::internal::core;
use crate::message::base::{Audience, MessengerType};
use crate::policy::base::response::Error as PolicyError;
use crate::policy::base::{response::Payload, Request};
use crate::policy::policy_handler::{ClientProxy, Create, PolicyHandler};
use crate::switchboard::base::{AudioStreamType, SettingType};
use futures::lock::Mutex;
use matches::assert_matches;
use std::collections::{HashMap, HashSet};
use std::iter::FromIterator;
use std::sync::Arc;

const CONTEXT_ID: u64 = 0;

struct TestEnvironment {
    store: Arc<Mutex<DeviceStorage<State>>>,
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
            assert_eq!(property.active_policies.first().unwrap().transform, transform);
        } else {
            // Other properties have no policies.
            assert_eq!(property.active_policies.len(), 0);
        }
    }
}

async fn create_audio_policy_test_environment() -> TestEnvironment {
    let core_messenger_factory = core::message::create_hub();
    let (core_messenger, _) = core_messenger_factory.create(MessengerType::Unbound).await.unwrap();
    let storage_factory = InMemoryStorageFactory::create();
    let store = storage_factory.lock().await.get_store::<State>(CONTEXT_ID);
    let client_proxy = ClientProxy::new(core_messenger, store.clone(), SettingType::Audio);

    let handler =
        AudioPolicyHandler::create(client_proxy.clone()).await.expect("failed to create handler");

    TestEnvironment { store, handler }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_state_builder() {
    let properties: HashMap<AudioStreamType, TransformFlags> = [
        (AudioStreamType::Background, TransformFlags::TRANSFORM_MAX),
        (AudioStreamType::Media, TransformFlags::TRANSFORM_MIN),
    ]
    .iter()
    .cloned()
    .collect();
    let mut builder = StateBuilder::new();

    for (property, value) in &properties {
        builder = builder.add_property(property.clone(), value.clone());
    }

    let state = builder.build();
    let retrieved_properties = state.get_properties();
    assert_eq!(retrieved_properties.len(), properties.len());

    let mut seen_targets = HashSet::<PropertyTarget>::new();
    for property in retrieved_properties.iter().cloned() {
        let target = property.target;
        // make sure only unique targets are encountered
        assert!(!seen_targets.contains(&target));
        seen_targets.insert(target);
        // ensure the specified transforms are present
        assert_eq!(
            property.available_transforms,
            *properties.get(&property.stream_type).expect("should be here")
        );
    }
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_property_transforms() {
    let supported_transforms = TransformFlags::TRANSFORM_MAX | TransformFlags::TRANSFORM_MIN;
    let transforms = [Transform::Min(0.1), Transform::Max(0.9)];
    let mut property = Property::new(AudioStreamType::Media, supported_transforms);
    let mut property2 = Property::new(AudioStreamType::Background, supported_transforms);

    for transform in transforms.iter().cloned() {
        property.add_transform(transform);
        property2.add_transform(transform);
    }

    // Ensure policy size matches transforms specified
    assert_eq!(property.active_policies.len(), transforms.len());
    assert_eq!(property2.active_policies.len(), transforms.len());

    let mut retrieved_ids: HashSet<PolicyId> =
        HashSet::from_iter(property.active_policies.iter().map(|policy| policy.id));
    retrieved_ids.extend(property2.active_policies.iter().map(|policy| policy.id));

    // Make sure all ids are unique, even across properties.
    assert_eq!(retrieved_ids.len(), transforms.len() * 2);

    // Verify transforms are present.
    let retrieved_transforms: Vec<Transform> =
        property.active_policies.iter().map(|policy| policy.transform).collect();
    let retrieved_transforms2: Vec<Transform> =
        property2.active_policies.iter().map(|policy| policy.transform).collect();
    for transform in transforms.iter() {
        assert!(retrieved_transforms.contains(&transform));
        assert!(retrieved_transforms2.contains(&transform));
    }
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

/// Verifies that the audio policy handler restores to a default state with all stream types when no
/// persisted value is found.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_no_persisted_state() {
    let expected_value = get_default_state();

    let mut env = create_audio_policy_test_environment().await;

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
    let (core_messenger, _) = core_messenger_factory.create(MessengerType::Unbound).await.unwrap();
    let storage_factory = InMemoryStorageFactory::create();
    let store = storage_factory.lock().await.get_store::<State>(CONTEXT_ID);
    let client_proxy = ClientProxy::new(core_messenger, store.clone(), SettingType::Audio);

    // Write the "persisted" value to storage for the handler to read on start.
    store.lock().await.write(&persisted_state, false).await.expect("write failed");

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

    let mut env = create_audio_policy_test_environment().await;

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
    let mut env = create_audio_policy_test_environment().await;
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

    let mut env = create_audio_policy_test_environment().await;

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
