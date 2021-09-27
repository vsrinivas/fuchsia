// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::convert::TryFrom;

use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings_policy::{PolicyParameters, Volume};

use bitflags::bitflags;

use crate::audio::types::AudioStreamType;
use crate::audio::utils::round_volume_level;
use crate::handler::device_storage::DeviceStorageCompatible;

pub mod audio_policy_handler;
pub mod volume_policy_fidl_handler;

pub type PropertyTarget = AudioStreamType;

/// Unique identifier for a policy.
#[derive(Debug, Copy, Clone, Hash, Ord, PartialOrd, Eq, PartialEq, Serialize, Deserialize)]
pub struct PolicyId(u32);

impl PolicyId {
    pub(crate) fn create(policy_id: u32) -> Self {
        Self(policy_id)
    }
}

/// `StateBuilder` is used to construct a new [`State`] as the internal
/// modification of properties should not be available post construction.
///
/// [`State`]: struct.State.html
pub(crate) struct StateBuilder {
    properties: HashMap<PropertyTarget, Property>,
}

impl StateBuilder {
    pub(crate) fn new() -> Self {
        Self { properties: HashMap::new() }
    }

    pub(crate) fn add_property(
        mut self,
        stream_type: PropertyTarget,
        available_transforms: TransformFlags,
    ) -> Self {
        let property = Property::new(stream_type, available_transforms);
        let _ = self.properties.insert(stream_type, property);

        self
    }

    pub(crate) fn build(self) -> State {
        State { properties: self.properties }
    }
}

/// `State` defines the current configuration of the audio policy. This
/// includes the available properties, which encompass the active transform
/// policies and transforms available to be set.
#[derive(PartialEq, Default, Debug, Clone, Serialize, Deserialize)]
pub struct State {
    properties: HashMap<PropertyTarget, Property>,
}

impl State {
    #[cfg(test)]
    pub(crate) fn get_properties(&self) -> Vec<Property> {
        self.properties.values().cloned().collect::<Vec<Property>>()
    }

    #[cfg(test)]
    pub(crate) fn properties(&mut self) -> &mut HashMap<PropertyTarget, Property> {
        &mut self.properties
    }

    /// Attempts to find the policy with the given ID from the state. Returns the policy target if
    /// it was found and removed, else returns None.
    pub(crate) fn find_policy_target(&self, policy_id: PolicyId) -> Option<PropertyTarget> {
        self.properties
            .values()
            .find_map(|property| property.find_policy(policy_id).map(|_| property.target))
    }

    /// Attempts to remove the policy with the given ID from the state. Returns the policy target if
    /// it was found and removed, else returns None.
    pub(crate) fn remove_policy(&mut self, policy_id: PolicyId) -> Option<PropertyTarget> {
        self.properties
            .values_mut()
            .find_map(|property| property.remove_policy(policy_id).map(|_| property.target))
    }

    /// Attempts to add a new policy transform to the given target. Returns the [`PolicyId`] of the
    /// new policy, or None if the target doesn't exist.
    pub(crate) fn add_transform(
        &mut self,
        target: PropertyTarget,
        transform: Transform,
    ) -> Option<PolicyId> {
        let next_id = self.next_id();

        // Round policy volume levels to the same precision as the base audio setting.
        let rounded_transform = match transform {
            Transform::Max(value) => Transform::Max(round_volume_level(value)),
            Transform::Min(value) => Transform::Min(round_volume_level(value)),
        };

        self.properties.get_mut(&target)?.add_transform(rounded_transform, next_id);

        Some(next_id)
    }

    /// Returns next [`PolicyId`] to assign for a new transform. The ID will be unique and the
    /// highest of any existing policy.
    fn next_id(&self) -> PolicyId {
        let PolicyId(highest_id) = self
            .properties
            .values()
            .filter_map(|property| property.highest_id())
            .max()
            .unwrap_or_else(|| PolicyId::create(0));
        PolicyId::create(highest_id + 1)
    }
}

impl DeviceStorageCompatible for State {
    fn default_value() -> Self {
        State { properties: Default::default() }
    }

    const KEY: &'static str = "audio_policy_state";
}

/// `AudioPolicyConfig` is read from config_data specified at build time to configure the behavior
/// of the audio policy API.
#[derive(PartialEq, Default, Debug, Clone, Serialize, Deserialize)]
pub struct AudioPolicyConfig {
    /// Transforms can be specified at build-time and are in effect immediately when the service
    /// starts. These transforms cannot be viewed, added, or removed by clients of the audio policy
    /// FIDL API.
    pub(crate) transforms: HashMap<PropertyTarget, Vec<Transform>>,
}

/// `Property` defines the current policy configuration over a given audio
/// stream type.
#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub(crate) struct Property {
    /// Identifier used to reference this type over other requests, such as
    /// setting a policy.
    pub(crate) target: PropertyTarget,
    /// The available transforms provided as a bitmask.
    pub(crate) available_transforms: TransformFlags,
    /// The active transform definitions on this stream type.
    pub(crate) active_policies: Vec<Policy>,
}

impl Property {
    pub(crate) fn new(stream_type: AudioStreamType, available_transforms: TransformFlags) -> Self {
        Self { target: stream_type, available_transforms, active_policies: vec![] }
    }

    /// Adds the given transform to this property.
    fn add_transform(&mut self, transform: Transform, id: PolicyId) {
        self.active_policies.push(Policy { id, transform });
    }

    /// Attempts to find the policy with the given ID in this property. Returns the policy if it
    /// was found, else returns None.
    pub(crate) fn find_policy(&self, policy_id: PolicyId) -> Option<Policy> {
        self.active_policies.iter().find(|policy| policy.id == policy_id).copied()
    }

    /// Attempts to remove the policy with the given ID from this property. Returns the policy if it
    /// was found and removed, else returns None.
    pub(crate) fn remove_policy(&mut self, policy_id: PolicyId) -> Option<Policy> {
        match self.active_policies.iter().position(|policy| policy.id == policy_id) {
            Some(index) => Some(self.active_policies.remove(index)),
            None => None,
        }
    }

    /// Returns the highest [`PolicyId`] in the active policies of this property, or None if there
    /// are no active policies.
    pub(crate) fn highest_id(&self) -> Option<PolicyId> {
        self.active_policies.iter().map(|policy| policy.id).max()
    }
}

impl From<Property> for fidl_fuchsia_settings_policy::Property {
    fn from(src: Property) -> Self {
        fidl_fuchsia_settings_policy::Property {
            target: Some(src.target.into()),
            available_transforms: Some(src.available_transforms.into()),
            active_policies: Some(
                src.active_policies.into_iter().map(Policy::into).collect::<Vec<_>>(),
            ),
            ..fidl_fuchsia_settings_policy::Property::EMPTY
        }
    }
}

impl From<fidl_fuchsia_settings_policy::Target> for AudioStreamType {
    fn from(src: fidl_fuchsia_settings_policy::Target) -> Self {
        match src {
            fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::Background) => {
                AudioStreamType::Background
            }
            fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::Media) => {
                AudioStreamType::Media
            }
            fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::Interruption) => {
                AudioStreamType::Interruption
            }
            fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::SystemAgent) => {
                AudioStreamType::SystemAgent
            }
            fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::Communication) => {
                AudioStreamType::Communication
            }
        }
    }
}

impl From<AudioStreamType> for fidl_fuchsia_settings_policy::Target {
    fn from(src: AudioStreamType) -> Self {
        match src {
            AudioStreamType::Background => {
                fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::Background)
            }
            AudioStreamType::Media => {
                fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::Media)
            }
            AudioStreamType::Interruption => {
                fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::Interruption)
            }
            AudioStreamType::SystemAgent => {
                fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::SystemAgent)
            }
            AudioStreamType::Communication => {
                fidl_fuchsia_settings_policy::Target::Stream(AudioRenderUsage::Communication)
            }
        }
    }
}

bitflags! {
    /// `TransformFlags` defines the available transform space.
    #[derive(Serialize, Deserialize)]
    pub(crate) struct TransformFlags: u64 {
        const TRANSFORM_MAX = 1 << 0;
        const TRANSFORM_MIN = 1 << 1;
    }
}

impl From<TransformFlags> for Vec<fidl_fuchsia_settings_policy::Transform> {
    fn from(src: TransformFlags) -> Self {
        let mut transforms = Vec::new();
        if src.contains(TransformFlags::TRANSFORM_MAX) {
            transforms.push(fidl_fuchsia_settings_policy::Transform::Max);
        }
        if src.contains(TransformFlags::TRANSFORM_MIN) {
            transforms.push(fidl_fuchsia_settings_policy::Transform::Min);
        }

        transforms
    }
}

/// `Policy` captures a fully specified transform.
#[derive(PartialEq, Debug, Copy, Clone, Serialize, Deserialize)]
pub(crate) struct Policy {
    pub(crate) id: PolicyId,
    pub(crate) transform: Transform,
}

impl From<Policy> for fidl_fuchsia_settings_policy::Policy {
    fn from(src: Policy) -> Self {
        fidl_fuchsia_settings_policy::Policy {
            policy_id: Some(src.id.0),
            parameters: Some(src.transform.into()),
            ..fidl_fuchsia_settings_policy::Policy::EMPTY
        }
    }
}

/// `Transform` provides the parameters for specifying a transform.
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum Transform {
    // Limits the maximum volume an audio stream can be set to.
    Max(f32),
    // Sets a floor for the minimum volume an audio stream can be set to.
    Min(f32),
}

impl TryFrom<PolicyParameters> for Transform {
    type Error = &'static str;

    fn try_from(src: PolicyParameters) -> Result<Self, Self::Error> {
        // Support future expansion of FIDL.
        #[allow(unreachable_patterns)]
        Ok(match src {
            PolicyParameters::Max(Volume { volume, .. }) => {
                if volume.map_or(false, |val| !val.is_finite()) {
                    return Err("max volume is not a finite number");
                } else {
                    Transform::Max(volume.ok_or("missing max volume")?)
                }
            }
            PolicyParameters::Min(Volume { volume, .. }) => {
                if volume.map_or(false, |val| !val.is_finite()) {
                    return Err("min volume is not a finite number");
                } else {
                    Transform::Min(volume.ok_or("missing min volume")?)
                }
            }
            _ => return Err("unknown policy parameter"),
        })
    }
}

impl From<Transform> for PolicyParameters {
    fn from(src: Transform) -> Self {
        match src {
            Transform::Max(vol) => {
                PolicyParameters::Max(Volume { volume: Some(vol), ..Volume::EMPTY })
            }
            Transform::Min(vol) => {
                PolicyParameters::Min(Volume { volume: Some(vol), ..Volume::EMPTY })
            }
        }
    }
}

/// Available requests to interact with the volume policy.
#[derive(PartialEq, Clone, Debug)]
pub enum Request {
    /// Adds a policy transform to the specified property. If successful, this transform will become
    /// a policy on the property.
    AddPolicy(PropertyTarget, Transform),
    /// Removes an existing policy on the property.
    RemovePolicy(PolicyId),
}

/// Successful responses for [`Request`]
///
/// [`Request`]: enum.Request.html
#[derive(PartialEq, Clone, Debug)]
pub enum Response {
    /// Response to any transform addition or policy removal. The returned id
    /// represents the modified policy.
    Policy(PolicyId),
}

#[cfg(test)]
mod tests {
    use crate::audio::policy::{
        PolicyId, Property, PropertyTarget, StateBuilder, Transform, TransformFlags,
    };
    use crate::audio::types::AudioStreamType;
    use crate::audio::utils::round_volume_level;
    use fidl_fuchsia_settings_policy::{PolicyParameters, Volume};
    use matches::assert_matches;
    use std::collections::{HashMap, HashSet};
    use std::convert::TryFrom;

    /// Verifies that adding a max volume transform with the given volume limit results in a
    /// transform being added with the expected volume limit.
    ///
    /// While the input limit and actual added limit are usually the same, rounding or clamping may
    /// result in differences.
    fn set_and_verify_max_volume(input_volume_limit: f32, actual_volume_limit: f32) {
        let target = AudioStreamType::Background;
        let mut state =
            StateBuilder::new().add_property(target, TransformFlags::TRANSFORM_MAX).build();
        state.add_transform(target, Transform::Max(input_volume_limit)).expect("add succeeded");

        let added_policy = state
            .properties()
            .get(&target)
            .expect("found target")
            .active_policies
            .first()
            .expect("has policy");

        // Volume limit values are rounded to two decimal places, similar to audio setting volumes.
        // When comparing the values, we use an epsilon that's smaller than the threshold for
        // rounding.
        let epsilon = 0.0001;
        assert!(matches!(added_policy.transform, Transform::Max(max_volume_limit)
            if (max_volume_limit - actual_volume_limit).abs() <= epsilon));
    }

    // Verifies that using `TryFrom` to convert a `PolicyParameters` into a `Transform` will fail
    // if the source did not have required parameters specified.
    #[test]
    fn parameter_to_transform_missing_arguments() {
        let max_params = PolicyParameters::Max(Volume { volume: None, ..Volume::EMPTY });
        let min_params = PolicyParameters::Min(Volume { volume: None, ..Volume::EMPTY });

        assert_matches!(Transform::try_from(max_params), Err(_));
        assert_matches!(Transform::try_from(min_params), Err(_));
    }

    // Verifies that using `TryFrom` to convert a `PolicyParameters` into a `Transform` will fail
    // if the source did not have a finite number.
    #[test]
    fn parameter_to_transform_invalid_arguments() {
        let max_params =
            PolicyParameters::Max(Volume { volume: Some(f32::NEG_INFINITY), ..Volume::EMPTY });
        let min_params = PolicyParameters::Min(Volume { volume: Some(f32::NAN), ..Volume::EMPTY });

        assert_matches!(Transform::try_from(max_params), Err(_));
        assert_matches!(Transform::try_from(min_params), Err(_));
    }

    // Verifies that using `TryFrom` to convert a `PolicyParameters` into a `Transform` succeeds
    // and that the result contains the same parameters as the source.
    #[test]
    fn parameter_to_transform() {
        let max_volume = 0.5;
        let min_volume = 0.5;
        let max_params =
            PolicyParameters::Max(Volume { volume: Some(max_volume), ..Volume::EMPTY });
        let min_params =
            PolicyParameters::Min(Volume { volume: Some(min_volume), ..Volume::EMPTY });

        assert_eq!(Transform::try_from(max_params), Ok(Transform::Max(max_volume)));
        assert_eq!(Transform::try_from(min_params), Ok(Transform::Min(min_volume)));
    }

    // Verifies that the audio policy state builder functions correctly for adding targets and
    // transforms.
    #[test]
    fn test_state_builder() {
        let properties: HashMap<AudioStreamType, TransformFlags> = [
            (AudioStreamType::Background, TransformFlags::TRANSFORM_MAX),
            (AudioStreamType::Media, TransformFlags::TRANSFORM_MIN),
        ]
        .iter()
        .cloned()
        .collect();
        let mut builder = StateBuilder::new();

        for (property, value) in properties.iter() {
            builder = builder.add_property(*property, *value);
        }

        let state = builder.build();
        let retrieved_properties = state.get_properties();
        assert_eq!(retrieved_properties.len(), properties.len());

        let mut seen_targets = HashSet::<PropertyTarget>::new();
        for property in retrieved_properties.iter().cloned() {
            let target = property.target;
            // Make sure only unique targets are encountered.
            #[allow(clippy::bool_assert_comparison)]
            {
                assert_eq!(seen_targets.contains(&target), false);
            }
            seen_targets.insert(target);
            // Ensure the specified transforms are present.
            assert_eq!(
                property.available_transforms,
                *properties.get(&target).expect("unexpected property")
            );
        }
    }

    // Verifies that the next ID for a new transform is 1 when the state is empty.
    #[test]
    fn test_state_next_id_when_empty() {
        let state = StateBuilder::new()
            .add_property(AudioStreamType::Background, TransformFlags::TRANSFORM_MAX)
            .build();

        assert_eq!(state.next_id(), PolicyId::create(1));
    }

    // Verifies that the audio policy state produces increasing IDs when adding transforms.
    #[test]
    fn test_state_add_transform_ids_increasing() {
        let target = AudioStreamType::Background;
        let mut state =
            StateBuilder::new().add_property(target, TransformFlags::TRANSFORM_MAX).build();

        let mut last_id = PolicyId::create(0);

        // Verify that each new policy ID is larger than the last.
        for _ in 0..10 {
            let next_id = state.next_id();
            let id = state.add_transform(target, Transform::Min(0.0)).expect("target found");
            assert!(id > last_id);
            assert_eq!(next_id, id);
            // Each new ID should also be the highest ID.
            last_id = id;
        }
    }

    // Verifies that the audio policy state rounds volume limit levels.
    #[test]
    fn test_state_add_transform_inputs_rounded() {
        // Test various starting volumes to make sure rounding works.
        let mut input_volume_limit = 0.0;
        set_and_verify_max_volume(input_volume_limit, round_volume_level(input_volume_limit));

        input_volume_limit = 0.000001;
        set_and_verify_max_volume(input_volume_limit, round_volume_level(input_volume_limit));

        input_volume_limit = 0.44444;
        set_and_verify_max_volume(input_volume_limit, round_volume_level(input_volume_limit));
    }

    // Verifies that the audio policy state clamps volume limit levels.
    #[test]
    fn test_state_add_transform_inputs_clamped() {
        let min_volume = 0.0;
        let max_volume = 1.0;

        // Values below the minimum volume level are clamped to the minimum volume level.
        set_and_verify_max_volume(-0.0, min_volume);
        set_and_verify_max_volume(-0.1, min_volume);
        set_and_verify_max_volume(f32::MIN, min_volume);
        set_and_verify_max_volume(f32::NEG_INFINITY, min_volume);

        // Values above the maximum volume level are clamped to the maximum volume level.
        set_and_verify_max_volume(1.1, max_volume);
        set_and_verify_max_volume(f32::MAX, max_volume);
        set_and_verify_max_volume(f32::INFINITY, max_volume);
    }

    // Verifies that adding transforms to policy properties works.
    #[test]
    fn test_property_transforms() {
        let supported_transforms = TransformFlags::TRANSFORM_MAX | TransformFlags::TRANSFORM_MIN;
        let transforms = [Transform::Min(0.1), Transform::Max(0.9)];
        let mut property = Property::new(AudioStreamType::Media, supported_transforms);
        let mut property2 = Property::new(AudioStreamType::Background, supported_transforms);

        for transform in transforms.iter().cloned() {
            property.add_transform(transform, PolicyId::create(0));
            property2.add_transform(transform, PolicyId::create(1));
        }

        // Ensure policy size matches transforms specified.
        assert_eq!(property.active_policies.len(), transforms.len());
        assert_eq!(property2.active_policies.len(), transforms.len());

        let mut retrieved_ids: HashSet<PolicyId> =
            property.active_policies.iter().map(|policy| policy.id).collect();
        retrieved_ids.extend(property2.active_policies.iter().map(|policy| policy.id));

        // Verify transforms are present.
        let mut retrieved_transforms =
            property.active_policies.iter().map(|policy| policy.transform);
        let mut retrieved_transforms2 =
            property2.active_policies.iter().map(|policy| policy.transform);
        for transform in transforms {
            assert!(retrieved_transforms.any(|x| x == transform));
            assert!(retrieved_transforms2.any(|x| x == transform));
        }
    }
}
