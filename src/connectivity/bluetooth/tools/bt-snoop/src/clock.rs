// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    core::convert::TryInto,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    once_cell::sync::OnceCell,
};

/// Global UTC Clock used by the component.
static UTC_CLOCK: OnceCell<zx::Clock> = OnceCell::new();

pub async fn set_utc_clock() -> Result<(), Error> {
    let clock = fuchsia_runtime::duplicate_utc_clock_handle(zx::Rights::SAME_RIGHTS)
        .context("Could not get Clock Handle")?;
    set_clock(clock).await
}

/// Set the global clock after it has started running.
async fn set_clock(clock: zx::Clock) -> Result<(), Error> {
    fasync::OnSignals::new(&clock, zx::Signals::CLOCK_STARTED)
        .await
        .context("Failed to wait on CLOCK_STARTED signal")?;

    UTC_CLOCK.set(clock).map_err(|_| format_err!("Clock already set"))?;

    Ok(())
}

/// Get the `clock`'s mono to synthetic clock transformation. Returns `None` if
/// the transformation cannot be retrieved.
fn get_transformation(clock: &zx::Clock) -> Option<zx::ClockTransformation> {
    clock.get_details().map(|details| details.mono_to_synthetic).ok()
}

/// If the UTC Clock is available, return the current transformation.
pub fn utc_clock_transformation() -> Option<zx::ClockTransformation> {
    UTC_CLOCK.get().and_then(get_transformation)
}

/// [Clock transformations](https://fuchsia.dev/fuchsia-src/concepts/kernel/clock_transformations)
/// can be applied to convert a time from a reference time to a synthetic time. The inverse
/// transformation can be applied to convert a synthetic time back to the reference time.
pub trait TransformClock {
    /// Apply the transformation from reference time to synthetic time.
    fn apply(self, xform: &zx::ClockTransformation) -> Self;
    /// Apply the inverse transformation from synthetic time to reference time.
    fn apply_inverse(self, xform: &zx::ClockTransformation) -> Self;
}

/// Apply affine transformation to convert the reference time "r" to the synthetic time
/// "c". All values are widened to i128 before calculations and the end result is converted back to
/// a i64. If "c" is a larger number than would fit in an i64, the result saturates when cast to
/// i64.
fn transform_clock(r: i64, r_offset: i64, c_offset: i64, r_rate: u32, c_rate: u32) -> i64 {
    let r = r as i128;
    let r_offset = r_offset as i128;
    let c_offset = c_offset as i128;
    let r_rate = r_rate as i128;
    let c_rate = c_rate as i128;
    let c = (((r - r_offset) * c_rate) / r_rate) + c_offset;
    c.try_into().unwrap_or_else(|_| if c.is_positive() { i64::MAX } else { i64::MIN })
}

impl TransformClock for zx::Time {
    fn apply(self, xform: &zx::ClockTransformation) -> Self {
        let c = transform_clock(
            self.into_nanos(),
            xform.reference_offset,
            xform.synthetic_offset,
            xform.rate.reference_ticks,
            xform.rate.synthetic_ticks,
        );

        zx::Time::from_nanos(c)
    }

    fn apply_inverse(self, xform: &zx::ClockTransformation) -> Self {
        let r = transform_clock(
            self.into_nanos(),
            xform.synthetic_offset,
            xform.reference_offset,
            xform.rate.synthetic_ticks,
            xform.rate.reference_ticks,
        );

        zx::Time::from_nanos(r as i64)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_zircon::HandleBased};

    #[test]
    fn clock_identity_transformation_roundtrip() {
        let t_0 = zx::Time::ZERO;
        // Identity clock transformation
        let xform = zx::ClockTransformation {
            reference_offset: 0,
            synthetic_offset: 0,
            rate: zx::sys::zx_clock_rate_t { synthetic_ticks: 1, reference_ticks: 1 },
        };

        // Transformation roundtrip should be equivalent with the identity transformation.
        assert_eq!(t_0, t_0.apply(&xform.clone()).apply_inverse(&xform));
    }

    #[test]
    fn clock_transformation_roundtrip() {
        let t_0 = zx::Time::ZERO;
        // Arbitrary clock transformation
        let xform = zx::ClockTransformation {
            reference_offset: 196980085208,
            synthetic_offset: 1616900096031887801,
            rate: zx::sys::zx_clock_rate_t { synthetic_ticks: 999980, reference_ticks: 1000000 },
        };

        // Transformation roundtrip should be equivalent modulo rounding error.
        let roundtrip_diff = t_0 - t_0.apply(&xform.clone()).apply_inverse(&xform);
        assert!(roundtrip_diff.into_nanos().abs() <= 1);
    }

    #[test]
    fn clock_trailing_transformation_roundtrip() {
        let t_0 = zx::Time::ZERO;
        // Arbitrary clock transformation where the synthetic clock is trailing behind the
        // reference clock.
        let xform = zx::ClockTransformation {
            reference_offset: 1616900096031887801,
            synthetic_offset: 196980085208,
            rate: zx::sys::zx_clock_rate_t { synthetic_ticks: 1000000, reference_ticks: 999980 },
        };

        // Transformation roundtrip should be equivalent modulo rounding error.
        let roundtrip_diff = t_0 - t_0.apply(&xform.clone()).apply_inverse(&xform);
        assert!(roundtrip_diff.into_nanos().abs() <= 1);
    }

    #[test]
    fn clock_saturating_transformations() {
        let t_0 = zx::Time::from_nanos(i64::MAX);
        // Clock transformation which will positively overflow t_0
        let xform = zx::ClockTransformation {
            reference_offset: 0,
            synthetic_offset: 1,
            rate: zx::sys::zx_clock_rate_t { synthetic_ticks: 1, reference_ticks: 1 },
        };

        // Applying the transformation will lead to saturation
        let time = t_0.apply(&xform.clone()).into_nanos();
        assert_eq!(time, i64::MAX);

        let t_0 = zx::Time::from_nanos(i64::MIN);
        // Clock transformation which will negatively overflow t_0
        let xform = zx::ClockTransformation {
            reference_offset: 1,
            synthetic_offset: 0,
            rate: zx::sys::zx_clock_rate_t { synthetic_ticks: 1, reference_ticks: 1 },
        };

        // Applying the transformation will lead to saturation
        let time = t_0.apply(&xform.clone()).into_nanos();
        assert_eq!(time, i64::MIN);
    }

    #[test]
    fn set_and_get_clock() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        // There is no transformation before the clock has been set.
        assert!(utc_clock_transformation().is_none());

        // Create a new clock and a manually polled future for setting the clock.
        let clock = zx::Clock::create(zx::ClockOpts::empty(), None).expect("create clock");
        let clock_ = clock.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("duplicate clock");
        let set_fut = set_clock(clock_);
        futures::pin_mut!(set_fut);

        // Clock has not yet been started so it is not set.
        assert!(exec.run_until_stalled(&mut set_fut).is_pending());

        clock
            .update(zx::ClockUpdate::new().value(zx::Time::from_nanos(1 * 1_000_000_000)))
            .expect("Clock update should succeed");

        // Clock has been started, so the clock should be set.
        assert!(exec.run_until_stalled(&mut set_fut).is_ready());

        // There is no transformation before the clock has been set.
        assert!(utc_clock_transformation().is_some());
    }
}
