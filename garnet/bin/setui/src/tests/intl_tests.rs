// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use {
    crate::create_fidl_service,
    crate::registry::device_storage::testing::*,
    crate::registry::service_context::ServiceContext,
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

const ENV_NAME: &str = "settings_service_intl_test_environment";

#[fuchsia_async::run_singlethreaded(test)]
async fn test_intl() {
    const INITIAL_TIME_ZONE: &'static str = "PDT";

    let service_gen = |service_name: &str, channel: zx::Channel| {
        if service_name != fidl_fuchsia_deprecatedtimezone::TimezoneMarker::NAME {
            return Err(format_err!("unsupported!"));
        }

        let mut timezone_stream =
            ServerEnd::<fidl_fuchsia_deprecatedtimezone::TimezoneMarker>::new(channel)
                .into_stream()?;

        fasync::spawn(async move {
            let mut stored_timezone = INITIAL_TIME_ZONE.to_string();
            while let Some(req) = timezone_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_deprecatedtimezone::TimezoneRequest::GetTimezoneId {
                        responder,
                    } => {
                        responder.send(&stored_timezone).unwrap();
                    }
                    fidl_fuchsia_deprecatedtimezone::TimezoneRequest::SetTimezone {
                        timezone_id,
                        responder,
                    } => {
                        stored_timezone = timezone_id;
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
        Box::new(InMemoryStorageFactory::create()),
    );

    let env = fs.create_salted_nested_environment(ENV_NAME).unwrap();
    fasync::spawn(fs.collect());

    let intl_service = env.connect_to_service::<IntlMarker>().unwrap();
    let settings = intl_service.watch().await.expect("watch completed").expect("watch successful");

    if let Some(time_zone_id) = settings.time_zone_id {
        assert_eq!(time_zone_id.id, INITIAL_TIME_ZONE);
    }

    let mut intl_settings = fidl_fuchsia_settings::IntlSettings::empty();
    let updated_timezone = "GMT";

    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect("set successful");
    let settings = intl_service.watch().await.expect("watch completed").expect("watch successful");
    assert_eq!(
        settings.time_zone_id,
        Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() })
    );
}
