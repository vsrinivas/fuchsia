// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy::{self as audio, PolicyId, Response, Transform};
use crate::ingress::{policy_request, Scoped};
use crate::job::source::{Error as JobError, PolicyErrorResponder};
use crate::job::Job;
use crate::policy::{response, PolicyInfo, PolicyType, Request};
use fidl_fuchsia_settings_policy::{
    Property, VolumePolicyControllerAddPolicyResponder, VolumePolicyControllerAddPolicyResult,
    VolumePolicyControllerGetPropertiesResponder, VolumePolicyControllerRemovePolicyResponder,
    VolumePolicyControllerRemovePolicyResult, VolumePolicyControllerRequest,
};
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use std::convert::TryFrom;
use std::convert::TryInto;

impl policy_request::Responder<Scoped<Vec<Property>>>
    for VolumePolicyControllerGetPropertiesResponder
{
    fn respond(self, Scoped(response): Scoped<Vec<Property>>) {
        let _ = self.send(&mut response.into_iter());
    }
}

impl policy_request::Responder<Scoped<VolumePolicyControllerAddPolicyResult>>
    for VolumePolicyControllerAddPolicyResponder
{
    fn respond(self, Scoped(mut response): Scoped<VolumePolicyControllerAddPolicyResult>) {
        let _ = self.send(&mut response);
    }
}

impl PolicyErrorResponder for VolumePolicyControllerAddPolicyResponder {
    fn id(&self) -> &'static str {
        "AddPolicy"
    }

    fn respond(
        self: Box<Self>,
        error: fidl_fuchsia_settings_policy::Error,
    ) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
    }
}

impl policy_request::Responder<Scoped<VolumePolicyControllerRemovePolicyResult>>
    for VolumePolicyControllerRemovePolicyResponder
{
    fn respond(self, Scoped(mut response): Scoped<VolumePolicyControllerRemovePolicyResult>) {
        let _ = self.send(&mut response);
    }
}

impl TryFrom<VolumePolicyControllerRequest> for Job {
    type Error = JobError;

    fn try_from(item: VolumePolicyControllerRequest) -> Result<Self, Self::Error> {
        #[allow(unreachable_patterns)]
        match item {
            VolumePolicyControllerRequest::GetProperties { responder } => {
                Ok(policy_request::Work::new(PolicyType::Audio, Request::Get, responder).into())
            }
            VolumePolicyControllerRequest::AddPolicy { target, parameters, responder } => {
                let transform: Transform = match parameters.try_into() {
                    Ok(transform) => transform,
                    Err(error_message) => {
                        fx_log_err!("Invalid policy parameters: {:?}", error_message);

                        return Err(JobError::InvalidPolicyInput(Box::new(responder)));
                    }
                };
                let policy_request =
                    Request::Audio(audio::Request::AddPolicy(target.into(), transform));
                Ok(policy_request::Work::new(PolicyType::Audio, policy_request, responder).into())
            }
            VolumePolicyControllerRequest::RemovePolicy { policy_id, responder } => {
                let policy_request =
                    Request::Audio(audio::Request::RemovePolicy(PolicyId(policy_id)));
                Ok(policy_request::Work::new(PolicyType::Audio, policy_request, responder).into())
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", item);
                Err(JobError::Unsupported)
            }
        }
    }
}

impl From<response::Response> for Scoped<Vec<Property>> {
    fn from(response: response::Response) -> Self {
        if let Ok(response::Payload::PolicyInfo(PolicyInfo::Audio(state))) = response {
            // Internally we store the data in a HashMap, need to flatten it out into a vector.
            return Scoped(
                state.properties.values().cloned().map(Property::from).collect::<Vec<_>>(),
            );
        }

        panic!("incorrect value sent to volume policy");
    }
}

impl From<response::Response> for Scoped<VolumePolicyControllerAddPolicyResult> {
    fn from(response: response::Response) -> Self {
        if let Ok(response::Payload::Audio(Response::Policy(id))) = response {
            return Scoped(Ok(id.0));
        }

        panic!("incorrect value sent to volume policy");
    }
}

impl From<response::Response> for Scoped<VolumePolicyControllerRemovePolicyResult> {
    fn from(response: response::Response) -> Self {
        if let Ok(response::Payload::Audio(Response::Policy(_))) = response {
            // We don't return any sort of ID on removal, can just ignore the ID in the policy
            // response.
            return Scoped(Ok(()));
        }

        panic!("incorrect value sent to volume policy");
    }
}

#[cfg(test)]
mod tests {
    use crate::audio::policy::{Property, PropertyTarget, State, TransformFlags};
    use crate::audio::types::AudioStreamType;
    use crate::ingress::Scoped;
    use crate::policy::{response, PolicyInfo};
    use std::collections::HashMap;

    // Verifies that converting a policy response containing an empty `State` into a vector of
    // `Property` results in an empty vector.
    #[test]
    fn test_response_to_property_vector_empty() {
        let response = Ok(response::Payload::PolicyInfo(PolicyInfo::Audio(State {
            properties: Default::default(),
        })));

        let Scoped(property_list): Scoped<Vec<fidl_fuchsia_settings_policy::Property>> =
            response.into();

        assert_eq!(property_list, vec![])
    }

    // Verifies that converting a policy response with several State objects into a vector of
    // `Property` results in a vector with the correct representations of data.
    #[test]
    fn test_response_to_property_vector() {
        let property1 = Property::new(AudioStreamType::Background, TransformFlags::TRANSFORM_MAX);
        let property2 = Property::new(AudioStreamType::Media, TransformFlags::TRANSFORM_MIN);
        let mut property_map: HashMap<PropertyTarget, Property> = HashMap::new();
        let _ = property_map.insert(property1.target, property1.clone());
        let _ = property_map.insert(property2.target, property2.clone());
        let response = Ok(response::Payload::PolicyInfo(PolicyInfo::Audio(State {
            properties: property_map,
        })));

        let Scoped(mut property_list): Scoped<Vec<fidl_fuchsia_settings_policy::Property>> =
            response.into();

        // Sort so the result is guaranteed to be in a consistent order.
        property_list.sort_by_key(|p| {
            *p.available_transforms
                .as_ref()
                .expect("test should have transforms")
                .first()
                .expect("test should have at least one transform")
        });

        assert_eq!(property_list, vec![property1.into(), property2.into()])
    }
}
