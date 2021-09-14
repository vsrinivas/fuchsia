// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::interface_tests::Services;
use crate::interface_tests::ENV_NAME;
use crate::intl;
use anyhow::{Context as _, Error};
use fidl_fuchsia_intl::{LocaleId, TemperatureUnit, TimeZoneId};
use fidl_fuchsia_settings::{IntlMarker, IntlRequest, IntlSettings};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

#[fuchsia_async::run_until_stalled(test)]
async fn validate_intl_set() -> Result<(), Error> {
    const TEST_TIME_ZONE: &str = "GMT";
    const TEST_TEMPERATURE_UNIT: TemperatureUnit = TemperatureUnit::Celsius;
    const TEST_LOCALE: &str = "blah";
    const TEST_HOUR_CYCLE: fidl_fuchsia_settings::HourCycle = fidl_fuchsia_settings::HourCycle::H12;

    let env = create_service!(Services::Intl,
        IntlRequest::Set { settings, responder } => {
            assert_eq!(Some(TimeZoneId { id: TEST_TIME_ZONE.to_string() }), settings.time_zone_id);
            assert_eq!(Some(TEST_TEMPERATURE_UNIT), settings.temperature_unit);
            assert_eq!(Some(vec![LocaleId { id: TEST_LOCALE.into() }]), settings.locales);
            assert_eq!(Some(TEST_HOUR_CYCLE), settings.hour_cycle);
            responder.send(&mut Ok(()))?;
    });

    let intl_service =
        env.connect_to_protocol::<IntlMarker>().context("Failed to connect to intl service")?;

    assert_set!(intl::command(
        intl_service,
        Some(TimeZoneId { id: TEST_TIME_ZONE.to_string() }),
        Some(TEST_TEMPERATURE_UNIT),
        vec![LocaleId { id: TEST_LOCALE.into() }],
        Some(TEST_HOUR_CYCLE),
        false,
    ));
    Ok(())
}

#[fuchsia_async::run_until_stalled(test)]
async fn validate_intl_watch() -> Result<(), Error> {
    const TEST_TIME_ZONE: &str = "GMT";
    const TEST_TEMPERATURE_UNIT: TemperatureUnit = TemperatureUnit::Celsius;
    const TEST_LOCALE: &str = "blah";
    const TEST_HOUR_CYCLE: fidl_fuchsia_settings::HourCycle = fidl_fuchsia_settings::HourCycle::H12;

    let env = create_service!(Services::Intl,
        IntlRequest::Watch { responder } => {
            responder.send(IntlSettings {
                locales: Some(vec![LocaleId { id: TEST_LOCALE.into() }]),
                temperature_unit: Some(TEST_TEMPERATURE_UNIT),
                time_zone_id: Some(TimeZoneId { id: TEST_TIME_ZONE.to_string() }),
                hour_cycle: Some(TEST_HOUR_CYCLE),
                ..IntlSettings::EMPTY
            })?;
        }
    );

    let intl_service =
        env.connect_to_protocol::<IntlMarker>().context("Failed to connect to intl service")?;

    let output = assert_watch!(intl::command(intl_service, None, None, vec![], None, false));
    assert_eq!(
        output,
        format!(
            "{:#?}",
            IntlSettings {
                locales: Some(vec![LocaleId { id: TEST_LOCALE.into() }]),
                temperature_unit: Some(TEST_TEMPERATURE_UNIT),
                time_zone_id: Some(TimeZoneId { id: TEST_TIME_ZONE.to_string() }),
                hour_cycle: Some(TEST_HOUR_CYCLE),
                ..IntlSettings::EMPTY
            }
        )
    );
    Ok(())
}
