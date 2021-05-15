// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_process;
use fidl_fuchsia_settings::{
    Error, PrivacyMarker, PrivacyRequest, PrivacySettings, PrivacyWatchResponder,
};
use fuchsia_async as fasync;

use crate::base::{SettingInfo, SettingType};
use crate::fidl_hanging_get_responder;
use crate::fidl_processor::settings::RequestContext;
use crate::handler::base::Request;
use crate::request_respond;

fidl_hanging_get_responder!(PrivacyMarker, PrivacySettings, PrivacyWatchResponder,);

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

impl From<PrivacySettings> for Request {
    fn from(settings: PrivacySettings) -> Self {
        Request::SetUserDataSharingConsent(settings.user_data_sharing_consent)
    }
}

fidl_process!(Privacy, SettingType::Privacy, process_request,);

async fn process_request(
    context: RequestContext<PrivacySettings, PrivacyWatchResponder>,
    req: PrivacyRequest,
) -> Result<Option<PrivacyRequest>, anyhow::Error> {
    #[allow(unreachable_patterns)]
    match req {
        PrivacyRequest::Set { settings, responder } => {
            fasync::Task::spawn(async move {
                request_respond!(
                    context,
                    responder,
                    SettingType::Privacy,
                    settings.into(),
                    Ok(()),
                    Err(Error::Failed),
                    PrivacyMarker
                );
            })
            .detach();
        }
        PrivacyRequest::Watch { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    Ok(None)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_request_from_settings_empty() {
        let request = Request::from(PrivacySettings::EMPTY);

        assert_eq!(request, Request::SetUserDataSharingConsent(None));
    }

    #[test]
    fn test_request_from_settings() {
        const USER_DATA_SHARING_CONSENT: bool = true;

        let mut privacy_settings = PrivacySettings::EMPTY;
        privacy_settings.user_data_sharing_consent = Some(USER_DATA_SHARING_CONSENT);

        let request = Request::from(privacy_settings);

        assert_eq!(request, Request::SetUserDataSharingConsent(Some(USER_DATA_SHARING_CONSENT)));
    }
}
