// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use std::sync::Arc;

use failure::{Error, ResultExt};
use futures::future::{AbortHandle, Abortable};
use futures::lock::Mutex;
use futures::prelude::*;

// Include Brightness Control FIDL bindings
use fidl_fuchsia_hardware_backlight::DeviceProxy as BacklightProxy;
use fidl_fuchsia_hardware_input::DeviceProxy as SensorProxy;
use fidl_fuchsia_ui_brightness::{
    ControlRequest as BrightnessControlRequest, ControlRequestStream,
};
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use fuchsia_zircon::{Duration, DurationNum};

mod backlight;
mod sensor;

// Delay between sensor reads
const SCAN_TIMEOUT_MS: i64 = 500;
// Delay if we have made a large change in auto brightness
const QUICK_SCAN_TIMEOUT_MS: i64 = 10;
// What constitutes a large change in brightness?
// This seems small but it is significant and works nicely.
const LARGE_CHANGE_THRESHOLD_NITS: i16 = 2;

async fn run_brightness_server(mut stream: ControlRequestStream) -> Result<(), Error> {
    // TODO(kpt): "Consider adding additional tests against the resulting FIDL service itself so
    // that you can ensure it continues serving clients correctly."
    let backlight = backlight::open_backlight()?;
    let sensor = sensor::open_sensor().await?;

    let backlight = Arc::new(Mutex::new(backlight));
    let sensor = Arc::new(Mutex::new(sensor));

    // Startup auto-brightness loop
    let mut auto_brightness_abort_handle =
        start_auto_brightness_task(sensor.clone(), backlight.clone());

    while let Some(request) = stream.try_next().await.context("error running brightness server")? {
        // TODO(kpt): (b/138802653) Use try_for_each_concurrent and short circuit set-brightness_slowly
        // TODO(kpt): Make each match a testable function when hanging gets are implemented
        match request {
            BrightnessControlRequest::SetAutoBrightness { control_handle: _ } => {
                fx_log_info!("Auto-brightness turned on");
                auto_brightness_abort_handle.abort();
                auto_brightness_abort_handle =
                    start_auto_brightness_task(sensor.clone(), backlight.clone());
            }
            BrightnessControlRequest::WatchAutoBrightness { responder } => {
                // Hanging get is not implemented yet. We want to get autobrightness into team-food.
                // TODO(kpt): Implement hanging get (b/138455166)
                fx_log_info!("Received get auto brightness enabled");
                responder.send(true)?;
            }
            BrightnessControlRequest::SetManualBrightness { value, control_handle: _ } => {
                fx_log_info!("Auto-brightness off, brightness set to {}", value);
                // Stop the background auto-brightness task, if any
                auto_brightness_abort_handle.abort();
                // Lossy conversion but will change when brightness is set as an f32 (0.0..=1.0)
                let nits = num_traits::clamp(value as u16, 0, 255);
                let backlight_clone = backlight.clone();
                set_brightness(nits, backlight_clone).await?;
            }
            BrightnessControlRequest::WatchCurrentBrightness { responder } => {
                // Hanging get is not implemented yet. We want to get autobrightness into team-food.
                // TODO(kpt): Implement hanging get (b/138455166)
                fx_log_info!("Received get current brightness request");
                let backlight_clone = backlight.clone();
                let backlight = backlight_clone.lock().await;
                let brightness = backlight::get_brightness(&backlight).await?;
                responder.send(brightness as f32).context("error sending response")?;
            }
            _ => fx_log_err!("received {:?}", request),
        }
    }
    Ok(())
}

/// Runs the main auto-brightness code.
/// This task monitors its running boolean and terminates if it goes false.
fn start_auto_brightness_task(
    sensor: Arc<Mutex<SensorProxy>>,
    backlight: Arc<Mutex<BacklightProxy>>,
) -> AbortHandle {
    let (abort_handle, abort_registration) = AbortHandle::new_pair();
    fasync::spawn(
        Abortable::new(
            async move {
                loop {
                    // TODO(b/139153875): Pull loop body, without timer into function for testing.
                    let lux = {
                        // Get the sensor reading in its own mutex block
                        let sensor = sensor.lock().await;
                        // TODO(kpt) Do we need a Mutex if sensor is only read?
                        let report = sensor::read_sensor(&sensor)
                            .await
                            .expect("Could not read from the sensor");
                        report.illuminance
                    };
                    let nits = brightness_curve_lux_to_nits(lux);
                    let backlight_clone = backlight.clone();
                    let large_change = set_brightness(nits, backlight_clone)
                        .await
                        .expect("Could not set the brightness");
                    let delay_timeout =
                        if large_change { QUICK_SCAN_TIMEOUT_MS } else { SCAN_TIMEOUT_MS };
                    fuchsia_async::Timer::new(Duration::from_millis(delay_timeout).after_now())
                        .await;
                }
            },
            abort_registration,
        )
        .unwrap_or_else(|_| ()),
    );
    abort_handle
}

/// Sets the appropriate backlight brightness based on the ambient light sensor reading.
/// This will be a brightness curve but for the time being we know that the sensor
/// goes from 0 to 800 for the office and the backlight takes 0-255 so we take a simple approach.
/// TODO(kpt): Fix these values when the drivers change. (b/138455663)
fn brightness_curve_lux_to_nits(mut lux: u16) -> u16 {
    // Currently the sensor gives 900 at office brightness, this code will change when the
    // driver changes to lux.
    let max_lux = 900;
    // We consider this to be dark, will change with true lux sensor readings.
    let min_lux = 48;
    // Currently the backlight takes only 0 to 255. This code will change when the driver changes.
    let max_nits = 255;

    if lux >= min_lux {
        lux = lux - min_lux;
    }
    let lux = num_traits::clamp(lux, 0, max_lux);
    let nits = (max_nits as f32 * lux as f32 / max_lux as f32) as u16;
    // Minimum brightness of 1 for nighttime viewing.
    num_traits::clamp(nits, 1, max_nits)
}

/// Sets the brightness of the backlight to a specific value.
/// Returns true if the change is considered to be large.
async fn set_brightness(nits: u16, backlight: Arc<Mutex<BacklightProxy>>) -> Result<bool, Error> {
    let backlight = backlight.lock().await;
    let current_nits = backlight::get_brightness(&backlight).await.unwrap_or_else(|e| {
        fx_log_err!("Failed to get backlight: {}. assuming 200", e);
        200
    }) as u16;
    let set_brightness = |nits| {
        backlight::set_brightness(&backlight, nits)
            .unwrap_or_else(|e| fx_log_err!("Failed to set backlight: {}", e))
    };
    set_brightness_slowly(current_nits, nits, &set_brightness, 10.millis()).await?;
    Ok((nits as i16 - current_nits as i16).abs() > LARGE_CHANGE_THRESHOLD_NITS)
}

/// Change the brightness of the screen slowly to `nits` nits. We don't want to change the screen
/// suddenly so we smooth the transition by doing it in a series of small steps.
/// The time per step can be changed if needed e.g. to fade up slowly and down quickly.
/// When testing we set time_per_step to zero.
async fn set_brightness_slowly(
    current_nits: u16,
    to_nits: u16,
    mut set_brightness: impl FnMut(u16),
    time_per_step: Duration,
) -> Result<(), Error> {
    let mut current_nits = current_nits;
    let to_nits = num_traits::clamp(to_nits, 0, 255);
    assert!(to_nits <= 255);
    assert!(current_nits <= 255);
    let difference = to_nits as i16 - current_nits as i16;
    // TODO(kpt): Assume step size of 1, change when driver accepts more values (b/138455166)
    let steps = difference.abs();
    if steps > 0 {
        let step_size = difference / steps;
        for _i in 0..steps {
            // Don't go below 1 so that we can see it at night.
            current_nits = num_traits::clamp(current_nits as i16 + step_size, 1, 255) as u16;
            set_brightness(current_nits);
            if time_per_step.into_millis() > 0 {
                fuchsia_async::Timer::new(time_per_step.after_now()).await;
            }
        }
    }
    // Make sure we get to the correct value, there may be rounding errors
    set_brightness(to_nits);
    Ok(())
}

// TODO(kpt): maybe removing this enum entirely and just passing `ControlRequestStream`
// around directly if we don't have any more services.
enum IncomingService {
    BrightnessControl(ControlRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["brightness"])?;
    fx_log_info!("Started");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingService::BrightnessControl);
    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10;
    let fut =
        fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::BrightnessControl(stream)| {
            run_brightness_server(stream).unwrap_or_else(|e| fx_log_info!("{:?}", e))
        });

    fut.await;
    Ok(())
}

// Testing TODO(kpt) Under construction.
#[cfg(test)]

mod tests {
    use super::*;

    #[test]
    fn test_brightness_curve() {
        assert_eq!(1, brightness_curve_lux_to_nits(0));
        assert_eq!(1, brightness_curve_lux_to_nits(48));
        assert_eq!(3, brightness_curve_lux_to_nits(60));
        assert_eq!(14, brightness_curve_lux_to_nits(100));
        assert_eq!(99, brightness_curve_lux_to_nits(400));
        assert_eq!(213, brightness_curve_lux_to_nits(800));
        assert_eq!(241, brightness_curve_lux_to_nits(900));
        assert_eq!(255, brightness_curve_lux_to_nits(1000));
        assert_eq!(255, brightness_curve_lux_to_nits(2000));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_slowly_in_range() {
        let mut result = Vec::new();
        let set_brightness = |nits| {
            result.push(nits);
        };
        set_brightness_slowly(100, 200, set_brightness, 0.millis()).await.unwrap();
        assert_eq!(101, result.len(), "wrong length");
        assert_eq!(101, result[0]);
        assert_eq!(102, result[1]);
        assert_eq!(151, result[50]);
        assert_eq!(199, result[98]);
        assert_eq!(200, result[99]);
        assert_eq!(200, result[100]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_slowly_min() {
        let mut result = Vec::new();
        let set_brightness = |nits| {
            result.push(nits);
        };
        set_brightness_slowly(100, 0, set_brightness, 0.millis()).await.unwrap();
        assert_eq!(101, result.len(), "wrong length");
        assert_eq!(99, result[0]);
        assert_eq!(98, result[1]);
        assert_eq!(49, result[50]);
        assert_eq!(2, result[97]);
        assert_eq!(1, result[99]);
        assert_eq!(0, result[100]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_slowly_max() {
        let mut result = Vec::new();
        let set_brightness = |nits| {
            result.push(nits);
        };
        set_brightness_slowly(240, 260, set_brightness, 0.millis()).await.unwrap();
        assert_eq!(16, result.len(), "wrong length");
        assert_eq!(241, result[0]);
        assert_eq!(248, result[7]);
        assert_eq!(254, result[13]);
        assert_eq!(255, result[14]);
        assert_eq!(255, result[15]);
    }
}
