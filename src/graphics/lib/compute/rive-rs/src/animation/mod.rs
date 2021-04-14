// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod animation;
mod animator;
mod cubic_interpolator;
mod key_frame;
mod key_frame_color;
mod key_frame_double;
mod key_frame_id;
mod keyed_object;
mod keyed_property;
mod linear_animation;
mod linear_animation_instance;
mod r#loop;

pub use animation::Animation;
pub(crate) use animator::Animator;
pub use cubic_interpolator::CubicInterpolator;
pub use key_frame::KeyFrame;
pub use key_frame_color::KeyFrameColor;
pub use key_frame_double::KeyFrameDouble;
pub use key_frame_id::KeyFrameId;
pub use keyed_object::KeyedObject;
pub use keyed_property::KeyedProperty;
pub use linear_animation::LinearAnimation;
pub use linear_animation_instance::LinearAnimationInstance;
pub use r#loop::Loop;
