// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::fidl_hanging_get_responder;
use crate::handler::base::{Request, Response};
use crate::ingress::Scoped;
use crate::ingress::{request, watch};
use crate::job::source::ErrorResponder;
use crate::job::{Job, Signature};
use fidl_fuchsia_settings::{
    Error, FactoryResetMarker, FactoryResetRequest, FactoryResetSetResponder,
    FactoryResetSetResult, FactoryResetSettings, FactoryResetWatchResponder,
};
use fuchsia_syslog::fx_log_warn;
use std::convert::TryFrom;

use crate::job::source::Error as JobError;

const WATCH_JOB_SIGNATURE: usize = 1;

fidl_hanging_get_responder!(FactoryResetMarker, FactoryResetSettings, FactoryResetWatchResponder,);

impl ErrorResponder for FactoryResetSetResponder {
    fn id(&self) -> &'static str {
        "FactoryReset_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
    }
}

impl From<Response> for Scoped<FactoryResetSetResult> {
    fn from(response: Response) -> Self {
        Scoped(response.map_or(Err(Error::Failed), |_| Ok(())))
    }
}

impl request::Responder<Scoped<FactoryResetSetResult>> for FactoryResetSetResponder {
    fn respond(self, Scoped(mut response): Scoped<FactoryResetSetResult>) {
        self.send(&mut response).ok();
    }
}

impl watch::Responder<FactoryResetSettings, fuchsia_zircon::Status> for FactoryResetWatchResponder {
    fn respond(self, response: Result<FactoryResetSettings, fuchsia_zircon::Status>) {
        match response {
            Ok(settings) => {
                self.send(settings).ok();
            }
            Err(error) => {
                self.control_handle().shutdown_with_epitaph(error);
            }
        }
    }
}

impl TryFrom<FactoryResetRequest> for Job {
    type Error = JobError;
    fn try_from(item: FactoryResetRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            FactoryResetRequest::Set { settings, responder } => match to_request(settings) {
                Some(request) => {
                    Ok(request::Work::new(SettingType::FactoryReset, request, responder).into())
                }
                None => Err(JobError::InvalidInput(Box::new(responder))),
            },
            FactoryResetRequest::Watch { responder } => Ok(watch::Work::new(
                SettingType::FactoryReset,
                Signature::new(WATCH_JOB_SIGNATURE),
                responder,
            )
            .into()),
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", item);
                Err(JobError::Unsupported)
            }
        }
    }
}

impl From<SettingInfo> for FactoryResetSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::FactoryReset(info) = response {
            let mut factory_reset_settings = FactoryResetSettings::EMPTY;
            factory_reset_settings.is_local_reset_allowed = Some(info.is_local_reset_allowed);
            factory_reset_settings
        } else {
            panic!("incorrect value sent to factory_reset");
        }
    }
}

fn to_request(settings: FactoryResetSettings) -> Option<Request> {
    settings.is_local_reset_allowed.map(Request::SetLocalResetAllowed)
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::job::{execution, work};
    use fidl_fuchsia_settings::FactoryResetRequestStream;
    use futures::StreamExt;
    use matches::assert_matches;

    #[test]
    fn to_request_maps_correctly() {
        let result = to_request(FactoryResetSettings {
            is_local_reset_allowed: Some(true),
            ..FactoryResetSettings::EMPTY
        });
        assert_matches!(result, Some(Request::SetLocalResetAllowed(true)));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_set_handles_missing_params() {
        let (proxy, server) = fidl::endpoints::create_proxy::<FactoryResetMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy.set(FactoryResetSettings::EMPTY);
        let mut request_stream: FactoryResetRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have on request before stream is closed")
            .expect("should have gotten a request");
        let job = Job::try_from(request);
        assert_matches!(job, Err(crate::job::source::Error::InvalidInput(_)));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_set_converts_supplied_params() {
        let (proxy, server) = fidl::endpoints::create_proxy::<FactoryResetMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy.set(FactoryResetSettings {
            is_local_reset_allowed: Some(true),
            ..FactoryResetSettings::EMPTY
        });
        let mut request_stream: FactoryResetRequestStream =
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
        let (proxy, server) = fidl::endpoints::create_proxy::<FactoryResetMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy.watch();
        let mut request_stream: FactoryResetRequestStream =
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
