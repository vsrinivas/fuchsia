// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_settings::{
    Error, IntlMarker, IntlRequest, IntlRequestStream, IntlSetResponder, IntlSettings,
    IntlWatchResponder,
};
use fuchsia_async as fasync;
use futures::future::LocalBoxFuture;
use futures::FutureExt;

use crate::fidl_processor::{process_stream, RequestContext};
use crate::switchboard::base::{
    FidlResponseErrorLogger, SettingRequest, SettingResponse, SettingResponseResult, SettingType,
    SwitchboardHandle,
};
use crate::switchboard::hanging_get_handler::Sender;

impl Sender<IntlSettings> for IntlWatchResponder {
    fn send_response(self, data: IntlSettings) {
        self.send(&mut Ok(data)).log_fidl_response_error(IntlMarker::DEBUG_NAME);
    }
}

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

async fn set(
    context: RequestContext<IntlSettings, IntlWatchResponder>,
    settings: IntlSettings,
    responder: IntlSetResponder,
) {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    match context.switchboard.lock().await.request(SettingType::Intl, settings.into(), response_tx)
    {
        Ok(_) => {
            fasync::spawn(async move {
                let result = match response_rx.await {
                    Ok(Ok(_)) => responder.send(&mut Ok(())),
                    _ => responder.send(&mut Err(Error::Failed)),
                };
                result.log_fidl_response_error(IntlMarker::DEBUG_NAME);
            });
        }
        Err(_) => {
            // Report back an error immediately if we could not successfully make the intl set
            // request. The return result can be ignored as there is no actionable steps that
            // can be taken.
            responder.send(&mut Err(Error::Failed)).log_fidl_response_error(IntlMarker::DEBUG_NAME);
        }
    }
}

pub fn spawn_intl_fidl_handler(switchboard: SwitchboardHandle, stream: IntlRequestStream) {
    process_stream::<IntlMarker, IntlSettings, IntlWatchResponder>(
        stream,
        switchboard,
        SettingType::Intl,
        Box::new(
            move |context, req| -> LocalBoxFuture<'_, Result<Option<IntlRequest>, anyhow::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        IntlRequest::Set { settings, responder } => {
                            set(context.clone(), settings, responder).await;
                        }
                        IntlRequest::Watch { responder } => context.watch(responder).await,
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
    use crate::switchboard::intl_types::{IntlInfo, LocaleId, TemperatureUnit};

    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_from_settings_empty() {
        let request = SettingRequest::from(IntlSettings::empty());

        assert_eq!(
            request,
            SettingRequest::SetIntlInfo(IntlInfo {
                locales: None,
                temperature_unit: None,
                time_zone_id: None,
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_from_settings() {
        const TIME_ZONE_ID: &'static str = "PDT";

        let intl_settings = IntlSettings {
            locales: Some(vec![fidl_fuchsia_intl::LocaleId { id: "blah".into() }]),
            temperature_unit: Some(fidl_fuchsia_intl::TemperatureUnit::Celsius),
            time_zone_id: Some(fidl_fuchsia_intl::TimeZoneId { id: TIME_ZONE_ID.to_string() }),
        };

        let request = SettingRequest::from(intl_settings);

        assert_eq!(
            request,
            SettingRequest::SetIntlInfo(IntlInfo {
                locales: Some(vec![LocaleId { id: "blah".into() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some(TIME_ZONE_ID.to_string()),
            })
        );
    }
}
