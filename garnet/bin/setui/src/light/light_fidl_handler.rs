// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_settings::{
    LightError, LightGroup, LightMarker, LightRequest, LightState, LightWatchLightGroupResponder,
    LightWatchLightGroupsResponder,
};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon::Status;
use futures::future::LocalBoxFuture;
use futures::FutureExt;

use crate::fidl_process_full;
use crate::fidl_processor::settings::RequestContext;
use crate::light::light_controller::ARG_NAME;
use crate::switchboard::base::{FidlResponseErrorLogger, SwitchboardError};
use crate::switchboard::base::{SettingRequest, SettingResponse, SettingType};
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

/// Responder that wraps LightWatchLightGroupResponder to filter the vector of light groups down to
/// the single light group the client is watching.
struct IndividualLightGroupResponder {
    responder: LightWatchLightGroupResponder,
    light_group_name: String,
}

impl Sender<Vec<LightGroup>> for IndividualLightGroupResponder {
    fn send_response(self, data: Vec<LightGroup>) {
        let light_group_name = self.light_group_name;
        self.responder
            .send(
                data.into_iter()
                    .find(|group| {
                        group.name.as_ref().map(|n| *n == light_group_name).unwrap_or(false)
                    })
                    .unwrap(),
            )
            .log_fidl_response_error(LightMarker::DEBUG_NAME);
    }

    fn on_error(self) {
        fx_log_err!(
            "error occurred watching light group {} for service: {:?}",
            self.light_group_name,
            LightMarker::DEBUG_NAME
        );
        self.responder.control_handle().shutdown_with_epitaph(Status::INTERNAL);
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

fidl_process_full!(
    Light,
    SettingType::Light,
    Vec<LightGroup>,
    LightWatchLightGroupsResponder,
    String,
    process_request,
    SettingType::Light,
    Vec<LightGroup>,
    IndividualLightGroupResponder,
    String,
    process_watch_light_group_request
);

async fn process_request(
    context: RequestContext<Vec<LightGroup>, LightWatchLightGroupsResponder>,
    req: LightRequest,
) -> Result<Option<LightRequest>, anyhow::Error> {
    match req {
        LightRequest::SetLightGroupValues { name, state, responder } => {
            fasync::Task::spawn(async move {
                let mut res = context
                    .request(
                        SettingType::Light,
                        SettingRequest::SetLightGroupValue(
                            name,
                            state.into_iter().map(LightState::into).collect::<Vec<_>>(),
                        ),
                    )
                    .await
                    .map(|_| ())
                    .map_err(|e| match e {
                        SwitchboardError::InvalidArgument(_, argument, _) => {
                            if ARG_NAME == argument {
                                LightError::InvalidName
                            } else {
                                LightError::InvalidValue
                            }
                        }
                        _ => LightError::Failed,
                    });
                responder.send(&mut res).log_fidl_response_error(LightMarker::DEBUG_NAME);
            })
            .detach();
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

/// Processes a request to watch a single light group.
async fn process_watch_light_group_request(
    context: RequestContext<Vec<LightGroup>, IndividualLightGroupResponder>,
    req: LightRequest,
) -> Result<Option<LightRequest>, anyhow::Error> {
    match req {
        LightRequest::WatchLightGroup { name, responder } => {
            if !validate_light_group_name(name.clone(), context.clone()).await {
                // Light group name not known, close the connection.
                responder.control_handle().shutdown_with_epitaph(Status::INTERNAL);
                return Ok(None);
            }

            let name_clone = name.clone();
            context
                .watch_with_change_fn(
                    name.clone(),
                    Box::new(move |old_data: &Vec<LightGroup>, new_data: &Vec<LightGroup>| {
                        let old_light_group = old_data.iter().find(|group| {
                            group.name.as_ref().map(|n| *n == name_clone).unwrap_or(false)
                        });
                        let new_light_group = new_data.iter().find(|group| {
                            group.name.as_ref().map(|n| *n == name_clone).unwrap_or(false)
                        });
                        old_light_group != new_light_group
                    }),
                    IndividualLightGroupResponder { responder, light_group_name: name },
                    true,
                )
                .await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    return Ok(None);
}

/// Returns true if the given string is the name of a known light group, else returns false.
async fn validate_light_group_name(
    name: String,
    context: RequestContext<Vec<LightGroup>, IndividualLightGroupResponder>,
) -> bool {
    let result = context.request(SettingType::Light, SettingRequest::Get).await;

    match result {
        Ok(Some(SettingResponse::Light(info))) => info.contains_light_group_name(name),
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use std::collections::HashMap;

    use crate::switchboard::base::SettingResponse;
    use crate::switchboard::light_types::{
        LightGroup, LightInfo, LightState, LightType, LightValue,
    };

    #[test]
    fn test_response_to_vector_empty() {
        let response: Vec<fidl_fuchsia_settings::LightGroup> =
            SettingResponse::into(SettingResponse::Light(LightInfo {
                light_groups: Default::default(),
            }));

        assert_eq!(response, vec![]);
    }

    #[test]
    fn test_response_to_vector() {
        let light_group_1 = LightGroup {
            name: "test".to_string(),
            enabled: true,
            light_type: LightType::Simple,
            lights: vec![LightState { value: Some(LightValue::Simple(true)) }],
            hardware_index: vec![],
        };
        let light_group_2 = LightGroup {
            name: "test2".to_string(),
            enabled: false,
            light_type: LightType::Rgb,
            lights: vec![LightState { value: Some(LightValue::Brightness(0.42)) }],
            hardware_index: vec![],
        };

        let mut light_groups: HashMap<String, crate::switchboard::light_types::LightGroup> =
            HashMap::new();
        light_groups.insert("test".to_string(), light_group_1.clone());
        light_groups.insert("test2".to_string(), light_group_2.clone());

        let mut response: Vec<fidl_fuchsia_settings::LightGroup> =
            SettingResponse::into(SettingResponse::Light(LightInfo { light_groups }));

        // Sort so light groups are in a predictable order.
        response.sort_by_key(|l| l.name.clone());

        assert_eq!(response, vec![light_group_1.into(), light_group_2.into()]);
    }
}
