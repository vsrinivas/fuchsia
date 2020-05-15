// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::fidl_processor::process_stream;

use futures::FutureExt;

use futures::future::LocalBoxFuture;

use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_settings::{
    Error, PrivacyMarker, PrivacyRequest, PrivacyRequestStream, PrivacySetResponder,
    PrivacySettings, PrivacyWatchResponder,
};
use fuchsia_async as fasync;

use crate::fidl_hanging_get_result_responder;
use crate::switchboard::base::{
    FidlResponseErrorLogger, SettingRequest, SettingResponse, SettingType, SwitchboardClient,
};
use crate::switchboard::hanging_get_handler::Sender;

fidl_hanging_get_result_responder!(
    PrivacySettings,
    PrivacyWatchResponder,
    PrivacyMarker::DEBUG_NAME
);

impl From<SettingResponse> for PrivacySettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Privacy(info) = response {
            return PrivacySettings { user_data_sharing_consent: info.user_data_sharing_consent };
        }

        panic!("incorrect value sent to privacy");
    }
}

impl From<PrivacySettings> for SettingRequest {
    fn from(settings: PrivacySettings) -> Self {
        SettingRequest::SetUserDataSharingConsent(settings.user_data_sharing_consent)
    }
}

async fn set(
    switchboard_client: &SwitchboardClient,
    settings: PrivacySettings,
    responder: PrivacySetResponder,
) {
    match switchboard_client.request(SettingType::Privacy, settings.into()).await {
        Ok(response_rx) => {
            fasync::spawn(async move {
                let result = match response_rx.await {
                    Ok(_) => responder.send(&mut Ok(())),
                    Err(_) => responder.send(&mut Err(Error::Failed)),
                };
                result.log_fidl_response_error(PrivacyMarker::DEBUG_NAME);
            });
        }
        Err(_) => {
            // Report back an error immediately if we could not successfully make the privacy
            // set request. The return result can be ignored as there is no actionable steps
            // that can be taken.
            responder
                .send(&mut Err(Error::Failed))
                .log_fidl_response_error(PrivacyMarker::DEBUG_NAME);
        }
    }
}

pub fn spawn_privacy_fidl_handler(
    switchboard_client: SwitchboardClient,
    stream: PrivacyRequestStream,
) {
    process_stream::<PrivacyMarker, PrivacySettings, PrivacyWatchResponder>(
        stream,
        switchboard_client,
        SettingType::Privacy,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<PrivacyRequest>, anyhow::Error>> {
                async move {
                    #[allow(unreachable_patterns)]
                    match req {
                        PrivacyRequest::Set { settings, responder } => {
                            set(&context.switchboard_client, settings, responder).await;
                        }
                        PrivacyRequest::Watch { responder } => {
                            context.watch(responder, false).await;
                        }
                        _ => {
                            return Ok(Some(req));
                        }
                    }

                    return Ok(None);
                }
                .boxed_local()
            },
        ),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_from_settings_empty() {
        let request = SettingRequest::from(PrivacySettings::empty());

        assert_eq!(request, SettingRequest::SetUserDataSharingConsent(None));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_from_settings() {
        const USER_DATA_SHARING_CONSENT: bool = true;

        let mut privacy_settings = PrivacySettings::empty();
        privacy_settings.user_data_sharing_consent = Some(USER_DATA_SHARING_CONSENT);

        let request = SettingRequest::from(privacy_settings);

        assert_eq!(
            request,
            SettingRequest::SetUserDataSharingConsent(Some(USER_DATA_SHARING_CONSENT))
        );
    }
}
