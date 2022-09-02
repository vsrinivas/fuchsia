// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::light_sensor::calibrator::{calculate_coex, Calibrate, Calibrator, LedType};
use crate::light_sensor::led_watcher::{LedState, LightGroup};
use crate::light_sensor::test_utils::{close_enough, LED1_NAME, LED2_NAME};
use crate::light_sensor::types::{Calibration, Parameters, Rgbc};
use std::cell::RefCell;
use std::collections::HashMap;
use std::mem::size_of;
use std::rc::Rc;

#[fuchsia::test]
fn led_type_check_null_ptr_optimization() {
    assert_eq!(size_of::<LedType>(), size_of::<String>(), "LedType should optimize to String");
}

#[fuchsia::test]
fn calculate_coex_only_accounts_for_intercept() {
    let left = Rgbc {
        red: Parameters { slope: 1.0, intercept: 5.0 },
        green: Parameters { slope: 1.0, intercept: 4.0 },
        blue: Parameters { slope: 1.0, intercept: 3.0 },
        clear: Parameters { slope: 1.0, intercept: 2.0 },
    };
    let right = Rgbc {
        red: Parameters { slope: 1.0, intercept: 1.0 },
        green: Parameters { slope: 1.0, intercept: 2.0 },
        blue: Parameters { slope: 1.0, intercept: 0.5 },
        clear: Parameters { slope: 1.0, intercept: 1.0 },
    };
    let coex = calculate_coex(left, right);
    assert!(Rgbc::match_all(
        coex,
        Rgbc { red: 4.0, green: 2.0, blue: 2.5, clear: 1.0 },
        close_enough
    ));
}

#[derive(Clone)]
struct FakeLedState {
    light_groups: Rc<RefCell<HashMap<String, LightGroup>>>,
    backlight_brightness: Rc<RefCell<f32>>,
}

impl FakeLedState {
    fn new(light_groups: HashMap<String, LightGroup>, backlight_brightness: f32) -> Self {
        Self {
            light_groups: Rc::new(RefCell::new(light_groups)),
            backlight_brightness: Rc::new(RefCell::new(backlight_brightness)),
        }
    }

    fn set_brightness(&self, brightness: f32) {
        *self.backlight_brightness.borrow_mut() = brightness;
    }

    fn set_light_group_brightness(&self, name: &str, brightness: Option<f32>) -> bool {
        self.light_groups
            .borrow_mut()
            .get_mut(name)
            .map(|light_group| light_group.set_brightness_for_test(brightness))
            .is_some()
    }
}

impl LedState for FakeLedState {
    fn light_groups(&self) -> HashMap<String, LightGroup> {
        Clone::clone(&*self.light_groups.borrow())
    }

    fn backlight_brightness(&self) -> f32 {
        *self.backlight_brightness.borrow()
    }
}

#[fuchsia::test(allow_stalls = false)]
async fn controller_new_initializes_coex_leds() {
    let light_groups = HashMap::from([
        // A light group that's not in the calibration data is ignored.
        (String::from("not_calibrated"), LightGroup::new(String::from("not_calibrated"), None)),
        // Light groups found in the calibration data are incorporated.
        (String::from("calibrated1"), LightGroup::new(String::from("calibrated1"), None)),
        (String::from("calibrated2"), LightGroup::new(String::from("calibrated2"), None)),
    ]);
    let calibration = Calibration::new_for_test(
        // leds
        HashMap::from([
            (
                String::from("calibrated1"),
                Rgbc {
                    red: Parameters { slope: 17.0, intercept: 21.0 },
                    green: Parameters { slope: 18.0, intercept: 22.0 },
                    blue: Parameters { slope: 19.0, intercept: 23.0 },
                    clear: Parameters { slope: 20.0, intercept: 24.0 },
                },
            ),
            (
                String::from("calibrated2"),
                Rgbc {
                    red: Parameters { slope: 9.0, intercept: 13.0 },
                    green: Parameters { slope: 10.0, intercept: 14.0 },
                    blue: Parameters { slope: 11.0, intercept: 15.0 },
                    clear: Parameters { slope: 12.0, intercept: 16.0 },
                },
            ),
            // Calibration data not found in light groups is ignored.
            (
                String::from("calibrated3"),
                Rgbc {
                    red: Parameters { slope: 0.0, intercept: 0.0 },
                    green: Parameters { slope: 0.0, intercept: 0.0 },
                    blue: Parameters { slope: 0.0, intercept: 0.0 },
                    clear: Parameters { slope: 0.0, intercept: 0.0 },
                },
            ),
        ]),
        // off
        Rgbc {
            red: Parameters { slope: 1.0, intercept: 100.0 },
            green: Parameters { slope: 2.0, intercept: 99.0 },
            blue: Parameters { slope: 3.0, intercept: 98.0 },
            clear: Parameters { slope: 4.0, intercept: 97.0 },
        },
        // all_on
        Rgbc {
            red: Parameters { slope: 25.0, intercept: 29.0 },
            green: Parameters { slope: 26.0, intercept: 30.0 },
            blue: Parameters { slope: 27.0, intercept: 31.0 },
            clear: Parameters { slope: 28.0, intercept: 32.0 },
        },
        // calibrated_slope
        Rgbc { red: 1.0, green: 2.0, blue: 3.0, clear: 4.0 },
    );

    let fake_led_state = FakeLedState::new(light_groups, 0.0);

    // Ensure that backlight_brightness and total_cwa, ifake_led_state).t;
    let calibrator = Calibrator::new(calibration, fake_led_state);

    // Only calibrator leds should be tracked, 2 that were the union of leds in the light groups and
    // calibration, and a manually added backlight.
    let coex_leds = &calibrator.coex_leds.borrow();
    assert_eq!(coex_leds.len(), 3);
    assert!(coex_leds.iter().all(|c| c.last_brightness.is_none()));
    assert!(coex_leds.iter().any(|c| matches!(c.led_type, LedType::Backlight)
        && Rgbc::match_all(
            c.coex_at_max,
            Rgbc { red: 95.0, green: 93.0, blue: 91.0, clear: 89.0 },
            close_enough
        )));
    assert!(coex_leds.iter().any(
        |c| matches!(c.led_type, LedType::Named(ref name) if name == "calibrated1")
            && Rgbc::match_all(
                c.coex_at_max,
                Rgbc { red: -79.0, green: -77.0, blue: -75.0, clear: -73.0 },
                close_enough
            )
    ));
    assert!(coex_leds.iter().any(
        |c| matches!(c.led_type, LedType::Named(ref name) if name == "calibrated2")
            && Rgbc::match_all(
                c.coex_at_max,
                Rgbc { red: -87.0, green: -85.0, blue: -83.0, clear: -81.0 },
                close_enough
            )
    ));
}

#[fuchsia::test(allow_stalls = false)]
async fn controller_calibrate_returns_calibrated_values() {
    let calibration = Calibration::new_for_test(
        // leds
        HashMap::from([
            (
                String::from(LED1_NAME),
                Rgbc {
                    red: Parameters { slope: 0.9, intercept: 0.1 },
                    green: Parameters { slope: 1.2, intercept: 0.2 },
                    blue: Parameters { slope: 0.7, intercept: 0.0 },
                    clear: Parameters { slope: 0.9, intercept: 0.2 },
                },
            ),
            (
                String::from(LED2_NAME),
                Rgbc {
                    red: Parameters { slope: 0.9, intercept: 0.0 },
                    green: Parameters { slope: 1.2, intercept: 0.9 },
                    blue: Parameters { slope: 0.9, intercept: 0.1 },
                    clear: Parameters { slope: 1.1, intercept: 0.3 },
                },
            ),
        ]),
        // off
        Rgbc {
            red: Parameters { slope: 0.0, intercept: 0.0 },
            green: Parameters { slope: 0.0, intercept: 0.0 },
            blue: Parameters { slope: 0.0, intercept: 0.0 },
            clear: Parameters { slope: 0.1, intercept: 0.0 },
        },
        // all_on
        Rgbc {
            red: Parameters { slope: 0.3, intercept: 1.0 },
            green: Parameters { slope: 1.2, intercept: 1.3 },
            blue: Parameters { slope: 0.3, intercept: 1.1 },
            clear: Parameters { slope: 0.5, intercept: 0.8 },
        },
        // calibrated_slope
        Rgbc { red: 1.33, green: 1.13, blue: 1.03, clear: 1.23 },
    );

    let light_groups = HashMap::from([
        (String::from(LED1_NAME), LightGroup::new(String::from(LED1_NAME), None)),
        (String::from(LED2_NAME), LightGroup::new(String::from(LED2_NAME), None)),
    ]);
    let fake_led_state = FakeLedState::new(light_groups, 0.0);
    let calibrator = Calibrator::new(calibration, fake_led_state.clone());

    const NEW_BRIGHTNESS: f32 = 0.8;
    fake_led_state.set_brightness(NEW_BRIGHTNESS);
    const NEW_LED1_BRIGHTNESS: f32 = 0.6;
    const NEW_LED2_BRIGHTNESS: f32 = 0.7;
    assert!(fake_led_state.set_light_group_brightness(LED1_NAME, Some(NEW_LED1_BRIGHTNESS)));
    assert!(fake_led_state.set_light_group_brightness(LED2_NAME, Some(NEW_LED2_BRIGHTNESS)));

    let calibrated = calibrator.calibrate(Rgbc { red: 1.1, green: 1.2, blue: 1.3, clear: 1.4 });
    assert!(
        Rgbc::match_all(
            calibrated,
            Rgbc { red: 0.4256, green: 0.3277, blue: 0.4429, clear: 1.0209 },
            close_enough
        ),
        "{:?}",
        calibrated
    );
}

#[fuchsia::test(allow_stalls = false)]
async fn controller_calibrate_returns_calibrated_values_when_lights_off() {
    let calibration = Calibration::new_for_test(
        // leds
        HashMap::from([
            (
                String::from(LED1_NAME),
                Rgbc {
                    red: Parameters { slope: 0.9, intercept: 0.1 },
                    green: Parameters { slope: 1.2, intercept: 0.2 },
                    blue: Parameters { slope: 0.7, intercept: 0.0 },
                    clear: Parameters { slope: 0.9, intercept: 0.2 },
                },
            ),
            (
                String::from(LED2_NAME),
                Rgbc {
                    red: Parameters { slope: 0.9, intercept: 0.0 },
                    green: Parameters { slope: 1.2, intercept: 0.9 },
                    blue: Parameters { slope: 0.9, intercept: 0.1 },
                    clear: Parameters { slope: 1.1, intercept: 0.3 },
                },
            ),
        ]),
        // off
        Rgbc {
            red: Parameters { slope: 0.0, intercept: 0.0 },
            green: Parameters { slope: 0.0, intercept: 0.0 },
            blue: Parameters { slope: 0.0, intercept: 0.0 },
            clear: Parameters { slope: 0.1, intercept: 0.0 },
        },
        // all_on
        Rgbc {
            red: Parameters { slope: 0.3, intercept: 1.0 },
            green: Parameters { slope: 1.2, intercept: 1.3 },
            blue: Parameters { slope: 0.3, intercept: 1.1 },
            clear: Parameters { slope: 0.5, intercept: 0.8 },
        },
        // calibrated_slope
        Rgbc { red: 1.33, green: 1.13, blue: 1.03, clear: 1.23 },
    );

    // Default values are all off.
    let light_groups = HashMap::from([
        (String::from(LED1_NAME), LightGroup::new(String::from(LED1_NAME), None)),
        (String::from(LED2_NAME), LightGroup::new(String::from(LED2_NAME), None)),
    ]);
    let fake_led_state = FakeLedState::new(light_groups, 0.3);
    let calibrator = Calibrator::new(calibration, fake_led_state);
    let calibrated = calibrator.calibrate(Rgbc { red: 1.1, green: 1.2, blue: 1.3, clear: 1.4 });
    assert!(
        Rgbc::match_all(
            calibrated,
            Rgbc { red: 1.1039, green: 1.2882, blue: 1.03, clear: 1.6113 },
            close_enough
        ),
        "{:?}",
        calibrated
    );
}

#[fuchsia::test(allow_stalls = false)]
async fn controller_calibrate_clamps_min_value_to_zeros() {
    let calibration = Calibration::new_for_test(
        // leds
        HashMap::from([
            (
                String::from(LED1_NAME),
                Rgbc {
                    red: Parameters { slope: 0.9, intercept: 0.1 },
                    green: Parameters { slope: 1.2, intercept: 0.2 },
                    blue: Parameters { slope: 0.7, intercept: 0.0 },
                    clear: Parameters { slope: 0.9, intercept: 0.2 },
                },
            ),
            (
                String::from(LED2_NAME),
                Rgbc {
                    red: Parameters { slope: 0.9, intercept: 0.0 },
                    green: Parameters { slope: 1.2, intercept: 0.9 },
                    blue: Parameters { slope: 0.9, intercept: 0.1 },
                    clear: Parameters { slope: 1.1, intercept: 0.3 },
                },
            ),
        ]),
        // off
        Rgbc {
            red: Parameters { slope: 0.0, intercept: 0.0 },
            green: Parameters { slope: 0.0, intercept: 0.0 },
            blue: Parameters { slope: 0.0, intercept: 0.0 },
            clear: Parameters { slope: 0.1, intercept: 0.0 },
        },
        // all_on
        Rgbc {
            red: Parameters { slope: 0.3, intercept: 1.0 },
            green: Parameters { slope: 1.2, intercept: 1.3 },
            blue: Parameters { slope: 0.3, intercept: 1.1 },
            clear: Parameters { slope: 0.5, intercept: 0.8 },
        },
        // calibrated_slope
        Rgbc { red: 1.33, green: 1.13, blue: 1.03, clear: 1.23 },
    );

    // Default values are all off.
    let light_groups = HashMap::from([
        (String::from(LED1_NAME), LightGroup::new(String::from(LED1_NAME), None)),
        (String::from(LED2_NAME), LightGroup::new(String::from(LED2_NAME), None)),
    ]);
    let fake_led_state = FakeLedState::new(light_groups, 0.0);
    let calibrator = Calibrator::new(calibration, fake_led_state);
    let calibrated = calibrator.calibrate(Rgbc { red: -1.0, green: -1.0, blue: -1.0, clear: -1.0 });
    assert!(
        Rgbc::match_all(
            calibrated,
            Rgbc { red: 0.0, green: 0.0, blue: 0.0, clear: 0.0 },
            close_enough
        ),
        "{:?}",
        calibrated
    );
}
