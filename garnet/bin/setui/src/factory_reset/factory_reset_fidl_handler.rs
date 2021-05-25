// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::fidl_hanging_get_responder;
use crate::handler::base::{Request, Response};
use crate::ingress::Scoped;
use crate::ingress::{request, watch};
use crate::job::{Job, Signature};
use fidl_fuchsia_settings::{
    Error, FactoryResetMarker, FactoryResetRequest, FactoryResetSetResponder,
    FactoryResetSetResult, FactoryResetSettings, FactoryResetWatchResponder,
};

const WATCH_JOB_SIGNATURE: usize = 1;

fidl_hanging_get_responder!(FactoryResetMarker, FactoryResetSettings, FactoryResetWatchResponder,);

impl From<Response> for Scoped<FactoryResetSetResult> {
    fn from(response: Response) -> Self {
        Scoped(response.map_or(Err(Error::Failed), |_| Ok(())))
    }
}

impl request::Responder<Scoped<FactoryResetSetResult>> for FactoryResetSetResponder {
    fn respond(self, response: Scoped<FactoryResetSetResult>) {
        self.send(&mut response.extract()).ok();
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

impl From<fidl::Error> for crate::job::source::Error {
    fn from(_item: fidl::Error) -> Self {
        crate::job::source::Error::Unknown
    }
}

impl From<FactoryResetRequest> for Job {
    fn from(item: FactoryResetRequest) -> Self {
        #[allow(unreachable_patterns)]
        match item {
            FactoryResetRequest::Set { settings, responder } => request::Work::new(
                SettingType::FactoryReset,
                to_request(settings).expect("should convert"),
                responder,
            )
            .into(),
            FactoryResetRequest::Watch { responder } => watch::Work::new(
                SettingType::FactoryReset,
                Signature::new(WATCH_JOB_SIGNATURE),
                responder,
            )
            .into(),
            _ => {
                panic!("Not supported!");
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

    #[test]
    fn to_request_maps_correctly() {
        let result = to_request(FactoryResetSettings {
            is_local_reset_allowed: Some(true),
            ..FactoryResetSettings::EMPTY
        });
        matches::assert_matches!(result, Some(Request::SetLocalResetAllowed(true)));
    }
}
