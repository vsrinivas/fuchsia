// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// [`DisplayMetrics`] encapsulate data associated with a display device.
///
/// [`DisplayMetrics`] are created from a display's width and height in pixels.
/// Pixel density and expected viewing distance can be supplied for more accurate
/// metrics (e.g., [`width_in_mm`] uses the display's pixel density to give the correct width).
///
/// If density or viewing distance is not supplied, default values are calculated based on the
/// display dimensions.
#[derive(Debug)]
pub struct DisplayMetrics {
    /// The width of the display in pixels.
    width_in_pixels: u32,

    /// The height of the display in pixels.
    height_in_pixels: u32,

    /// The pixel density of the display. This is either supplied by the client constructing
    /// the display metrics, or a hard-coded default is used based on the display dimensions.
    display_pixel_density: f32,

    /// The expected viewing distance for the display, in millimeters. For example, a desktop
    /// monitor may have an expected viewing distance of around 500 mm.
    viewing_distance: f32,
}

impl DisplayMetrics {
    /// The default viewing distance for a display, used if no expected viewing distance is
    /// supplied when constructing the metrics.
    const DEFAULT_VIEWING_DISTANCE: f32 = 500.0;

    /// Creates a new [`DisplayMetrics`] struct.
    ///
    /// The width and height of the display in pixels are required to construct sensible display
    /// metrics. Defaults can be computed for the other metrics, but they may not match expectations.
    ///
    /// For example, a default display pixel density can be determined based on width and height in
    /// pixels, but it's unlikely to match the actual density of the display.
    ///
    /// # Parameters
    /// - `width_in_pixels`: The width of the display, in pixels.
    /// - `height_in_pixels`: The height of the display, in pixels.
    /// - `display_pixel_density`: The density of the display, in pixels per mm. If no density is
    /// provided, a best guess is made based on the width and height of the display.
    /// - `viewing_distance`: The expected viewing distance for the display (i.e., how far away the
    /// user is expected to be from the display) in mm. Defaults to [`DisplayMetrics::DEFAULT_VIEWING_DISTANCE`].
    /// This is used to compute the ratio of pixels per pip.
    pub fn new(
        width_in_pixels: u32,
        height_in_pixels: u32,
        display_pixel_density: Option<f32>,
        viewing_distance: Option<f32>,
    ) -> DisplayMetrics {
        let mut display_pixel_density = display_pixel_density.unwrap_or_else(|| {
            DisplayMetrics::default_display_pixel_density(width_in_pixels, height_in_pixels)
        });
        if display_pixel_density == 0.0 {
            display_pixel_density =
                DisplayMetrics::default_display_pixel_density(width_in_pixels, height_in_pixels);
        }

        let mut viewing_distance =
            viewing_distance.unwrap_or_else(|| DisplayMetrics::DEFAULT_VIEWING_DISTANCE);
        if viewing_distance == 0.0 {
            viewing_distance = DisplayMetrics::DEFAULT_VIEWING_DISTANCE;
        }

        DisplayMetrics {
            width_in_pixels,
            height_in_pixels,
            display_pixel_density,
            viewing_distance,
        }
    }

    /// Returns the width of the display in pixels.
    pub fn width_in_pixels(&self) -> u32 {
        self.width_in_pixels
    }

    /// Returns the height of the display in pixels.
    pub fn height_in_pixels(&self) -> u32 {
        self.height_in_pixels
    }

    /// Returns the width of the display in pips.
    pub fn width_in_pips(&self) -> f32 {
        self.width_in_pixels as f32 / self.pixels_per_pip()
    }

    /// Returns the height of the display in pips.
    pub fn height_in_pips(&self) -> f32 {
        self.height_in_pixels as f32 / self.pixels_per_pip()
    }

    /// Returns the width of the display in millimeters.
    pub fn width_in_mm(&self) -> f32 {
        self.width_in_pips() * self.mm_per_pip()
    }

    /// Returns the height of the display in millimeters.
    pub fn height_in_mm(&self) -> f32 {
        self.height_in_pips() * self.mm_per_pip()
    }

    /// Returns the number of pixels per pip.
    pub fn pixels_per_pip(&self) -> f32 {
        // The ideal visual angle of a pip unit in degrees, assuming default settings.
        // The value has been empirically determined.
        const IDEAL_VISUAL_ANGLE: f32 = 0.0255;

        // Compute the pixel's visual size.
        let pixel_visual_size = 1.0 / (self.display_pixel_density * self.viewing_distance);

        // The adaption factor is an empirically determined fudge factor to take into account
        // human perceptual differences for objects at varying distances, even if those objects
        // are adjusted to be the same size to the eye.
        let adaption_factor = (self.viewing_distance * 0.5 + 180.0) / self.viewing_distance;

        // Compute the pip's visual size.
        let pip_visual_size =
            (IDEAL_VISUAL_ANGLE * std::f32::consts::PI / 180.0).tan() * adaption_factor;

        if pixel_visual_size == 0.0 {
            return 1.0;
        }

        pip_visual_size / pixel_visual_size
    }

    /// Returns the number of pips per millimeter.
    pub fn pips_per_mm(&self) -> f32 {
        self.display_pixel_density / self.pixels_per_pip()
    }

    /// Returns the number of millimeters per pip.
    pub fn mm_per_pip(&self) -> f32 {
        if self.pips_per_mm() == 0.0 {
            0.0
        } else {
            1.0 / self.pips_per_mm()
        }
    }

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// an Acer Switch 12 Alpha. Used to set a default display pixel density.
    const ACER_SWITCH_12_ALPHA_DIMENSIONS: (u32, u32) = (2160, 1440);

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// a Google Pixelbook. Used to set a default display pixel density.
    const GOOGLE_PIXELBOOK_DIMENSIONS: (u32, u32) = (2400, 1600);

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// a 24 inch 4k monitor. Used to set a default display pixel density.
    const MONITOR_24_IN_4K_DIMENSIONS: (u32, u32) = (3840, 2160);

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// a 24 inch monitor. Used to set a default display pixel density.
    const MONITOR_24_IN_DIMENSIONS: (u32, u32) = (1920, 1200);

    /// The display pixel density used for an Acer Switch 12 Alpha.
    const ACER_SWITCH_12_ALPHA_DENSITY: f32 = 8.5;

    /// The display pixel density used for a Google Pixelbook.
    const GOOGLE_PIXELBOOK_DENSITY: f32 = 9.252;

    /// The display pixel density used for a 24 inch 4K monitor.
    const MONITOR_24_IN_4K_DENSITY: f32 = 7.323761;

    /// The display pixel density used for a 24 inch monitor.
    const MONITOR_24_IN_DENSITY: f32 = 4.16;

    /// The display pixel density used as default when no other default device matches.
    const DEFAULT_DENSITY: f32 = 9.0;

    /// Returns a default display pixel density based on the provided display dimensions.
    ///
    /// The pixel density is defined as pixels per millimeters.
    ///
    /// Clients using a `SceneManager` are expected to provide the pixel density for the display,
    /// but this provides reasonable defaults for a few commonly used devices.
    ///
    /// # Parameters
    /// - `width_in_pixels`: The width of the display in pixels.
    /// - `height_in_pixels`: The height of the display in pixels.
    fn default_display_pixel_density(width_in_pixels: u32, height_in_pixels: u32) -> f32 {
        match (width_in_pixels, height_in_pixels) {
            DisplayMetrics::ACER_SWITCH_12_ALPHA_DIMENSIONS => {
                DisplayMetrics::ACER_SWITCH_12_ALPHA_DENSITY
            }
            DisplayMetrics::GOOGLE_PIXELBOOK_DIMENSIONS => DisplayMetrics::GOOGLE_PIXELBOOK_DENSITY,
            DisplayMetrics::MONITOR_24_IN_4K_DIMENSIONS => DisplayMetrics::MONITOR_24_IN_4K_DENSITY,
            DisplayMetrics::MONITOR_24_IN_DIMENSIONS => DisplayMetrics::MONITOR_24_IN_DENSITY,
            _ => DisplayMetrics::DEFAULT_DENSITY,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Density is used as the denominator in pip calculation, so must be handled explicitly.
    #[test]
    fn test_zero_density() {
        let metrics = DisplayMetrics::new(100, 100, Some(0.0), None);
        let second_metrics = DisplayMetrics::new(100, 100, None, None);
        assert_eq!(metrics.width_in_pips(), second_metrics.width_in_pips());
        assert_eq!(metrics.height_in_pips(), second_metrics.height_in_pips());
    }

    // Viewing distance is used as the denominator in pip calculation, so must be handled explicitly.
    #[test]
    fn test_zero_distance() {
        let metrics = DisplayMetrics::new(100, 100, None, Some(0.0));
        let second_metrics = DisplayMetrics::new(100, 100, None, None);
        assert_eq!(metrics.width_in_pips(), second_metrics.width_in_pips());
        assert_eq!(metrics.height_in_pips(), second_metrics.height_in_pips());
    }

    // Tests that a known default density produces the same metrics as explicitly specified.
    #[test]
    fn test_pixels_per_pip_default() {
        let dimensions = DisplayMetrics::ACER_SWITCH_12_ALPHA_DIMENSIONS;
        let metrics = DisplayMetrics::new(dimensions.0, dimensions.1, None, None);
        let second_metrics = DisplayMetrics::new(
            dimensions.0,
            dimensions.1,
            Some(DisplayMetrics::ACER_SWITCH_12_ALPHA_DENSITY),
            Some(DisplayMetrics::DEFAULT_VIEWING_DISTANCE),
        );
        assert_eq!(metrics.width_in_pips(), second_metrics.width_in_pips());
        assert_eq!(metrics.height_in_pips(), second_metrics.height_in_pips());

        // The expected values here were generated and tested manually to be the expected
        // values for the Acer Switch 12 Alpha.
        assert_eq!(metrics.width_in_pips(), 1327.8492);
        assert_eq!(metrics.height_in_pips(), 885.2328);
    }
}
