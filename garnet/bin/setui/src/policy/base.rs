// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy as audio;
use crate::handler::device_storage::DeviceStorageFactory;
use crate::internal::core::message;
use crate::policy::policy_handler::PolicyHandler;
use crate::switchboard::base::SettingType;
use anyhow::Error;
use async_trait::async_trait;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::borrow::Cow;
use std::sync::Arc;
use thiserror::Error;

/// `Request` defines the request space for all policies handled by
/// the Setting Service. Note that the actions that can be taken upon each
/// policy should be defined within each policy's Request enum.
#[derive(PartialEq, Debug, Clone)]
pub enum Request {
    Audio(audio::Request),
}

pub mod response {
    use super::*;

    pub type Response = Result<Payload, Error>;

    /// `Payload` defines the possible successful responses for a request. There
    /// should be a corresponding policy response payload type for each request type.
    #[derive(PartialEq, Debug, Clone)]
    pub enum Payload {
        Audio(audio::Response),
    }

    /// The possible errors that can be returned from a request. Note that
    /// unlike the request and response space, errors are not type specific.
    #[derive(Error, Debug, Clone, PartialEq)]
    #[allow(dead_code)]
    pub enum Error {
        #[error("Unexpected error")]
        Unexpected,

        #[error("Communication error")]
        CommunicationError,

        #[error("Invalid input argument for setting type: {0:?} argument:{1:?} value:{2:?}")]
        InvalidArgument(SettingType, Cow<'static, str>, Cow<'static, str>),

        #[error("Write failed. setting type: {0:?}")]
        WriteFailure(SettingType),
    }
}

pub type BoxedHandler = Box<dyn PolicyHandler + Send + Sync>;
pub type GenerateHandlerResult = Result<BoxedHandler, Error>;
pub type GenerateHandler<T> =
    Box<dyn Fn(Context<T>) -> BoxFuture<'static, GenerateHandlerResult> + Send + Sync>;

/// Context captures all details necessary for a policy handler to execute in a given
/// settings service environment.
pub struct Context<T: DeviceStorageFactory> {
    pub setting_type: SettingType,
    pub messenger: message::Messenger,
    pub storage_factory_handle: Arc<Mutex<T>>,
    pub id: u64,
}

/// A factory capable of creating a policy handler for a given setting on-demand. If no
/// viable handler can be created, None will be returned.
#[async_trait]
pub trait PolicyHandlerFactory {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        messenger_factory: message::Factory,
    ) -> Result<BoxedHandler, PolicyHandlerFactoryError>;
}

#[derive(thiserror::Error, Debug, Clone, PartialEq)]
pub enum PolicyHandlerFactoryError {
    #[error("Setting type {0:?} not registered in environment")]
    SettingNotFound(SettingType),

    #[error("Cannot find setting handler generator for {0:?}")]
    GeneratorNotFound(SettingType),

    #[error("Core messageHub messenger for setting handler could not be created")]
    HandlerMessengerError,

    #[error("Setting handler for {0:?} failed to startup")]
    HandlerStartupError(SettingType),
}
