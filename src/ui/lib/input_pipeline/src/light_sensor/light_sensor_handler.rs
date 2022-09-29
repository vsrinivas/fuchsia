// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::input_device::{Handled, InputDeviceDescriptor, InputDeviceEvent, InputEvent};
use crate::input_handler::InputHandler;
use crate::light_sensor::calibrator::{Calibrate, Calibrator};
use crate::light_sensor::led_watcher::{CancelableTask, LedWatcher, LedWatcherHandle};
use crate::light_sensor::types::{AdjustmentSetting, Calibration, Rgbc, SensorConfiguration};
use anyhow::{format_err, Context, Error};
use async_trait::async_trait;
use async_utils::hanging_get::server::HangingGet;
use fidl_fuchsia_input_report::{FeatureReport, InputDeviceProxy, SensorFeatureReport};
use fidl_fuchsia_lightsensor::{
    LightSensorData as FidlLightSensorData, Rgbc as FidlRgbc, SensorRequest, SensorRequestStream,
    SensorWatchResponder,
};
use fidl_fuchsia_settings::LightProxy;
use fidl_fuchsia_ui_brightness::ControlProxy as BrightnessControlProxy;
use fuchsia_syslog::fx_log_warn;
use fuchsia_zircon as zx;
use futures::channel::oneshot;
use futures::TryStreamExt;
use std::cell::RefCell;
use std::rc::Rc;

type NotifyFn = Box<dyn Fn(&LightSensorData, SensorWatchResponder) -> bool>;
type SensorHangingGet = HangingGet<LightSensorData, SensorWatchResponder, NotifyFn>;

// Precise value is 2.78125ms, but data sheet lists 2.78ms.
const MIN_TIME_STEP_US: u32 = 2780;
// Base multiplier.
#[allow(dead_code)]
const BASE_GAIN: u32 = 16;
// Maximum multiplier.
const MAX_GAIN: u32 = 64;
// Maximum sensor reading per cycle for any 1 color channel.
const MAX_COUNT_PER_CYCLE: u32 = 1024;
// Absolute maximum reading the sensor can return for any 1 color channel.
const MAX_SATURATION: u32 = u16::MAX as u32;
const MAX_ATIME: u32 = 256;
// Driver scales the values by max gain & atime in ms.
const ADC_SCALING_FACTOR: f32 = 16.0 * 712.0;
// The gain up margin should be 10% of the saturation point.
const GAIN_UP_MARGIN_DIVISOR: u32 = 10;

#[derive(Copy, Clone, Debug)]
struct LightReading {
    rgbc: Rgbc<f32>,
    lux: f32,
    cct: f32,
}

fn num_cycles(atime: u32) -> u32 {
    MAX_ATIME - atime
}

struct ActiveSetting {
    settings: Vec<AdjustmentSetting>,
    active_setting: usize,
}

impl ActiveSetting {
    fn new(settings: Vec<AdjustmentSetting>, active_setting: usize) -> Self {
        Self { settings, active_setting }
    }

    // TODO(fxbug.dev/110275) Remove allow once all clients are transitioned.
    #[cfg_attr(not(test), allow(dead_code))]
    async fn adjust(
        &mut self,
        reading: Rgbc<u16>,
        device_proxy: InputDeviceProxy,
    ) -> Result<(), Error> {
        let saturation_point =
            (num_cycles(self.active_setting().atime) * MAX_COUNT_PER_CYCLE).min(MAX_SATURATION);
        let gain_up_margin = saturation_point / GAIN_UP_MARGIN_DIVISOR;

        let step_change = self.step_change();
        let mut pull_up = true;

        if saturated(reading) {
            if self.adjust_down() {
                self.update_device(device_proxy).await.context("updating light sensor device")?;
            }
            return Ok(());
        }

        for value in [reading.red, reading.green, reading.blue, reading.clear] {
            let value = value as u32;
            if value >= saturation_point {
                if self.adjust_down() {
                    self.update_device(device_proxy)
                        .await
                        .context("updating light sensor device")?;
                }
                return Ok(());
            } else if (value * step_change + gain_up_margin) >= saturation_point {
                pull_up = false;
            }
        }

        if pull_up {
            if self.adjust_up() {
                self.update_device(device_proxy).await.context("updating light sensor device")?;
            }
        }

        Ok(())
    }

    async fn update_device(&self, device_proxy: InputDeviceProxy) -> Result<(), Error> {
        let active_setting = self.active_setting();
        let feature_report = device_proxy
            .get_feature_report()
            .await
            .context("calling get_feature_report")?
            .map_err(|e| {
                format_err!(
                    "getting feature report on light sensor device: {:?}",
                    zx::Status::from_raw(e),
                )
            })?;
        device_proxy
            .set_feature_report(FeatureReport {
                sensor: Some(SensorFeatureReport {
                    sensitivity: Some(vec![active_setting.gain as i64]),
                    sampling_rate: Some(active_setting.atime as i64),
                    ..(feature_report
                        .sensor
                        .ok_or_else(|| format_err!("missing sensor in feature_report"))?)
                }),
                ..feature_report
            })
            .await
            .context("calling set_feature_report")?
            .map_err(|e| {
                format_err!(
                    "updating feature report on light sensor device: {:?}",
                    zx::Status::from_raw(e),
                )
            })
    }

    fn active_setting(&self) -> AdjustmentSetting {
        self.settings[self.active_setting]
    }

    /// Adjusts to a lower setting. Returns whether or not the setting changed.
    fn adjust_down(&mut self) -> bool {
        if self.active_setting == 0 {
            false
        } else {
            self.active_setting -= 1;
            true
        }
    }

    /// Calculate the effect to saturation that occurs by moving the setting up a step.
    fn step_change(&self) -> u32 {
        let current = self.active_setting();
        let new = match self.settings.get(self.active_setting + 1) {
            Some(setting) => *setting,
            // If we're at the limit, just return a coefficient of 1 since there will be no step
            // change.
            None => return 1,
        };
        div_round_up(new.gain, current.gain) * div_round_up(to_us(new.atime), to_us(current.atime))
    }

    /// Adjusts to a higher setting. Returns whether or not the setting changed.
    fn adjust_up(&mut self) -> bool {
        if self.active_setting == self.settings.len() - 1 {
            false
        } else {
            self.active_setting += 1;
            true
        }
    }
}

#[derive(Clone)]
pub struct LightSensorHandler<T> {
    hanging_get: Rc<RefCell<SensorHangingGet>>,
    calibrator: T,
    // TODO(fxbug.dev/110275) Remove allow once all clients are transitioned.
    #[cfg_attr(not(test), allow(dead_code))]
    active_setting: Rc<RefCell<ActiveSetting>>,
    rgbc_to_lux_coefs: Rgbc<f32>,
    si_scaling_factors: Rgbc<f32>,
    vendor_id: u32,
    product_id: u32,
}

pub type CalibratedLightSensorHandler = LightSensorHandler<Calibrator<LedWatcherHandle>>;
pub async fn make_light_sensor_handler_and_spawn_led_watcher(
    light_proxy: LightProxy,
    brightness_proxy: BrightnessControlProxy,
    calibration: Calibration,
    configuration: SensorConfiguration,
) -> Result<(Rc<CalibratedLightSensorHandler>, CancelableTask), Error> {
    let light_groups =
        light_proxy.watch_light_groups().await.context("request initial light groups")?;
    let led_watcher = LedWatcher::new(light_groups);
    let (cancelation_tx, cancelation_rx) = oneshot::channel();
    let (led_watcher_handle, watcher_task) = led_watcher.handle_light_groups_and_brightness_watch(
        light_proxy,
        brightness_proxy,
        cancelation_rx,
    );
    let watcher_task = CancelableTask::new(cancelation_tx, watcher_task);
    let calibrator = Calibrator::new(calibration, led_watcher_handle);
    Ok((LightSensorHandler::new(calibrator, configuration), watcher_task))
}

impl<T> LightSensorHandler<T> {
    pub fn new(calibrator: T, configuration: SensorConfiguration) -> Rc<Self> {
        let hanging_get = Rc::new(RefCell::new(HangingGet::new(
            LightSensorData {
                rgbc: Rgbc { red: 0.0, green: 0.0, blue: 0.0, clear: 0.0 },
                calculated_lux: 0.0,
                correlated_color_temperature: 0.0,
            },
            Box::new(|sensor_data: &LightSensorData, responder: SensorWatchResponder| -> bool {
                if let Err(e) = responder.send(FidlLightSensorData::from(*sensor_data)) {
                    fx_log_warn!("Failed to send updated data to client: {e:?}",);
                }
                true
            }) as NotifyFn,
        )));
        let active_setting = Rc::new(RefCell::new(ActiveSetting::new(configuration.settings, 0)));
        Rc::new(Self {
            hanging_get,
            calibrator,
            active_setting,
            rgbc_to_lux_coefs: configuration.rgbc_to_lux_coefficients,
            si_scaling_factors: configuration.si_scaling_factors,
            vendor_id: configuration.vendor_id,
            product_id: configuration.product_id,
        })
    }

    pub async fn handle_light_sensor_request_stream(
        self: &Rc<Self>,
        mut stream: SensorRequestStream,
    ) -> Result<(), Error> {
        let subscriber = self.hanging_get.borrow_mut().new_subscriber();
        while let Some(request) =
            stream.try_next().await.context("Error handling light sensor request stream")?
        {
            match request {
                SensorRequest::Watch { responder } => {
                    subscriber
                        .register(responder)
                        .context("registering responder for Watch call")?;
                }
            }
        }

        Ok(())
    }

    /// Normalize raw sensor counts.
    ///
    /// I.e. values being read in dark lighting will be returned as their original value,
    /// but values in the brighter lighting will be returned larger, as a reading within the true
    /// output range of the light sensor.
    fn process_reading(&self, reading: Rgbc<u16>) -> Rgbc<u32> {
        let active_setting = self.active_setting.borrow().active_setting();
        let gain_bias = MAX_GAIN / active_setting.gain as u32;

        reading.map(|v| {
            div_round_closest(v as u32 * gain_bias * MAX_ATIME, num_cycles(active_setting.atime))
        })
    }

    /// Calculates the lux of a reading.
    fn calculate_lux(&self, reading: Rgbc<f32>) -> f32 {
        Rgbc::multi_map(reading, self.rgbc_to_lux_coefs, |reading, coef| reading * coef)
            .fold(0.0, |lux, c| lux + c)
            .max(0.0)
    }
}

impl<T> LightSensorHandler<T>
where
    T: Calibrate,
{
    async fn get_calibrated_data(
        &self,
        reading: Rgbc<u16>,
        device_proxy: InputDeviceProxy,
    ) -> Result<LightReading, Error> {
        let uncalibrated_rgbc = self.process_reading(reading);
        let rgbc = self.calibrator.calibrate(uncalibrated_rgbc.map(|v| v as f32));
        let rgbc = (self.si_scaling_factors * rgbc).map(|c| c / ADC_SCALING_FACTOR);
        let lux = self.calculate_lux(rgbc);
        let cct = correlated_color_temperature(rgbc);
        // TODO(fxbug.dev/110275) Enable this code once all clients are using fuchsia.lightsensor.Sensor
        // rather than fuchsia.settings.LightSensor. The latter does not account for adjustments
        // and will return incorrect values to clients if any ajdustments occur.
        // Update the sensor after the active setting has been used for calculations, since it may
        // change after this call.
        if false {
            self.active_setting
                .borrow_mut()
                .adjust(reading, device_proxy)
                .await
                .context("updating active setting")?;
        }

        // TODO(fxbug.dev/110275) Use calibrated rgbc in SI units once all clients are using lux and cct.
        let rgbc = uncalibrated_rgbc.map(|c| c as f32);
        Ok(LightReading { rgbc, lux, cct })
    }
}

/// Converts atime values to microseconds.
fn to_us(atime: u32) -> u32 {
    num_cycles(atime) * MIN_TIME_STEP_US
}

/// Divides n by d, rounding up.
fn div_round_up(n: u32, d: u32) -> u32 {
    (n + d - 1) / d
}

/// Divides n by d, rounding to the closest value.
fn div_round_closest(n: u32, d: u32) -> u32 {
    (n + (d / 2)) / d
}

// These values are defined in //src/devices/light-sensor/ams-light/tcs3400.cc
const MAX_SATURATION_RED: u16 = 21_067;
const MAX_SATURATION_GREEN: u16 = 20_395;
const MAX_SATURATION_BLUE: u16 = 20_939;
const MAX_SATURATION_CLEAR: u16 = 65_085;

fn saturated(reading: Rgbc<u16>) -> bool {
    reading.red >= MAX_SATURATION_RED
        && reading.green >= MAX_SATURATION_GREEN
        && reading.blue >= MAX_SATURATION_BLUE
        && reading.clear >= MAX_SATURATION_CLEAR
}

// The details can be found on equation 7 on page 5 (7 in pdf) in the AMS documentation accessed
// at https://ams.com/documents/20143/36005/TCS34xx_AN000517_1-00.pdf, last accessed on
// August 18, 2022.
fn correlated_color_temperature(reading: Rgbc<f32>) -> f32 {
    let n = (0.23881 * reading.red + 0.25499 * reading.green - 0.58291 * reading.blue)
        / (0.11109 * reading.red - 0.85406 * reading.green + 0.52289 * reading.blue);
    let n2 = n * n;
    let n3 = n2 * n;
    449.0 * n3 + 3525.0 * n2 + 6823.3 * n + 5520.33
}

#[async_trait(?Send)]
impl<T> InputHandler for LightSensorHandler<T>
where
    T: Calibrate + 'static,
{
    async fn handle_input_event(self: Rc<Self>, mut input_event: InputEvent) -> Vec<InputEvent> {
        if let InputEvent {
            device_event: InputDeviceEvent::LightSensor(ref light_sensor_event),
            device_descriptor: InputDeviceDescriptor::LightSensor(ref light_sensor_descriptor),
            event_time: _,
            handled: Handled::No,
            trace_id: _,
        } = input_event
        {
            // Validate descriptor matches.
            if !(light_sensor_descriptor.vendor_id == self.vendor_id
                && light_sensor_descriptor.product_id == self.product_id)
            {
                // Don't handle the event.
                fx_log_warn!(
                    "Unexpected device in light sensor handler: {:?}",
                    light_sensor_descriptor,
                );
                return vec![input_event];
            }

            let LightReading { rgbc, lux, cct } = match self
                .get_calibrated_data(
                    light_sensor_event.rgbc,
                    light_sensor_event.device_proxy.clone(),
                )
                .await
            {
                Ok(data) => data,
                Err(e) => {
                    fx_log_warn!("Failed to get light sensor readings: {e:?}");
                    // Don't handle the event.
                    return vec![input_event];
                }
            };
            let publisher = self.hanging_get.borrow().new_publisher();
            publisher.set(LightSensorData {
                rgbc,
                calculated_lux: lux,
                correlated_color_temperature: cct,
            });
            input_event.handled = Handled::Yes;
        }
        vec![input_event]
    }
}

#[derive(Copy, Clone, PartialEq)]
struct LightSensorData {
    rgbc: Rgbc<f32>,
    calculated_lux: f32,
    correlated_color_temperature: f32,
}

impl From<LightSensorData> for FidlLightSensorData {
    fn from(data: LightSensorData) -> Self {
        Self {
            rgbc: Some(FidlRgbc::from(data.rgbc)),
            calculated_lux: Some(data.calculated_lux),
            correlated_color_temperature: Some(data.correlated_color_temperature),
            ..FidlLightSensorData::EMPTY
        }
    }
}

impl From<Rgbc<f32>> for FidlRgbc {
    fn from(rgbc: Rgbc<f32>) -> Self {
        Self {
            red_intensity: rgbc.red,
            green_intensity: rgbc.green,
            blue_intensity: rgbc.blue,
            clear_intensity: rgbc.clear,
        }
    }
}

#[cfg(test)]
mod light_sensor_handler_tests;
