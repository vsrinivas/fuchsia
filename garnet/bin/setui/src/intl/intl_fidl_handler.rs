// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use fidl_fuchsia_settings::{
    IntlMarker, IntlRequest, IntlSettings, IntlWatch2Responder, IntlWatchResponder,
};
use fuchsia_async as fasync;
use futures::future::LocalBoxFuture;
use futures::FutureExt;

use crate::fidl_hanging_get_responder;
use crate::fidl_process;
use crate::fidl_processor::RequestContext;
use crate::request_respond;
use crate::switchboard::base::{SettingRequest, SettingResponse, SettingType};
use crate::switchboard::hanging_get_handler::Sender;

fidl_hanging_get_responder!(
    IntlMarker,
    IntlSettings,
    IntlWatchResponder,
    IntlSettings,
    IntlWatch2Responder,
);

impl From<SettingResponse> for IntlSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Intl(info) = response {
            return info.into();
        }

        panic!("incorrect value sent to intl");
    }
}

impl From<IntlSettings> for SettingRequest {
    fn from(settings: IntlSettings) -> Self {
        SettingRequest::SetIntlInfo(settings.into())
    }
}

fidl_process!(
    Intl,
    SettingType::Intl,
    process_request,
    SettingType::Intl,
    IntlSettings,
    IntlWatch2Responder,
    process_request_2,
);

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
                    IntlMarker::DEBUG_NAME
                );
            })
            .detach();
        }
        IntlRequest::Watch { responder } => context.watch(responder, true).await,
        _ => {
            return Ok(Some(req));
        }
    }

    return Ok(None);
}

// TODO(fxbug.dev/55719): Remove when clients are ported to watch.
async fn process_request_2(
    context: RequestContext<IntlSettings, IntlWatch2Responder>,
    req: IntlRequest,
) -> Result<Option<IntlRequest>, anyhow::Error> {
    // Support future expansion of FIDL
    #[allow(unreachable_patterns)]
    match req {
        IntlRequest::Watch2 { responder } => context.watch(responder, true).await,
        _ => {
            return Ok(Some(req));
        }
    }

    return Ok(None);
}

#[cfg(test)]
mod tests {
    use crate::switchboard::intl_types::{HourCycle, IntlInfo, LocaleId, TemperatureUnit};

    use super::*;

    #[test]
    fn test_request_from_settings_empty() {
        let request = SettingRequest::from(IntlSettings::empty());

        assert_eq!(
            request,
            SettingRequest::SetIntlInfo(IntlInfo {
                locales: None,
                temperature_unit: None,
                time_zone_id: None,
                hour_cycle: None,
            })
        );
    }

    #[test]
    fn test_request_from_settings() {
        const TIME_ZONE_ID: &'static str = "PDT";

        let intl_settings = IntlSettings {
            locales: Some(vec![fidl_fuchsia_intl::LocaleId { id: "blah".into() }]),
            temperature_unit: Some(fidl_fuchsia_intl::TemperatureUnit::Celsius),
            time_zone_id: Some(fidl_fuchsia_intl::TimeZoneId { id: TIME_ZONE_ID.to_string() }),
            hour_cycle: Some(fidl_fuchsia_settings::HourCycle::H12),
        };

        let request = SettingRequest::from(intl_settings);

        assert_eq!(
            request,
            SettingRequest::SetIntlInfo(IntlInfo {
                locales: Some(vec![LocaleId { id: "blah".into() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some(TIME_ZONE_ID.to_string()),
                hour_cycle: Some(HourCycle::H12),
            })
        );
    }
}
