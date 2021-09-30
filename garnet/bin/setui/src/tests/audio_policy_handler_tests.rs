// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::authority::Authority;
use crate::agent::Lifespan;
use crate::audio::default_audio_info;
use crate::audio::policy::audio_policy_handler::{AudioPolicyHandler, ARG_POLICY_ID};
use crate::audio::policy::{
    self as audio, AudioPolicyConfig, PolicyId, PropertyTarget, Response, State, StateBuilder,
    Transform, TransformFlags,
};
use crate::audio::types::AudioStreamType;
use crate::audio::types::{AudioInfo, AudioSettingSource, AudioStream, SetAudioStream};
use crate::audio::utils::round_volume_level;
use crate::base::SettingType;
use crate::handler::base::{Payload as SettingPayload, Request as SettingRequest};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::device_storage::{
    DeviceStorage, DeviceStorageCompatible, DeviceStorageFactory,
};
use crate::message::base::{filter, Audience, MessageEvent, MessengerType, Status};
use crate::message::{MessageHubDefinition, MessageHubUtil};
use crate::policy::policy_handler::{ClientProxy, Create, PolicyHandler, RequestTransform};
use crate::policy::response::{Error as PolicyError, Payload};
use crate::policy::{PolicyInfo, PolicyType, Request};
use crate::service;
use crate::service::TryFromWithClient;
use crate::service_context::ServiceContext;
use crate::tests::message_utils::verify_payload;
use fuchsia_async::Task;
use futures::StreamExt;
use matches::assert_matches;
use std::collections::HashSet;
use std::sync::Arc;

// The types of data that can be sent.
#[cfg_attr(test, derive(PartialEq))]
#[derive(Clone, Debug)]
pub(crate) enum TestEnvironmentPayload {
    Request(SettingRequest),
    Serve(AudioInfo),
}

struct MessageHub;
impl MessageHubDefinition for MessageHub {
    type Payload = TestEnvironmentPayload;
    type Address = crate::message::base::default::Address;
    type Role = crate::message::base::default::Role;
}

struct TestEnvironment {
    /// Device storage handle.
    store: Arc<DeviceStorage>,

    /// A newly created AudioPolicyHandler.
    handler: AudioPolicyHandler,

    /// Factory for internal message hub.
    internal_delegate: <MessageHub as MessageHubUtil>::Delegate,

    setting_handler_signature: <MessageHub as MessageHubUtil>::Signature,

    delegate: service::message::Delegate,
}

impl TestEnvironment {
    async fn create_storage_factory() -> Arc<InMemoryStorageFactory> {
        let storage_factory = InMemoryStorageFactory::new();
        // Initialize storage since there's no EnvironmentBuilder to manage that here.
        storage_factory.initialize_storage::<State>().await;
        Arc::new(storage_factory)
    }

    async fn new() -> Self {
        TestEnvironment::new_with_store(TestEnvironment::create_storage_factory().await, None).await
    }

    // TODO(fxbug.dev/81232): take in options with a builder
    async fn new_with_store(
        storage_factory: Arc<InMemoryStorageFactory>,
        audio_policy_config: Option<AudioPolicyConfig>,
    ) -> Self {
        let store = storage_factory.get_store().await;
        let delegate = service::MessageHub::create_hub();
        let (messenger, _) =
            delegate.create(MessengerType::Unbound).await.expect("core messenger created");

        let test_delegate = MessageHub::create_hub();

        let handler_signature =
            TestEnvironment::spawn_setting_handler(&delegate, &test_delegate).await;

        let client_proxy = ClientProxy::new(messenger);

        let components: HashSet<_> = std::array::IntoIter::new([SettingType::Audio]).collect();
        let policies: HashSet<_> = std::array::IntoIter::new([PolicyType::Audio]).collect();
        let mut agent_authority = Authority::create(delegate.clone(), components, policies, None)
            .await
            .expect("failed to create agent authority");

        agent_authority
            .register(Arc::new(crate::agent::storage_agent::Blueprint::new(storage_factory)))
            .await;
        agent_authority
            .execute_lifespan(
                Lifespan::Initialization,
                Arc::new(ServiceContext::new(None, None)),
                false,
            )
            .await
            .expect("failed to initialize storage");
        agent_authority
            .execute_lifespan(Lifespan::Service, Arc::new(ServiceContext::new(None, None)), false)
            .await
            .expect("failed to start storage");

        let mut handler = match audio_policy_config {
            Some(audio_policy_config) => {
                AudioPolicyHandler::create_with_config(client_proxy.clone(), audio_policy_config)
                    .await
                    .expect("failed to create handler")
            }
            None => AudioPolicyHandler::create(client_proxy.clone())
                .await
                .expect("failed to create handler"),
        };

        let result = handler.handle_policy_request(Request::Restore).await;
        match result {
            Ok(Payload::Restore) => {} // no-op
            _ => panic!("Failed to restore policy handler: {:?}", result),
        }

        Self {
            store,
            handler,
            internal_delegate: test_delegate,
            setting_handler_signature: handler_signature,
            delegate,
        }
    }

    async fn create_volume_listener(&self) -> service::message::Receptor {
        let messenger = self
            .delegate
            .create(MessengerType::Unbound)
            .await
            .expect("messenger should be present")
            .0;

        messenger
            .message(
                SettingPayload::Request(SettingRequest::Listen).into(),
                Audience::Address(service::Address::Handler(SettingType::Audio)),
            )
            .send()
    }

    async fn create_request_observer(&self) -> <MessageHub as MessageHubUtil>::Receptor {
        self.internal_delegate
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    matches!(message.payload(), TestEnvironmentPayload::Request(_))
                })),
            ))))
            .await
            .expect("receptor should be present")
            .1
    }

    async fn spawn_setting_handler(
        service_delegate: &service::message::Delegate,
        test_delegate: &<MessageHub as MessageHubUtil>::Delegate,
    ) -> <MessageHub as MessageHubUtil>::Signature {
        let cmd_receptor = test_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("receptor for handler should be created")
            .1;

        let signature = cmd_receptor.get_signature();

        let receptor = service_delegate
            .create(MessengerType::Addressable(service::Address::Handler(SettingType::Audio)))
            .await
            .expect("should create setting messenger")
            .1;

        let receptor_fuse = receptor.fuse();
        let cmd_receptor_fuse = cmd_receptor.fuse();

        let test_messenger = test_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("receptor for handler should be created")
            .0;

        Task::spawn(async move {
            futures::pin_mut!(receptor_fuse, cmd_receptor_fuse);
            let mut listeners = Vec::new();
            let mut audio_info = default_audio_info();

            loop {
                futures::select! {
                    event = cmd_receptor_fuse.select_next_some() => {
                        if let MessageEvent::Message(TestEnvironmentPayload::Serve(info),
                                mut client) = event {
                            client.acknowledge().await;
                            audio_info = info;
                        }
                    }
                    event = receptor_fuse.select_next_some() => {
                        if let Ok((SettingPayload::Request(request), client)) =
                                SettingPayload::try_from_with_client(event) {
                            // Echo request to those listening.
                            let _ = test_messenger
                                .message(
                                    TestEnvironmentPayload::Request(request.clone()),
                                    Audience::Broadcast,
                                )
                                .send();

                            match request {
                                SettingRequest::Listen => {
                                    listeners.push(client);
                                }
                                SettingRequest::Get => {
                                    let _ = client
                                        .reply(SettingPayload::Response(Ok(Some(
                                            audio_info.clone().into()))).into())
                                        .send();
                                }
                                SettingRequest::Rebroadcast => {
                                    // Inform all the service message hub listeners.
                                    for listener in &listeners {
                                        let _ = listener
                                            .reply(SettingPayload::Response(Ok(Some(
                                                audio_info.clone().into()))).into())
                                            .send();
                                    }
                                }
                                _ => {
                                }
                            }
                        }
                    }
                }
            }
        })
        .detach();

        signature
    }

    async fn serve_audio_info(&mut self, audio_info: AudioInfo) {
        let messenger = self
            .internal_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("receptor for handler should be created")
            .0;
        let mut receptor = messenger
            .message(
                TestEnvironmentPayload::Serve(audio_info),
                Audience::Messenger(self.setting_handler_signature),
            )
            .send();

        while let Some(event) = receptor.next().await {
            if MessageEvent::Status(Status::Acknowledged) == event {
                return;
            }
        }
    }
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

/// Creates a media stream with the given volume and mute state.
fn create_media_stream(volume_level: f32, muted: bool) -> AudioStream {
    AudioStream {
        stream_type: AudioStreamType::Media,
        source: AudioSettingSource::User,
        user_volume_level: volume_level,
        user_volume_muted: muted,
    }
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

/// Adds a media volume limit to the handler in the test environment. Automatically handles a Get
/// request for the current audio info, which happens whenever policies are modified.
async fn set_media_volume_limit(
    env: &mut TestEnvironment,
    transform: Transform,
    audio_info: AudioInfo,
) -> PolicyId {
    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    env.serve_audio_info(audio_info).await;

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
    audio_info.replace_stream(create_media_stream(internal_volume_level, false));
    env.serve_audio_info(audio_info.clone()).await;

    // Send the get request to the handler.
    let request_transform = env.handler.handle_setting_request(SettingRequest::Get).await;

    // Modify the audio info to contain the expected volume level.
    audio_info.replace_stream(create_media_stream(expected_volume_level, false));
    assert_eq!(request_transform, Some(RequestTransform::Result(Ok(Some(audio_info.into())))));
}

/// Asks the handler in the environment to handle a set request and verifies that the transformed
/// request matches the given stream.
async fn set_and_verify_stream(
    env: &mut TestEnvironment,
    request_stream: impl Into<SetAudioStream>,
    expected_stream: impl Into<SetAudioStream>,
) {
    let request_transform = env
        .handler
        .handle_setting_request(SettingRequest::SetVolume(vec![request_stream.into()], 0))
        .await;

    assert_eq!(
        request_transform,
        Some(RequestTransform::Request(SettingRequest::SetVolume(vec![expected_stream.into()], 0)))
    );
}

/// Asks the handler in the environment to handle a set request and verifies that the transformed
/// request matches the given volume level.
async fn set_and_verify_media_volume(
    env: &mut TestEnvironment,
    request_volume_level: f32,
    expected_volume_level: f32,
) {
    set_and_verify_stream(
        env,
        create_media_stream(request_volume_level, false),
        create_media_stream(expected_volume_level, false),
    )
    .await;
}

async fn verify_stream_set(
    receptor: &mut <MessageHub as MessageHubUtil>::Receptor,
    stream: impl Into<SetAudioStream>,
) {
    while let Some(message_event) = receptor.next().await {
        if let MessageEvent::Message(incoming_payload, mut client) = message_event {
            client.acknowledge().await;
            if let TestEnvironmentPayload::Request(SettingRequest::SetVolume(streams, _)) =
                incoming_payload
            {
                assert_eq!(vec![stream.into()], streams);
                return;
            }
        }
    }

    // We expect the setting handler scaffold to report it received a set request with the specified
    // AudioStream. If that payload is not encountered before the end of the message stream
    // (triggered by either removal of the receptor from the MessageHub or receptor going out of
    // scope), this is considered an error.
    panic!("didn't expected request");
}

/// Verifies that the setting proxy in the test environment received a set request for media volume.
async fn verify_media_volume_set(
    receptor: &mut <MessageHub as MessageHubUtil>::Receptor,
    volume_level: f32,
) {
    verify_stream_set(receptor, create_media_stream(volume_level, false)).await;
}

// Verifies that the audio policy handler restores to a default state with all stream types when no
// persisted value is found.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_no_persisted_state() {
    let expected_value = get_default_state();

    let mut env = TestEnvironment::new().await;

    // Request the policy state from the handler.
    let payload = env.handler.handle_policy_request(Request::Get).await.expect("get failed");

    // The state response matches the expected value.
    assert_eq!(payload, Payload::PolicyInfo(PolicyInfo::Audio(expected_value)));

    // Verify that nothing was written to storage.
    assert_eq!(env.store.get::<State>().await, State::default_value());
}

// Verifies that the audio policy handler reads the persisted state and restores it.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_restore_persisted_state() {
    let modified_property = AudioStreamType::Media;
    let expected_transform = Transform::Max(1.0f32);

    // Persisted state with only one stream and transform.
    let mut persisted_state =
        StateBuilder::new().add_property(AudioStreamType::Media, TransformFlags::all()).build();
    let _ = persisted_state.add_transform(modified_property, expected_transform);

    let storage_factory = TestEnvironment::create_storage_factory().await;
    let store = storage_factory.get_store().await;

    // Write the "persisted" value to storage for the handler to read on start.
    let _ = store.write(&persisted_state, false).await.expect("write failed");

    let mut env = TestEnvironment::new_with_store(storage_factory, None).await;

    // Start task to provide audio info to the handler, which it requests at startup.
    env.serve_audio_info(default_audio_info()).await;

    // Request the policy state from the handler.
    let payload = env.handler.handle_policy_request(Request::Get).await.expect("get failed");

    // The state response matches the expected value.
    if let Payload::PolicyInfo(PolicyInfo::Audio(mut state)) = payload {
        // The persisted transform was found in the returned state.
        verify_state(&state, modified_property, expected_transform);

        // The returned state has the full list of properties from configuration, not just the
        // single persisted property.
        assert_eq!(state.properties().len(), get_default_state().properties().len());
    } else {
        panic!("unexpected response")
    }
}

// Tests adding and reading policies.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_policy() {
    let expected_transform = Transform::Max(1.0);
    let modified_property = AudioStreamType::Media;

    let mut env = TestEnvironment::new().await;

    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    env.serve_audio_info(default_audio_info()).await;

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
    let stored_value = env.store.get::<State>().await;
    verify_state(&stored_value, modified_property, expected_transform);

    // Request the policy state from the handler and verify that it matches the stored value.
    let payload = env.handler.handle_policy_request(Request::Get).await.expect("get failed");
    assert_eq!(payload, Payload::PolicyInfo(PolicyInfo::Audio(stored_value)));
}

// Tests that attempting to removing an unknown policy returns an appropriate error.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_remove_unknown_policy() {
    let mut env = TestEnvironment::new().await;
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
            PolicyType::Audio,
            ARG_POLICY_ID.into(),
            format!("{:?}", invalid_policy_id).into()
        ))
    );
}

// Tests removing added policies.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_remove_policy() {
    let expected_value = get_default_state();

    let mut env = TestEnvironment::new().await;

    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    env.serve_audio_info(default_audio_info()).await;

    // Add a policy transform.
    let payload = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::AddPolicy(
            AudioStreamType::Media,
            Transform::Max(1.0),
        )))
        .await
        .expect("get failed");
    let policy_id = match payload {
        Payload::Audio(Response::Policy(policy_id)) => policy_id,
        _ => panic!("unexpected response"),
    };

    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    env.serve_audio_info(default_audio_info()).await;

    // Remove the added transform
    let payload = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::RemovePolicy(policy_id)))
        .await
        .expect("get failed");

    // The returned ID should match the original ID.
    assert_eq!(payload, Payload::Audio(Response::Policy(policy_id)));

    // Request the policy state from the handler.
    let payload = env.handler.handle_policy_request(Request::Get).await.expect("get failed");

    // The state response matches the expected value.
    assert_eq!(payload, Payload::PolicyInfo(PolicyInfo::Audio(expected_value.clone())));

    // Verify that the expected value is persisted to storage.
    assert_eq!(env.store.get::<State>().await, expected_value);
}

// Verifies that adding a max volume policy adjusts the internal volume level if it's above the
// new max.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_policy_modifies_internal_volume_above_max() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at max volume.
    starting_audio_info.replace_stream(create_media_stream(1.0, false));

    // Set the max volume limit to 60%.
    let max_volume = 0.6;

    // Create observer to capture the events to follow.
    let mut receptor = env.create_request_observer().await;
    let _ = set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info).await;

    // Handler requests that the setting handler set the volume to 60% (the max set by policy).
    verify_media_volume_set(&mut receptor, max_volume).await;
}

// Verifies that adding a min volume policy adjusts the internal volume level if it's below the
// new min.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_policy_modifies_internal_volume_below_min() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 0%.
    starting_audio_info.replace_stream(create_media_stream(0.0, false));

    // Set the min volume limit to 40%.
    let min_volume = 0.4;
    let mut receptor = env.create_request_observer().await;
    let _ = set_media_volume_limit(&mut env, Transform::Min(min_volume), starting_audio_info).await;

    // Handler requests that the setting handler set the volume to 40% (the min set by policy).
    verify_media_volume_set(&mut receptor, min_volume).await;
}

// Verifies that adding a min volume policy unmutes the volume stream.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_min_limit_unmutes() {
    let mut env = TestEnvironment::new().await;

    let starting_volume_level = 0.8;
    let mut starting_audio_info = default_audio_info();

    let expected_stream = create_media_stream(starting_volume_level, false);

    let mut receptor = env.create_request_observer().await;

    // Starting audio info has media volume at max volume.
    starting_audio_info.replace_stream(create_media_stream(starting_volume_level, true));

    // Set a min limit so that the stream is unmuted.
    let _ = set_media_volume_limit(
        &mut env,
        Transform::Min(starting_volume_level),
        starting_audio_info,
    )
    .await;

    // Handler requests that the setting handler unmute the stream.
    verify_stream_set(&mut receptor, expected_stream).await;
}

// Verifies that adding a max volume policy of 0% succeeds and sets the volume 0%.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_zero_max() {
    let mut env = TestEnvironment::new().await;

    // Starting audio info at non-zero value.
    let mut starting_audio_info = default_audio_info();
    starting_audio_info.replace_stream(create_media_stream(0.5, false));

    let mut receptor = env.create_request_observer().await;

    // Set the max volume limit to 0%.
    let max_volume = 0.0;
    let _ = set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info).await;

    // Handler requests that the setting handler set the volume to 0%.
    verify_media_volume_set(&mut receptor, max_volume).await;
}

// Verifies that the internal volume will not be adjusted when it's already within policy limits.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_add_policy_does_not_modify_internal_volume_within_limits() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50%.
    starting_audio_info.replace_stream(create_media_stream(0.5, false));

    let mut receptor = env.create_request_observer().await;

    // Set the min volume limit to 40%.
    let _ =
        set_media_volume_limit(&mut env, Transform::Min(0.4), starting_audio_info.clone()).await;

    // Set the max volume limit to 60%.
    let _ =
        set_media_volume_limit(&mut env, Transform::Max(0.6), starting_audio_info.clone()).await;

    // So far, no set requests should have been sent to the setting proxy, set the min volume limit
    // to 55% to ensure that the first set request the setting proxy receives is this one.
    let _ = set_media_volume_limit(&mut env, Transform::Min(0.55), starting_audio_info).await;

    // Handler requests that the setting handler set the volume to 55% (the min set by policy).
    verify_media_volume_set(&mut receptor, 0.55).await;
}

/// Verifies that the requestor in the test environment received a changed notification with the
/// given audio info.
async fn verify_media_volume_changed(
    receptor: &mut service::message::Receptor,
    audio_info: AudioInfo,
) {
    verify_payload(SettingPayload::Response(Ok(Some(audio_info.into()))).into(), receptor, None)
        .await;
}

// Verifies that when a max volume policy is removed, listeners will receive a changed event with
// the new external volume.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_remove_policy_notifies_listeners() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    let max_volume = 0.6;

    // Starting audio info has media volume at 60% volume.
    starting_audio_info.replace_stream(create_media_stream(max_volume, false));

    env.serve_audio_info(starting_audio_info.clone()).await;

    let mut listener = env.create_volume_listener().await;

    // Set the max volume limit to 60%.
    let policy_id =
        set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info.clone())
            .await;

    // The internal volume didn't change but the external volume did, so the handler manually sends
    // a changed event to listeners. The value will still be the original value as there is
    // no policy proxy to intercept the message and pass it back to the handler for processing.
    verify_media_volume_changed(&mut listener, starting_audio_info.clone()).await;

    // Start task to provide audio info to the handler, which it requests when policies are
    // modified.
    env.serve_audio_info(starting_audio_info.clone()).await;

    // Remove the policy from the handler.
    let _ = env
        .handler
        .handle_policy_request(Request::Audio(audio::Request::RemovePolicy(policy_id)))
        .await
        .expect("remove policy succeeds");

    // The internal volume didn't change but the external volume should be back at the original
    // values.
    verify_media_volume_changed(&mut listener, starting_audio_info).await;
}

// Verifies that when multiple max volume policies are in place, that the most strict one applies.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_lowest_max_limit_applies_internally() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at max volume.
    starting_audio_info.replace_stream(create_media_stream(1.0, false));

    let mut receptor = env.create_request_observer().await;

    // Add a policy transform to limit the max media volume to 60%.
    let max_volume = 0.6;
    let _ =
        set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info.clone())
            .await;

    // Handler requests that the setting handler set the volume to 60% (the max set by policy).
    verify_media_volume_set(&mut receptor, max_volume).await;

    // Internal audio level should now be at 60%.
    starting_audio_info.replace_stream(create_media_stream(max_volume, false));

    // Add a higher limit, which won't result in a new set request.
    let _ =
        set_media_volume_limit(&mut env, Transform::Max(0.7), starting_audio_info.clone()).await;

    // Add a lower limit, which will cause a new set request.
    let lower_max_volume = 0.5;
    let _ = set_media_volume_limit(
        &mut env,
        Transform::Max(lower_max_volume),
        starting_audio_info.clone(),
    )
    .await;

    // Handler requests that the setting handler set the volume to 50% (the lowest max).
    verify_media_volume_set(&mut receptor, lower_max_volume).await;
}

// Verifies that when multiple min volume policies are in place, that the most strict one applies.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_highest_min_limit_applies_internally() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 0% volume.
    starting_audio_info.replace_stream(create_media_stream(0.0, false));

    let mut receptor = env.create_request_observer().await;

    // Add a policy transform to limit the min media volume to 30%.
    let min_volume = 0.3;
    let _ =
        set_media_volume_limit(&mut env, Transform::Min(min_volume), starting_audio_info.clone())
            .await;

    // Handler requests that the setting handler set the volume to 30% (the max set by policy).
    verify_media_volume_set(&mut receptor, min_volume).await;

    // Internal audio level should now be at 30%.
    starting_audio_info.replace_stream(create_media_stream(min_volume, false));

    // Add a lower limit, which won't result in a new set request.
    let _ =
        set_media_volume_limit(&mut env, Transform::Min(0.2), starting_audio_info.clone()).await;

    // Add a higher limit, which will cause a new set request.
    let higher_min_volume = 0.4;
    let _ = set_media_volume_limit(
        &mut env,
        Transform::Min(higher_min_volume),
        starting_audio_info.clone(),
    )
    .await;

    // Handler requests that the setting handler set the volume to 40% (the highest min).
    verify_media_volume_set(&mut receptor, higher_min_volume).await;
}

// Verifies that when no policies are in place that external set requests are not modified.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_no_policy_external_sets_not_modified() {
    let mut env = TestEnvironment::new().await;

    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let external_volume_level = i as f32 / 10.0;

        // Set requests have the same volume level before and after passing through the policy
        // handler.
        set_and_verify_media_volume(&mut env, external_volume_level, external_volume_level).await;
    }
}

// Verifies that adding a max volume policy scales external set requests to an appropriate internal
// volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_max_volume_policy_scales_external_sets() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at max volume.
    starting_audio_info.replace_stream(create_media_stream(1.0, false));

    // Set the max volume limit to 60%.
    let max_volume = 0.6;
    let _ = set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info).await;

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

// Verifies that adding a min volume policy scales external set requests to an appropriate internal
// volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_volume_policy_scales_external_sets() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 0% volume.
    starting_audio_info.replace_stream(create_media_stream(0.0, false));

    // Set the min volume limit to 60%.
    let min_volume = 0.2;
    let _ = set_media_volume_limit(&mut env, Transform::Min(min_volume), starting_audio_info).await;

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

// Verifies that adding a min volume policy prevents external sets from muting the stream.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_volume_policy_prevents_mute() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();
    let starting_stream = create_media_stream(0.5, false);
    starting_audio_info.replace_stream(starting_stream);

    // Set a min volume limit so the stream can't be muted.
    let _ = set_media_volume_limit(&mut env, Transform::Min(0.1), starting_audio_info).await;

    // External client attempts to mute the volume.
    let mut request = starting_stream;
    request.user_volume_muted = true;

    // Mute state doesn't change
    set_and_verify_stream(&mut env, request, starting_stream).await;
}

// Verifies that adding both a max and a min volume policy scales external set requests to an
// appropriate internal volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_and_max_volume_policy_scales_external_volume() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50% volume.
    starting_audio_info.replace_stream(create_media_stream(0.5, false));

    // Set the max volume limit to 80%.
    let max_volume = 0.8;
    let _ =
        set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info.clone())
            .await;

    // Set the min volume limit to 20%.
    let min_volume = 0.2;
    let _ = set_media_volume_limit(&mut env, Transform::Min(min_volume), starting_audio_info).await;

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

// Verifies that when no policies are in place that external get requests return the same
// level as the internal volume levels.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_no_policy_gets_not_modified() {
    let mut env = TestEnvironment::new().await;

    // Test get requests with internal volumes from 0.0 to 1.0.
    for i in 0..=10 {
        // 0 to 1 inclusive, with steps of 0.1.
        let internal_volume_level = i as f32 / 10.0;

        // Get requests have the same volume level before and after passing through the policy
        // handler.
        get_and_verify_media_volume(&mut env, internal_volume_level, internal_volume_level).await;
    }
}

// Verifies that a max volume policy scales the result of get requests to the correct external
// volume level.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_max_volume_policy_scales_gets() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50% volume.
    starting_audio_info.replace_stream(create_media_stream(0.5, false));

    // Set the max volume limit to 60%.
    let max_volume = 0.6;
    let _ = set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info).await;

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

// Verifies that a min volume policy alone won't result in any scaling of the external volume
// levels.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_volume_policy_scales_gets() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50% volume.
    starting_audio_info.replace_stream(create_media_stream(0.5, false));

    // Set the max volume limit to 60%.
    let _ = set_media_volume_limit(&mut env, Transform::Min(0.2), starting_audio_info).await;

    for i in 2..=10 {
        // 0.2 to 1 inclusive, with steps of 0.1. Starting at 20% since the min limit clamps the
        // internal volume at 20%.
        let internal_volume_level = i as f32 / 10.0;

        // When only a min limit is present, the output volume does not need to be scaled.
        get_and_verify_media_volume(&mut env, internal_volume_level, internal_volume_level).await;
    }
}

// Verifies that the external volume returned by a get request is properly scaled when both a min
// and a max volume policy are in place.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_min_and_max_volume_policy_scales_gets() {
    let mut env = TestEnvironment::new().await;

    let mut starting_audio_info = default_audio_info();

    // Starting audio info has media volume at 50% volume.
    starting_audio_info.replace_stream(create_media_stream(0.5, false));

    // Set the max volume limit to 80%.
    let max_volume = 0.8;
    let _ =
        set_media_volume_limit(&mut env, Transform::Max(max_volume), starting_audio_info.clone())
            .await;

    // Set the min volume limit to 20%.
    // Since internal volumes shouldn't be below the min volume anyways, the min volume level has no
    // effect here.
    let _ = set_media_volume_limit(&mut env, Transform::Min(0.2), starting_audio_info).await;

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

// Verifies that build-time configuration scales external set calls.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_audio_policy_config_scales_external_sets() {
    // Set a max volume of 0.6 through the configuration.
    let max_volume = 0.6;
    let mut audio_policy_config = AudioPolicyConfig { transforms: Default::default() };
    let _ = audio_policy_config
        .transforms
        .insert(AudioStreamType::Media, vec![Transform::Max(max_volume)]);

    let mut env = TestEnvironment::new_with_store(
        TestEnvironment::create_storage_factory().await,
        Some(audio_policy_config),
    )
    .await;

    // A set request for max volume will be limited to the max level specified by the config.
    set_and_verify_media_volume(&mut env, 1.0, max_volume).await;
}

// Verifies that build-time configuration scales get calls.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_audio_policy_config_scales_gets() {
    // Set a max volume of 0.6 through the configuration.
    let max_volume = 0.6;
    let mut audio_policy_config = AudioPolicyConfig { transforms: Default::default() };
    let _ = audio_policy_config
        .transforms
        .insert(AudioStreamType::Media, vec![Transform::Max(max_volume)]);

    let mut env = TestEnvironment::new_with_store(
        TestEnvironment::create_storage_factory().await,
        Some(audio_policy_config),
    )
    .await;

    // When the internal volume is max, it'll be limited to the max level specified by the config.
    get_and_verify_media_volume(&mut env, max_volume, 1.0).await;
}

// Verifies that build-time configuration scales external set calls and work in conjunction with
// policies added by clients of the volume policy FIDL api.
#[fuchsia_async::run_until_stalled(test)]
async fn test_handler_audio_policy_config_works_with_client_policies() {
    // Set a max volume of 0.6 through the configuration.
    let config_max_volume = 0.6;
    let mut audio_policy_config = AudioPolicyConfig { transforms: Default::default() };
    let _ = audio_policy_config
        .transforms
        .insert(AudioStreamType::Media, vec![Transform::Max(config_max_volume)]);

    let mut env = TestEnvironment::new_with_store(
        TestEnvironment::create_storage_factory().await,
        Some(audio_policy_config),
    )
    .await;

    // Set a higher max volume limit from a FIDL client.
    let _ = set_media_volume_limit(&mut env, Transform::Max(0.8), default_audio_info()).await;

    // A set request for max volume will be limited to the max level specified by the config.
    set_and_verify_media_volume(&mut env, 1.0, config_max_volume).await;

    // Set a lower max volume limit from a FIDL client.
    let fidl_client_max_volume = 0.4;
    let _ = set_media_volume_limit(
        &mut env,
        Transform::Max(fidl_client_max_volume),
        default_audio_info(),
    )
    .await;

    // A set request for max volume will be limited to the max level specified by the client.
    set_and_verify_media_volume(&mut env, 1.0, fidl_client_max_volume).await;
}
