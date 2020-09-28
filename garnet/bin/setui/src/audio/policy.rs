// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::base::AudioStreamType;
use bitflags::bitflags;
use std::collections::HashMap;

/// `StateBuilder` is used to construct a new [`State`] as the internal
/// modification of properties should not be available post construction.
/// TODO(fxbug.dev/59747): remove dead_code macro once used in production code as well.
///
/// [`State`]: struct.State.html
pub struct StateBuilder {
    next_id: u64,
    properties: HashMap<u64, Property>,
}

#[allow(dead_code)]
impl StateBuilder {
    pub fn new() -> Self {
        Self { next_id: 0, properties: HashMap::new() }
    }

    pub fn add_property(
        mut self,
        stream_type: AudioStreamType,
        available_transforms: TransformFlags,
    ) -> Self {
        let property = Property::new(self.next_id, stream_type, available_transforms);
        self.next_id += 1;
        self.properties.insert(property.id, property);

        self
    }

    pub fn build(self) -> State {
        State { properties: self.properties }
    }
}

/// `State` defines the current configuration of the audio policy. This
/// includes the available properties, which encompass the active transform
/// policies and transforms available to be set.
pub struct State {
    properties: HashMap<u64, Property>,
}

#[allow(dead_code)]
impl State {
    pub fn get_properties(&self) -> Vec<Property> {
        self.properties.values().cloned().collect::<Vec<Property>>()
    }
}

/// `Property` defines the current policy configuration over a given audio
/// stream type.
#[derive(PartialEq, Debug, Clone)]
pub struct Property {
    /// Identifier used to reference this type over other requests, such as
    /// setting a policy.
    pub id: u64,
    /// The next id to be assigned to a transform transformation.
    next_policy_id: u64,
    /// The stream type uniquely identifies the type of stream.
    pub stream_type: AudioStreamType,
    /// The available transforms provided as a bitmask.
    pub available_transforms: TransformFlags,
    /// The active transform definitions on this stream type.
    pub active_policies: Vec<Policy>,
}

impl Property {
    pub fn new(
        id: u64,
        stream_type: AudioStreamType,
        available_transforms: TransformFlags,
    ) -> Self {
        Self { id, next_policy_id: 0, stream_type, available_transforms, active_policies: vec![] }
    }

    #[allow(dead_code)]
    pub fn add_transform(&mut self, transform: Transform) {
        let policy = Policy { id: self.next_policy_id, transform };

        self.next_policy_id += 1;
        self.active_policies.push(policy);
    }
}

bitflags! {
    /// `TransformFlags` defines the available transform space.
    pub struct TransformFlags: u64 {
        const TRANSFORM_MAX = 1 << 0;
        const TRANSFORM_MIN = 1 << 1;
    }
}

/// `Policy` captures a fully specified transform.
#[derive(PartialEq, Debug, Clone)]
pub struct Policy {
    pub id: u64,
    pub transform: Transform,
}

/// `Transform` provides the parameters for specifying a transform.
/// TODO(fxbug.dev/60367): Add Mute and Disable transforms.
#[allow(dead_code)]
#[derive(PartialEq, Debug, Clone, Copy)]
pub enum Transform {
    Max(f64),
    Min(f64),
}
