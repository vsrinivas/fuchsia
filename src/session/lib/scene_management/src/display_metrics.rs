// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_scenic::DisplayRotation, std::f32::consts as f32};

// TODO(fxb/45535): Replace native C calls in display_metrics.rs with rust-lang/libm.
// Wrap native implementations of libm functions until rust-lang/libm is adopted into Fuchsia's
// third_party/rust_crates.
extern "C" {
    fn frexpf(x: f32, exp: *mut i32) -> f32;

    fn ldexpf(x: f32, n: i32) -> f32;
}

#[inline]
fn libm_frexpf(x: f32) -> (f32, i32) {
    let mut exp: i32 = 0;
    let v = unsafe { frexpf(x, &mut exp) };
    (v, exp)
}

#[inline]
fn libm_ldexpf(x: f32, n: i32) -> f32 {
    unsafe { ldexpf(x, n) }
}

/// Predefined viewing distances with values in millimeters.
#[derive(Copy, Clone, PartialEq, Debug)]
pub enum ViewingDistance {
    Handheld = 360,
    Close = 500,
    Near = 720,
    Midrange = 1200,
    Far = 3000,
    Unknown = 600, // Should not be used, but offers a reasonable, non-zero (and unique) default
}

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
    density_in_pixels_per_mm: f32,

    /// The expected viewing distance for the display, in millimeters. For example, a desktop
    /// monitor may have an expected viewing distance of around 500 mm.
    viewing_distance: ViewingDistance,

    /// The screen rotation: 0 (none), 90, 180, or 270.
    display_rotation: DisplayRotation,

    /// The pip scale factor in pixels per pip in either X or Y dimension.
    /// (Assumes square pixels.)
    scale_in_pixels_per_pip: f32,

    /// The pip density in pips per millimeter.
    density_in_pips_per_mm: f32,
}

/// Quantizes the specified floating point number to 8 significant bits of
/// precision in its mantissa (including the implicit leading 1 bit).
///
/// We quantize scale factors to reduce the likelihood of round-off errors in
/// subsequent calculations due to excess precision.  Since IEEE 754 float
/// has 24 significant bits, by using only 8 significant bits for the scaling
/// factor we're guaranteed that we can multiply the factor by any integer
/// between -65793 and 65793 without any loss of precision.  The scaled integers
/// can likewise be added or subtracted without any loss of precision.
fn quantize(f: f32) -> f32 {
    let (frac, exp) = libm_frexpf(f);
    libm_ldexpf((frac as f64 * 256.0).round() as f32, exp - 8)
}

// TODO(fxb/45535): Replace native C calls in display_metrics.rs with rust-lang/libm.
// If rust-lang/libm is added to third_party/rust_crates, remove the unsafe extern C
// implementations and change "libm_" to "libm::". (Also, if needed, "use libm".)
// fn quantize(f: f32) -> f32 {
//     let (frac, exp) = libm::frexpf(f);
//     libm::ldexpf((frac as f64 * 256.0).round() as f32, exp - 8)
// }

impl DisplayMetrics {
    /// The ideal visual angle of a pip unit in degrees, assuming default settings.
    /// The value has been empirically determined.
    const IDEAL_PIP_VISUAL_ANGLE_DEGREES: f32 = 0.0255;

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
    /// - `density_in_pixels_per_mm`: The density of the display, in pixels per mm. If no density is
    /// provided, a best guess is made based on the width and height of the display.
    /// - `viewing_distance`: The expected viewing distance for the display (i.e., how far away the
    /// user is expected to be from the display). Defaults to [`DisplayMetrics::DEFAULT_VIEWING_DISTANCE`].
    /// This is used to compute the ratio of pixels per pip.
    /// - `display_rotation`: Stores the current rotation setting for the screen. This is typically
    /// used to rotate scenic rendering by 90, 180, or 270 degrees if the native hardware screen
    /// orientation is different from the intended physical orientation (e.g, a portrait mode screen
    /// on its side, to render in landscape).
    pub fn new(
        width_in_pixels: u32,
        height_in_pixels: u32,
        density_in_pixels_per_mm: Option<f32>,
        viewing_distance: Option<ViewingDistance>,
        display_rotation: Option<DisplayRotation>,
    ) -> DisplayMetrics {
        let mut density_in_pixels_per_mm = density_in_pixels_per_mm.unwrap_or_else(|| {
            Self::default_density_in_pixels_per_mm(width_in_pixels, height_in_pixels)
        });
        if density_in_pixels_per_mm == 0.0 {
            density_in_pixels_per_mm =
                Self::default_density_in_pixels_per_mm(width_in_pixels, height_in_pixels);
        }

        let mut viewing_distance = viewing_distance
            .unwrap_or_else(|| Self::default_viewing_distance(width_in_pixels, height_in_pixels));
        if viewing_distance == ViewingDistance::Unknown {
            viewing_distance = Self::default_viewing_distance(width_in_pixels, height_in_pixels);
        }
        let viewing_distance_in_mm = viewing_distance as u32 as f32;

        let display_rotation = match display_rotation {
            Some(rotation) => rotation,
            None => DisplayRotation::None,
        };

        assert!(density_in_pixels_per_mm != 0.0);
        assert!(viewing_distance_in_mm != 0.0);

        let scale_in_pixels_per_pip =
            Self::compute_scale(density_in_pixels_per_mm, viewing_distance_in_mm);
        let density_in_pips_per_mm = density_in_pixels_per_mm / scale_in_pixels_per_pip;

        DisplayMetrics {
            width_in_pixels,
            height_in_pixels,
            density_in_pixels_per_mm,
            viewing_distance,
            display_rotation,
            scale_in_pixels_per_pip,
            density_in_pips_per_mm,
        }
    }

    /// Computes and returns `scale_in_pixels_per_pip`.
    ///
    /// # Parameters
    /// - `density_in_pixels_per_mm`: The density of the display as given, or the default (see
    /// `new()`).
    /// - `viewing_distance_in_mm`: The expected viewing distance for the display (i.e., how far
    /// away the user is expected to be from the display) as given, or the default (see `new()`).
    ///
    /// Returns the computed scale ratio in pixels per pip.
    fn compute_scale(density_in_pixels_per_mm: f32, viewing_distance_in_mm: f32) -> f32 {
        // Compute the pixel visual size as a function of viewing distance in
        // millimeters per millimeter.
        let pvsize_in_mm_per_mm = 1.0 / (density_in_pixels_per_mm * viewing_distance_in_mm);

        // The adaption factor is an empirically determined fudge factor to take into account
        // human perceptual differences for objects at varying distances, even if those objects
        // are adjusted to be the same size to the eye.
        let adaptation_factor = (viewing_distance_in_mm * 0.5 + 180.0) / viewing_distance_in_mm;

        // Compute the pip visual size as a function of viewing distance in
        // millimeters per millimeter.
        let pip_visual_size_in_mm_per_mm =
            (Self::IDEAL_PIP_VISUAL_ANGLE_DEGREES * f32::PI / 180.0).tan() * adaptation_factor;

        quantize(pip_visual_size_in_mm_per_mm / pvsize_in_mm_per_mm)
    }

    /// Returns the number of pixels per pip.
    #[inline]
    pub fn pixels_per_pip(&self) -> f32 {
        self.scale_in_pixels_per_pip
    }

    /// Returns the number of pips per millimeter.
    #[inline]
    pub fn pips_per_mm(&self) -> f32 {
        self.density_in_pips_per_mm
    }

    /// Returns the number of millimeters per pip.
    #[inline]
    pub fn mm_per_pip(&self) -> f32 {
        1.0 / self.pips_per_mm()
    }
    /// Returns the width of the display in pixels.
    #[inline]
    pub fn width_in_pixels(&self) -> u32 {
        self.width_in_pixels
    }

    /// Returns the height of the display in pixels.
    #[inline]
    pub fn height_in_pixels(&self) -> u32 {
        self.height_in_pixels
    }

    /// Returns the width of the display in pips.
    #[inline]
    pub fn width_in_pips(&self) -> f32 {
        self.width_in_pixels as f32 / self.pixels_per_pip()
    }

    /// Returns the height of the display in pips.
    #[inline]
    pub fn height_in_pips(&self) -> f32 {
        self.height_in_pixels as f32 / self.pixels_per_pip()
    }

    /// Returns the width of the display in millimeters.
    #[inline]
    pub fn width_in_mm(&self) -> f32 {
        self.width_in_pips() * self.mm_per_pip()
    }

    /// Returns the height of the display in millimeters.
    #[inline]
    pub fn height_in_mm(&self) -> f32 {
        self.height_in_pips() * self.mm_per_pip()
    }

    #[inline]
    pub fn rotation(&self) -> DisplayRotation {
        self.display_rotation
    }

    #[inline]
    pub fn rotation_in_degrees(&self) -> f32 {
        self.display_rotation as u32 as f32
    }

    #[inline]
    pub fn viewing_distance(&self) -> ViewingDistance {
        self.viewing_distance
    }

    #[inline]
    pub fn viewing_distance_in_mm(&self) -> f32 {
        self.viewing_distance as u32 as f32
    }

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// an Acer Switch 12 Alpha. Used to set a default display pixel density.
    const ACER_SWITCH_12_ALPHA_DIMENSIONS: (u32, u32) = (2160, 1440);

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// a Google Pixelbook. Used to set a default display pixel density.
    const GOOGLE_PIXELBOOK_DIMENSIONS: (u32, u32) = (2400, 1600);

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// a Google Pixelbook Go with a 2K display. Used to set a default display pixel density.
    const GOOGLE_PIXELBOOK_GO_2K_DIMENSIONS: (u32, u32) = (1920, 1080);

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// a 24 inch 4k monitor. Used to set a default display pixel density.
    const MONITOR_24_IN_4K_DIMENSIONS: (u32, u32) = (3840, 2160);

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// a 24 inch monitor. Used to set a default display pixel density.
    const MONITOR_24_IN_DIMENSIONS: (u32, u32) = (1920, 1200);

    /// The dimensions used to determine whether or not the device dimensions correspond to
    /// a 27 inch, 2K monitor. Used to set a default display pixel density.
    const MONITOR_27_IN_2K_DIMENSIONS: (u32, u32) = (2560, 1440);

    /// The display pixel density used for an Acer Switch 12 Alpha.
    const ACER_SWITCH_12_ALPHA_DENSITY: f32 = 8.5;

    /// The display pixel density used for a Google Pixelbook.
    const GOOGLE_PIXELBOOK_DENSITY: f32 = 9.252;

    /// The display pixel density used for a Google Pixelbook Go with a 2K display.
    const GOOGLE_PIXELBOOK_GO_2K_DENSITY: f32 = 6.5;

    /// The display pixel density used for a 24 inch 4K monitor.
    const MONITOR_24_IN_4K_DENSITY: f32 = 7.323761;

    /// The display pixel density used for a 24 inch monitor.
    const MONITOR_24_IN_DENSITY: f32 = 4.16;

    // TODO(fxb/42794): Allow Root Presenter clients to specify exact pixel ratio
    /// The display pixel density used for a 27 inch monitor.
    const MONITOR_27_IN_2K_DENSITY: f32 = 5.22;

    // TODO(SCN-384): Don't lie.
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
    fn default_density_in_pixels_per_mm(width_in_pixels: u32, height_in_pixels: u32) -> f32 {
        match (width_in_pixels, height_in_pixels) {
            DisplayMetrics::ACER_SWITCH_12_ALPHA_DIMENSIONS => {
                DisplayMetrics::ACER_SWITCH_12_ALPHA_DENSITY
            }
            DisplayMetrics::GOOGLE_PIXELBOOK_DIMENSIONS => DisplayMetrics::GOOGLE_PIXELBOOK_DENSITY,
            DisplayMetrics::GOOGLE_PIXELBOOK_GO_2K_DIMENSIONS => {
                DisplayMetrics::GOOGLE_PIXELBOOK_GO_2K_DENSITY
            }
            DisplayMetrics::MONITOR_24_IN_4K_DIMENSIONS => DisplayMetrics::MONITOR_24_IN_4K_DENSITY,
            DisplayMetrics::MONITOR_24_IN_DIMENSIONS => DisplayMetrics::MONITOR_24_IN_DENSITY,
            DisplayMetrics::MONITOR_27_IN_2K_DIMENSIONS => DisplayMetrics::MONITOR_27_IN_2K_DENSITY,
            _ => DisplayMetrics::DEFAULT_DENSITY,
        }
    }

    fn default_viewing_distance(width_in_pixels: u32, height_in_pixels: u32) -> ViewingDistance {
        match (width_in_pixels, height_in_pixels) {
            DisplayMetrics::ACER_SWITCH_12_ALPHA_DIMENSIONS => ViewingDistance::Close,
            DisplayMetrics::GOOGLE_PIXELBOOK_DIMENSIONS => ViewingDistance::Close,
            DisplayMetrics::MONITOR_24_IN_4K_DIMENSIONS => ViewingDistance::Near,
            DisplayMetrics::MONITOR_24_IN_DIMENSIONS => ViewingDistance::Near,
            DisplayMetrics::MONITOR_27_IN_2K_DIMENSIONS => ViewingDistance::Near,
            _ => ViewingDistance::Close,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Density is used as the denominator in pip calculation, so must be handled explicitly.
    #[test]
    fn test_zero_density() {
        let metrics = DisplayMetrics::new(100, 100, Some(0.0), None, None);
        let second_metrics = DisplayMetrics::new(100, 100, None, None, None);
        assert_eq!(metrics.width_in_pips(), second_metrics.width_in_pips());
        assert_eq!(metrics.height_in_pips(), second_metrics.height_in_pips());
    }

    // Viewing distance is used as the denominator in pip calculation, so must be handled explicitly.
    #[test]
    fn test_zero_distance() {
        let metrics = DisplayMetrics::new(100, 100, None, Some(ViewingDistance::Unknown), None);
        let second_metrics = DisplayMetrics::new(100, 100, None, None, None);
        assert_eq!(metrics.width_in_pips(), second_metrics.width_in_pips());
        assert_eq!(metrics.height_in_pips(), second_metrics.height_in_pips());
    }

    // Tests that a known default density produces the same metrics as explicitly specified.
    #[test]
    fn test_pixels_per_pip_default() {
        let dimensions = DisplayMetrics::ACER_SWITCH_12_ALPHA_DIMENSIONS;
        let metrics = DisplayMetrics::new(dimensions.0, dimensions.1, None, None, None);
        let second_metrics = DisplayMetrics::new(
            dimensions.0,
            dimensions.1,
            Some(DisplayMetrics::ACER_SWITCH_12_ALPHA_DENSITY),
            Some(ViewingDistance::Close),
            None,
        );
        assert_eq!(metrics.width_in_pips(), second_metrics.width_in_pips());
        assert_eq!(metrics.height_in_pips(), second_metrics.height_in_pips());

        // The expected values here were generated and tested manually to be the expected
        // values for the Acer Switch 12 Alpha.
        assert_eq!(metrics.width_in_pips(), 1329.2307);
        assert_eq!(metrics.height_in_pips(), 886.1539);
    }
}
