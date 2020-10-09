// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::policy_request_respond;
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_settings_policy::{
    Property, VolumePolicyControllerAddPolicyResponder, VolumePolicyControllerAddPolicyResult,
    VolumePolicyControllerGetPropertiesResponder, VolumePolicyControllerMarker,
    VolumePolicyControllerRemovePolicyResponder, VolumePolicyControllerRemovePolicyResult,
    VolumePolicyControllerRequest,
};
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon::Status;
use futures::future::LocalBoxFuture;
use futures::FutureExt;
use std::convert::TryInto;

use crate::audio::policy::{self as audio, PolicyId, Response, Transform};
use crate::fidl_process_policy;
use crate::fidl_processor::policy::RequestContext;
use crate::fidl_result_sender_for_responder;
use crate::internal::policy::{Address, Payload};
use crate::policy::base as policy;
use crate::switchboard::base::{FidlResponseErrorLogger, SettingType};
use crate::switchboard::hanging_get_handler::Sender;

fidl_result_sender_for_responder!(
    VolumePolicyControllerMarker,
    VolumePolicyControllerAddPolicyResult,
    VolumePolicyControllerAddPolicyResponder,
    VolumePolicyControllerRemovePolicyResult,
    VolumePolicyControllerRemovePolicyResponder
);

/// Custom sender implementation for the GetProperty call, since the return is a vector and not a
/// single item.
impl Sender<Vec<Property>> for VolumePolicyControllerGetPropertiesResponder {
    fn send_response(self, data: Vec<Property>) {
        self.send(&mut data.into_iter())
            .log_fidl_response_error(VolumePolicyControllerMarker::DEBUG_NAME);
    }

    fn on_error(self) {
        self.control_handle().shutdown_with_epitaph(Status::INTERNAL);
    }
}

impl From<Response> for Vec<Property> {
    fn from(response: Response) -> Self {
        if let Response::State(state) = response {
            // Internally we store the data in a HashMap, need to flatten it out into a vector.
            return state.properties.values().cloned().map(Property::from).collect::<Vec<_>>();
        }

        panic!("incorrect value sent to volume policy");
    }
}

impl From<Response> for VolumePolicyControllerAddPolicyResult {
    fn from(response: Response) -> Self {
        if let Response::Policy(id) = response {
            return Ok(id.0);
        }

        panic!("incorrect value sent to volume policy");
    }
}

impl From<Response> for VolumePolicyControllerRemovePolicyResult {
    fn from(response: Response) -> Self {
        if let Response::Policy(_) = response {
            // We don't return any sort of ID on removal, can just ignore the ID in the policy
            // response.
            return Ok(());
        }

        panic!("incorrect value sent to volume policy");
    }
}

fidl_process_policy!(VolumePolicyController, process_request);

async fn process_request(
    context: RequestContext<Payload, Address>,
    request: VolumePolicyControllerRequest,
) -> Result<Option<VolumePolicyControllerRequest>, anyhow::Error> {
    match request {
        VolumePolicyControllerRequest::GetProperties { responder } => {
            policy_request_respond!(
                context,
                responder,
                SettingType::Audio,
                policy::Request::Audio(audio::Request::Get),
                Audio
            );
        }
        VolumePolicyControllerRequest::AddPolicy { target, parameters, responder } => {
            let transform: Transform = match parameters.try_into() {
                Ok(transform) => transform,
                Err(error_message) => {
                    fx_log_err!("Invalid policy parameters: {:?}", error_message);
                    responder.on_error();
                    return Ok(None);
                }
            };
            let policy_request =
                policy::Request::Audio(audio::Request::AddPolicy(target.into(), transform));
            policy_request_respond!(context, responder, SettingType::Audio, policy_request, Audio);
        }
        VolumePolicyControllerRequest::RemovePolicy { policy_id, responder } => {
            let policy_request =
                policy::Request::Audio(audio::Request::RemovePolicy(PolicyId(policy_id)));
            policy_request_respond!(context, responder, SettingType::Audio, policy_request, Audio);
        }
    };
    return Ok(None);
}

#[cfg(test)]
mod tests {
    use crate::audio::policy::{Property, PropertyTarget, Response, State, TransformFlags};
    use crate::switchboard::base::AudioStreamType;
    use std::collections::HashMap;

    /// Verifies that converting a policy response containing an empty `State` into a vector of
    /// `Property` results in an empty vector.
    #[test]
    fn test_response_to_property_vector_empty() {
        let response = Response::State(State { properties: Default::default() });

        let property_list: Vec<fidl_fuchsia_settings_policy::Property> = response.into();

        assert_eq!(property_list, vec![])
    }

    /// Verifies that converting a policy response with several State objects into a vector of
    /// `Property` results in a vector with the correct representations of data.
    #[test]
    fn test_response_to_property_vector() {
        let property1 = Property::new(AudioStreamType::Background, TransformFlags::TRANSFORM_MAX);
        let property2 = Property::new(AudioStreamType::Media, TransformFlags::TRANSFORM_MIN);
        let mut property_map: HashMap<PropertyTarget, Property> = HashMap::new();
        property_map.insert(property1.target, property1.clone());
        property_map.insert(property2.target, property2.clone());
        let response = Response::State(State { properties: property_map });

        let mut property_list: Vec<fidl_fuchsia_settings_policy::Property> = response.into();
        // Sort so the result is guaranteed to be in a consistent order.
        property_list
            .sort_by_key(|p| p.available_transforms.as_ref().unwrap().first().unwrap().clone());

        assert_eq!(property_list, vec![property1.into(), property2.into()])
    }
}
