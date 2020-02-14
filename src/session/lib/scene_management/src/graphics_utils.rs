// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_images as images, fidl_fuchsia_ui_gfx as ui_gfx,
    fidl_fuchsia_ui_gfx::{Quaternion, Vec3},
    fuchsia_scenic as scenic, fuchsia_scenic,
};

/// A struct that contains all the information needed to use an image with [`scenic`].
pub struct ImageResource {
    /// The path to the image that was loaded.
    pub path: String,
    /// The width of the image.
    pub width: f32,
    /// The height of the image.
    pub height: f32,
    /// The [`scenic::Material`] containing the image information that can added to a [`scenic::ShapeNode`].
    pub material: scenic::Material,
}

impl ImageResource {
    /// Creates a new instance of `ImageResource`
    ///
    /// # Parameters
    /// - `image_path`: Path to the image file to load.
    /// - `session`: The [scenic::SessionPtr] to use for loading the image.
    ///
    /// # Returns
    /// The [`ImageResource`] containing the information for the requested image.
    ///
    /// # Errors
    /// - If image file could not be opened.
    /// - If the image file format can not be read.
    /// - If the system could not allocate memeory to store the loaded image.
    pub fn new(image_path: &str, session: scenic::SessionPtr) -> Result<Self, Error> {
        let decoder = png::Decoder::new(std::fs::File::open(image_path)?);
        let (info, mut reader) = decoder.read_info()?;
        let mut buf = vec![0; info.buffer_size()];
        reader.next_frame(&mut buf)?;
        let px_size_bytes = std::mem::size_of::<u8>() * 4; // RGBA

        let (width, height) = (info.width, info.height);
        let size_bytes = width as usize * height as usize * px_size_bytes;
        let image_info = images::ImageInfo {
            transform: images::Transform::Normal,
            width,
            height,
            stride: width * px_size_bytes as u32,
            pixel_format: images::PixelFormat::Bgra8,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::NonPremultiplied,
        };
        let host_memory = scenic::HostMemory::allocate(session.clone(), size_bytes)?;
        let host_image = scenic::HostImage::new(&host_memory, 0, image_info);

        // swizzle RGBA to BGRA
        for i in (0..size_bytes).step_by(px_size_bytes) {
            let (r, g, b, a) = (buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
            buf[i] = b;
            buf[i + 1] = g;
            buf[i + 2] = r;
            buf[i + 3] = a;
        }

        host_image.mapping().write(&buf);

        let material = scenic::Material::new(session.clone());
        material.set_texture(Some(&host_image));
        material.set_color(ui_gfx::ColorRgba { red: 255, green: 255, blue: 255, alpha: 250 });

        Ok(ImageResource {
            path: image_path.to_owned(),
            width: width as f32,
            height: height as f32,
            material,
        })
    }
}

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
