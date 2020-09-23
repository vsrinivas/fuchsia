// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy::{Property, StateBuilder, Transform, TransformFlags};
use crate::switchboard::base::AudioStreamType;
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
    // Make sure all transforms are present.
    for transform in retrieved_transforms {
        assert!(transforms.contains(&transform));
    }
}
