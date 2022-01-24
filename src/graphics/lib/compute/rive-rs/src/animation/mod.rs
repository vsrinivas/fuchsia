// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// We want to keep structure similar to rive-cpp.
#[allow(clippy::module_inception)]
mod animation;
mod animation_state;
mod animator;
mod any_state;
mod cubic_interpolator;
mod entry_state;
mod exit_state;
mod key_frame;
mod key_frame_color;
mod key_frame_double;
mod key_frame_id;
mod keyed_object;
mod keyed_property;
mod layer_state;
mod linear_animation;
mod linear_animation_instance;
mod r#loop;
mod state_machine;
mod state_machine_bool;
mod state_machine_component;
mod state_machine_double;
mod state_machine_input;
mod state_machine_layer;
mod state_machine_layer_component;
mod state_machine_trigger;
mod state_transition;
mod transition_bool_condition;
mod transition_condition;
mod transition_double_condition;
mod transition_trigger_condition;
mod transition_value_condition;

pub use animation::Animation;
pub use animation_state::AnimationState;
pub(crate) use animator::Animator;
pub use any_state::AnyState;
pub use cubic_interpolator::CubicInterpolator;
pub use entry_state::EntryState;
pub use exit_state::ExitState;
pub use key_frame::KeyFrame;
pub use key_frame_color::KeyFrameColor;
pub use key_frame_double::KeyFrameDouble;
pub use key_frame_id::KeyFrameId;
pub use keyed_object::KeyedObject;
pub use keyed_property::KeyedProperty;
pub use layer_state::LayerState;
pub use linear_animation::LinearAnimation;
pub use linear_animation_instance::LinearAnimationInstance;
pub use r#loop::Loop;
pub use state_machine::StateMachine;
pub use state_machine_bool::StateMachineBool;
pub use state_machine_component::StateMachineComponent;
pub use state_machine_double::StateMachineDouble;
pub use state_machine_input::StateMachineInput;
pub use state_machine_layer::StateMachineLayer;
pub use state_machine_layer_component::StateMachineLayerComponent;
pub use state_machine_trigger::StateMachineTrigger;
pub use state_transition::StateTransition;
pub use transition_bool_condition::TransitionBoolCondition;
pub use transition_condition::TransitionCondition;
pub use transition_double_condition::TransitionDoubleCondition;
pub use transition_trigger_condition::TransitionTriggerCondition;
pub use transition_value_condition::TransitionValueCondition;
