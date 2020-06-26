// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::display::{light_sensor_testing::*, LIGHT_SENSOR_SERVICE_NAME},
    crate::registry::device_storage::testing::*,
    crate::switchboard::base::SettingType,
    crate::EnvironmentBuilder,
    anyhow::format_err,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::BoxFuture,
    futures::prelude::*,
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
            ServerEnd::<fidl_fuchsia_hardware_input::DeviceMarker>::new(channel).into_stream();

        if stream_result.is_err() {
            return Box::pin(async { Err(format_err!("could not connect to service")) });
        }

        let mut stream = stream_result.unwrap();

        let data = get_mock_sensor_response();
        fasync::spawn(async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                if let fidl_fuchsia_hardware_input::DeviceRequest::GetReport {
                    type_: _,
                    id: _,
                    responder,
                } = request
                {
                    responder.send(0, &data).unwrap();
                }
            }
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
    let data = display_service
        .watch_light_sensor(0.0)
        .await
        .expect("watch completed")
        .expect("watch successful");

    assert_eq!(data.illuminance_lux, Some(TEST_LUX_VAL.into()));
    assert_eq!(
        data.color,
        Some(fidl_fuchsia_ui_types::ColorRgb {
            red: TEST_RED_VAL.into(),
            green: TEST_GREEN_VAL.into(),
            blue: TEST_BLUE_VAL.into(),
        })
    );
}
