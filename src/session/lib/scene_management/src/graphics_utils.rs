// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display_metrics::DisplayMetrics,
    anyhow::Error,
    fidl_fuchsia_images as images, fidl_fuchsia_ui_gfx as ui_gfx, fuchsia_scenic as scenic,
    fuchsia_scenic,
    input::{Position, Size},
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
        let pwidth_size_bytes = std::mem::size_of::<u8>() * 4; // RGBA

        let (width, height) = (info.width, info.height);
        let size_bytes = width as usize * height as usize * pwidth_size_bytes;
        let image_info = images::ImageInfo {
            transform: images::Transform::Normal,
            width,
            height,
            stride: width * pwidth_size_bytes as u32,
            pixel_format: images::PixelFormat::Bgra8,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::NonPremultiplied,
        };
        let host_memory = scenic::HostMemory::allocate(session.clone(), size_bytes)?;
        let host_image = scenic::HostImage::new(&host_memory, 0, image_info);

        // swizzle RGBA to BGRA
        for i in (0..size_bytes).step_by(pwidth_size_bytes) {
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

/// [`ScreenCoordinates`] represents a point on the screen. It can be created from pixels, pips, or
/// millimeters and the coordinate vales can be retrieved in any of those units.
#[derive(Clone, Copy, Debug)]
pub struct ScreenCoordinates {
    x_pixels: f32,
    y_pixels: f32,
    display_metrics: DisplayMetrics,
}

#[allow(dead_code)]
impl ScreenCoordinates {
    /// Create a [`ScreenCoordinates`] from the given pixel corrdinates
    pub fn from_pixels(x: f32, y: f32, display_metrics: DisplayMetrics) -> Self {
        Self { x_pixels: x, y_pixels: y, display_metrics }
    }

    /// Create a [`ScreenCoordinates`] from the given pip corrdinates
    pub fn from_pips(x: f32, y: f32, display_metrics: DisplayMetrics) -> Self {
        Self {
            x_pixels: x * display_metrics.pixels_per_pip(),
            y_pixels: y * display_metrics.pixels_per_pip(),
            display_metrics,
        }
    }

    /// Create a [`ScreenCoordinates`] from the given millimeter corrdinates
    pub fn from_mm(x: f32, y: f32, display_metrics: DisplayMetrics) -> Self {
        Self {
            x_pixels: x * display_metrics.pixels_per_pip() * display_metrics.mm_per_pip(),
            y_pixels: y * display_metrics.pixels_per_pip() * display_metrics.mm_per_pip(),
            display_metrics,
        }
    }

    /// This takes a Position struct and maps it to ScreenCoordinates
    pub fn from_position(position: &Position, display_metrics: DisplayMetrics) -> Self {
        Self { x_pixels: position.x, y_pixels: position.y, display_metrics }
    }

    /// Retreive the x and y positions as pixel values
    pub fn pixels(&self) -> (f32, f32) {
        (self.x_pixels, self.y_pixels)
    }

    /// Retreive the x and y positions as pip values
    pub fn pips(&self) -> (f32, f32) {
        (
            self.x_pixels / self.display_metrics.pixels_per_pip(),
            self.y_pixels / self.display_metrics.pixels_per_pip(),
        )
    }

    /// Retreive the x and y positions as millimeter values
    pub fn mm(&self) -> (f32, f32) {
        let (x_pips, y_pips) = self.pips();
        (x_pips / self.display_metrics.mm_per_pip(), y_pips / self.display_metrics.mm_per_pip())
    }

    /// Retreive the x and y positions as a [`Position`]
    pub fn position(&self) -> Position {
        Position { x: self.x_pixels, y: self.y_pixels }
    }
}

/// [`ScreenSize`] represents a size on the screen. It can be created from pixels, pips, or
/// millimeters and can be retrieved in any of those units.
#[derive(Clone, Copy, Debug)]
pub struct ScreenSize {
    width_pixels: f32,
    height_pixels: f32,
    display_metrics: DisplayMetrics,
}

#[allow(dead_code)]
impl ScreenSize {
    /// Create a [`ScreenSize`] from the given pixel corrdinates
    pub fn from_pixels(width: f32, height: f32, display_metrics: DisplayMetrics) -> Self {
        Self { width_pixels: width, height_pixels: height, display_metrics }
    }

    /// Create a [`ScreenSize`] from the given pip corrdinates
    pub fn from_pips(width: f32, height: f32, display_metrics: DisplayMetrics) -> Self {
        Self {
            width_pixels: width * display_metrics.pixels_per_pip(),
            height_pixels: height * display_metrics.pixels_per_pip(),
            display_metrics,
        }
    }

    /// Create a [`ScreenSize`] from the given millimeter corrdinates
    pub fn from_mm(width: f32, height: f32, display_metrics: DisplayMetrics) -> Self {
        Self {
            width_pixels: width * display_metrics.pixels_per_pip() * display_metrics.mm_per_pip(),
            height_pixels: height * display_metrics.pixels_per_pip() * display_metrics.mm_per_pip(),
            display_metrics,
        }
    }

    /// This takes a Position struct and maps it to ScreenSize
    pub fn from_size(size: &Size, display_metrics: DisplayMetrics) -> Self {
        Self { width_pixels: size.width, height_pixels: size.height, display_metrics }
    }

    /// Retreive the width and height as pixel values
    pub fn pixels(&self) -> (f32, f32) {
        (self.width_pixels, self.height_pixels)
    }

    /// Retreive the width and height as pip values
    pub fn pips(&self) -> (f32, f32) {
        (
            self.width_pixels / self.display_metrics.pixels_per_pip(),
            self.height_pixels / self.display_metrics.pixels_per_pip(),
        )
    }

    /// Retreive the width and height as millimeter values
    pub fn mm(&self) -> (f32, f32) {
        let (width_pips, height_pips) = self.pips();
        (
            width_pips / self.display_metrics.mm_per_pip(),
            height_pips / self.display_metrics.mm_per_pip(),
        )
    }

    /// Retreive the width and height as a [`Size`]
    pub fn size(&self) -> Size {
        Size { width: self.width_pixels, height: self.height_pixels }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[inline]
    fn assert_close_enough(left: (f32, f32), right: (f32, f32)) {
        const EPSILON: f32 = 0.0001;
        assert!(left.0 > right.0 - EPSILON, format!("Left: {:?} Right: {:?}", left, right));
        assert!(left.0 < right.0 + EPSILON, format!("Left: {:?} Right: {:?}", left, right));
        assert!(left.1 > right.1 - EPSILON, format!("Left: {:?} Right: {:?}", left, right));
        assert!(left.1 < right.1 + EPSILON, format!("Left: {:?} Right: {:?}", left, right));
    }

    #[inline]
    fn assert_postion_close_enough(left: Position, right: Position) {
        assert_close_enough((left.x, left.y), (right.x, right.y));
    }

    #[test]
    fn test_pixel_constructor() {
        let display_metrics =
            DisplayMetrics::new(Size { width: 1000.0, height: 500.0 }, Some(10.0), None, None);
        let coords = ScreenCoordinates::from_pixels(100.0, 100.0, display_metrics);

        assert_close_enough(coords.pixels(), (100.0, 100.0));
        assert_close_enough(coords.pips(), (52.2449, 52.2449));
        assert_close_enough(coords.mm(), (272.9529, 272.9529));
        assert_postion_close_enough(coords.position(), Position { x: 100.0, y: 100.0 });
    }

    #[test]
    fn test_pip_constructor() {
        let display_metrics =
            DisplayMetrics::new(Size { width: 1000.0, height: 500.0 }, Some(10.0), None, None);
        let coords = ScreenCoordinates::from_pips(52.2449, 52.2449, display_metrics);

        assert_close_enough(coords.pixels(), (100.0, 100.0));
        assert_close_enough(coords.pips(), (52.2449, 52.2449));
        assert_close_enough(coords.mm(), (272.9529, 272.9529));
        assert_postion_close_enough(coords.position(), Position { x: 100.0, y: 100.0 });
    }

    #[test]
    fn test_mm_constructor() {
        let display_metrics =
            DisplayMetrics::new(Size { width: 1000.0, height: 500.0 }, Some(10.0), None, None);
        let coords = ScreenCoordinates::from_mm(272.9529, 272.9529, display_metrics);

        assert_close_enough(coords.pixels(), (100.0, 100.0));
        assert_close_enough(coords.pips(), (52.2449, 52.2449));
        assert_close_enough(coords.mm(), (272.9529, 272.9529));
        assert_postion_close_enough(coords.position(), Position { x: 100.0, y: 100.0 });
    }

    #[test]
    fn test_position_constructor() {
        let display_metrics =
            DisplayMetrics::new(Size { width: 1000.0, height: 500.0 }, Some(10.0), None, None);
        let coords =
            ScreenCoordinates::from_position(&Position { x: 100.0, y: 100.0 }, display_metrics);

        assert_close_enough(coords.pixels(), (100.0, 100.0));
        assert_close_enough(coords.pips(), (52.2449, 52.2449));
        assert_close_enough(coords.mm(), (272.9529, 272.9529));
        assert_postion_close_enough(coords.position(), Position { x: 100.0, y: 100.0 });
    }
}
