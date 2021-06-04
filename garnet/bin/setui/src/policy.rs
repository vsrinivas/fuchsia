// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy::{self as audio, State};
use crate::base::SettingType;
use crate::generate_inspect_with_info;
use crate::handler::device_storage::DeviceStorageFactory;
use crate::payload_convert;
use crate::policy::policy_handler::PolicyHandler;
use crate::policy::response::Response;
use crate::service;
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;
use serde::{Deserialize, Serialize};
use std::borrow::Cow;
use std::convert::TryFrom;
use std::sync::Arc;
use thiserror::Error;

/// Defines the policy handler trait, which describes a component that persists and applies the
/// policies specified by policy clients
pub mod policy_handler;

/// Defines a proxy between the policy FIDL handler and policy handler that is responsible for
/// intercepting incoming setting requests and returning responses to the policy FIDL handler.
pub mod policy_proxy;

/// Defines a factory for creating policy handlers on demand.
pub mod policy_handler_factory_impl;

/// The policy types supported by the service.
#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy, Serialize, Deserialize)]
pub enum PolicyType {
    /// This type is reserved for testing purposes.
    #[cfg(test)]
    Unknown,
    Audio,
}

pub(crate) trait HasPolicyType {
    const POLICY_TYPE: PolicyType;
}

impl PolicyType {
    /// Returns the corresponding setting type that this policy controls.
    pub(crate) fn setting_type(&self) -> SettingType {
        match self {
            #[cfg(test)]
            PolicyType::Unknown => SettingType::Unknown,
            PolicyType::Audio => SettingType::Audio,
        }
    }

    /// Initialize the storage needed for this particular policy handler.
    pub async fn initialize_storage<T>(&self, storage_factory: &Arc<T>) -> Result<(), Error>
    where
        T: DeviceStorageFactory,
    {
        match self {
            #[cfg(test)]
            Self::Unknown => Ok(()),
            Self::Audio => {
                storage_factory
                    .initialize::<audio::audio_policy_handler::AudioPolicyHandler>()
                    .await
            }
        }
    }
}

generate_inspect_with_info! {
    /// Enumeration over the possible policy state information for all policies.
    #[derive(PartialEq, Debug, Clone)]
    pub enum PolicyInfo {
        /// This value is reserved for testing purposes.
        #[cfg(test)]
        Unknown(UnknownInfo),
        Audio(State),
    }
}

macro_rules! conversion_impls {
    ($($(#[cfg($test:meta)])? $variant:ident($info_ty:ty) => $ty_variant:ident ),+ $(,)?) => {
        $(
            $(#[cfg($test)])?
            impl HasPolicyType for $info_ty {
                const POLICY_TYPE: PolicyType = PolicyType::$ty_variant;
            }

            $(#[cfg($test)])?
            impl TryFrom<PolicyInfo> for $info_ty {
                type Error = ();

                fn try_from(setting_info: PolicyInfo) -> Result<Self, ()> {
                    // Remove allow once additional non-test variant is added.
                    #[allow(unreachable_patterns)]
                    match setting_info {
                        PolicyInfo::$variant(info) => Ok(info),
                        _ => Err(()),
                    }
                }
            }
        )+
    }
}

conversion_impls! {
    #[cfg(test)] Unknown(UnknownInfo) => Unknown,
    Audio(State) => Audio,
}

#[cfg(test)]
mod testing {
    use super::{PolicyInfo, UnknownInfo};
    use crate::handler::device_storage::DeviceStorageCompatible;

    impl DeviceStorageCompatible for UnknownInfo {
        const KEY: &'static str = "unknown_info";

        fn default_value() -> Self {
            Self(false)
        }
    }

    impl From<UnknownInfo> for PolicyInfo {
        fn from(unknown_info: UnknownInfo) -> Self {
            PolicyInfo::Unknown(unknown_info)
        }
    }
}

impl From<State> for PolicyInfo {
    fn from(state: State) -> Self {
        PolicyInfo::Audio(state)
    }
}

impl From<&PolicyInfo> for PolicyType {
    fn from(policy_info: &PolicyInfo) -> Self {
        match policy_info {
            #[cfg(test)]
            PolicyInfo::Unknown(_) => PolicyType::Unknown,
            PolicyInfo::Audio(_) => PolicyType::Audio,
        }
    }
}

/// This struct is reserved for testing purposes.
#[derive(PartialEq, Debug, Copy, Clone, Serialize, Deserialize)]
#[cfg(test)]
pub struct UnknownInfo(pub bool);

#[derive(PartialEq, Clone, Debug)]
pub enum Payload {
    Request(Request),
    Response(Response),
}

payload_convert!(Policy, Payload);

/// `Role` defines grouping for responsibilities on the policy message hub.
#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub enum Role {
    /// This role indicates that the messenger handles and enacts policy requests.
    PolicyHandler,
}

/// `Request` defines the request space for all policies handled by
/// the Setting Service. Note that the actions that can be taken upon each
/// policy should be defined within each policy's Request enum.
#[derive(PartialEq, Debug, Clone)]
pub enum Request {
    /// Fetches the current policy state.
    Get,

    /// Restore saved state from disk.
    Restore,

    /// Request targeted to the Audio policy.
    Audio(audio::Request),
}

pub mod response {
    use super::*;

    pub type Response = Result<Payload, Error>;

    /// `Payload` defines the possible successful responses for a request. There
    /// should be a corresponding policy response payload type for each request type.
    #[derive(PartialEq, Debug, Clone)]
    pub enum Payload {
        PolicyInfo(PolicyInfo),
        Restore,
        Audio(audio::Response),
    }

    /// The possible errors that can be returned from a request. Note that
    /// unlike the request and response space, errors are not type specific.
    #[derive(Error, Debug, Clone, PartialEq)]
    pub enum Error {
        #[error("Unexpected error")]
        Unexpected,

        #[error("Communication error")]
        CommunicationError,

        #[error("Invalid input argument for policy: {0:?} argument:{1:?} value:{2:?}")]
        InvalidArgument(PolicyType, Cow<'static, str>, Cow<'static, str>),

        #[error("Write failed for policy: {0:?}")]
        WriteFailure(PolicyType),
    }
}

pub type BoxedHandler = Box<dyn PolicyHandler + Send + Sync>;
pub type GenerateHandlerResult = Result<BoxedHandler, Error>;
pub type GenerateHandler<T> =
    Box<dyn Fn(Context<T>) -> BoxFuture<'static, GenerateHandlerResult> + Send + Sync>;

/// Context captures all details necessary for a policy handler to execute in a given
/// settings service environment.
pub struct Context<T: DeviceStorageFactory> {
    pub policy_type: PolicyType,
    pub service_messenger: service::message::Messenger,
    pub storage_factory: Arc<T>,
    pub id: u64,
}

/// A factory capable of creating a policy handler for a given setting on-demand. If no
/// viable handler can be created, None will be returned.
#[async_trait]
pub trait PolicyHandlerFactory {
    async fn generate(
        &mut self,
        policy_type: PolicyType,
        service_messenger: service::message::Messenger,
    ) -> Result<BoxedHandler, PolicyHandlerFactoryError>;
}

#[derive(thiserror::Error, Debug, Clone, PartialEq)]
pub enum PolicyHandlerFactoryError {
    #[error("Policy type {0:?} not registered in environment")]
    PolicyNotFound(PolicyType),

    #[error("Setting type {0:?} for policy {0:?} not registered in environment")]
    SettingNotFound(SettingType, PolicyType),

    #[error("Cannot find policy handler generator for {0:?}")]
    GeneratorNotFound(PolicyType),

    #[error("Policy handler {0:?} failed to startup")]
    HandlerStartupError(PolicyType),
}
