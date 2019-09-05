// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::{Arc, RwLock};

use futures::lock::Mutex;
use futures::TryStreamExt;

use fidl_fuchsia_settings::{
    Error, PrivacyRequest, PrivacyRequestStream, PrivacySetResponder, PrivacySettings,
    PrivacyWatchResponder,
};
use fuchsia_async as fasync;

use crate::switchboard::base::{
    SettingRequest, SettingResponse, SettingResponseResult, SettingType, Switchboard,
};
use crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender};

type PrivacyHangingGetHandler =
    Arc<Mutex<HangingGetHandler<PrivacySettings, PrivacyWatchResponder>>>;

impl Sender<PrivacySettings> for PrivacyWatchResponder {
    fn send_response(self, data: PrivacySettings) {
        self.send(&mut Ok(data)).unwrap();
    }
}

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

pub struct PrivacyFidlHandler {
    switchboard_handle: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    hanging_get_handler: PrivacyHangingGetHandler,
}

/// Handler for translating Privacy service requests into SetUI switchboard commands.
impl PrivacyFidlHandler {
    pub fn spawn(
        switchboard: Arc<RwLock<dyn Switchboard + Send + Sync>>,
        mut stream: PrivacyRequestStream,
    ) {
        fasync::spawn(async move {
            let handler = Self {
                switchboard_handle: switchboard.clone(),
                hanging_get_handler: HangingGetHandler::create(
                    switchboard.clone(),
                    SettingType::Privacy,
                ),
            };

            while let Ok(Some(req)) = stream.try_next().await {
                // Support future expansion of FIDL
                #[allow(unreachable_patterns)]
                match req {
                    PrivacyRequest::Set { settings, responder } => {
                        handler.set(settings, responder);
                    }
                    PrivacyRequest::Watch { responder } => {
                        handler.watch(responder).await;
                    }
                    _ => {}
                }
            }
        })
    }

    fn set(&self, settings: PrivacySettings, responder: PrivacySetResponder) {
        let (response_tx, response_rx) =
            futures::channel::oneshot::channel::<SettingResponseResult>();
        match self.switchboard_handle.write().unwrap().request(
            SettingType::Privacy,
            settings.into(),
            response_tx,
        ) {
            Ok(_) => {
                fasync::spawn(async move {
                    let _ = match response_rx.await {
                        Ok(_) => responder.send(&mut Ok(())),
                        Err(_) => responder.send(&mut Err(Error::Failed)),
                    };
                });
            }
            Err(_) => {
                // Report back an error immediately if we could not successfully make the privacy
                // set request. The return result can be ignored as there is no actionable steps
                // that can be taken.
                let _ = responder.send(&mut Err(Error::Failed));
            }
        }
    }

    async fn watch(&self, responder: PrivacyWatchResponder) {
        let mut hanging_get_lock = self.hanging_get_handler.lock().await;
        hanging_get_lock.watch(responder).await;
    }
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
