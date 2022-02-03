// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::fidl_hanging_get_responder;
use crate::handler::base::Request;
use crate::ingress::{request, watch, Scoped};
use crate::job::source::{Error as JobError, ErrorResponder};
use crate::job::Job;
use crate::night_mode::types::NightModeInfo;
use fidl::prelude::*;
use fidl_fuchsia_settings::{
    NightModeMarker, NightModeRequest, NightModeSetResponder, NightModeSetResult,
    NightModeSettings, NightModeWatchResponder,
};
use fuchsia_syslog::fx_log_warn;
use std::convert::TryFrom;

fidl_hanging_get_responder!(NightModeMarker, NightModeSettings, NightModeWatchResponder,);

impl ErrorResponder for NightModeSetResponder {
    fn id(&self) -> &'static str {
        "NightMode_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
    }
}

impl request::Responder<Scoped<NightModeSetResult>> for NightModeSetResponder {
    fn respond(self, Scoped(mut response): Scoped<NightModeSetResult>) {
        let _ = self.send(&mut response);
    }
}

impl watch::Responder<NightModeSettings, fuchsia_zircon::Status> for NightModeWatchResponder {
    fn respond(self, response: Result<NightModeSettings, fuchsia_zircon::Status>) {
        match response {
            Ok(settings) => {
                let _ = self.send(settings);
            }
            Err(error) => {
                self.control_handle().shutdown_with_epitaph(error);
            }
        }
    }
}

impl From<SettingInfo> for NightModeSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::NightMode(info) = response {
            let mut night_mode_settings = NightModeSettings::EMPTY;
            night_mode_settings.night_mode_enabled = info.night_mode_enabled;
            return night_mode_settings;
        }

        panic!("incorrect value sent to night_mode");
    }
}

impl From<NightModeSettings> for Request {
    fn from(settings: NightModeSettings) -> Self {
        let mut night_mode_info = NightModeInfo::empty();
        night_mode_info.night_mode_enabled = settings.night_mode_enabled;
        Request::SetNightModeInfo(night_mode_info)
    }
}

impl TryFrom<NightModeRequest> for Job {
    type Error = JobError;

    fn try_from(item: NightModeRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            NightModeRequest::Set { settings, responder } => {
                Ok(request::Work::new(SettingType::NightMode, to_request(settings), responder)
                    .into())
            }
            NightModeRequest::Watch { responder } => {
                Ok(watch::Work::new_job(SettingType::NightMode, responder))
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", item);
                Err(JobError::Unsupported)
            }
        }
    }
}

fn to_request(settings: NightModeSettings) -> Request {
    Request::SetNightModeInfo(NightModeInfo { night_mode_enabled: settings.night_mode_enabled })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::job::{execution, work};
    use assert_matches::assert_matches;
    use fidl_fuchsia_settings::NightModeRequestStream;
    use futures::StreamExt;

    #[test]
    fn test_request_from_settings_empty() {
        let request = to_request(NightModeSettings::EMPTY);
        let night_mode_info = NightModeInfo::empty();
        assert_eq!(request, Request::SetNightModeInfo(night_mode_info));
    }

    #[test]
    fn test_request_from_settings() {
        const NIGHT_MODE_ENABLED: bool = true;

        let mut night_mode_settings = NightModeSettings::EMPTY;
        night_mode_settings.night_mode_enabled = Some(NIGHT_MODE_ENABLED);
        let request = to_request(night_mode_settings);

        let mut night_mode_info = NightModeInfo::empty();
        night_mode_info.night_mode_enabled = Some(NIGHT_MODE_ENABLED);

        assert_eq!(request, Request::SetNightModeInfo(night_mode_info));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_set_converts_supplied_params() {
        let (proxy, server) = fidl::endpoints::create_proxy::<NightModeMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy
            .set(NightModeSettings { night_mode_enabled: Some(true), ..NightModeSettings::EMPTY });
        let mut request_stream: NightModeRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have on request before stream is closed")
            .expect("should have gotten a request");
        let job = Job::try_from(request);
        let job = job.as_ref();
        assert_matches!(job.map(|j| j.workload()), Ok(work::Load::Independent(_)));
        assert_matches!(job.map(|j| j.execution_type()), Ok(execution::Type::Independent));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_watch_converts_supplied_params() {
        let (proxy, server) = fidl::endpoints::create_proxy::<NightModeMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy.watch();
        let mut request_stream: NightModeRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have on request before stream is closed")
            .expect("should have gotten a request");
        let job = Job::try_from(request);
        let job = job.as_ref();
        assert_matches!(job.map(|j| j.workload()), Ok(work::Load::Sequential(_, _)));
        assert_matches!(job.map(|j| j.execution_type()), Ok(execution::Type::Sequential(_)));
    }
}
