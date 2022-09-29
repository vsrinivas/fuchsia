// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::types::FileLoader;
use crate::light_sensor::led_watcher::LedState;
use crate::light_sensor::types::{Calibration, Parameters, Rgbc};
use anyhow::{format_err, Context, Error};
use async_trait::async_trait;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_factory::MiscFactoryStoreProviderProxy;
use fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, OpenFlags};
use std::cell::RefCell;
use std::path::Path;
use std::rc::Rc;

/// Represents the types of LEDs that are tracked.
#[derive(Debug, Clone)]
enum LedType {
    /// Represents the backlight display.
    Backlight,
    /// Represents a specific LED on the device.
    Named(String),
}

/// Tracks the coexistence (coex) impact and last brightness of a particular LED. The coex impact is
/// the impact an LED has on the light sensor when it is turned on.
#[derive(Debug, Clone)]
struct CoexLed {
    led_type: LedType,
    /// The impact of the LED on the light sensor when it's turned on to its maximum brightest.
    coex_at_max: Rgbc<f32>,
    /// The last read brightness for this LED.
    last_brightness: Option<f32>,
}

impl CoexLed {
    fn new(led_type: LedType, coex_at_max: Rgbc<f32>) -> Self {
        Self { led_type, coex_at_max, last_brightness: None }
    }
}

/// Subtract the right value's intercept from the left value's intercept for each color channel.
fn calculate_coex(left: Rgbc<Parameters>, right: Rgbc<Parameters>) -> Rgbc<f32> {
    left.map(|c| c.intercept) - right.map(|c| c.intercept)
}

pub trait Calibrate {
    /// Calibrate the supplied `sensor_data`.
    fn calibrate(&self, rgbc: Rgbc<f32>) -> Rgbc<f32>;
}

/// Handles the calibration calculation.
#[derive(Clone)]
pub struct Calibrator<T> {
    calibration: Calibration,
    led_state: T,
    coex_leds: Rc<RefCell<Vec<CoexLed>>>,
}

impl<T> Calibrator<T>
where
    T: LedState,
{
    /// Create a new [CalibrationController].
    pub(crate) fn new(calibration: Calibration, led_state: T) -> Self {
        let mut coex_leds: Vec<_> = led_state
            .light_groups()
            .into_iter()
            .filter_map(|(name, light_group)| {
                calibration.leds().get(&name).copied().map(|cal| (light_group, cal))
            })
            .map(|(light_group, cal)| {
                let coex_at_max = calculate_coex(cal, calibration.off());
                CoexLed::new(LedType::Named(light_group.name().clone()), coex_at_max)
            })
            .collect();
        // To get the coex for the backlight, get the coex for all lights on...
        let mut all_coex_at_max = calculate_coex(calibration.all_on(), calibration.off());

        // and then remove the impact from the other leds.
        for coex_led in &coex_leds {
            all_coex_at_max = all_coex_at_max - coex_led.coex_at_max;
        }

        let _ = coex_leds.push(CoexLed::new(LedType::Backlight, all_coex_at_max));
        Self { calibration, led_state, coex_leds: Rc::new(RefCell::new(coex_leds)) }
    }
}

impl<T> Calibrate for Calibrator<T>
where
    T: LedState,
{
    fn calibrate(&self, sensor_data: Rgbc<f32>) -> Rgbc<f32> {
        let backlight_brightness = self.led_state.backlight_brightness();
        let light_groups = self.led_state.light_groups();
        let total_coex_impact =
            self.coex_leds.borrow_mut().iter_mut().fold(Rgbc::<f32>::default(), |acc, coex_led| {
                let current_brightness = match &coex_led.led_type {
                    LedType::Backlight => {
                        if backlight_brightness <= 0.0 {
                            coex_led.last_brightness = None;
                            return acc;
                        }

                        backlight_brightness
                    }
                    LedType::Named(led_name) => {
                        let light_group = match light_groups.get(&*led_name) {
                            None => return acc,
                            Some(light_group) => light_group,
                        };
                        if let Some(brightness) = light_group.brightness() {
                            brightness
                        } else {
                            coex_led.last_brightness = None;
                            return acc;
                        }
                    }
                };

                let mean_brightness = match (current_brightness, coex_led.last_brightness) {
                    (current_brightness, Some(last_brightness)) if current_brightness >= 0.0 => {
                        (current_brightness + last_brightness) / 2.0
                    }
                    (_, Some(last_brightness)) => last_brightness,
                    (current_brightness, _) => current_brightness,
                };

                coex_led.last_brightness = Some(current_brightness);
                acc + coex_led.coex_at_max.map(|c| mean_brightness * c)
            });
        let compensated_for_coex = (sensor_data - total_coex_impact).map(|c| c.max(0.0));
        compensated_for_coex * self.calibration.calibrated_slope()
    }
}

pub struct FactoryFileLoader {
    directory_proxy: DirectoryProxy,
}

impl FactoryFileLoader {
    // TODO(fxbug.dev/100664) Remove allow once used.
    #[allow(dead_code)]
    pub fn new(factory_store_proxy: MiscFactoryStoreProviderProxy) -> Result<Self, Error> {
        let (directory_proxy, directory_server_end) = create_proxy::<DirectoryMarker>()
            .map_err(|e| format_err!("{:?}", e))
            .context("Failed to create directory proxy")?;
        factory_store_proxy
            .get_factory_store(directory_server_end)
            .map_err(|e| format_err!("{:?}", e))
            .context("Update to get factory store")?;
        Ok(Self { directory_proxy })
    }
}

#[async_trait(?Send)]
impl FileLoader for FactoryFileLoader {
    async fn load_file(&self, file_path: &Path) -> Result<String, Error> {
        let file_proxy = fuchsia_fs::open_file(
            &self.directory_proxy,
            file_path,
            OpenFlags::NOT_DIRECTORY | OpenFlags::RIGHT_READABLE,
        )
        .with_context(|| format!("Failed to open configuration at {:?}", file_path))?;
        fuchsia_fs::read_file(&file_proxy)
            .await
            .map_err(|e| format_err!("{:?}", e))
            .with_context(|| format!("Failed to read contents of {:?}", file_path))
    }
}

#[cfg(test)]
mod calibrator_tests;
