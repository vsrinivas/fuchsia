// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service,
    crate::registry::device_storage::testing::*,
    crate::registry::device_storage::DeviceStorageFactory,
    crate::service_context::ServiceContext,
    crate::switchboard::base::SettingType,
    failure::format_err,
    fidl::endpoints::{ServerEnd, ServiceMarker},
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::prelude::*,
    parking_lot::RwLock,
    std::sync::Arc,
};

use crate::switchboard::base::IntlInfo;

const ENV_NAME: &str = "settings_service_intl_test_environment";

async fn create_test_intl_env(storage_factory: Box<InMemoryStorageFactory>) -> IntlProxy {
    let service_gen = |service_name: &str, channel: zx::Channel| {
        if service_name != fidl_fuchsia_deprecatedtimezone::TimezoneMarker::NAME {
            return Err(format_err!("unsupported!"));
        }

        let mut timezone_stream =
            ServerEnd::<fidl_fuchsia_deprecatedtimezone::TimezoneMarker>::new(channel)
                .into_stream()?;

        fasync::spawn(async move {
            while let Some(req) = timezone_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_deprecatedtimezone::TimezoneRequest::GetTimezoneId {
                        responder,
                    } => {
                        responder.send("PDT").unwrap();
                    }
                    fidl_fuchsia_deprecatedtimezone::TimezoneRequest::SetTimezone {
                        timezone_id: _,
                        responder,
                    } => {
                        responder.send(true).unwrap();
                    }
                    _ => {}
                }
            }
        });
        Ok(())
    };

    let mut fs = ServiceFs::new();

    create_fidl_service(
        fs.root_dir(),
        [SettingType::Intl].iter().cloned().collect(),
        Arc::new(RwLock::new(ServiceContext::new(Some(Box::new(service_gen))))),
        storage_factory,
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    env.connect_to_service::<IntlMarker>().unwrap()
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_intl_e2e() {
    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = Box::new(InMemoryStorageFactory::create());
    let store = factory.get_store::<IntlInfo>();
    let intl_service = create_test_intl_env(factory).await;

    // Initial value is None.
    let settings = intl_service.watch().await.expect("watch completed").expect("watch successful");
    assert_eq!(settings.time_zone_id, None);

    // Set new values.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::empty();
    let updated_timezone = "GMT";
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

    // Verify the values we set are returned when watching.
    let settings = intl_service.watch().await.expect("watch completed").expect("watch successful");
    assert_eq!(
        settings.time_zone_id,
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() })
    );

    // Verify the value we set is persisted in DeviceStorage.
    let mut store_lock = store.lock().await;
    let retrieved_struct = store_lock.get().await;
    assert_eq!(retrieved_struct.time_zone_id.unwrap(), updated_timezone);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_intl_invalid_timezone() {
    const INITIAL_TIME_ZONE: &'static str = "GMT";

    let factory = Box::new(InMemoryStorageFactory::create());
    let intl_service = create_test_intl_env(factory).await;

    // Set a real value.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::empty();
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: INITIAL_TIME_ZONE.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

    // Set with an invalid timezone value.
    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::empty();
    let updated_timezone = "not_a_real_time_zone";
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect_err("invalid");

    // Verify the returned when watching hasn't changed.
    let settings = intl_service.watch().await.expect("watch completed").expect("watch successful");
    assert_eq!(
        settings.time_zone_id,
        Some(fidl_fuchsia_intl::TimeZoneId { id: INITIAL_TIME_ZONE.to_string() })
    );
}
