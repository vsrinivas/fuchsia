// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::IntlTest;

use fidl_fuchsia_settings::{HourCycle, IntlSettings};

mod common;

const INITIAL_LOCALE: &str = "en-US-x-fxdef";

#[fuchsia::test]
async fn test_intl_e2e() {
    // Set new values.
    let expected_settings = IntlSettings {
        locales: Some(vec![fidl_fuchsia_intl::LocaleId { id: "blah".into() }]),
        temperature_unit: Some(fidl_fuchsia_intl::TemperatureUnit::Celsius),
        time_zone_id: Some(fidl_fuchsia_intl::TimeZoneId { id: "GMT".to_string() }),
        hour_cycle: Some(HourCycle::H24),
        ..IntlSettings::EMPTY
    };

    let instance = IntlTest::create_realm().await.expect("setting up test realm");
    {
        let intl_service = IntlTest::connect_to_intl_marker(&instance);

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
        assert_eq!(settings.hour_cycle, Some(HourCycle::H12));

        intl_service
            .set(expected_settings.clone())
            .await
            .expect("set completed")
            .expect("set successful");

        // Verify the values we set are returned when watching.
        let settings = intl_service.watch().await.expect("watch completed");
        assert_eq!(settings, expected_settings.clone());
    }

    {
        let proxy = IntlTest::connect_to_intl_marker(&instance);
        // Ensure retrieved value matches set value
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(settings, expected_settings);
    }

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_intl_e2e_set_twice() {
    let updated_timezone = "GMT";

    let instance = IntlTest::create_realm().await.expect("setting up test realm");
    {
        let intl_service = IntlTest::connect_to_intl_marker(&instance);

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
        assert_eq!(settings.hour_cycle, Some(HourCycle::H12));

        // Set new values.
        let mut intl_settings = IntlSettings::EMPTY;
        intl_settings.time_zone_id =
            Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
        intl_settings.hour_cycle = Some(HourCycle::H24);
        intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

        // Try to set to a new value: this second set should succeed too.
        let mut intl_settings = IntlSettings::EMPTY;
        intl_settings.time_zone_id =
            Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
        intl_service
            .set(intl_settings)
            .await
            .expect("set completed")
            .expect("repeated set successful");
    }

    {
        let proxy = IntlTest::connect_to_intl_marker(&instance);
        // Ensure retrieved value matches set value
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(settings.time_zone_id.unwrap().id, updated_timezone);
    }

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_intl_e2e_idempotent_set() {
    let updated_timezone = "GMT";

    let instance = IntlTest::create_realm().await.expect("setting up test realm");
    {
        let intl_service = IntlTest::connect_to_intl_marker(&instance);

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
        assert_eq!(settings.hour_cycle, Some(HourCycle::H12));

        // Set new values.
        let mut intl_settings = IntlSettings::EMPTY;
        let updated_timezone = "GMT";
        intl_settings.time_zone_id =
            Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
        intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

        // Try to set again to the same value: this second set should succeed.
        let mut intl_settings = IntlSettings::EMPTY;
        intl_settings.time_zone_id =
            Some(fidl_fuchsia_intl::TimeZoneId { id: updated_timezone.to_string() });
        intl_service
            .set(intl_settings)
            .await
            .expect("set completed")
            .expect("repeated set successful");
    }

    {
        let proxy = IntlTest::connect_to_intl_marker(&instance);
        // Ensure retrieved value matches set value
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(settings.time_zone_id.unwrap().id, updated_timezone);
    }

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_intl_invalid_timezone() {
    const INITIAL_TIME_ZONE: &str = "GMT";

    let instance = IntlTest::create_realm().await.expect("setting up test realm");
    let intl_service = IntlTest::connect_to_intl_marker(&instance);

    // Set a real value.
    let mut intl_settings = IntlSettings::EMPTY;
    intl_settings.time_zone_id =
        Some(fidl_fuchsia_intl::TimeZoneId { id: INITIAL_TIME_ZONE.to_string() });
    intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

    // Set with an invalid timezone value.
    let mut intl_settings = IntlSettings::EMPTY;
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

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_intl_invalid_locale() {
    const INITIAL_LOCALE: &str = "sr-Cyrl-RS-u-ca-hebrew-fw-monday-ms-ussystem-nu-deva-tz-usnyc";

    let instance = IntlTest::create_realm().await.expect("setting up test realm");
    let intl_service = IntlTest::connect_to_intl_marker(&instance);

    // Set a real value.
    let mut intl_settings = IntlSettings::EMPTY;
    intl_settings.locales =
        Some(vec![fidl_fuchsia_intl::LocaleId { id: INITIAL_LOCALE.to_string() }]);
    intl_service.set(intl_settings).await.expect("set completed").expect("set successful");

    // Set with an invalid locale.
    let mut intl_settings = IntlSettings::EMPTY;
    let updated_locale = "nope nope nope";
    intl_settings.locales =
        Some(vec![fidl_fuchsia_intl::LocaleId { id: updated_locale.to_string() }]);
    let _ = intl_service.set(intl_settings).await.expect("set completed").expect_err("invalid");

    // Verify the returned setting when watching hasn't changed.
    let settings = intl_service.watch().await.expect("watch completed");
    assert_eq!(
        settings.locales,
        Some(vec![fidl_fuchsia_intl::LocaleId { id: INITIAL_LOCALE.to_string() }])
    );

    let _ = instance.destroy().await;
}
