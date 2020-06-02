// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use fidl_fuchsia_settings::{
    Error, IntlMarker, IntlRequest, IntlRequestStream, IntlSetResponder, IntlSettings,
    IntlWatch2Responder, IntlWatchResponder,
};
use fuchsia_async as fasync;
use futures::future::LocalBoxFuture;
use futures::FutureExt;

use crate::fidl_hanging_get_responder;
use crate::fidl_hanging_get_result_responder;
use crate::fidl_processor::{process_stream_both_watches, RequestContext};
use crate::switchboard::base::{SettingRequest, SettingResponse, SettingType, SwitchboardClient};
use crate::switchboard::hanging_get_handler::Sender;

fidl_hanging_get_responder!(IntlSettings, IntlWatch2Responder, IntlMarker::DEBUG_NAME);

// TODO(fxb/52593): Remove when clients are ported to watch2.
fidl_hanging_get_result_responder!(IntlSettings, IntlWatchResponder, IntlMarker::DEBUG_NAME);

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
    context: RequestContext<IntlSettings, IntlWatch2Responder>,
    settings: IntlSettings,
    responder: IntlSetResponder,
) {
    match context.switchboard_client.request(SettingType::Intl, settings.into()).await {
        Ok(response_rx) => {
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

pub fn spawn_intl_fidl_handler(switchboard_client: SwitchboardClient, stream: IntlRequestStream) {
    // TODO(fxb/52593): Convert back to process_stream when clients are ported to watch2.
    process_stream_both_watches::<IntlMarker, IntlSettings, IntlWatchResponder, IntlWatch2Responder>(
        stream,
        switchboard_client,
        SettingType::Intl,
        // Separate handlers because there are two separate Responders for Watch and
        // Watch2. The hanging get handlers can only handle one type of Responder
        // at a time, so they must be registered separately.
        Box::new(
            move |context, req| -> LocalBoxFuture<'_, Result<Option<IntlRequest>, anyhow::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        IntlRequest::Watch { responder } => context.watch(responder, false).await,
                        _ => {
                            return Ok(Some(req));
                        }
                    }
                    return Ok(None);
                }
                .boxed_local()
            },
        ),
        Box::new(
            move |context, req| -> LocalBoxFuture<'_, Result<Option<IntlRequest>, anyhow::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        IntlRequest::Set { settings, responder } => {
                            set(context.clone(), settings, responder).await;
                        }
                        IntlRequest::Watch2 { responder } => context.watch(responder, true).await,
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
    use crate::switchboard::intl_types::{HourCycle, IntlInfo, LocaleId, TemperatureUnit};

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
                hour_cycle: None,
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
            hour_cycle: Some(fidl_fuchsia_settings::HourCycle::H12),
        };

        let request = SettingRequest::from(intl_settings);

        assert_eq!(
            request,
            SettingRequest::SetIntlInfo(IntlInfo {
                locales: Some(vec![LocaleId { id: "blah".into() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some(TIME_ZONE_ID.to_string()),
                hour_cycle: Some(HourCycle::H12),
            })
        );
    }
}
