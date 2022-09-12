// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display_metrics::DisplayMetrics,
    input_pipeline::{Position, Size},
};

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
        assert!(left.0 > right.0 - EPSILON, "Left: {:?} Right: {:?}", left, right);
        assert!(left.0 < right.0 + EPSILON, "Left: {:?} Right: {:?}", left, right);
        assert!(left.1 > right.1 - EPSILON, "Left: {:?} Right: {:?}", left, right);
        assert!(left.1 < right.1 + EPSILON, "Left: {:?} Right: {:?}", left, right);
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
