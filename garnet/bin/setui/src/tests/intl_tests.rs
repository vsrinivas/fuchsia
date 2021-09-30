// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::ingress::fidl::Interface;
use crate::tests::test_failure_utils::create_test_env_with_failures;
use crate::EnvironmentBuilder;
use anyhow::format_err;
use fidl::endpoints::{ProtocolMarker, ServerEnd};
use fidl::Error::ClientChannelClosed;
use fidl_fuchsia_settings::*;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use fuchsia_zircon::Status;
use futures::future::BoxFuture;
use futures::prelude::*;
use matches::assert_matches;
use std::sync::Arc;

use crate::intl::types::IntlInfo;

const ENV_NAME: &str = "settings_service_intl_test_environment";
const INITIAL_LOCALE: &str = "en-US-x-fxdef";

async fn create_test_intl_env(storage_factory: Arc<InMemoryStorageFactory>) -> IntlProxy {
    let service_gen = Box::new(
        |service_name: &str,
         channel: zx::Channel|
         -> BoxFuture<'static, Result<(), anyhow::Error>> {
            if service_name != fidl_fuchsia_deprecatedtimezone::TimezoneMarker::NAME {
                return Box::pin(async { Err(format_err!("unsupported!")) });
            }

            let timezone_stream_result =
                ServerEnd::<fidl_fuchsia_deprecatedtimezone::TimezoneMarker>::new(channel)
                    .into_stream();

            if timezone_stream_result.is_err() {
                return Box::pin(async { Err(format_err!("could not open stream")) });
            }

            let mut timezone_stream = timezone_stream_result.unwrap();

            fasync::Task::spawn(async move {
                while let Some(req) = timezone_stream.try_next().await.unwrap() {
                    #[allow(unreachable_patterns)]
                    match req {
                        fidl_fuchsia_deprecatedtimezone::TimezoneRequest::SetTimezone {
                            timezone_id: _,
                            responder,
                        } => {
                            responder.send(true).unwrap();
                        }
                        _ => {
                            panic!("unexpected call: {:?}", &req);
                        }
                    }
                }
            })
            .detach();
            Box::pin(async { Ok(()) })
        },
    );

    let env = EnvironmentBuilder::new(storage_factory)
        .service(Box::new(service_gen))
        .fidl_interfaces(&[Interface::Intl])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap();

    env.connect_to_protocol::<IntlMarker>().unwrap()
}

/// Creates an environment that will fail on a get request.
async fn create_intl_test_env_with_failures(
    storage_factory: Arc<InMemoryStorageFactory>,
) -> IntlProxy {
    create_test_env_with_failures(storage_factory, ENV_NAME, Interface::Intl, SettingType::Intl)
        .await
        .connect_to_protocol::<IntlMarker>()
        .unwrap()
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_intl_e2e() {
    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = Arc::new(InMemoryStorageFactory::new());
    let intl_service = create_test_intl_env(Arc::clone(&factory)).await;

    // Check if the initial value is correct.
    let settings = intl_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.time_zone_id,
        Some(fidl_fuchsia_intl::TimeZoneId { id: "UTC".to_string() })
    );
    assert_eq!(
        settings.locales,
        Some(vec![fidl_fuchsia_intl::LocaleId { id: INITIAL_LOCALE.to_string() }])
    );
    assert_eq!(settings.temperature_unit, Some(fidl_fuchsia_intl::TemperatureUnit::Celsius));
    assert_eq!(settings.hour_cycle, Some(fidl_fuchsia_settings::HourCycle::H12));

    // Set new values.
    let intl_settings = fidl_fuchsia_settings::IntlSettings {
        locales: Some(vec![fidl_fuchsia_intl::LocaleId { id: "blah".into() }]),
        temperature_unit: Some(fidl_fuchsia_intl::TemperatureUnit::Celsius),
        time_zone_id: Some(fidl_fuchsia_intl::TimeZoneId { id: "GMT".to_string() }),
        hour_cycle: Some(fidl_fuchsia_settings::HourCycle::H24),
        ..fidl_fuchsia_settings::IntlSettings::EMPTY
    };
    intl_service.set(intl_settings.clone()).await.expect("set completed").expect("set successful");

    // Verify the values we set are returned when watching.
    let settings = intl_service.watch().await.expect("watch completed");
    assert_eq!(settings, intl_settings.clone());

    // Verify the value we set is persisted in DeviceStorage.
    let store = factory.get_device_storage().await;
    let retrieved_struct = store.get::<IntlInfo>().await;
    assert_eq!(retrieved_struct, intl_settings.clone().into());
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_intl_e2e_set_twice() {
    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = Arc::new(InMemoryStorageFactory::new());
    let intl_service = create_test_intl_env(Arc::clone(&factory)).await;
    let store = factory.get_device_storage().await;

    // Initial value is not None.
    let settings = intl_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.time_zone_id,
        Some(fidl_fuchsia_intl::TimeZoneId { id: "UTC".to_string() })
    );
    assert_eq!(
        settings.locales,
        Some(vec![fidl_fuchsia_intl::LocaleId { id: INITIAL_LOCALE.to_string() }])
    );
    assert_eq!(settings.temperature_unit, Some(fidl_fuchsia_intl::TemperatureUnit::Celsius));
    assert_eq!(settings.hour_cycle, Some(fidl_fuchsia_settings::HourCycle::H12));

    // Set new values.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::EMPTY;
    let updated_timezone = "GMT";
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
    intl_settings.hour_cycle = Some(fidl_fuchsia_settings::HourCycle::H24);
    intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

    // Try to set to a new value: this second set should succeed too.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::EMPTY;
    let updated_timezone = "PST";
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect("repeated set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let retrieved_struct = store.get::<IntlInfo>().await;
    assert_eq!(retrieved_struct.time_zone_id.unwrap(), updated_timezone);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_intl_e2e_idempotent_set() {
    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = Arc::new(InMemoryStorageFactory::new());
    let intl_service = create_test_intl_env(Arc::clone(&factory)).await;
    let store = factory.get_device_storage().await;

    // Check if the initial value is correct.
    let settings = intl_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.time_zone_id,
        Some(fidl_fuchsia_intl::TimeZoneId { id: "UTC".to_string() })
    );
    assert_eq!(
        settings.locales,
        Some(vec![fidl_fuchsia_intl::LocaleId { id: INITIAL_LOCALE.to_string() }])
    );
    assert_eq!(settings.temperature_unit, Some(fidl_fuchsia_intl::TemperatureUnit::Celsius));
    assert_eq!(settings.hour_cycle, Some(fidl_fuchsia_settings::HourCycle::H12));

    // Set new values.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::EMPTY;
    let updated_timezone = "GMT";
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

    // Try to set again to the same value: this second set should succeed.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::EMPTY;
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect("repeated set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let retrieved_struct = store.get::<IntlInfo>().await;
    assert_eq!(retrieved_struct.time_zone_id.unwrap(), updated_timezone);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_intl_invalid_timezone() {
    const INITIAL_TIME_ZONE: &str = "GMT";

    let factory = InMemoryStorageFactory::new();
    let intl_service = create_test_intl_env(Arc::new(factory)).await;

    // Set a real value.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::EMPTY;
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: INITIAL_TIME_ZONE.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

    // Set with an invalid timezone value.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::EMPTY;
    let updated_timezone = "not_a_real_time_zone";
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
    let _ = intl_service.set(intl_settings).await.expect("set completed").expect_err("invalid");

    // Verify the returned when watching hasn't changed.
    let settings = intl_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.time_zone_id,
        Some(fidl_fuchsia_intl::TimeZoneId { id: INITIAL_TIME_ZONE.to_string() })
    );
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let intl_service =
        create_intl_test_env_with_failures(Arc::new(InMemoryStorageFactory::new())).await;
    let result = intl_service.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}
