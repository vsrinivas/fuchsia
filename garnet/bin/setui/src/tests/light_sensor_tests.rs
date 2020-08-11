// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display::{light_sensor_testing, LIGHT_SENSOR_SERVICE_NAME},
    crate::registry::device_storage::testing::*,
    crate::switchboard::base::SettingType,
    crate::EnvironmentBuilder,
    anyhow::format_err,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_settings::*,
    fuchsia_zircon as zx,
    futures::future::{self, BoxFuture},
};

const ENV_NAME: &str = "settings_service_light_sensor_test_environment";

#[fuchsia_async::run_until_stalled(test)]
async fn test_light_sensor() {
    let service_gen = |service_name: &str,
                       channel: zx::Channel|
     -> BoxFuture<'static, Result<(), anyhow::Error>> {
        if service_name != LIGHT_SENSOR_SERVICE_NAME {
            let service = String::from(service_name);
            return Box::pin(async move { Err(format_err!("{:?} unsupported!", service)) });
        }

        let stream_result =
            ServerEnd::<fidl_fuchsia_input_report::InputDeviceMarker>::new(channel).into_stream();

        if stream_result.is_err() {
            return Box::pin(async { Err(format_err!("could not connect to service")) });
        }

        let stream = stream_result.unwrap();
        let (sensor_axes, data_fn) = light_sensor_testing::get_mock_sensor_response();
        light_sensor_testing::spawn_mock_sensor_with_data(stream, sensor_axes, move || {
            future::ready(data_fn())
        });

        Box::pin(async { Ok(()) })
    };

    let env = EnvironmentBuilder::new(InMemoryStorageFactory::create())
        .service(Box::new(service_gen))
        .settings(&[SettingType::LightSensor])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    let display_service = env.connect_to_service::<DisplayMarker>().unwrap();
    let data = display_service.watch_light_sensor2(0.0).await.expect("watch completed");

    assert_eq!(data.illuminance_lux, Some(light_sensor_testing::TEST_LUX_VAL as f32));
    assert_eq!(
        data.color,
        Some(fidl_fuchsia_ui_types::ColorRgb {
            red: light_sensor_testing::TEST_RED_VAL as f32,
            green: light_sensor_testing::TEST_GREEN_VAL as f32,
            blue: light_sensor_testing::TEST_BLUE_VAL as f32,
        })
    );
}
