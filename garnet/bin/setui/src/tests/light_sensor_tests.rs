// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_environment, crate::display::LIGHT_SENSOR_SERVICE_NAME,
    crate::registry::device_storage::testing::*, crate::service_context::ServiceContext,
    crate::switchboard::base::SettingType, anyhow::format_err, fidl::endpoints::ServerEnd,
    fidl_fuchsia_settings::*, fuchsia_async as fasync, fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx, futures::prelude::*,
};

const ENV_NAME: &str = "settings_service_light_sensor_test_environment";

const TEST_LUX_VAL: u8 = 25;
const TEST_RED_VAL: u8 = 10;
const TEST_GREEN_VAL: u8 = 9;
const TEST_BLUE_VAL: u8 = 6;

#[fuchsia_async::run_singlethreaded(test)]
async fn test_light_sensor() {
    let service_gen = |service_name: &str, channel: zx::Channel| {
        if service_name != LIGHT_SENSOR_SERVICE_NAME {
            return Err(format_err!("{:?} unsupported!", service_name));
        }

        let mut stream =
            ServerEnd::<fidl_fuchsia_hardware_input::DeviceMarker>::new(channel).into_stream()?;

        fasync::spawn(async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                if let fidl_fuchsia_hardware_input::DeviceRequest::GetReport {
                    type_: _,
                    id: _,
                    responder,
                } = request
                {
                    // Taken from actual sensor report
                    let data: [u8; 11] = [
                        1,
                        1,
                        0,
                        TEST_LUX_VAL,
                        0,
                        TEST_RED_VAL,
                        0,
                        TEST_GREEN_VAL,
                        0,
                        TEST_BLUE_VAL,
                        0,
                    ];
                    responder.send(0, &mut data.iter().cloned()).unwrap();
                }
            }
        });
        Ok(())
    };

    let mut fs = ServiceFs::new();

    assert!(create_environment(
        fs.root_dir(),
        [SettingType::LightSensor].iter().cloned().collect(),
        vec![],
        ServiceContext::create(Some(Box::new(service_gen))),
        Box::new(InMemoryStorageFactory::create()),
    )
    .await
    .unwrap()
    .is_ok());

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

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
