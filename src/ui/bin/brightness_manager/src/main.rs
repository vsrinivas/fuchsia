// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use failure::{Error, ResultExt};
use futures::future::{AbortHandle, Abortable};
use futures::lock::Mutex;
use futures::prelude::*;

// Include Brightness Control FIDL bindings
use fidl_fuchsia_ui_brightness::{
    ControlRequest as BrightnessControlRequest, ControlRequestStream,
};

use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use fuchsia_zircon::{Duration, DurationNum};

use backlight::{Backlight, BacklightControl};
use sensor::{Sensor, SensorControl};

mod backlight;
mod sensor;

// Delay between sensor reads
const SLOW_SCAN_TIMEOUT_MS: i64 = 2000;
// Delay if we have made a large change in auto brightness
const QUICK_SCAN_TIMEOUT_MS: i64 = 100;
// What constitutes a large change in brightness?
// This seems small but it is significant and works nicely.
const LARGE_CHANGE_THRESHOLD_NITS: i32 = 4;
//TODO(b/38459) This should use the actual max brightness from the driver.
const MAX_BRIGHTNESS: u16 = 250;

async fn run_brightness_server(mut stream: ControlRequestStream) -> Result<(), Error> {
    // TODO(kpt): "Consider adding additional tests against the resulting FIDL service itself so
    // that you can ensure it continues serving clients correctly."
    let backlight = Backlight::new()?;
    let sensor = Sensor::new().await;

    let backlight = Arc::new(Mutex::new(backlight));
    let sensor = Arc::new(Mutex::new(sensor));

    let mut set_brightness_abort_handle = None::<AbortHandle>;

    let mut auto_brightness_on = true;

    // Startup auto-brightness loop
    let mut auto_brightness_abort_handle =
        start_auto_brightness_task(sensor.clone(), backlight.clone(), auto_brightness_on);

    while let Some(request) = stream.try_next().await.context("error running brightness server")? {
        // TODO(kpt): Make each match a testable function when hanging gets are implemented
        match request {
            BrightnessControlRequest::SetAutoBrightness { control_handle: _ } => {
                if !auto_brightness_on {
                    fx_log_info!("Auto-brightness turned on");
                    if let Some(handle) = set_brightness_abort_handle.as_ref() {
                        handle.abort();
                    }
                    auto_brightness_abort_handle = start_auto_brightness_task(
                        sensor.clone(),
                        backlight.clone(),
                        auto_brightness_on,
                    );
                    auto_brightness_on = true;
                }
                auto_brightness_abort_handle.abort();
                auto_brightness_abort_handle = start_auto_brightness_task(
                    sensor.clone(),
                    backlight.clone(),
                    auto_brightness_on,
                );
            }
            BrightnessControlRequest::WatchAutoBrightness { responder } => {
                // Hanging get is not implemented yet. We want to get autobrightness into team-food.
                // TODO(kpt): Implement hanging get (b/138455166)
                fx_log_info!("Received get auto brightness enabled");
                responder.send(true)?;
            }
            BrightnessControlRequest::SetManualBrightness { value, control_handle: _ } => {
                // Stop the background brightness tasks, if any
                if let Some(handle) = set_brightness_abort_handle.as_ref() {
                    handle.abort();
                }
                if auto_brightness_on {
                    fx_log_info!("Auto-brightness off, brightness set to {}", value);

                    auto_brightness_abort_handle.abort();
                    auto_brightness_on = false;
                }

                // TODO(b/138455663): remove this when the driver changes.
                let adjusted_value = convert_to_scaled_value_based_on_max_brightness(value);
                let nits = num_traits::clamp(adjusted_value, 0, MAX_BRIGHTNESS);
                let backlight_clone = backlight.clone();
                set_brightness_abort_handle =
                    Some(set_brightness(nits, backlight_clone, false).await);
            }
            BrightnessControlRequest::WatchCurrentBrightness { responder } => {
                // Hanging get is not implemented yet. We want to get autobrightness into team-food.
                // TODO(kpt): Implement hanging get (b/138455166)
                fx_log_info!("Received get current brightness request");
                let backlight_clone = backlight.clone();
                let backlight = backlight_clone.lock().await;
                if auto_brightness_on {
                    let brightness = backlight.get_brightness(true).await?;
                    responder.send(brightness.into()).context("error sending response")?;
                } else {
                    let brightness = backlight.get_brightness(false).await?;
                    let brightness = convert_from_scaled_value_based_on_max_brightness(brightness);
                    responder.send(brightness.into()).context("error sending response")?;
                }
                // TODO(b/138455663): remove this when the driver changes.
            }
            _ => fx_log_err!("received {:?}", request),
        }
    }
    Ok(())
}

/// Converts from our FIDL's 0.0-1.0 value to backlight's 0-255 value
fn convert_to_scaled_value_based_on_max_brightness(value: f32) -> u16 {
    let value = num_traits::clamp(value, 0.0, 1.0);
    (value * MAX_BRIGHTNESS as f32) as u16
}

/// Converts from backlight's 0-255 value to our FIDL's 0.0-1.0 value
fn convert_from_scaled_value_based_on_max_brightness(value: u16) -> f32 {
    let value = num_traits::clamp(value, 0, MAX_BRIGHTNESS);
    value as f32 / MAX_BRIGHTNESS as f32
}

/// Runs the main auto-brightness code.
/// This task monitors its running boolean and terminates if it goes false.
fn start_auto_brightness_task(
    sensor: Arc<Mutex<dyn SensorControl>>,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    auto_brightness_on: bool,
) -> AbortHandle {
    let (abort_handle, abort_registration) = AbortHandle::new_pair();
    fasync::spawn(
        Abortable::new(
            async move {
                let mut set_brightness_abort_handle = None::<AbortHandle>;
                // initialize to an impossible number
                let mut last_nits: i32 = -1000;
                loop {
                    let sensor = sensor.clone();
                    let nits = read_sensor_and_get_brightness(sensor).await;
                    let backlight_clone = backlight.clone();
                    if let Some(handle) = set_brightness_abort_handle {
                        handle.abort();
                    }
                    set_brightness_abort_handle =
                        Some(set_brightness(nits, backlight_clone, auto_brightness_on).await);
                    let large_change =
                        (last_nits as i32 - nits as i32).abs() > LARGE_CHANGE_THRESHOLD_NITS;
                    last_nits = nits as i32;
                    let delay_timeout =
                        if large_change { QUICK_SCAN_TIMEOUT_MS } else { SLOW_SCAN_TIMEOUT_MS };
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

async fn read_sensor_and_get_brightness(sensor: Arc<Mutex<dyn SensorControl>>) -> u16 {
    let lux = {
        // Get the sensor reading in its own mutex block
        let sensor = sensor.lock().await;
        // TODO(kpt) Do we need a Mutex if sensor is only read?
        let fut = sensor.read();
        let report = fut.await.expect("Could not read from the sensor");
        report.illuminance
    };
    brightness_curve_lux_to_nits(lux)
}

/// Sets the appropriate backlight brightness based on the ambient light sensor reading.
/// This will be a brightness curve but for the time being we know that the sensor
/// goes from 0 to 800 for the office and the backlight takes 0-255 so we take a simple approach.
/// TODO(kpt): Fix these values when the drivers change. (b/138455663)
fn brightness_curve_lux_to_nits(lux: u16) -> u16 {
    // Office brightness is about 240 lux, we want the screen full on at this level.
    let max_lux = 240;

    let lux = num_traits::clamp(lux, 0, max_lux);
    let nits = (MAX_BRIGHTNESS as f32 * lux as f32 / max_lux as f32) as u16;
    // Minimum brightness of 1 for nighttime viewing.
    num_traits::clamp(nits, 1, MAX_BRIGHTNESS)
}

/// Sets the brightness of the backlight to a specific value.
/// An abortable task is spawned to handle this as it can take a while to do.
async fn set_brightness(
    nits: u16,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    auto_brightness_on: bool,
) -> AbortHandle {
    let (abort_handle, abort_registration) = AbortHandle::new_pair();
    fasync::spawn(
        Abortable::new(
            async move {
                let mut backlight = backlight.lock().await;
                let current_nits = {
                    //let backlight = backlight.lock().await;
                    let fut = backlight.get_brightness(auto_brightness_on);
                    fut.await.unwrap_or_else(|e| {
                        fx_log_err!("Failed to get backlight: {}. assuming 200", e);
                        200
                    }) as u16
                };
                let set_brightness = |nits| {
                    backlight
                        .set_brightness(nits, auto_brightness_on)
                        .unwrap_or_else(|e| fx_log_err!("Failed to set backlight: {}", e))
                };
                set_brightness_slowly(current_nits, nits, set_brightness, 10.millis()).await;
            },
            abort_registration,
        )
        .unwrap_or_else(|_task_aborted| ()),
    );
    abort_handle
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
) {
    let mut current_nits = current_nits;
    let to_nits = num_traits::clamp(to_nits, 0, MAX_BRIGHTNESS);
    assert!(to_nits <= MAX_BRIGHTNESS);
    assert!(current_nits <= MAX_BRIGHTNESS);
    let difference = to_nits as i16 - current_nits as i16;
    // TODO(kpt): Assume step size of 1, change when driver accepts more values (b/138455166)
    let steps = difference.abs();

    if steps > 0 {
        let step_size = difference / steps;
        for _i in 0..steps {
            // Don't go below 1 so that we can see it at night.
            current_nits =
                num_traits::clamp(current_nits as i16 + step_size, 1, MAX_BRIGHTNESS as i16) as u16;
            set_brightness(current_nits);
            if time_per_step.into_millis() > 0 {
                fuchsia_async::Timer::new(time_per_step.after_now()).await;
            }
        }
    }
    // Make sure we get to the correct value, there may be rounding errors
    set_brightness(to_nits);
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

#[cfg(test)]

mod tests {
    use super::*;

    use async_trait::async_trait;
    use sensor::AmbientLightInputRpt;

    struct MockSensor {
        illuminence: u16,
    }

    #[async_trait]
    impl SensorControl for MockSensor {
        async fn read(&self) -> Result<AmbientLightInputRpt, Error> {
            Ok(AmbientLightInputRpt {
                rpt_id: 0,
                state: 0,
                event: 0,
                illuminance: self.illuminence,
                red: 0,
                green: 0,
                blue: 0,
            })
        }
    }

    struct MockBacklight {
        value: u16,
    }

    #[async_trait]
    impl BacklightControl for MockBacklight {
        async fn get_brightness(&self, _auto_brightness_on: bool) -> Result<u16, Error> {
            Ok(self.value)
        }

        fn set_brightness(&mut self, value: u16, _auto_brightness_on: bool) -> Result<(), Error> {
            self.value = value;
            Ok(())
        }
    }

    fn set_mocks(
        sensor: u16,
        backlight: u16,
    ) -> (Arc<Mutex<impl SensorControl>>, Arc<Mutex<impl BacklightControl>>) {
        let sensor = MockSensor { illuminence: sensor };
        let sensor = Arc::new(Mutex::new(sensor));
        let backlight = MockBacklight { value: backlight };
        let backlight = Arc::new(Mutex::new(backlight));
        (sensor, backlight)
    }

    fn approx(v: f32) -> f32 {
        (v * 10.0).round() / 10.0
    }

    #[test]
    fn test_brightness_curve() {
        assert_eq!(1, brightness_curve_lux_to_nits(0));
        assert_eq!(1, brightness_curve_lux_to_nits(1));
        assert_eq!(2, brightness_curve_lux_to_nits(2));
        assert_eq!(15, brightness_curve_lux_to_nits(15));
        assert_eq!(16, brightness_curve_lux_to_nits(16));
        assert_eq!(104, brightness_curve_lux_to_nits(100));
        assert_eq!(156, brightness_curve_lux_to_nits(150));
        assert_eq!(208, brightness_curve_lux_to_nits(200));
        assert_eq!(250, brightness_curve_lux_to_nits(240));
        assert_eq!(250, brightness_curve_lux_to_nits(300));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_slowly_in_range() {
        let mut result = Vec::new();
        let set_brightness = |nits| {
            result.push(nits);
        };
        set_brightness_slowly(100, 200, set_brightness, 0.millis()).await;
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
        set_brightness_slowly(100, 0, set_brightness, 0.millis()).await;
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
        set_brightness_slowly(240, 260, set_brightness, 0.millis()).await;
        assert_eq!(11, result.len(), "wrong length");
        assert_eq!(241, result[0]);
        assert_eq!(244, result[3]);
        assert_eq!(250, result[9]);
        assert_eq!(250, result[10]);
    }

    #[test]
    fn test_to_scaled_value_based_on_max_brightness() {
        assert_eq!(0, convert_to_scaled_value_based_on_max_brightness(0.0));
        assert_eq!(125, convert_to_scaled_value_based_on_max_brightness(0.5));
        assert_eq!(250, convert_to_scaled_value_based_on_max_brightness(1.0));
        // Out of bounds
        assert_eq!(0, convert_to_scaled_value_based_on_max_brightness(-0.5));
        assert_eq!(250, convert_to_scaled_value_based_on_max_brightness(2.0));
    }

    #[test]
    fn test_from_scaled_value_based_on_max_brightness() {
        assert_eq!(0.0, approx(convert_from_scaled_value_based_on_max_brightness(0)));
        assert_eq!(0.5, approx(convert_from_scaled_value_based_on_max_brightness(127)));
        assert_eq!(1.0, approx(convert_from_scaled_value_based_on_max_brightness(255)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sensor_and_get_brightness_bright() {
        let (sensor, _backlight) = set_mocks(400, 253);
        let nits = read_sensor_and_get_brightness(sensor).await;
        assert_eq!(250, nits);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sensor_and_get_brightness_low_light() {
        let (sensor, _backlight) = set_mocks(0, 0);
        let nits = read_sensor_and_get_brightness(sensor).await;
        assert_eq!(1, nits);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_is_abortable_with_auto_brightness_on() {
        let (_sensor, backlight) = set_mocks(0, 0);
        let backlight_clone = backlight.clone();
        let abort_handle = set_brightness(100, backlight_clone, true).await;
        // Abort the task before it really gets going
        abort_handle.abort();
        // It should not have reached the final value yet.
        // We know that set_brightness_slowly, at the bottom of the task, finishes at the correct
        // nits value from other tests if it has sufficient time.
        let backlight = backlight.lock().await;
        assert_ne!(100_u16, backlight.get_brightness(true).await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_is_abortable_with_auto_brightness_off() {
        let (_sensor, backlight) = set_mocks(0, 0);
        let backlight_clone = backlight.clone();
        let abort_handle = set_brightness(100, backlight_clone, false).await;
        // Abort the task before it really gets going
        abort_handle.abort();
        // It should not have reached the final value yet.
        // We know that set_brightness_slowly, at the bottom of the task, finishes at the correct
        // nits value from other tests if it has sufficient time.
        let backlight = backlight.lock().await;
        assert_ne!(100_u16, backlight.get_brightness(false).await.unwrap());
    }
}
