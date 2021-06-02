// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use fidl_fuchsia_settings::{IntlMarker, IntlRequest, IntlSettings, IntlWatchResponder};
use fuchsia_async as fasync;

use crate::base::{SettingInfo, SettingType};
use crate::fidl_hanging_get_responder;
use crate::fidl_process;
use crate::fidl_processor::settings::RequestContext;
use crate::handler::base::Request;
use crate::request_respond;

fidl_hanging_get_responder!(IntlMarker, IntlSettings, IntlWatchResponder,);

impl From<SettingInfo> for IntlSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::Intl(info) = response {
            return info.into();
        }

        panic!("incorrect value sent to intl");
    }
}

impl From<IntlSettings> for Request {
    fn from(settings: IntlSettings) -> Self {
        Request::SetIntlInfo(settings.into())
    }
}

fidl_process!(Intl, SettingType::Intl, process_request,);

async fn process_request(
    context: RequestContext<IntlSettings, IntlWatchResponder>,
    req: IntlRequest,
) -> Result<Option<IntlRequest>, anyhow::Error> {
    // Support future expansion of FIDL
    #[allow(unreachable_patterns)]
    match req {
        IntlRequest::Set { settings, responder } => {
            fasync::Task::spawn(async move {
                request_respond!(
                    context,
                    responder,
                    SettingType::Intl,
                    settings.into(),
                    Ok(()),
                    Err(fidl_fuchsia_settings::Error::Failed),
                    IntlMarker
                );
            })
            .detach();
        }
        IntlRequest::Watch { responder } => context.watch(responder, true).await,
        _ => {
            return Ok(Some(req));
        }
    }

    Ok(None)
}

#[cfg(test)]
mod tests {
    use crate::intl::types::{HourCycle, IntlInfo, LocaleId, TemperatureUnit};

    use super::*;

    #[test]
    fn test_request_from_settings_empty() {
        let request = Request::from(IntlSettings::EMPTY);

        assert_eq!(
            request,
            Request::SetIntlInfo(IntlInfo {
                locales: None,
                temperature_unit: None,
                time_zone_id: None,
                hour_cycle: None,
            })
        );
    }

    #[test]
    fn test_request_from_settings() {
        const TIME_ZONE_ID: &str = "PDT";

        let intl_settings = IntlSettings {
            locales: Some(vec![fidl_fuchsia_intl::LocaleId { id: "blah".into() }]),
            temperature_unit: Some(fidl_fuchsia_intl::TemperatureUnit::Celsius),
            time_zone_id: Some(fidl_fuchsia_intl::TimeZoneId { id: TIME_ZONE_ID.to_string() }),
            hour_cycle: Some(fidl_fuchsia_settings::HourCycle::H12),
            ..IntlSettings::EMPTY
        };

        let request = Request::from(intl_settings);

        assert_eq!(
            request,
            Request::SetIntlInfo(IntlInfo {
                locales: Some(vec![LocaleId { id: "blah".into() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some(TIME_ZONE_ID.to_string()),
                hour_cycle: Some(HourCycle::H12),
            })
        );
    }
}
