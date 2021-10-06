// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::fidl_hanging_get_responder;
use crate::handler::base::Request;
use crate::ingress::{request, watch, Scoped};
use crate::job::source::{Error as JobError, ErrorResponder};
use crate::job::Job;
use fidl::prelude::*;
use fidl_fuchsia_settings::{
    PrivacyMarker, PrivacyRequest, PrivacySetResponder, PrivacySetResult, PrivacySettings,
    PrivacyWatchResponder,
};
use fuchsia_syslog::fx_log_warn;
use fuchsia_zircon as zx;
use std::convert::TryFrom;

fidl_hanging_get_responder!(PrivacyMarker, PrivacySettings, PrivacyWatchResponder,);

impl ErrorResponder for PrivacySetResponder {
    fn id(&self) -> &'static str {
        "Privacy_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
    }
}

impl request::Responder<Scoped<PrivacySetResult>> for PrivacySetResponder {
    fn respond(self, Scoped(mut response): Scoped<PrivacySetResult>) {
        let _ = self.send(&mut response);
    }
}

impl watch::Responder<PrivacySettings, zx::Status> for PrivacyWatchResponder {
    fn respond(self, response: Result<PrivacySettings, zx::Status>) {
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

impl TryFrom<PrivacyRequest> for Job {
    type Error = JobError;

    fn try_from(item: PrivacyRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            PrivacyRequest::Set { settings, responder } => {
                Ok(request::Work::new(SettingType::Privacy, to_request(settings), responder).into())
            }
            PrivacyRequest::Watch { responder } => {
                Ok(watch::Work::new(SettingType::Privacy, responder).into())
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", item);
                Err(JobError::Unsupported)
            }
        }
    }
}

impl From<SettingInfo> for PrivacySettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::Privacy(info) = response {
            return PrivacySettings {
                user_data_sharing_consent: info.user_data_sharing_consent,
                ..PrivacySettings::EMPTY
            };
        }

        panic!("incorrect value sent to privacy");
    }
}

fn to_request(settings: PrivacySettings) -> Request {
    Request::SetUserDataSharingConsent(settings.user_data_sharing_consent)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::job::{execution, work};
    use fidl_fuchsia_settings::PrivacyRequestStream;
    use futures::StreamExt;
    use matches::assert_matches;

    #[test]
    fn test_request_from_settings_empty() {
        let request = to_request(PrivacySettings::EMPTY);

        assert_eq!(request, Request::SetUserDataSharingConsent(None));
    }

    #[test]
    fn test_request_from_settings() {
        const USER_DATA_SHARING_CONSENT: bool = true;

        let mut privacy_settings = PrivacySettings::EMPTY;
        privacy_settings.user_data_sharing_consent = Some(USER_DATA_SHARING_CONSENT);

        let request = to_request(privacy_settings);

        assert_eq!(request, Request::SetUserDataSharingConsent(Some(USER_DATA_SHARING_CONSENT)));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_set_converts_supplied_params() {
        let (proxy, server) = fidl::endpoints::create_proxy::<PrivacyMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy.set(PrivacySettings {
            user_data_sharing_consent: Some(true),
            ..PrivacySettings::EMPTY
        });
        let mut request_stream: PrivacyRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have on request before stream is closed")
            .expect("should have gotten a request");
        let job = Job::try_from(request);
        assert_matches!(
            job,
            Ok(Job {
                workload: work::Load::Independent(_),
                execution_type: execution::Type::Independent
            })
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_watch_converts_supplied_params() {
        let (proxy, server) = fidl::endpoints::create_proxy::<PrivacyMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy.watch();
        let mut request_stream: PrivacyRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have on request before stream is closed")
            .expect("should have gotten a request");
        let job = Job::try_from(request);
        assert_matches!(
            job,
            Ok(Job {
                workload: work::Load::Sequential(_, _),
                execution_type: execution::Type::Sequential(_)
            })
        );
    }
}
