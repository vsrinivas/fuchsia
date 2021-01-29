// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::accessibility::types::AccessibilityInfo;
use crate::audio::types::AudioStream;
use crate::base::{SettingInfo, SettingType};
use crate::display::types::{LowLightMode, Theme};
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::handler::device_storage::DeviceStorageFactory;
use crate::handler::setting_handler::ControllerError;
use crate::input::types::InputDevice;
use crate::input::{ButtonType, VolumeGain};
use crate::internal::handler::message;
use crate::intl::types::IntlInfo;
use crate::light::types::LightState;
use crate::night_mode::types::NightModeInfo;
use crate::payload_convert;
use crate::service_context::ServiceContextHandle;
use crate::setup::types::ConfigurationInterfaceFlags;
use async_trait::async_trait;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::borrow::Cow;
use std::collections::HashSet;
use std::sync::Arc;
use thiserror;

#[cfg(test)]
use {
    crate::internal::event::message::Factory as EventMessengerFactory,
    crate::service_context::ServiceContext,
};

pub type SettingHandlerResult = Result<Option<SettingInfo>, ControllerError>;
pub type ControllerGenerateResult = Result<(), anyhow::Error>;
pub type ExitResult = Result<(), ControllerError>;

pub type GenerateHandler<T> =
    Box<dyn Fn(Context<T>) -> BoxFuture<'static, ControllerGenerateResult> + Send + Sync>;

pub type Response = Result<Option<SettingInfo>, Error>;
/// The possible requests that can be made on a setting. The sink will expect a
/// subset of the values defined below based on the associated type.
/// The types are arranged alphabetically.
#[derive(PartialEq, Debug, Clone)]
pub enum Request {
    /// Returns the current setting information.
    Get,

    /// Requests ongoing updates when the setting changes.
    Listen,

    // Input events.
    OnButton(ButtonType),
    OnVolume(VolumeGain),

    // Accessibility requests.
    SetAccessibilityInfo(AccessibilityInfo),

    // Account requests
    ScheduleClearAccounts,

    // Audio requests.
    SetVolume(Vec<AudioStream>),

    // Audio in requests.
    SetMicMute(bool),

    // Display requests.
    SetAutoBrightness(bool),
    SetBrightness(f32),
    SetLowLightMode(LowLightMode),
    SetScreenEnabled(bool),
    SetTheme(Theme),

    // Do not disturb requests.
    SetDnD(DoNotDisturbInfo),

    // Factory Reset requests.
    SetLocalResetAllowed(bool),

    // Input requests.
    SetInputStates(Vec<InputDevice>),

    // Intl requests.
    SetIntlInfo(IntlInfo),

    // Light requests.
    SetLightGroupValue(String, Vec<LightState>),

    // Night mode requests.
    SetNightModeInfo(NightModeInfo),

    // Power requests.
    Reboot,

    // Restores settings to outside dependencies.
    Restore,

    // Privacy requests.
    SetUserDataSharingConsent(Option<bool>),

    // Setup info requests.
    SetConfigurationInterfaces(ConfigurationInterfaceFlags),
}

/// The data that is sent to and from setting handlers through the service
/// MessageHub.
#[derive(Clone, Debug)]
pub enum Payload {
    /// The `Request` payload communicates actions to be taken upon the setting.
    /// These actions can be around access (get/listen) and changes (set). Note
    /// that there is not necessarily a 1:1 relationship between `Request` and
    /// [`Response`] defined later. It is possible a single `Request` will
    /// result into multiple [`Response`] that are delivered on the same
    /// MessageHub receptor.
    Request(Request),
    /// The `Response` payload represents the result of a `Request` action. Note
    /// that Response is a Result; receipients should confirm whether an error
    /// was returned and if a successful result (wich is an Option) has a value.
    Response(Response),
}

// Conversions for Handler Payload.
payload_convert!(Setting, Payload);

impl Request {
    /// Returns the name of the enum, for writing to inspect.
    /// TODO(fxbug.dev/56718): write a macro to simplify this
    pub fn for_inspect(self) -> &'static str {
        match self {
            Request::Get => "Get",
            Request::Listen => "Listen",
            Request::OnButton(_) => "OnButton",
            Request::OnVolume(_) => "OnVolume",
            Request::SetAccessibilityInfo(_) => "SetAccessibilityInfo",
            Request::ScheduleClearAccounts => "ScheduleClearAccounts",
            Request::SetVolume(_) => "SetVolume",
            Request::SetMicMute(_) => "SetMicMute",
            Request::SetBrightness(_) => "SetBrightness",
            Request::SetAutoBrightness(_) => "SetAutoBrightness",
            Request::SetLocalResetAllowed(_) => "SetLocalResetAllowed",
            Request::SetLowLightMode(_) => "SetLowLightMode",
            Request::SetDnD(_) => "SetDnD",
            Request::SetInputStates(_) => "SetInputStates",
            Request::SetIntlInfo(_) => "SetIntlInfo",
            Request::SetLightGroupValue(_, _) => "SetLightGroupValue",
            Request::SetNightModeInfo(_) => "SetNightModeInfo",
            Request::Reboot => "Reboot",
            Request::Restore => "Restore",
            Request::SetUserDataSharingConsent(_) => "SetUserDataSharingConsent",
            Request::SetConfigurationInterfaces(_) => "SetConfigurationInterfaces",
            Request::SetScreenEnabled(_) => "SetScreenEnabled",
            Request::SetTheme(_) => "SetTheme",
        }
    }
}

#[derive(thiserror::Error, Debug, Clone, PartialEq)]
pub enum Error {
    #[error("Unimplemented Request:{0:?} for setting type: {1:?}")]
    UnimplementedRequest(SettingType, Request),

    #[error("Storage failure for setting type: {0:?}")]
    StorageFailure(SettingType),

    #[error("Initialization failure: cause {0:?}")]
    InitFailure(Cow<'static, str>),

    #[error("Restoration of setting on controller startup failed: cause {0:?}")]
    RestoreFailure(Cow<'static, str>),

    #[error("Invalid argument for setting type: {0:?} argument:{1:?} value:{2:?}")]
    InvalidArgument(SettingType, Cow<'static, str>, Cow<'static, str>),

    #[error("External failure for setting type:{0:?} dependency: {1:?} request:{2:?}")]
    ExternalFailure(SettingType, Cow<'static, str>, Cow<'static, str>),

    #[error("Unhandled type: {0:?}")]
    UnhandledType(SettingType),

    #[error("Delivery error for type: {0:?} received by: {1:?}")]
    DeliveryError(SettingType, SettingType),

    #[error("Unexpected error: {0}")]
    UnexpectedError(Cow<'static, str>),

    #[error("Undeliverable Request:{1:?} for setting type: {0:?}")]
    UndeliverableError(SettingType, Request),

    #[error("Unsupported request for setting type: {0:?}")]
    UnsupportedError(SettingType),

    #[error("Communication error")]
    CommunicationError,

    #[error("Irrecoverable error")]
    IrrecoverableError,

    #[error("Timeout error")]
    TimeoutError,
}

impl From<ControllerError> for Error {
    fn from(error: ControllerError) -> Self {
        match error {
            ControllerError::UnimplementedRequest(setting_type, request) => {
                Error::UnimplementedRequest(setting_type, request)
            }
            ControllerError::WriteFailure(setting_type) => Error::StorageFailure(setting_type),
            ControllerError::InitFailure(description) => Error::InitFailure(description),
            ControllerError::RestoreFailure(description) => Error::RestoreFailure(description),
            ControllerError::ExternalFailure(setting_type, dependency, request) => {
                Error::ExternalFailure(setting_type, dependency, request)
            }
            ControllerError::InvalidArgument(setting_type, argument, value) => {
                Error::InvalidArgument(setting_type, argument, value)
            }
            ControllerError::UnhandledType(setting_type) => Error::UnhandledType(setting_type),
            ControllerError::UnexpectedError(error) => Error::UnexpectedError(error),
            ControllerError::UndeliverableError(setting_type, request) => {
                Error::UndeliverableError(setting_type, request)
            }
            ControllerError::UnsupportedError(setting_type) => {
                Error::UnsupportedError(setting_type)
            }
            ControllerError::DeliveryError(setting_type, setting_type_2) => {
                Error::DeliveryError(setting_type, setting_type_2)
            }
            ControllerError::IrrecoverableError => Error::IrrecoverableError,
            ControllerError::TimeoutError => Error::TimeoutError,
            ControllerError::ExitError => Error::IrrecoverableError,
        }
    }
}

/// An command represents messaging from the proxy to take a
/// particular action.
#[derive(Debug, Clone, PartialEq)]
pub enum Command {
    HandleRequest(Request),
    ChangeState(State),
}

/// Events are sent from the setting handler back to the parent
/// proxy to indicate changes that happen out-of-band (happening
/// outside of response to a Command above). They indicate a
/// change in the handler that should potentially be handled by
/// the proxy.
#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    // Sent when the publicly perceived values of the setting
    // handler have been changed.
    Changed(SettingInfo),
    Exited(ExitResult),
}

#[derive(Debug, Clone, Copy, Eq, Hash, PartialEq)]
pub enum State {
    /// State of a controller immediately after it is created. Intended
    /// to initialize state on the controller.
    Startup,

    /// State of a controller when at least one client is listening on
    /// changes to the setting state.
    Listen,

    /// State of a controller when there are no more clients listening
    /// on changes to the setting state.
    EndListen,

    /// State of a controller when there are no requests or listeners on
    /// the setting type. Intended to tear down state before taking down
    /// the controller.
    Teardown,
}

#[derive(thiserror::Error, Debug, Clone, PartialEq)]
pub enum SettingHandlerFactoryError {
    #[error("Setting type {0:?} not registered in environment")]
    SettingNotFound(SettingType),

    #[error("Cannot find setting handler generator for {0:?}")]
    GeneratorNotFound(SettingType),

    #[error("MessageHub Messenger for setting handler could not be created")]
    HandlerMessengerError,

    #[error("MessageHub Messenger for controller messenger could not be created")]
    ControllerMessengerError,

    #[error("MessageHub Messenger for lifecycle messenger could not be created")]
    LifecycleMessengerError,

    #[error("Setting handler for {0:?} failed to startup")]
    HandlerStartupError(SettingType),
}

/// A factory capable of creating a handler for a given setting on-demand. If no
/// viable handler can be created, None will be returned.
#[async_trait]
pub trait SettingHandlerFactory {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        messenger_factory: message::Factory,
        notifier_signature: message::Signature,
    ) -> Result<message::Signature, SettingHandlerFactoryError>;
}

pub struct Environment<T: DeviceStorageFactory> {
    pub settings: HashSet<SettingType>,
    pub service_context_handle: ServiceContextHandle,
    pub storage_factory_handle: Arc<Mutex<T>>,
}

impl<T: DeviceStorageFactory> Clone for Environment<T> {
    fn clone(&self) -> Environment<T> {
        Environment::new(
            self.settings.clone(),
            self.service_context_handle.clone(),
            self.storage_factory_handle.clone(),
        )
    }
}

impl<T: DeviceStorageFactory> Environment<T> {
    pub fn new(
        settings: HashSet<SettingType>,
        service_context_handle: ServiceContextHandle,
        storage_factory_handle: Arc<Mutex<T>>,
    ) -> Environment<T> {
        return Environment { settings, service_context_handle, storage_factory_handle };
    }
}

/// Context captures all details necessary for a handler to execute in a given
/// settings service environment.
pub struct Context<T: DeviceStorageFactory> {
    pub setting_type: SettingType,
    pub messenger: message::Messenger,
    pub receptor: message::Receptor,
    pub notifier_signature: message::Signature,
    pub environment: Environment<T>,
    pub id: u64,
}

impl<T: DeviceStorageFactory> Context<T> {
    pub fn new(
        setting_type: SettingType,
        messenger: message::Messenger,
        receptor: message::Receptor,
        notifier_signature: message::Signature,
        environment: Environment<T>,
        id: u64,
    ) -> Context<T> {
        return Context {
            setting_type,
            messenger,
            receptor,
            notifier_signature,
            environment: environment.clone(),
            id,
        };
    }
}

/// ContextBuilder is a convenience builder to facilitate creating a Context
/// (and associated environment).
#[cfg(test)]
pub struct ContextBuilder<T: DeviceStorageFactory> {
    setting_type: SettingType,
    storage_factory: Arc<Mutex<T>>,
    settings: HashSet<SettingType>,
    service_context: Option<ServiceContextHandle>,
    event_messenger_factory: Option<EventMessengerFactory>,
    messenger: message::Messenger,
    receptor: message::Receptor,
    notifier_signature: message::Signature,
    id: u64,
}

#[cfg(test)]
impl<T: DeviceStorageFactory> ContextBuilder<T> {
    pub fn new(
        setting_type: SettingType,
        storage_factory: Arc<Mutex<T>>,
        messenger: message::Messenger,
        receptor: message::Receptor,
        notifier_signature: message::Signature,
        id: u64,
    ) -> Self {
        Self {
            setting_type,
            storage_factory,
            settings: HashSet::new(),
            service_context: None,
            event_messenger_factory: None,
            messenger,
            receptor,
            notifier_signature,
            id,
        }
    }

    // Sets the service context to be used.
    pub fn service_context(mut self, service_context_handle: ServiceContextHandle) -> Self {
        self.service_context = Some(service_context_handle);

        self
    }

    pub fn event_messenger_factory(
        mut self,
        event_messenger_factory: EventMessengerFactory,
    ) -> Self {
        self.event_messenger_factory = Some(event_messenger_factory);

        self
    }

    /// Adds the settings to given environment.
    pub fn add_settings(mut self, settings: &[SettingType]) -> Self {
        for setting in settings {
            self.settings.insert(setting.clone());
        }

        self
    }

    /// Generates the Context.
    pub fn build(self) -> Context<T> {
        let service_context = if self.service_context.is_none() {
            ServiceContext::create(None, self.event_messenger_factory)
        } else {
            self.service_context.unwrap()
        };
        let environment = Environment::new(self.settings, service_context, self.storage_factory);

        // Note: ContextBuilder should use the same context id system as the SettingHandlerFactoryImpl.
        // If it is used in conjunction with Context::new, then a new way of tracking unique Contexts
        // may need to be devised. If it replaces all usages of Context::new, the id creation can
        // be moved to the ContextBuilder struct.
        Context::new(
            self.setting_type,
            self.messenger,
            self.receptor,
            self.notifier_signature,
            environment,
            self.id,
        )
    }
}
