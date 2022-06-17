// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
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
    let _ = fasync::OnSignals::new(&clock, zx::Signals::CLOCK_STARTED)
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

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_zircon::HandleBased};

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
            .update(
                zx::ClockUpdate::builder()
                    .approximate_value(zx::Time::from_nanos(1 * 1_000_000_000)),
            )
            .expect("Clock update should succeed");

        // Clock has been started, so the clock should be set.
        assert!(exec.run_until_stalled(&mut set_fut).is_ready());

        // There is no transformation before the clock has been set.
        assert!(utc_clock_transformation().is_some());
    }
}
