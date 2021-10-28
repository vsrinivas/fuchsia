// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::fidl_hanging_get_responder;
use crate::handler::base::Request;
use crate::ingress::Scoped;
use crate::ingress::{request, watch};
use crate::job::source::{Error as JobError, ErrorResponder};
use crate::job::Job;

use fidl::endpoints::{ControlHandle, Responder};
use fidl_fuchsia_settings::{
    IntlMarker, IntlRequest, IntlSetResponder, IntlSetResult, IntlSettings, IntlWatchResponder,
};
use fuchsia_syslog::fx_log_warn;
use std::convert::TryFrom;

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

impl TryFrom<IntlRequest> for Job {
    type Error = JobError;
    fn try_from(item: IntlRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            IntlRequest::Set { settings, responder } => Ok(request::Work::new(
                SettingType::Intl,
                Request::SetIntlInfo(settings.into()),
                responder,
            )
            .into()),
            IntlRequest::Watch { responder } => {
                Ok(watch::Work::new_job(SettingType::Intl, responder))
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", item);
                Err(JobError::Unsupported)
            }
        }
    }
}

impl ErrorResponder for IntlSetResponder {
    fn id(&self) -> &'static str {
        "Intl_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
    }
}

impl watch::Responder<IntlSettings, fuchsia_zircon::Status> for IntlWatchResponder {
    fn respond(self, response: Result<IntlSettings, fuchsia_zircon::Status>) {
        match response {
            Ok(settings) => {
                let _ = self.send(settings).ok();
            }
            Err(error) => {
                self.control_handle().shutdown_with_epitaph(error);
            }
        }
    }
}

impl request::Responder<Scoped<IntlSetResult>> for IntlSetResponder {
    fn respond(self, Scoped(mut response): Scoped<IntlSetResult>) {
        let _ = self.send(&mut response).ok();
    }
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
