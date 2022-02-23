// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_intl_args::Intl;
use fidl_fuchsia_settings::{IntlProxy, IntlSettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin("setui", IntlProxy = "core/setui_service:expose:fuchsia.settings.Intl")]
pub async fn run_command(intl_proxy: IntlProxy, intl: Intl) -> Result<()> {
    handle_mixed_result("Intl", command(intl_proxy, IntlSettings::from(intl)).await).await
}

async fn command(proxy: IntlProxy, settings: IntlSettings) -> WatchOrSetResult {
    if settings == IntlSettings::EMPTY {
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    } else {
        Ok(Either::Set(if let Err(err) = proxy.set(settings.clone()).await? {
            format!("{:?}", err)
        } else {
            format!("Successfully set Intl to {:?}", Intl::from(settings))
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_intl::{LocaleId, TemperatureUnit, TimeZoneId};
    use fidl_fuchsia_settings::{HourCycle, IntlRequest};
    use futures::prelude::*;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
        let proxy = setup_fake_intl_proxy(move |req| match req {
            IntlRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            IntlRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let intl = Intl {
            time_zone: None,
            temperature_unit: Some(TemperatureUnit::Celsius),
            locales: vec![],
            hour_cycle: None,
            clear_locales: false,
        };
        let response = run_command(proxy, intl).await;
        assert!(response.is_ok());
    }

    #[test_case(
        Intl {
            time_zone: Some(TimeZoneId { id: "GMT".to_string() }),
            temperature_unit: Some(TemperatureUnit::Celsius),
            locales: vec![LocaleId { id: "fr-u-hc-h12".into() }],
            hour_cycle: Some(HourCycle::H12),
            clear_locales: false,
        };
        "Test intl set() output with non-empty input."
    )]
    #[test_case(
        Intl {
            time_zone: Some(TimeZoneId { id: "GMT".to_string() }),
            temperature_unit: Some(TemperatureUnit::Fahrenheit),
            locales: vec![],
            hour_cycle: Some(HourCycle::H24),
            clear_locales: true,
        };
        "Test intl set() output with a different non-empty input."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_intl_set_output(expected_intl: Intl) -> Result<()> {
        let proxy = setup_fake_intl_proxy(move |req| match req {
            IntlRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            IntlRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, IntlSettings::from(expected_intl.clone())));
        assert_eq!(output, format!("Successfully set Intl to {:?}", expected_intl));
        Ok(())
    }

    #[test_case(
        Intl {
            time_zone: None,
            temperature_unit: None,
            locales: vec![],
            hour_cycle: None,
            clear_locales: false,
        };
        "Test intl watch() output with empty input."
    )]
    #[test_case(
        Intl {
            time_zone: Some(TimeZoneId { id: "GMT".to_string() }),
            temperature_unit: Some(TemperatureUnit::Celsius),
            locales: vec![LocaleId { id: "fr-u-hc-h12".into() }],
            hour_cycle: Some(HourCycle::H12),
            clear_locales: false,
        };
        "Test intl watch() output with non-empty input."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_intl_watch_output(expected_intl: Intl) -> Result<()> {
        let expected_intl_clone = expected_intl.clone();
        let proxy = setup_fake_intl_proxy(move |req| match req {
            IntlRequest::Set { .. } => {
                panic!("Unexpected call to set");
            }
            IntlRequest::Watch { responder } => {
                let _ = responder.send(IntlSettings::from(expected_intl.clone()));
            }
        });

        let output = utils::assert_watch!(command(
            proxy,
            IntlSettings::from(Intl {
                time_zone: None,
                temperature_unit: None,
                locales: vec![],
                hour_cycle: None,
                clear_locales: false,
            })
        ));
        assert_eq!(output, format!("{:#?}", IntlSettings::from(expected_intl_clone)));
        Ok(())
    }
}
