// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    correlated_color_temperature, div_round_closest, div_round_up, saturated, to_us, ActiveSetting,
    LightSensorHandler, MAX_SATURATION_BLUE, MAX_SATURATION_CLEAR, MAX_SATURATION_GREEN,
    MAX_SATURATION_RED,
};
use crate::input_device::{Handled, InputDeviceDescriptor, InputDeviceEvent, InputEvent};
use crate::input_handler::InputHandler;
use crate::light_sensor::calibrator::Calibrate;
use crate::light_sensor::types::{AdjustmentSetting, Rgbc, SensorConfiguration};
use crate::light_sensor_binding::{LightSensorDeviceDescriptor, LightSensorEvent};
use assert_matches::assert_matches;
use fasync::Task;
use fidl::endpoints::create_proxy_and_stream;
use fidl_fuchsia_input_report::{
    FeatureReport, InputDeviceGetFeatureReportResult, InputDeviceMarker, InputDeviceProxy,
    InputDeviceRequest, InputDeviceSetFeatureReportResult, SensorFeatureReport,
    SensorReportingState,
};
use fidl_fuchsia_lightsensor::{SensorMarker, SensorProxy, SensorRequestStream};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::StreamExt;
use std::cell::RefCell;
use std::rc::Rc;
use test_case::test_case;
use zx::Time;

const VENDOR_ID: u32 = 1;
const PRODUCT_ID: u32 = 2;

fn get_adjustment_settings() -> Vec<AdjustmentSetting> {
    vec![
        AdjustmentSetting { atime: 100, gain: 1 },
        AdjustmentSetting { atime: 100, gain: 4 },
        AdjustmentSetting { atime: 100, gain: 16 },
        AdjustmentSetting { atime: 100, gain: 64 },
        AdjustmentSetting { atime: 0, gain: 64 },
    ]
}

#[fuchsia::test]
fn to_us_converts_atime_to_microseconds() {
    let atime = 112;
    let us = to_us(atime);
    assert_eq!(us, 400_320);
}

#[test_case(11, 10 => 2; "1.1 rounds to 2")]
#[test_case(19, 10 => 2; "1.9 rounds to 2")]
#[fuchsia::test]
fn div_round_up_returns_ceil_of_div(n: u32, d: u32) -> u32 {
    div_round_up(n, d)
}

#[test_case(14, 10 => 1; "1.4 rounds to 1")]
#[test_case(15, 10 => 2; "1.5 rounds to 2")]
#[fuchsia::test]
fn div_round_closest_returns_half_rounding(n: u32, d: u32) -> u32 {
    div_round_closest(n, d)
}

#[test_case(Rgbc {
    red: MAX_SATURATION_RED,
    green: MAX_SATURATION_GREEN,
    blue: MAX_SATURATION_BLUE,
    clear: MAX_SATURATION_CLEAR,
} => true; "all max is saturated")]
#[test_case(Rgbc {
    red: MAX_SATURATION_RED,
    green: 0,
    blue: 0,
    clear: 0,
} => false; "only red max is not saturated")]
#[test_case(Rgbc {
    red: 0,
    green: MAX_SATURATION_GREEN,
    blue: 0,
    clear: 0,
} => false; "only green max is not saturated")]
#[test_case(Rgbc {
    red: 0,
    green: 0,
    blue: MAX_SATURATION_BLUE,
    clear: 0,
} => false; "only blue max is not saturated")]
#[test_case(Rgbc {
    red: 0,
    green: 0,
    blue: 0,
    clear: MAX_SATURATION_CLEAR,
} => false; "only clear max is not saturated")]
#[fuchsia::test]
fn saturated_cases(rgbc: Rgbc<u16>) -> bool {
    saturated(rgbc)
}

#[fuchsia::test]
fn cct() {
    let rgbc = Rgbc { red: 1.0, green: 2.0, blue: 3.0, clear: 4.0 };
    let cct = correlated_color_temperature(rgbc);
    // See doc-comment for `correlated_color_temperature`.
    // let n = (0.23881 * 1.0 + 0.25499 * 2.0 - 0.58291 * 3.0)
    //     / (0.11109 * 1.0 - 0.85406 * 2.0 + 0.52289 * 3.0)
    //      = 35.25881523272195;
    // let n2 = n * n = 1243.1840516152254;
    // let n3 = n2 * n = 43833.1967761679;
    // 449.0 * n3 + 3525.0 * n2 + 6823.3 * n + 5520.33
    // = 449.0 * 9.915565 + 3525.0 * 4.621159 + 6823.3 * n + 2.145688
    // = 24_309_116.0
    // = 24,309,430.938420482
    const EXPECTED_COLOR_TEMPERATURE: f32 = 24_309_116.0;
    assert!((cct - EXPECTED_COLOR_TEMPERATURE).abs() <= std::f32::EPSILON);
}

fn get_mock_device_proxy(
) -> (InputDeviceProxy, Rc<RefCell<Option<FeatureReport>>>, fasync::Task<()>) {
    get_mock_device_proxy_with_response(None, Ok(()))
}

fn get_mock_device_proxy_with_response(
    mut get_response: Option<InputDeviceGetFeatureReportResult>,
    mut response: InputDeviceSetFeatureReportResult,
) -> (InputDeviceProxy, Rc<RefCell<Option<FeatureReport>>>, fasync::Task<()>) {
    let (device_proxy, mut stream) =
        create_proxy_and_stream::<InputDeviceMarker>().expect("proxy created");
    let called = Rc::new(RefCell::new(Option::<FeatureReport>::None));
    let task = fasync::Task::local({
        let called = Rc::clone(&called);
        async move {
            while let Some(Ok(request)) = stream.next().await {
                match request {
                    InputDeviceRequest::GetFeatureReport { responder } => {
                        let mut response;
                        let response_ref = match get_response {
                            Some(ref mut response) => response,
                            None => {
                                response = Ok(match called.borrow().as_ref() {
                                    Some(report) => report.clone(),
                                    None => FeatureReport {
                                        sensor: Some(SensorFeatureReport {
                                            report_interval: Some(1),
                                            sensitivity: Some(vec![16]),
                                            reporting_state: Some(
                                                SensorReportingState::ReportAllEvents,
                                            ),
                                            threshold_high: Some(vec![1]),
                                            threshold_low: Some(vec![1]),
                                            sampling_rate: Some(100),
                                            ..SensorFeatureReport::EMPTY
                                        }),
                                        ..FeatureReport::EMPTY
                                    },
                                });
                                &mut response
                            }
                        };
                        responder.send(response_ref).expect("sending get response to test")
                    }
                    InputDeviceRequest::SetFeatureReport { report, responder } => {
                        *called.borrow_mut() = Some(report);
                        responder.send(&mut response).expect("sending set response to test");
                    }
                    _ => {} // no-op
                }
            }
        }
    });
    (device_proxy, called, task)
}

#[fuchsia::test(allow_stalls = false)]
async fn active_setting_adjusts_down_on_saturation() {
    let (device_proxy, called, task) = get_mock_device_proxy();
    let mut active_setting = ActiveSetting::new(get_adjustment_settings(), 1);
    active_setting
        .adjust(Rgbc { red: 21_067, green: 20_395, blue: 20_939, clear: 65_085 }, device_proxy)
        .await
        .expect("should succeed");
    assert_matches!(&*called.borrow(), &Some(FeatureReport {
        sensor: Some(SensorFeatureReport {
            sensitivity: Some(ref gains),
            sampling_rate: Some(100),
            ..
        }),
        ..
    }) if gains.len() == 1 && gains.contains(&1));
    task.await;
}

#[test_case(Rgbc {
    red: 65_535,
    green: 0,
    blue: 0,
    clear: 0,
}; "red")]
#[test_case(Rgbc {
    red: 0,
    green: 65_535,
    blue: 0,
    clear: 0,
}; "green")]
#[test_case(Rgbc {
    red: 0,
    green: 0,
    blue: 65_535,
    clear: 0,
}; "blue")]
#[test_case(Rgbc {
    red: 0,
    green: 0,
    blue: 0,
    clear: 65_535,
}; "clear")]
#[fuchsia::test(allow_stalls = false)]
async fn active_setting_adjusts_down_on_single_channel_saturation(rgbc: Rgbc<u16>) {
    let (device_proxy, called, task) = get_mock_device_proxy();
    let mut active_setting = ActiveSetting::new(get_adjustment_settings(), 1);
    active_setting.adjust(rgbc, device_proxy).await.expect("should succeed");
    assert_matches!(&*called.borrow(), &Some(FeatureReport {
        sensor: Some(SensorFeatureReport {
            sensitivity: Some(ref gains),
            sampling_rate: Some(100),
            ..
        }),
        ..
    }) if gains.len() == 1 && gains.contains(&1));
    task.await;
}

// Calculation for value in test
// let new_us = (256-new_atime)*2780; = (256-100)*2780 = 433_680
// let cur_us = (256-cur_atime)*2780;
// 65_534=v*((new_gain + cur_gain - 1) / cur_gain)*((new_us + cur_us - 1)/cur_us)+65_535/10
// v = (65_534-6_553)/((nagain + cgain - 1)/cgain)
// v = 14_745
#[test_case(Rgbc {
    red: 14_745,
    green: 0,
    blue: 0,
    clear: 0,
}; "red")]
#[test_case(Rgbc {
    red: 0,
    green: 14_745,
    blue: 0,
    clear: 0,
}; "green")]
#[test_case(Rgbc {
    red: 0,
    green: 0,
    blue: 14_745,
    clear: 0,
}; "blue")]
#[test_case(Rgbc {
    red: 0,
    green: 0,
    blue: 0,
    clear: 14_745,
}; "clear")]
#[fuchsia::test(allow_stalls = false)]
async fn active_setting_adjusts_up_on_low_readings(rgbc: Rgbc<u16>) {
    let (device_proxy, called, task) = get_mock_device_proxy();
    let mut active_setting = ActiveSetting::new(get_adjustment_settings(), 1);
    active_setting.adjust(rgbc, device_proxy).await.expect("should succeed");
    assert_matches!(&*called.borrow(), &Some(FeatureReport {
        sensor: Some(SensorFeatureReport {
            sensitivity: Some(ref gains),
            sampling_rate: Some(100),
            ..
        }),
        ..
    }) if gains.len() == 1 && gains.contains(&16));
    task.await;
}

// Value is one above the calculation above.
#[test_case(Rgbc {
    red: 14_746,
    green: 0,
    blue: 0,
    clear: 0,
}; "red")]
#[test_case(Rgbc {
    red: 0,
    green: 14_746,
    blue: 0,
    clear: 0,
}; "green")]
#[test_case(Rgbc {
    red: 0,
    green: 0,
    blue: 14_746,
    clear: 0,
}; "blue")]
#[test_case(Rgbc {
    red: 0,
    green: 0,
    blue: 0,
    clear: 14_746,
}; "clear")]
#[fuchsia::test(allow_stalls = false)]
async fn active_setting_does_not_adjust_on_high_readings(rgbc: Rgbc<u16>) {
    let (device_proxy, called, task) = get_mock_device_proxy();
    let mut active_setting = ActiveSetting::new(get_adjustment_settings(), 1);
    active_setting.adjust(rgbc, device_proxy).await.expect("should succeed");
    assert_matches!(&*called.borrow(), &None);
    task.await;
}

#[fuchsia::test(allow_stalls = false)]
async fn active_setting_adjusts_down_on_saturation_reports_error() {
    let (device_proxy, _, task) =
        get_mock_device_proxy_with_response(None, Err(zx::sys::ZX_ERR_CONNECTION_RESET));
    let mut active_setting = ActiveSetting::new(get_adjustment_settings(), 1);
    active_setting
        .adjust(Rgbc { red: 21_067, green: 20_395, blue: 20_939, clear: 65_085 }, device_proxy)
        .await
        .expect_err("should fail");
    task.await;
}

#[fuchsia::test(allow_stalls = false)]
async fn active_setting_adjusts_down_on_single_channel_saturation_reports_error() {
    let (device_proxy, _, task) =
        get_mock_device_proxy_with_response(None, Err(zx::sys::ZX_ERR_CONNECTION_RESET));
    let mut active_setting = ActiveSetting::new(get_adjustment_settings(), 1);
    active_setting
        .adjust(Rgbc { red: 65_535, green: 0, blue: 0, clear: 0 }, device_proxy)
        .await
        .expect_err("should fail");
    task.await;
}

#[fuchsia::test(allow_stalls = false)]
async fn active_setting_adjusts_up_on_low_readings_reports_error() {
    let (device_proxy, _, task) =
        get_mock_device_proxy_with_response(None, Err(zx::sys::ZX_ERR_CONNECTION_RESET));
    let mut active_setting = ActiveSetting::new(get_adjustment_settings(), 1);
    active_setting
        .adjust(Rgbc { red: 14_745, green: 0, blue: 0, clear: 0 }, device_proxy)
        .await
        .expect_err("should fail");
    task.await;
}

#[fuchsia::test]
fn light_sensor_handler_process_reading_lower_gain() {
    let sensor_configuration = SensorConfiguration {
        vendor_id: VENDOR_ID,
        product_id: PRODUCT_ID,
        rgbc_to_lux_coefficients: Rgbc { red: 1.0, green: 1.0, blue: 1.0, clear: 1.0 },
        si_scaling_factors: Rgbc { red: 1.0, green: 1.0, blue: 1.0, clear: 1.0 },
        settings: get_adjustment_settings(),
    };

    let handler = LightSensorHandler::new((), sensor_configuration);
    // Uses AdjustmentSetting with a gain of 1.
    let rgbc = handler.process_reading(Rgbc { red: 1, green: 2, blue: 3, clear: 4 });
    assert_eq!(rgbc, Rgbc { red: 105, green: 210, blue: 315, clear: 420 });
}

#[fuchsia::test]
fn light_sensor_handler_process_reading_higher_gain() {
    let sensor_configuration = SensorConfiguration {
        vendor_id: VENDOR_ID,
        product_id: PRODUCT_ID,
        rgbc_to_lux_coefficients: Rgbc { red: 1.0, green: 1.0, blue: 1.0, clear: 1.0 },
        si_scaling_factors: Rgbc { red: 1.0, green: 1.0, blue: 1.0, clear: 1.0 },
        settings: vec![AdjustmentSetting { atime: 100, gain: 64 }],
    };

    let handler = LightSensorHandler::new((), sensor_configuration);
    let rgbc = handler.process_reading(Rgbc { red: 1, green: 2, blue: 3, clear: 4 });
    assert_eq!(rgbc, Rgbc { red: 2, green: 3, blue: 5, clear: 7 });
}

#[fuchsia::test]
fn light_sensor_handler_calculate_lux() {
    let sensor_configuration = SensorConfiguration {
        vendor_id: VENDOR_ID,
        product_id: PRODUCT_ID,
        rgbc_to_lux_coefficients: Rgbc { red: 2.0, green: 3.0, blue: 5.0, clear: 7.0 },
        si_scaling_factors: Rgbc { red: 1.0, green: 1.0, blue: 1.0, clear: 1.0 },
        settings: vec![],
    };

    let handler = LightSensorHandler::new((), sensor_configuration);
    let lux = handler.calculate_lux(Rgbc { red: 11.0, green: 13.0, blue: 17.0, clear: 19.0 });
    assert_eq!(lux, 2.0 * 11.0 + 3.0 * 13.0 + 5.0 * 17.0 + 7.0 * 19.0);
}

/// Mock calibrator that just multiplies the input by 2.
struct DoublingCalibrator;

impl Calibrate for DoublingCalibrator {
    fn calibrate(&self, rgbc: Rgbc<f32>) -> Rgbc<f32> {
        rgbc.map(|c| c * 2.0)
    }
}

#[fuchsia::test(allow_stalls = false)]
async fn light_sensor_handler_get_calibrated_data() {
    let sensor_configuration = SensorConfiguration {
        vendor_id: VENDOR_ID,
        product_id: PRODUCT_ID,
        rgbc_to_lux_coefficients: Rgbc { red: 1.5, green: 1.6, blue: 1.7, clear: 1.8 },
        si_scaling_factors: Rgbc { red: 1.1, green: 1.2, blue: 1.3, clear: 1.4 },
        settings: get_adjustment_settings(),
    };

    let (device_proxy, called, task) = get_mock_device_proxy();
    let handler = LightSensorHandler::new(DoublingCalibrator, sensor_configuration);
    let reading = handler
        .get_calibrated_data(Rgbc { red: 1, green: 2, blue: 3, clear: 4 }, device_proxy.clone())
        .await;
    let reading = reading.expect("calibration should succeed");
    // = round(1 * 64 * 256 / (256 - 100)) * 2 * 1.1 / (16 * 712)
    assert!((reading.rgbc.red - 105.0).abs() <= f32::EPSILON);
    // = round(2 * 64 * 256 / (256 - 100)) * 2 * 1.2 / (16 * 712)
    assert!((reading.rgbc.green - 210.0).abs() <= f32::EPSILON);
    // = round(3 * 64 * 256 / (256 - 100)) * 2 * 1.3 / (16 * 712)
    assert!((reading.rgbc.blue - 315.0).abs() <= f32::EPSILON);
    // = round(4 * 64 * 256 / (256 - 100)) * 2 * 1.4 / (16 * 712)
    assert!((reading.rgbc.clear - 420.0).abs() <= f32::EPSILON);
    // = 0.020277388 * 1.5 + 0.044241577 * 1.6 + 0.07189256 * 1.7 + 0.103230335 * 1.8
    assert!((reading.lux - 0.40923458).abs() <= f32::EPSILON);
    // n = (0.23881 * 0.020277388 + 0.25499 * 0.044241577 - 0.58291 * 0.07189256)
    //   / (0.11109 * 0.020277388 - 0.85406 * 0.044241577 + 0.52289 * 0.07189256)
    // n = -12.51886739037
    // cct = 449.0 * n^3 + 3525.0 * n^2 + 6823.3 * n + 5520.33
    assert!((reading.cct - -408385.84).abs() <= f32::EPSILON);

    // TODO(fxbug.dev/110275) Renable assertion once adjustment enabled.
    if false {
        // The call should have adjusted the sensor.
        assert_matches!(&*called.borrow(), &Some(FeatureReport {
            sensor: Some(SensorFeatureReport {
                sensitivity: Some(ref gains),
                sampling_rate: Some(100),
                ..
            }),
            ..
        }) if gains.len() == 1 && gains.contains(&4));

        // Since the sensor is adjusted, reading the same values should now return a different result.
        let reading = handler
            .get_calibrated_data(Rgbc { red: 1, green: 2, blue: 3, clear: 4 }, device_proxy)
            .await;
        let reading = reading.expect("calibration should succeed");
        // = round(1 * 16 * 256 / (256 - 100)) * 2 * 1.1 / (16 * 712)
        assert!((reading.rgbc.red - 0.0050210673).abs() <= f32::EPSILON);
        // = round(2 * 16 * 256 / (256 - 100)) * 2 * 1.2 / (16 * 712)
        assert!((reading.rgbc.green - 0.011165731).abs() <= f32::EPSILON);
        // = round(3 * 16 * 256 / (256 - 100)) * 2 * 1.3 / (16 * 712)
        assert!((reading.rgbc.blue - 0.018030196).abs() <= f32::EPSILON);
        // = round(4 * 16 * 256 / (256 - 100)) * 2 * 1.4 / (16 * 712)
        assert!((reading.rgbc.clear - 0.025807584).abs() <= f32::EPSILON);
        // = 0.020277388 * 1.5 + 0.044241577 * 1.6 + 0.07189256 * 1.7 + 0.103230335 * 1.8
        assert!((reading.lux - 0.10250176).abs() <= f32::EPSILON);
        // n = (0.23881 * 0.0050210673 + 0.25499 * 0.011165731 - 0.58291 * 0.018030196)
        //   / (0.11109 * 0.0050210673 - 0.85406 * 0.011165731 + 0.52289 * 0.018030196)
        // n = -14.38281601143
        // cct = 449.0 * n^3 + 3525.0 * n^2 + 6823.3 * n + 5520.33
        assert!((reading.cct - -699402.0).abs() <= f32::EPSILON);
    } else {
        assert_matches!(&*called.borrow(), &None);
        drop(device_proxy);
    }

    task.await;
}

// TODO(fxbug.dev/110275) Restore once adjustment is enabled.
#[ignore]
#[fuchsia::test(allow_stalls = false)]
async fn light_sensor_handler_get_calibrated_data_should_proxy_error() {
    let sensor_configuration = SensorConfiguration {
        vendor_id: VENDOR_ID,
        product_id: PRODUCT_ID,
        rgbc_to_lux_coefficients: Rgbc { red: 1.5, green: 1.6, blue: 1.7, clear: 1.8 },
        si_scaling_factors: Rgbc { red: 1.1, green: 1.2, blue: 1.3, clear: 1.4 },
        settings: get_adjustment_settings(),
    };

    let (device_proxy, _, task) =
        get_mock_device_proxy_with_response(None, Err(zx::sys::ZX_ERR_CONNECTION_RESET));
    let handler = LightSensorHandler::new(DoublingCalibrator, sensor_configuration);
    let reading = handler
        .get_calibrated_data(Rgbc { red: 1, green: 2, blue: 3, clear: 4 }, device_proxy)
        .await;
    reading.expect_err("calibration should fail");
    task.await;
}

#[fuchsia::test(allow_stalls = false)]
async fn light_sensor_handler_input_event_handler() {
    let sensor_configuration = SensorConfiguration {
        vendor_id: VENDOR_ID,
        product_id: PRODUCT_ID,
        rgbc_to_lux_coefficients: Rgbc { red: 1.5, green: 1.6, blue: 1.7, clear: 1.8 },
        si_scaling_factors: Rgbc { red: 1.1, green: 1.2, blue: 1.3, clear: 1.4 },
        settings: get_adjustment_settings(),
    };

    let (device_proxy, _, task) = get_mock_device_proxy();
    let handler = LightSensorHandler::new(DoublingCalibrator, sensor_configuration);

    let (sensor_proxy, stream): (SensorProxy, SensorRequestStream) =
        create_proxy_and_stream::<SensorMarker>().expect("should get proxy and streamns");
    // Register stream so subscriber is created.
    let request_task = Task::local({
        let handler = Rc::clone(&handler);
        async move {
            handler.handle_light_sensor_request_stream(stream).await.expect("can register");
        }
    });
    // Wait for the results in a separate task while we trigger the event below.
    let watch_task = Task::local(async move { sensor_proxy.watch().await.expect("watch called") });

    // Trigger the event. The data should match what was used in
    // `light_sensor_handler_get_calibrated_data` so the same results will be returned.
    let events = handler
        .handle_input_event(InputEvent {
            device_event: InputDeviceEvent::LightSensor(LightSensorEvent {
                device_proxy,
                rgbc: Rgbc { red: 1, green: 2, blue: 3, clear: 4 },
            }),
            device_descriptor: InputDeviceDescriptor::LightSensor(LightSensorDeviceDescriptor {
                vendor_id: VENDOR_ID,
                product_id: PRODUCT_ID,
                device_id: 3,
                sensor_layout: Rgbc { red: 1, green: 2, blue: 3, clear: 4 },
            }),
            event_time: Time::get_monotonic(),
            handled: Handled::No,
            trace_id: None,
        })
        .await;

    assert_eq!(events.len(), 1);
    let event = &events[0];
    assert_eq!(event.handled, Handled::Yes);

    let reading = watch_task.await;
    // The readings should match the results in the `light_sensor_handler_get_calibrated_data` test.
    assert!((reading.rgbc.unwrap().red_intensity - 105.0).abs() <= f32::EPSILON);
    assert!((reading.rgbc.unwrap().green_intensity - 210.0).abs() <= f32::EPSILON);
    assert!((reading.rgbc.unwrap().blue_intensity - 315.0).abs() <= f32::EPSILON);
    assert!((reading.rgbc.unwrap().clear_intensity - 420.0).abs() <= f32::EPSILON);
    assert!((reading.calculated_lux.unwrap() - 0.40923458).abs() <= f32::EPSILON);
    assert!((reading.correlated_color_temperature.unwrap() - -408385.84).abs() <= f32::EPSILON);
    request_task.await;
    drop(events);
    task.await;
}
