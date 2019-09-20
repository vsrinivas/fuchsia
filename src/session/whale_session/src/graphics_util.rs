// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_gfx::{Quaternion, Vec3};

/// Creates a quaternion representation from an axis-angle representation of the specified rotation.
///
/// # Parameters
/// - `axis`: The direction of the axis of rotation.
/// - `angle`: The angle around the axis of rotation.
///
/// # Returns
/// A `Quaternion` representing the rotation.
pub fn quaternion_from_axis_angle(axis: Vec3, angle: f32) -> Quaternion {
    let half_angle = angle / 2.0;
    let sin_half_angle = half_angle.sin();
    Quaternion {
        x: axis.x * sin_half_angle,
        y: axis.y * sin_half_angle,
        z: axis.z * sin_half_angle,
        w: half_angle.cos(),
    }
}
