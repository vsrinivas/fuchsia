// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use anyhow::Error;
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_syslog::fx_log_info;
use fuchsia_zircon::Duration;
use led::{Led, LedControl};
use lib::backlight::{Backlight, BacklightControl};
use lib::sensor::{Sensor, SensorControl};

const SMALL_SLEEP_TIME_MS: i64 = 1;
const LARGE_SLEEP_TIME_MS: i64 = 500;
// Threshold for deciding whether we use a large or small sleep time
const SMALL_BRIGHTNESS_CHANGE: f64 = 0.01;

// Simple linear correlation between light sensor readings and LED and backlight brightness
// This is the approximate light reading in a bright room, divide the reading by this
// factor to get the screen and LED brightness on a scale of 0.0 to 1.0.
const LIGHT_FACTOR: f64 = 18000.0;

// Range of values that may be sent to the screen
const MIN_LIGHT: f64 = 0.003;
const MAX_LIGHT: f64 = 1.0;

// If the brightness size is larger than the BRIGHTNESS[i] then use STEP_SIZE[i].
// The default is to use the final STEP_SIZE value
const BRIGHTNESS: [f64; 3] = [0.5, 0.3, 0.1];
const STEP_SIZE: [f64; 4] = [0.05, 0.01, 0.005, 0.001];

mod led;

pub struct Control {}

impl Control {
    pub async fn run(
        sensor: &impl SensorControl,
        backlight: &impl BacklightControl,
        leds: &mut impl LedControl,
    ) -> Result<(), Error> {
        let num_leds = leds.get_num_lights().await.context("error received get num lights")?;
        fx_log_info!("There is/are {} led(s)", num_leds);
        for i in 0..num_leds {
            let info = match leds.get_info(i).await {
                Ok(Ok(info)) => info,
                _ => continue,
            };
            fx_log_info!("LED {} is {:?}", i + 1, info);
        }
        loop {
            let sleep_time =
                Self::run_auto_brightness_step(sensor, backlight, leds, num_leds).await?;
            fuchsia_async::Timer::new(sleep_time.after_now()).await;
        }
    }

    async fn run_auto_brightness_step(
        sensor: &impl SensorControl,
        backlight: &impl BacklightControl,
        leds: &mut impl LedControl,
        num_leds: u32,
    ) -> Result<Duration, Error> {
        let mut sleep_time = Duration::from_millis(SMALL_SLEEP_TIME_MS);
        if let Some(ambient_light_input_rpt) = sensor.read().await? {
            let brightness = backlight.get_brightness().await?;
            let mut new_brightness = num_traits::clamp(
                ambient_light_input_rpt.illuminance as f64 / LIGHT_FACTOR,
                MIN_LIGHT,
                MAX_LIGHT,
            );
            // Choose a step size that is not noticeable.
            // The brighter it is the larger the step size we can get away with
            let mut step_size = STEP_SIZE[STEP_SIZE.len() - 1];
            for i in 0..BRIGHTNESS.len() {
                if brightness > BRIGHTNESS[i] {
                    step_size = STEP_SIZE[i];
                    break;
                }
            }
            let desired_brightness = new_brightness;
            new_brightness = if new_brightness > brightness {
                num_traits::clamp(brightness + step_size, 0.0, new_brightness)
            } else {
                num_traits::clamp(brightness - step_size, new_brightness, 1.0)
            };
            new_brightness = num_traits::clamp(new_brightness, MIN_LIGHT, MAX_LIGHT);
            backlight.set_brightness(new_brightness).await?;
            for i in 0..num_leds {
                let _ = leds.set_brightness(i, new_brightness).await.unwrap();
            }

            // If we are not close to the required brightness then use short sleep time
            let step_time =
                if num_traits::abs(new_brightness - desired_brightness) > SMALL_BRIGHTNESS_CHANGE {
                    SMALL_SLEEP_TIME_MS
                } else {
                    LARGE_SLEEP_TIME_MS
                };
            sleep_time = Duration::from_millis(step_time);
        }
        Ok(sleep_time)
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auto-brightness"])?;
    fx_log_info!("Started");

    let backlight = Backlight::without_display_power()?;
    let mut leds = Led::new().await?;
    let sensor = Sensor::new().await;

    Control::run(&sensor, &backlight, &mut leds).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{Control, LIGHT_FACTOR};
    use crate::led::LedControl;
    use anyhow::Error;
    use async_trait::async_trait;
    use fidl_fuchsia_hardware_light::{Capability, Info, LightError};
    use fuchsia_async::{self as fasync};
    use fuchsia_zircon::Duration;
    use futures::lock::Mutex;
    use lib::sensor::AmbientLightInputRpt;
    use lib::{backlight::BacklightControl, sensor::SensorControl};
    use std::sync::Arc;

    struct MockSensor {
        next_reading: f32,
    }

    #[async_trait]
    impl SensorControl for MockSensor {
        async fn read(&self) -> Result<Option<AmbientLightInputRpt>, Error> {
            let report = AmbientLightInputRpt {
                illuminance: self.next_reading,
                red: 0.0,
                green: 0.0,
                blue: 0.0,
            };
            Ok(Some(report))
        }
    }

    #[derive(Debug)]
    struct MockBacklight {
        brightness: Arc<Mutex<f64>>,
    }

    #[async_trait]
    impl BacklightControl for MockBacklight {
        async fn get_brightness(&self) -> Result<f64, Error> {
            let brightness = self.brightness.lock().await;
            Ok(*brightness)
        }
        async fn set_brightness(&self, value: f64) -> Result<(), Error> {
            let mut brightness = self.brightness.lock().await;
            *brightness = value;
            Ok(())
        }
        async fn get_max_absolute_brightness(&self) -> Result<f64, Error> {
            Ok(0.0)
        }
    }

    struct MockLed {
        num_lights: u32,
        brightness: f64,
        info: Info,
    }

    #[async_trait]
    impl LedControl for MockLed {
        async fn set_brightness(
            &mut self,
            _index: u32,
            value: f64,
        ) -> Result<Result<(), LightError>, fidl::Error> {
            self.brightness = value;
            Ok(Ok(()))
        }
        async fn get_num_lights(&mut self) -> Result<u32, fidl::Error> {
            Ok(self.num_lights)
        }
        async fn get_info(&self, _index: u32) -> Result<Result<Info, LightError>, fidl::Error> {
            Ok(Ok(self.info.clone()))
        }
    }

    struct Devices {
        sensor: MockSensor,
        backlight: MockBacklight,
        leds: MockLed,
    }

    fn setup(next_reading: f64, brightness: f64) -> Devices {
        let sensor = MockSensor { next_reading: next_reading as f32 };
        let backlight = MockBacklight { brightness: Arc::new(Mutex::new(brightness)) };
        let leds = MockLed {
            brightness: 0.0,
            num_lights: 1,
            info: Info { name: "amber".into(), capability: Capability::Brightness },
        };
        Devices { sensor: sensor, backlight: backlight, leds: leds }
    }

    async fn run_step(devices: &mut Devices) -> Duration {
        let num_leds = 1;
        Control::run_auto_brightness_step(
            &devices.sensor,
            &devices.backlight,
            &mut devices.leds,
            num_leds,
        )
        .await
        .expect("a Duration")
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_brightness_large_sleep_test() {
        let mut devices = setup(0.0 * LIGHT_FACTOR, 0.0);

        let sleep_time = run_step(&mut devices).await;
        assert_eq!(sleep_time.into_millis(), 500);
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_brightness_small_sleep_test() {
        let mut devices = setup(0.5 * LIGHT_FACTOR, 0.0);

        let sleep_time = run_step(&mut devices).await;
        assert_eq!(sleep_time.into_millis(), 1);
    }

    async fn auto_brightness_step_size_test(brightness: f64, expected_step_size: f64) {
        let mut devices = setup(0.0, brightness);

        run_step(&mut devices).await;
        let new_brightness = devices.backlight.get_brightness().await.unwrap();
        let step_size = brightness - new_brightness;
        assert!((step_size - expected_step_size).abs() < 0.000001);
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_brightness_step_size1_test() {
        auto_brightness_step_size_test(0.6, 0.05).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_brightness_step_size2_test() {
        auto_brightness_step_size_test(0.4, 0.01).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_brightness_step_size3_test() {
        auto_brightness_step_size_test(0.2, 0.005).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_brightness_step_size4_test() {
        auto_brightness_step_size_test(0.01, 0.001).await;
    }
}
