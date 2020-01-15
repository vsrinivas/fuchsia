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

#[cfg(test)]
mod tests {
    use super::*;

    const RADS_FROM_90_DEG: f32 = 90.0 * (3.14159 / 180.0);
    const SIN_45_DEG: f32 = 0.7071;

    fn compare_quaternions(q1: &Quaternion, q2: &Quaternion) {
        const EPSILON: f32 = 0.0001;

        if q1.x - q2.x < -EPSILON
            || q1.x - q2.x > EPSILON
            || q1.y - q2.y < -EPSILON
            || q1.y - q2.y > EPSILON
            || q1.z - q2.z < -EPSILON
            || q1.z - q2.z > EPSILON
            || q1.w - q2.w < -EPSILON
            || q1.w - q2.w > EPSILON
        {
            panic!(
                "Left value: {{ {}, {}, {}, {} }} Right value: {{ {}, {}, {}, {} }}",
                q1.x, q1.y, q1.z, q1.w, q2.x, q2.y, q2.z, q2.w
            );
        }
    }

    #[test]
    fn quaternion_from_0_degrees() {
        let expected_quat = Quaternion { x: 0.0, y: 0.0, z: 0.0, w: 1.0 };
        let actual_quat = quaternion_from_axis_angle(Vec3 { x: 0.0, y: 0.0, z: 0.0 }, 0.0);
        compare_quaternions(&expected_quat, &actual_quat);
    }

    #[test]
    fn quaternion_from_90_degrees_x() {
        let expected_quat = Quaternion { x: SIN_45_DEG, y: 0.0, z: 0.0, w: SIN_45_DEG };
        let actual_quat =
            quaternion_from_axis_angle(Vec3 { x: 1.0, y: 0.0, z: 0.0 }, RADS_FROM_90_DEG);
        compare_quaternions(&expected_quat, &actual_quat);
    }

    #[test]
    fn quaternion_from_90_degrees_y() {
        let expected_quat = Quaternion { x: 0.0, y: SIN_45_DEG, z: 0.0, w: SIN_45_DEG };
        let actual_quat =
            quaternion_from_axis_angle(Vec3 { x: 0.0, y: 1.0, z: 0.0 }, RADS_FROM_90_DEG);
        compare_quaternions(&expected_quat, &actual_quat);
    }

    #[test]
    fn quaternion_from_90_degrees_z() {
        let expected_quat = Quaternion { x: 0.0, y: 0.0, z: SIN_45_DEG, w: SIN_45_DEG };
        let actual_quat =
            quaternion_from_axis_angle(Vec3 { x: 0.0, y: 0.0, z: 1.0 }, RADS_FROM_90_DEG);
        compare_quaternions(&expected_quat, &actual_quat);
    }

    #[test]
    fn quaternion_from_90_degrees_xy() {
        let expected_quat = Quaternion { x: SIN_45_DEG, y: SIN_45_DEG, z: 0.0, w: SIN_45_DEG };
        let actual_quat =
            quaternion_from_axis_angle(Vec3 { x: 1.0, y: 1.0, z: 0.0 }, RADS_FROM_90_DEG);
        compare_quaternions(&expected_quat, &actual_quat);
    }

    #[test]
    fn quaternion_from_90_degrees_xz() {
        let expected_quat = Quaternion { x: SIN_45_DEG, y: 0.0, z: SIN_45_DEG, w: SIN_45_DEG };
        let actual_quat =
            quaternion_from_axis_angle(Vec3 { x: 1.0, y: 0.0, z: 1.0 }, RADS_FROM_90_DEG);
        compare_quaternions(&expected_quat, &actual_quat);
    }

    #[test]
    fn quaternion_from_90_degrees_yz() {
        let expected_quat = Quaternion { x: 0.0, y: SIN_45_DEG, z: SIN_45_DEG, w: SIN_45_DEG };
        let actual_quat =
            quaternion_from_axis_angle(Vec3 { x: 0.0, y: 1.0, z: 1.0 }, RADS_FROM_90_DEG);
        compare_quaternions(&expected_quat, &actual_quat);
    }
}
