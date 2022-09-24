// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::handler::base::Request;
use crate::ingress::{request, watch, Scoped};
use crate::job::source::{Error as JobError, ErrorResponder};
use crate::job::Job;
use crate::keyboard::types::{Autorepeat, KeyboardInfo, KeymapId};
use fidl::prelude::*;
use fidl_fuchsia_settings::{
    KeyboardRequest, KeyboardSetResponder, KeyboardSetSetResult, KeyboardSettings,
    KeyboardWatchResponder,
};
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use fuchsia_zircon as zx;
use std::convert::TryFrom;

impl ErrorResponder for KeyboardSetResponder {
    fn id(&self) -> &'static str {
        "Keyboard_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
    }
}

impl request::Responder<Scoped<KeyboardSetSetResult>> for KeyboardSetResponder {
    fn respond(self, Scoped(mut response): Scoped<KeyboardSetSetResult>) {
        let _ = self.send(&mut response);
    }
}

impl watch::Responder<KeyboardSettings, zx::Status> for KeyboardWatchResponder {
    fn respond(self, response: Result<KeyboardSettings, zx::Status>) {
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

impl TryFrom<KeyboardRequest> for Job {
    type Error = JobError;

    fn try_from(item: KeyboardRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            KeyboardRequest::Set { settings, responder } => match to_request(settings) {
                Ok(request) => {
                    Ok(request::Work::new(SettingType::Keyboard, request, responder).into())
                }
                Err(e) => {
                    fx_log_err!(
                        "Transferring from KeyboardSettings to a Set request has an error: {:?}",
                        e
                    );
                    Err(JobError::InvalidInput(Box::new(responder)))
                }
            },
            KeyboardRequest::Watch { responder } => {
                Ok(watch::Work::new_job(SettingType::Keyboard, responder))
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", item);
                Err(JobError::Unsupported)
            }
        }
    }
}

impl From<SettingInfo> for KeyboardSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::Keyboard(info) = response {
            return KeyboardSettings {
                keymap: info.keymap.map(KeymapId::into),
                autorepeat: info.autorepeat.map(Autorepeat::into),
                ..KeyboardSettings::EMPTY
            };
        }

        panic!("incorrect value sent to keyboard");
    }
}

fn to_request(settings: KeyboardSettings) -> Result<Request, String> {
    let autorepeat: Option<Autorepeat> = settings.autorepeat.map(|src| src.into());
    let keymap = settings.keymap.map(KeymapId::try_from).transpose()?;
    Ok(Request::SetKeyboardInfo(KeyboardInfo { keymap, autorepeat }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::job::{execution, work};
    use assert_matches::assert_matches;
    use fidl_fuchsia_settings::{KeyboardMarker, KeyboardRequestStream};
    use futures::StreamExt;

    #[test]
    fn test_request_from_settings_empty() {
        let request = to_request(KeyboardSettings::EMPTY).unwrap();

        assert_eq!(
            request,
            Request::SetKeyboardInfo(KeyboardInfo { keymap: None, autorepeat: None })
        );
    }

    #[test]
    fn test_request_from_settings_error() {
        let mut keyboard_settings = KeyboardSettings::EMPTY;
        keyboard_settings.keymap = Some(fidl_fuchsia_input::KeymapId::unknown());

        assert!(format!("{:?}", to_request(keyboard_settings).unwrap_err())
            .contains("Received an invalid keymap id:"));
    }

    #[test]
    fn test_request_from_settings() {
        use crate::keyboard::types::Autorepeat;

        const KEYMAP_ID: fidl_fuchsia_input::KeymapId = fidl_fuchsia_input::KeymapId::FrAzerty;
        const DELAY: i64 = 1;
        const PERIOD: i64 = 2;
        const AUTOREPEAT: fidl_fuchsia_settings::Autorepeat =
            fidl_fuchsia_settings::Autorepeat { delay: DELAY, period: PERIOD };

        let mut keyboard_settings = KeyboardSettings::EMPTY;
        keyboard_settings.keymap = Some(KEYMAP_ID);
        keyboard_settings.autorepeat = Some(AUTOREPEAT);

        let request = to_request(keyboard_settings).unwrap();

        assert_eq!(
            request,
            Request::SetKeyboardInfo(KeyboardInfo {
                keymap: Some(KeymapId::FrAzerty),
                autorepeat: Some(Autorepeat { delay: DELAY, period: PERIOD }),
            })
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_set_converts_supplied_params() {
        let (proxy, server) = fidl::endpoints::create_proxy::<KeyboardMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy.set(KeyboardSettings {
            keymap: Some(fidl_fuchsia_input::KeymapId::FrAzerty),
            ..KeyboardSettings::EMPTY
        });
        let mut request_stream: KeyboardRequestStream =
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
        let (proxy, server) = fidl::endpoints::create_proxy::<KeyboardMarker>()
            .expect("should be able to create proxy");
        let _fut = proxy.watch();
        let mut request_stream: KeyboardRequestStream =
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
