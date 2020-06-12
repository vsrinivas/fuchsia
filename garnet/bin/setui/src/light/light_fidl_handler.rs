// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_settings::{
    LightError, LightGroup, LightMarker, LightRequest, LightRequestStream,
    LightSetLightGroupValuesResponder, LightState, LightWatchLightGroupsResponder,
};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon::Status;
use futures::future::LocalBoxFuture;
use futures::FutureExt;

use crate::fidl_processor::{process_stream, RequestContext};
use crate::switchboard::base::FidlResponseErrorLogger;
use crate::switchboard::base::{SettingRequest, SettingResponse, SettingType, SwitchboardClient};
use crate::switchboard::hanging_get_handler::Sender;

impl Sender<Vec<LightGroup>> for LightWatchLightGroupsResponder {
    fn send_response(self, data: Vec<LightGroup>) {
        self.send(&mut data.into_iter()).log_fidl_response_error(LightMarker::DEBUG_NAME);
    }

    fn on_error(self) {
        fx_log_err!("error occurred watching for service: {:?}", LightMarker::DEBUG_NAME);
        self.control_handle().shutdown_with_epitaph(Status::INTERNAL);
    }
}

impl From<SettingResponse> for Vec<LightGroup> {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Light(info) = response {
            // Internally we store the data in a HashMap, need to flatten it out into a vector.
            return info
                .light_groups
                .values()
                .into_iter()
                .cloned()
                .map(LightGroup::from)
                .collect::<Vec<_>>();
        }

        panic!("incorrect value sent to light");
    }
}

pub fn spawn_light_fidl_handler(switchboard_client: SwitchboardClient, stream: LightRequestStream) {
    // TODO(fxb/53312): implement WatchLightGroup.
    process_stream::<LightMarker, Vec<LightGroup>, LightWatchLightGroupsResponder>(
        stream,
        switchboard_client,
        SettingType::Light,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<LightRequest>, anyhow::Error>> {
                async move {
                    // Support future expansion of FIDL
                    #[allow(unreachable_patterns)]
                    match req {
                        LightRequest::SetLightGroupValues { name, state, responder } => {
                            set(context.clone(), name, state, responder).await;
                        }
                        LightRequest::WatchLightGroups { responder } => {
                            context.watch(responder, true).await;
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

async fn set(
    context: RequestContext<Vec<LightGroup>, LightWatchLightGroupsResponder>,
    name: String,
    state: Vec<LightState>,
    responder: LightSetLightGroupValuesResponder,
) {
    match context
        .switchboard_client
        .request(
            SettingType::Light,
            SettingRequest::SetLightGroupValue(
                name,
                state.into_iter().map(LightState::into).collect::<Vec<_>>(),
            ),
        )
        .await
    {
        Ok(response_rx) => {
            fasync::spawn(async move {
                let result = match response_rx.await {
                    Ok(Ok(_)) => responder.send(&mut Ok(())),
                    _ => responder.send(&mut Err(LightError::Failed)),
                };
                result.log_fidl_response_error(LightMarker::DEBUG_NAME);
            });
        }
        Err(_) => {
            // Report back an error immediately if we could not successfully make the light set
            // request. The return result can be ignored as there is no actionable steps that
            // can be taken.
            responder
                .send(&mut Err(LightError::Failed))
                .log_fidl_response_error(LightMarker::DEBUG_NAME);
        }
    }
}

#[cfg(test)]
mod tests {
    use std::collections::HashMap;

    use fidl_fuchsia_settings::{LightType, LightValue};

    use crate::fidl_clone::FIDLClone;
    use crate::switchboard::light_types::LightInfo;

    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_response_to_vector_empty() {
        let response: Vec<LightGroup> = SettingResponse::into(SettingResponse::Light(LightInfo {
            light_groups: Default::default(),
        }));

        assert_eq!(response, vec![]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_response_to_vector() {
        let light_group_1 = LightGroup {
            name: Some("test".to_string()),
            enabled: Some(true),
            type_: Some(LightType::Simple),
            lights: Some(vec![LightState { value: Some(LightValue::On(true)) }]),
        };
        let light_group_2 = LightGroup {
            name: Some("test2".to_string()),
            enabled: Some(false),
            type_: Some(LightType::Rgb),
            lights: Some(vec![LightState { value: Some(LightValue::Brightness(42)) }]),
        };

        let mut light_groups: HashMap<String, crate::switchboard::light_types::LightGroup> =
            HashMap::new();
        light_groups.insert("test".to_string(), light_group_1.clone().into());
        light_groups.insert("test2".to_string(), light_group_2.clone().into());

        let mut response: Vec<LightGroup> =
            SettingResponse::into(SettingResponse::Light(LightInfo { light_groups }));

        // Sort so light groups are in a predictable order.
        response.sort_by_key(|l| l.name.clone());

        assert_eq!(response, vec![light_group_1, light_group_2]);
    }
}
