// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::accessibility::types::AccessibilityInfo;
use crate::audio::types::SetAudioStream;
use crate::base::{SettingInfo, SettingType};
use crate::display::types::SetDisplayInfo;
use crate::do_not_disturb::types::DoNotDisturbInfo;
use crate::handler::setting_handler::ControllerError;
use crate::input::types::InputDevice;
use crate::input::{MediaButtons, VolumeGain};
use crate::intl::types::IntlInfo;
use crate::keyboard::types::KeyboardInfo;
use crate::light::types::LightState;
use crate::night_mode::types::NightModeInfo;
use crate::payload_convert;
use crate::service::message::{Delegate, Messenger, Receptor, Signature};
use crate::service_context::ServiceContext;
use crate::setup::types::SetConfigurationInterfacesParams;

use async_trait::async_trait;
use fuchsia_trace as ftrace;
use futures::future::BoxFuture;
use std::borrow::Cow;
use std::collections::HashSet;
use std::sync::Arc;
use thiserror;

pub type ControllerGenerateResult = Result<(), anyhow::Error>;

pub(crate) type GenerateHandler =
    Box<dyn Fn(Context) -> BoxFuture<'static, ControllerGenerateResult> + Send + Sync>;

pub type Response = Result<Option<SettingInfo>, Error>;

/// This macro takes an enum, which has variants associated with various numbers of data, and
/// generates the same enum and implements a for_inspect method.
/// The for_inspect method returns variants' names.
#[macro_export]
macro_rules! generate_inspect {
    (@underscore $_type:ty) => { _ };
    ($(#[$metas:meta])* pub enum $name:ident {
        $(
            $(#[$variant_meta:meta])*
            $variant:ident
            $( ($($data:ty),+ $(,)?) )?
        ),* $(,)?
    }
    ) => {
        $(#[$metas])*
        pub enum $name {
            $(
                $(#[$variant_meta])*
                $variant$(($($data,)+))?,
            )*
        }

        impl $name {
            pub(crate) fn for_inspect(&self) -> &'static str {
                match self {
                    $(
                        $name::$variant $(
                            ( $(generate_inspect!(@underscore $data)),+ )
                        )? => stringify!($variant),
                    )*
                }
            }
        }
    };
}

generate_inspect! {
    /// The possible requests that can be made on a setting. The sink will expect a
    /// subset of the values defined below based on the associated type.
    /// The types are arranged alphabetically.
    #[derive(PartialEq, Debug, Clone)]
    pub enum Request {
        /// Returns the current setting information.
        Get,

        /// Requests ongoing updates when the setting changes.
        Listen,

        // Camera watcher events.
        OnCameraSWState(bool),

        // Input events.
        OnButton(MediaButtons),
        OnVolume(VolumeGain),

        // Accessibility requests.
        SetAccessibilityInfo(AccessibilityInfo),

        // Audio requests.
        SetVolume(Vec<SetAudioStream>, ftrace::Id),

        // Display requests.
        SetDisplayInfo(SetDisplayInfo),

        // Do not disturb requests.
        SetDnD(DoNotDisturbInfo),

        // Factory Reset requests.
        SetLocalResetAllowed(bool),

        // Input requests.
        SetInputStates(Vec<InputDevice>),

        // Intl requests.
        SetIntlInfo(IntlInfo),

        // Keyboard requests.
        SetKeyboardInfo(KeyboardInfo),

        // Light requests.
        SetLightGroupValue(String, Vec<LightState>),

        // Night mode requests.
        SetNightModeInfo(NightModeInfo),

        // Restores settings to outside dependencies.
        Restore,

        // Instructs handler to rebroadcast its current value.
        Rebroadcast,

        // Privacy requests.
        SetUserDataSharingConsent(Option<bool>),

        // Setup info requests.
        SetConfigurationInterfaces(SetConfigurationInterfacesParams),
    }
}

/// The data that is sent to and from setting handlers through the service
/// MessageHub.
#[derive(Clone, PartialEq, Debug)]
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

#[derive(thiserror::Error, Debug, Clone, PartialEq)]
// If any new variants are added here, they should also be updated in the response types
// for inspect in inspect::utils::enums::ResponseType.
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

    #[error(
        "Incompatible argument values passed: {setting_type:?} argument:{main_arg:?} cannot be \
         combined with arguments:[{other_args:?}] with respective values:[{values:?}]. {reason:?}"
    )]
    IncompatibleArguments {
        setting_type: SettingType,
        main_arg: Cow<'static, str>,
        other_args: Cow<'static, str>,
        values: Cow<'static, str>,
        reason: Cow<'static, str>,
    },

    #[error("External failure for setting type:{0:?} dependency: {1:?} request:{2:?} error:{3}")]
    ExternalFailure(SettingType, Cow<'static, str>, Cow<'static, str>, Cow<'static, str>),

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

    // TODO(fxbug.dev/76425): Add source of error.
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
            ControllerError::ExternalFailure(setting_type, dependency, request, error) => {
                Error::ExternalFailure(setting_type, dependency, request, error)
            }
            ControllerError::InvalidArgument(setting_type, argument, value) => {
                Error::InvalidArgument(setting_type, argument, value)
            }
            ControllerError::IncompatibleArguments {
                setting_type,
                main_arg,
                other_args,
                values,
                reason,
            } => {
                Error::IncompatibleArguments { setting_type, main_arg, other_args, values, reason }
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

    #[error("Setting handler for {0:?} failed to startup. cause: {1:?}")]
    HandlerStartupError(SettingType, Cow<'static, str>),
}

/// A factory capable of creating a handler for a given setting on-demand. If no
/// viable handler can be created, None will be returned.
#[async_trait]
pub(crate) trait SettingHandlerFactory {
    async fn generate(
        &mut self,
        setting_type: SettingType,
        delegate: Delegate,
        notifier_signature: Signature,
    ) -> Result<Signature, SettingHandlerFactoryError>;
}

pub struct Environment {
    pub settings: HashSet<SettingType>,
    pub service_context: Arc<ServiceContext>,
}

impl Clone for Environment {
    fn clone(&self) -> Environment {
        Environment::new(self.settings.clone(), self.service_context.clone())
    }
}

impl Environment {
    pub(crate) fn new(
        settings: HashSet<SettingType>,
        service_context: Arc<ServiceContext>,
    ) -> Environment {
        Environment { settings, service_context }
    }
}

/// Context captures all details necessary for a handler to execute in a given
/// settings service environment.
pub struct Context {
    pub setting_type: SettingType,
    pub messenger: Messenger,
    pub receptor: Receptor,
    pub notifier_signature: Signature,
    pub environment: Environment,
    pub id: u64,
}

impl Context {
    pub(crate) fn new(
        setting_type: SettingType,
        messenger: Messenger,
        receptor: Receptor,
        notifier_signature: Signature,
        environment: Environment,
        id: u64,
    ) -> Context {
        Context { setting_type, messenger, receptor, notifier_signature, environment, id }
    }
}

/// ContextBuilder is a convenience builder to facilitate creating a Context
/// (and associated environment).
#[cfg(test)]
pub(crate) struct ContextBuilder {
    setting_type: SettingType,
    settings: HashSet<SettingType>,
    service_context: Option<Arc<ServiceContext>>,
    messenger: Messenger,
    receptor: Receptor,
    notifier_signature: Signature,
    id: u64,
}

#[cfg(test)]
impl ContextBuilder {
    pub(crate) fn new(
        setting_type: SettingType,
        messenger: Messenger,
        receptor: Receptor,
        notifier_signature: Signature,
        id: u64,
    ) -> Self {
        Self {
            setting_type,
            settings: HashSet::new(),
            service_context: None,
            messenger,
            receptor,
            notifier_signature,
            id,
        }
    }

    /// Generates the Context.
    pub(crate) fn build(self) -> Context {
        let service_context =
            self.service_context.unwrap_or_else(|| Arc::new(ServiceContext::new(None, None)));
        let environment = Environment::new(self.settings, service_context);

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
