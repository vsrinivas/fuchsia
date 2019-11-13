use crate::backlight::BacklightControl;
use crate::sensor::SensorControl;
use async_trait::async_trait;
use std::sync::Arc;

use fidl_fuchsia_ui_brightness::ControlRequest as BrightnessControlRequest;
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use fuchsia_zircon::{Duration, DurationNum};
use futures::future::{AbortHandle, Abortable};
use futures::lock::Mutex;
use futures::prelude::*;

// Delay between sensor reads
const SLOW_SCAN_TIMEOUT_MS: i64 = 2000;
// Delay if we have made a large change in auto brightness
const QUICK_SCAN_TIMEOUT_MS: i64 = 100;
// What constitutes a large change in brightness?
// This seems small but it is significant and works nicely.
const LARGE_CHANGE_THRESHOLD_NITS: i32 = 4;

pub struct Control {
    sensor: Arc<Mutex<dyn SensorControl>>,
    backlight: Arc<Mutex<dyn BacklightControl>>,
    set_brightness_abort_handle: Option<AbortHandle>,
    auto_brightness_on: bool,
    auto_brightness_abort_handle: AbortHandle,
    max_brightness: f64,
}

impl Control {
    pub fn new(
        sensor: Arc<Mutex<dyn SensorControl>>,
        backlight: Arc<Mutex<dyn BacklightControl>>,
    ) -> Control {
        fx_log_info!("New Control class");

        let set_brightness_abort_handle = None::<AbortHandle>;

        let auto_brightness_on = true;

        // Startup auto-brightness loop
        let auto_brightness_abort_handle =
            start_auto_brightness_task(sensor.clone(), backlight.clone());

        Control {
            backlight,
            sensor,
            set_brightness_abort_handle,
            auto_brightness_on,
            auto_brightness_abort_handle,
            max_brightness: 250.0,
        }
    }

    pub async fn handle_request(&mut self, request: BrightnessControlRequest) {
        // TODO(kpt): "Consider adding additional tests against the resulting FIDL service itself so
        // that you can ensure it continues serving clients correctly."
        // TODO(kpt): Make each match a testable function when hanging gets are implemented
        match request {
            BrightnessControlRequest::SetAutoBrightness { control_handle: _ } => {
                self.set_auto_brightness();
            }
            BrightnessControlRequest::WatchAutoBrightness { responder } => {
                let response = self.watch_auto_brightness();
                if let Err(e) = responder.send(response) {
                    fx_log_err!("Failed to reply to WatchAutoBrightnessReply: {}", e);
                }
            }
            BrightnessControlRequest::SetManualBrightness { value, control_handle: _ } => {
                self.set_manual_brightness(value).await;
            }
            BrightnessControlRequest::WatchCurrentBrightness { responder } => {
                let response = self.watch_current_brightness().await;
                if let Err(e) = responder.send(response as f32) {
                    fx_log_err!("Failed to reply to WatchCurrentBrightness: {}", e);
                }
            }
            _ => fx_log_err!("received {:?}", request),
        }
    }

    // FIDL message handlers
    fn set_auto_brightness(&mut self) {
        if !self.auto_brightness_on {
            fx_log_info!("Auto-brightness turned on");
            self.auto_brightness_abort_handle.abort();
            self.auto_brightness_abort_handle =
                start_auto_brightness_task(self.sensor.clone(), self.backlight.clone());
            self.auto_brightness_on = true;
        }
    }

    fn watch_auto_brightness(&mut self) -> bool {
        // Hanging get is not implemented yet. We want to get autobrightness into team-food.
        // TODO(kpt): Implement hanging get (b/138455166)
        fx_log_info!("Received get auto brightness enabled");
        self.auto_brightness_on
    }

    async fn set_manual_brightness(&mut self, value: f32) {
        // Stop the background brightness tasks, if any
        if let Some(handle) = self.set_brightness_abort_handle.as_ref() {
            handle.abort();
        }
        if self.auto_brightness_on {
            fx_log_info!("Auto-brightness off, brightness set to {}", value);

            self.auto_brightness_abort_handle.abort();
            self.auto_brightness_on = false;
        }

        // TODO(b/138455663): remove this when the driver changes.
        let adjusted_value = self.convert_to_scaled_value_based_on_max_brightness(value);
        let nits = num_traits::clamp(adjusted_value, 0, self.max_brightness as u16);
        let backlight_clone = self.backlight.clone();
        self.set_brightness_abort_handle = Some(set_brightness(nits, backlight_clone, false).await);
    }

    async fn watch_current_brightness(&mut self) -> f64 {
        // Hanging get is not implemented yet. We want to get autobrightness into team-food.
        // TODO(kpt): Implement hanging get (b/138455166)
        fx_log_info!("Received get current brightness request");
        let backlight_clone = self.backlight.clone();
        let backlight = backlight_clone.lock().await;
        if self.auto_brightness_on {
            backlight.get_brightness(true).await.unwrap_or_else(|e| {
                fx_log_err!("Failed to get brightness: {}", e);
                self.max_brightness
            })
        } else {
            let brightness = backlight.get_brightness(false).await.unwrap_or_else(|e| {
                fx_log_err!("Failed to get brightness: {}", e);
                self.max_brightness
            });
            self.convert_from_scaled_value_based_on_max_brightness(brightness)
        }
    }

    /// Converts from our FIDL's 0.0-1.0 value to backlight's 0-max_brightness value
    fn convert_to_scaled_value_based_on_max_brightness(&self, value: f32) -> u16 {
        let value = num_traits::clamp(value, 0.0, 1.0);
        (value * self.max_brightness as f32) as u16
    }

    /// Converts from backlight's 0-max_brightness value to our FIDL's 0.0-1.0 value
    fn convert_from_scaled_value_based_on_max_brightness(&self, value: f64) -> f64 {
        let value = num_traits::clamp(value, 0.0, self.max_brightness);
        value / self.max_brightness
    }
}

#[async_trait(?Send)]
pub trait ControlTrait {
    async fn handle_request(&mut self, request: BrightnessControlRequest);
}

#[async_trait(?Send)]
impl ControlTrait for Control {
    async fn handle_request(&mut self, request: BrightnessControlRequest) {
        self.handle_request(request).await;
    }
}

// TODO(kpt) Move this and other functions into Control so that they can share the struct
/// Runs the main auto-brightness code.
/// This task monitors its running boolean and terminates if it goes false.
fn start_auto_brightness_task(
    sensor: Arc<Mutex<dyn SensorControl>>,
    backlight: Arc<Mutex<dyn BacklightControl>>,
) -> AbortHandle {
    let (abort_handle, abort_registration) = AbortHandle::new_pair();
    fasync::spawn(
        Abortable::new(
            async move {
                let max_brightness = {
                    let backlight = backlight.lock().await;
                    backlight.get_max_absolute_brightness()
                };
                let mut set_brightness_abort_handle = None::<AbortHandle>;
                // initialize to an impossible number
                let mut last_nits: i32 = -1000;
                loop {
                    let sensor = sensor.clone();
                    let nits = read_sensor_and_get_brightness(sensor, max_brightness).await;
                    let backlight_clone = backlight.clone();
                    if let Some(handle) = set_brightness_abort_handle {
                        handle.abort();
                    }
                    set_brightness_abort_handle =
                        Some(set_brightness(nits, backlight_clone, true).await);
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

async fn read_sensor_and_get_brightness(
    sensor: Arc<Mutex<dyn SensorControl>>,
    max_brightness: f64,
) -> u16 {
    let lux = {
        // Get the sensor reading in its own mutex block
        let sensor = sensor.lock().await;
        // TODO(kpt) Do we need a Mutex if sensor is only read?
        let fut = sensor.read();
        let report = fut.await.expect("Could not read from the sensor");
        report.illuminance
    };
    brightness_curve_lux_to_nits(lux, max_brightness)
}

/// Sets the appropriate backlight brightness based on the ambient light sensor reading.
/// This will be a brightness curve but for the time being we know that the sensor
/// goes from 0 to 800 for the office and the backlight takes 0-255 so we take a simple approach.
fn brightness_curve_lux_to_nits(lux: u16, max_brightness: f64) -> u16 {
    // Office brightness is about 240 lux, we want the screen full on at this level.
    let max_lux = 240;

    let lux = num_traits::clamp(lux, 0, max_lux);
    let nits = (max_brightness * lux as f64 / max_lux as f64) as u16;
    // Minimum brightness of 1 for nighttime viewing.
    num_traits::clamp(nits, 1, max_brightness as u16)
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
                let max_brightness = backlight.get_max_absolute_brightness();
                let current_nits = {
                    let fut = backlight.get_brightness(auto_brightness_on);
                    fut.await.unwrap_or_else(|e| {
                        fx_log_err!("Failed to get backlight: {}. assuming 200", e);
                        200.0
                    }) as u16
                };
                let set_brightness = |nits| {
                    backlight
                        .set_brightness(nits, auto_brightness_on)
                        .unwrap_or_else(|e| fx_log_err!("Failed to set backlight: {}", e))
                };
                set_brightness_slowly(
                    current_nits,
                    nits,
                    set_brightness,
                    10.millis(),
                    max_brightness,
                )
                .await;
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
    mut set_brightness: impl FnMut(f64),
    time_per_step: Duration,
    max_brightness: f64,
) {
    let mut current_nits = current_nits;
    let to_nits = num_traits::clamp(to_nits, 0, max_brightness as u16);
    assert!(to_nits <= max_brightness as u16);
    assert!(current_nits <= max_brightness as u16);
    let difference = to_nits as i16 - current_nits as i16;
    // TODO(kpt): Assume step size of 1, change when driver accepts more values (b/138455166)
    let steps = difference.abs();

    if steps > 0 {
        let step_size = difference / steps;
        for _i in 0..steps {
            // Don't go below 1 so that we can see it at night.
            current_nits =
                num_traits::clamp(current_nits as i16 + step_size, 1, max_brightness as i16) as u16;
            set_brightness(current_nits as f64);
            if time_per_step.into_millis() > 0 {
                fuchsia_async::Timer::new(time_per_step.after_now()).await;
            }
        }
    }
    // Make sure we get to the correct value, there may be rounding errors

    set_brightness(to_nits as f64);
}

#[cfg(test)]

mod tests {
    use super::*;

    use crate::sensor::AmbientLightInputRpt;
    use async_trait::async_trait;
    use failure::Error;

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
        value: f64,
        max_brightness: f64,
    }

    #[async_trait]
    impl BacklightControl for MockBacklight {
        async fn get_brightness(&self, _auto_brightness_on: bool) -> Result<f64, Error> {
            Ok(self.value)
        }

        fn set_brightness(&mut self, value: f64, _auto_brightness_on: bool) -> Result<(), Error> {
            self.value = value;
            Ok(())
        }

        fn get_max_absolute_brightness(&self) -> f64 {
            self.max_brightness
        }
    }

    fn set_mocks(
        sensor: u16,
        backlight: f64,
    ) -> (Arc<Mutex<impl SensorControl>>, Arc<Mutex<impl BacklightControl>>) {
        let sensor = MockSensor { illuminence: sensor };
        let sensor = Arc::new(Mutex::new(sensor));
        let backlight = MockBacklight { value: backlight, max_brightness: 250.0 };
        let backlight = Arc::new(Mutex::new(backlight));
        (sensor, backlight)
    }

    fn approx(v: f64) -> f64 {
        (v * 10.0).round() / 10.0
    }

    fn generate_control_struct() -> Control {
        let (sensor, _backlight) = set_mocks(400, 253.0);
        let set_brightness_abort_handle = None::<AbortHandle>;
        let (auto_brightness_abort_handle, _abort_registration) = AbortHandle::new_pair();
        Control {
            sensor,
            backlight: _backlight,
            set_brightness_abort_handle,
            auto_brightness_on: true,
            auto_brightness_abort_handle,
            max_brightness: 250.0,
        }
    }
    #[test]
    fn test_brightness_curve() {
        assert_eq!(1, brightness_curve_lux_to_nits(0, 250.0));
        assert_eq!(1, brightness_curve_lux_to_nits(1, 250.0));
        assert_eq!(2, brightness_curve_lux_to_nits(2, 250.0));
        assert_eq!(15, brightness_curve_lux_to_nits(15, 250.0));
        assert_eq!(16, brightness_curve_lux_to_nits(16, 250.0));
        assert_eq!(104, brightness_curve_lux_to_nits(100, 250.0));
        assert_eq!(156, brightness_curve_lux_to_nits(150, 250.0));
        assert_eq!(208, brightness_curve_lux_to_nits(200, 250.0));
        assert_eq!(250, brightness_curve_lux_to_nits(240, 250.0));
        assert_eq!(250, brightness_curve_lux_to_nits(300, 250.0));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_slowly_in_range() {
        let mut result = Vec::new();
        let set_brightness = |nits| {
            result.push(nits as u16);
        };
        set_brightness_slowly(100, 200, set_brightness, 0.millis(), 250.0).await;
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
            result.push(nits as u16);
        };
        set_brightness_slowly(100, 0, set_brightness, 0.millis(), 250.0).await;
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
            result.push(nits as u16);
        };
        set_brightness_slowly(240, 260, set_brightness, 0.millis(), 250.0).await;
        assert_eq!(11, result.len(), "wrong length");
        assert_eq!(241, result[0]);
        assert_eq!(244, result[3]);
        assert_eq!(250, result[9]);
        assert_eq!(250, result[10]);
    }

    #[test]
    fn test_to_old_backlight_value() {
        let control = generate_control_struct();
        assert_eq!(0, control.convert_to_scaled_value_based_on_max_brightness(0.0));
        assert_eq!(125, control.convert_to_scaled_value_based_on_max_brightness(0.5));
        assert_eq!(250, control.convert_to_scaled_value_based_on_max_brightness(1.0));
        // Out of bounds
        assert_eq!(0, control.convert_to_scaled_value_based_on_max_brightness(-0.5));
        assert_eq!(250, control.convert_to_scaled_value_based_on_max_brightness(2.0));
    }

    #[test]
    fn test_from_old_backlight_value() {
        let control = generate_control_struct();
        assert_eq!(0.0, approx(control.convert_from_scaled_value_based_on_max_brightness(0.0)));
        assert_eq!(0.5, approx(control.convert_from_scaled_value_based_on_max_brightness(125.0)));
        assert_eq!(1.0, approx(control.convert_from_scaled_value_based_on_max_brightness(250.0)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sensor_and_get_brightness_bright() {
        let (sensor, _backlight) = set_mocks(400, 253.0);
        let nits = read_sensor_and_get_brightness(sensor, 250.0).await;
        assert_eq!(250, nits);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_sensor_and_get_brightness_low_light() {
        let (sensor, _backlight) = set_mocks(0, 0.0);
        let nits = read_sensor_and_get_brightness(sensor, 250.0).await;
        assert_eq!(1, nits);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_is_abortable_with_auto_brightness_on() {
        let (_sensor, backlight) = set_mocks(0, 0.0);
        let backlight_clone = backlight.clone();
        let abort_handle = set_brightness(100, backlight_clone, true).await;
        // Abort the task before it really gets going
        abort_handle.abort();
        // It should not have reached the final value yet.
        // We know that set_brightness_slowly, at the bottom of the task, finishes at the correct
        // nits value from other tests if it has sufficient time.
        let backlight = backlight.lock().await;
        assert_ne!(100_f64, backlight.get_brightness(true).await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_brightness_is_abortable_with_auto_brightness_off() {
        let (_sensor, backlight) = set_mocks(0, 0.0);
        let backlight_clone = backlight.clone();
        let abort_handle = set_brightness(100, backlight_clone, false).await;
        // Abort the task before it really gets going
        abort_handle.abort();
        // It should not have reached the final value yet.
        // We know that set_brightness_slowly, at the bottom of the task, finishes at the correct
        // nits value from other tests if it has sufficient time.
        let backlight = backlight.lock().await;
        assert_ne!(100_f64, backlight.get_brightness(false).await.unwrap());
    }
}
