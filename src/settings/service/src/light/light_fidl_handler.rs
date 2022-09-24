// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::DefaultHasher;
use std::convert::TryFrom;
use std::hash::Hash;
use std::hash::Hasher;

use fidl::prelude::*;
use fidl_fuchsia_settings::{
    LightError, LightGroup, LightRequest, LightSetLightGroupValuesResponder,
    LightSetLightGroupValuesResult, LightState, LightWatchLightGroupResponder,
    LightWatchLightGroupsResponder,
};
use fuchsia_syslog::fx_log_warn;
use fuchsia_zircon::Status;

use crate::base::{SettingInfo, SettingType};
use crate::handler;
use crate::handler::base::{Request, Response};
use crate::ingress::{request, watch, Scoped};
use crate::job::source::Error as JobError;
use crate::job::Job;
use crate::light::light_controller::ARG_NAME;

impl watch::Responder<Vec<LightGroup>, fuchsia_zircon::Status> for LightWatchLightGroupsResponder {
    fn respond(self, response: Result<Vec<LightGroup>, fuchsia_zircon::Status>) {
        match response {
            Ok(light_groups) => {
                let _ = self.send(&mut light_groups.into_iter());
            }
            Err(error) => {
                self.control_handle().shutdown_with_epitaph(error);
            }
        }
    }
}

impl watch::Responder<Vec<LightGroup>, fuchsia_zircon::Status> for IndividualLightGroupResponder {
    fn respond(self, response: Result<Vec<LightGroup>, fuchsia_zircon::Status>) {
        let light_group_name = self.light_group_name;
        match response {
            Ok(light_groups) => {
                match light_groups.into_iter().find(|group| {
                    group.name.as_ref().map(|n| *n == light_group_name).unwrap_or(false)
                }) {
                    Some(group) => {
                        let _ = self.responder.send(group);
                    }
                    None => {
                        // Failed to find the given light group, close the connection.
                        self.responder.control_handle().shutdown_with_epitaph(Status::NOT_FOUND);
                    }
                }
            }
            Err(error) => {
                self.responder.control_handle().shutdown_with_epitaph(error);
            }
        }
    }
}

impl From<SettingInfo> for Vec<LightGroup> {
    fn from(info: SettingInfo) -> Self {
        if let SettingInfo::Light(light_info) = info {
            // Internally we store the data in a HashMap, need to flatten it out into a vector.
            light_info.light_groups.values().cloned().map(LightGroup::from).collect::<Vec<_>>()
        } else {
            panic!("incorrect value sent to light: {:?}", info);
        }
    }
}

impl From<Response> for Scoped<LightSetLightGroupValuesResult> {
    fn from(response: Response) -> Self {
        Scoped(response.map(|_| ()).map_err(|e| match e {
            handler::base::Error::InvalidArgument(_, argument, _) => {
                if ARG_NAME == argument {
                    LightError::InvalidName
                } else {
                    LightError::InvalidValue
                }
            }
            _ => LightError::Failed,
        }))
    }
}

impl request::Responder<Scoped<LightSetLightGroupValuesResult>>
    for LightSetLightGroupValuesResponder
{
    fn respond(self, Scoped(mut response): Scoped<LightSetLightGroupValuesResult>) {
        let _ = self.send(&mut response);
    }
}

impl TryFrom<LightRequest> for Job {
    type Error = JobError;

    fn try_from(item: LightRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            LightRequest::WatchLightGroups { responder } => {
                Ok(watch::Work::new_job(SettingType::Light, responder))
            }
            LightRequest::WatchLightGroup { name, responder } => {
                let mut hasher = DefaultHasher::new();
                name.hash(&mut hasher);
                let name_clone = name.clone();
                Ok(watch::Work::new_job_with_change_function(
                    SettingType::Light,
                    IndividualLightGroupResponder { responder, light_group_name: name },
                    watch::ChangeFunction::new(
                        hasher.finish(),
                        Box::new(move |old: &SettingInfo, new: &SettingInfo| match (old, new) {
                            (SettingInfo::Light(old_info), SettingInfo::Light(new_info)) => {
                                let old_light_group = old_info.light_groups.get(&name_clone);
                                let new_light_group = new_info.light_groups.get(&name_clone);
                                old_light_group != new_light_group
                            }
                            _ => false,
                        }),
                    ),
                ))
            }
            LightRequest::SetLightGroupValues { name, state, responder } => Ok(request::Work::new(
                SettingType::Light,
                Request::SetLightGroupValue(
                    name,
                    state.into_iter().map(LightState::into).collect::<Vec<_>>(),
                ),
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
/// Responder that wraps LightWatchLightGroupResponder to filter the vector of light groups down to
/// the single light group the client is watching.
struct IndividualLightGroupResponder {
    responder: LightWatchLightGroupResponder,
    light_group_name: String,
}

#[cfg(test)]
mod tests {
    use std::collections::HashMap;

    use fidl_fuchsia_settings::{LightMarker, LightRequestStream};
    use futures::StreamExt;

    use assert_matches::assert_matches;

    use crate::base::SettingInfo;
    use crate::job::{execution, work, Signature};
    use crate::light::types::{LightGroup, LightInfo, LightState, LightType, LightValue};

    use super::*;

    #[test]
    fn test_response_to_vector_empty() {
        let response: Vec<fidl_fuchsia_settings::LightGroup> =
            SettingInfo::into((LightInfo { light_groups: Default::default() }).into());

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
            disable_conditions: vec![],
        };
        let light_group_2 = LightGroup {
            name: "test2".to_string(),
            enabled: false,
            light_type: LightType::Rgb,
            lights: vec![LightState { value: Some(LightValue::Brightness(0.42)) }],
            hardware_index: vec![],
            disable_conditions: vec![],
        };

        let light_groups: HashMap<_, _> = IntoIterator::into_iter([
            (String::from("test"), light_group_1.clone()),
            (String::from("test2"), light_group_2.clone()),
        ])
        .collect();

        let mut response: Vec<fidl_fuchsia_settings::LightGroup> =
            SettingInfo::into((LightInfo { light_groups }).into());

        // Sort so light groups are in a predictable order.
        response.sort_by_key(|l| l.name.clone());

        assert_eq!(response, vec![light_group_1.into(), light_group_2.into()]);
    }

    // Verify that a WatchLightGroups request is converted into a sequential job.
    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_watch_light_groups_request() {
        // Connect to the Light service and make a watch request.
        let (proxy, server) =
            fidl::endpoints::create_proxy::<LightMarker>().expect("should be able to create proxy");
        let _fut = proxy.watch_light_groups();
        let mut request_stream: LightRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have an request before stream is closed")
            .expect("should have gotten a request");

        // Verify the request is sequential.
        let job = Job::try_from(request).expect("job conversion should succeed");
        assert_matches!(job.workload(), work::Load::Sequential(_, _));
        assert_matches!(job.execution_type(), execution::Type::Sequential(_));
    }

    /// Converts a WatchLightGroup call with the given light group name to a job and returns the
    /// signature. Also asserts that the created job is sequential.
    ///
    /// This method creates a FIDL proxy to make requests through because directly creating FIDL
    /// request objects is difficult and not intended by the API.
    async fn signature_from_watch_light_group_request(light_name: &str) -> Signature {
        let (proxy, server) =
            fidl::endpoints::create_proxy::<LightMarker>().expect("should be able to create proxy");
        let _fut = proxy.watch_light_group(light_name);
        let mut request_stream: LightRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have an request before stream is closed")
            .expect("should have gotten a request");
        let job = Job::try_from(request).expect("job conversion should succeed");
        assert_matches!(job.workload(), work::Load::Sequential(_, _));
        assert_matches!(job.execution_type(), execution::Type::Sequential(signature) => signature)
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_watch_individual_light_group_request() {
        const TEST_LIGHT_NAME: &str = "test_light";

        // Verify that a request is transformed into a sequential job and save the signature.
        let signature = signature_from_watch_light_group_request(TEST_LIGHT_NAME).await;

        // Make another request with the same light group name and save the signature.
        let signature2 = signature_from_watch_light_group_request(TEST_LIGHT_NAME).await;

        // Verify the two requests have the same signature, since they provide the same light group
        // name as input.
        assert_eq!(signature, signature2);

        // Make a request with a different light group name and save the signature.
        let signature3 = signature_from_watch_light_group_request("different_name").await;

        // Verify that the signature of the third request differs from the other two, as it provides
        // a different light group name as input.
        assert_ne!(signature, signature3);
    }

    // Verify that a SetLightGroupValues request is converted into an independent job
    #[fuchsia_async::run_until_stalled(test)]
    async fn try_from_set_light_group_values_request() {
        // Connect to the Light service and make a set request.
        let (proxy, server) =
            fidl::endpoints::create_proxy::<LightMarker>().expect("should be able to create proxy");
        let _fut = proxy.set_light_group_values("arbitrary name", &mut vec![].into_iter());
        let mut request_stream: LightRequestStream =
            server.into_stream().expect("should be able to convert to stream");
        let request = request_stream
            .next()
            .await
            .expect("should have an request before stream is closed")
            .expect("should have gotten a request");

        // Verify the request is sequential.
        let job = Job::try_from(request).expect("job conversion should succeed");
        assert_matches!(job.workload(), work::Load::Independent(_));
        assert_matches!(job.execution_type(), execution::Type::Independent);
    }
}
