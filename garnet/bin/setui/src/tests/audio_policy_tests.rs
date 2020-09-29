// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy::{
    Property, Request as AudioRequest, Response as AudioResponse, StateBuilder, Transform,
    TransformFlags,
};
use crate::internal;
use crate::message::base::{Audience, MessengerType};
use crate::policy::base::{response::Payload, Request};
use crate::switchboard::base::{AudioStreamType, SettingType};
use std::collections::{HashMap, HashSet};
use std::iter::FromIterator;

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

    let mut seen_ids = HashSet::new();
    for property in retrieved_properties.iter().cloned() {
        let id = property.id;
        // make sure only unique ids are encountered
        assert!(!seen_ids.contains(&id));
        seen_ids.insert(id);
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
    let mut property = Property::new(24, AudioStreamType::Media, supported_transforms);

    for transform in transforms.iter().cloned() {
        property.add_transform(transform);
    }

    // Ensure policy size matches transforms specified
    assert_eq!(property.active_policies.len(), transforms.len());

    let retrieved_ids: HashSet<u64> =
        HashSet::from_iter(property.active_policies.iter().map(|policy| policy.id));

    // Make sure all ids are unique.
    assert_eq!(retrieved_ids.len(), transforms.len());
    let retrieved_transforms: Vec<Transform> =
        property.active_policies.iter().map(|policy| policy.transform).collect();

    // Verify transforms are present.
    for transform in retrieved_transforms {
        assert!(transforms.contains(&transform));
    }
}

// A simple validation test to ensure the policy message hub propagates messages
// properly.
#[fuchsia_async::run_until_stalled(test)]
async fn test_policy_message_hub() {
    let messenger_factory = internal::policy::message::create_hub();
    let policy_handler_address = internal::policy::Address::Policy(SettingType::Audio);

    // Create messenger to send request.
    let (messenger, _) = messenger_factory
        .create(MessengerType::Unbound)
        .await
        .expect("unbound messenger should be present");

    // Create receptor to act as policy endpoint.
    let (_, mut receptor) = messenger_factory
        .create(MessengerType::Addressable(policy_handler_address))
        .await
        .expect("addressable messenger should be present");

    let request_payload = internal::policy::Payload::Request(Request::Audio(AudioRequest::Get));

    // Send request.
    let mut reply_receptor = messenger
        .message(request_payload.clone(), Audience::Address(policy_handler_address))
        .send();

    // Wait and verify request received.
    let (payload, client) = receptor.next_payload().await.expect("should receive message");
    assert_eq!(payload, request_payload);
    assert_eq!(client.get_author(), messenger.get_signature());

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
