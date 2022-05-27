// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{ensure, format_err, Context, Error},
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_intl as fintl, fidl_fuchsia_settings as fsettings,
    fuchsia_component::client,
    futures::StreamExt,
};

/// Opens a connection to the given discoverable service, provides an error context on failure.
fn connect_to_service<M: DiscoverableProtocolMarker>() -> Result<M::Proxy, Error> {
    client::connect_to_protocol::<M>().with_context(|| format!("connecting to {}", M::DEBUG_NAME))
}

#[fuchsia::test]
async fn set_then_get() -> Result<(), Error> {
    // In order to ensure that the test can revert its changes to the intl settings, the body of
    // the test must not panic.
    async fn run_test(intl_settings_client: fsettings::IntlProxy) -> Result<(), Error> {
        let intl_property_provider: fintl::PropertyProviderProxy =
            connect_to_service::<fintl::PropertyProviderMarker>()?;
        let mut event_stream = intl_property_provider.take_event_stream();

        // This warms up the intl services component and the set_ui component, avoiding potential
        // data races later.
        let _initial_profile =
            intl_property_provider.get_profile().await.context("get initial profile")?;

        let new_settings = {
            let mut s = fsettings::IntlSettings::EMPTY;
            s.locales = Some(vec![fintl::LocaleId { id: "sr-RS".to_string() }]);
            s.time_zone_id = Some(fintl::TimeZoneId { id: "Europe/Belgrade".to_string() });
            s.temperature_unit = Some(fintl::TemperatureUnit::Celsius);
            s.hour_cycle = Some(fsettings::HourCycle::H23);
            s
        };

        intl_settings_client
            .set(new_settings)
            .await
            .context("modify settings (FIDL)")?
            .map_err(|e| format_err!("{:?}", e))
            .context("modify settings (Settings server)")?;

        match event_stream.next().await.ok_or_else(|| format_err!("No event"))?? {
            fintl::PropertyProviderEvent::OnChange {} => {}
        };

        let updated_profile =
            intl_property_provider.get_profile().await.context("get updated profile")?;
        let expected_profile = {
            let mut p = fintl::Profile::EMPTY;
            p.locales = Some(vec![fintl::LocaleId {
                id: "sr-RS-u-ca-gregory-fw-mon-hc-h23-ms-metric-nu-latn-tz-rsbeg".to_string(),
            }]);
            p.time_zones = Some(vec![fintl::TimeZoneId { id: "Europe/Belgrade".to_string() }]);
            p.temperature_unit = Some(fintl::TemperatureUnit::Celsius);
            p
        };

        ensure!(updated_profile.locales == expected_profile.locales);
        ensure!(updated_profile.time_zones == expected_profile.time_zones);
        ensure!(updated_profile.temperature_unit == expected_profile.temperature_unit);

        Ok(())
    }

    // Setup
    let intl_settings_client = connect_to_service::<fsettings::IntlMarker>()?;
    let original_settings = intl_settings_client.watch().await.context("get original settings")?;

    // Test
    let test_result = run_test(intl_settings_client.clone()).await;

    // Teardown
    intl_settings_client
        .set(original_settings)
        .await
        .context("restore original settings (FIDL)")?
        .map_err(|e| format_err!("{:?}", e))
        .context("restore original settings (Settings server)")?;

    test_result
}
