// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::base::{HasSettingType, SettingInfo, SettingType};
use crate::handler::base::{Context, ControllerGenerateResult, Request};
use crate::message::base::Audience;
use crate::payload_convert;
use crate::service::message::{MessageClient, Messenger, Signature};
use crate::service_context::ServiceContext;
use crate::storage::StorageInfo;
use crate::{trace, trace_guard};
use async_trait::async_trait;
use core::convert::TryFrom;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use settings_storage::storage_factory::StorageFactory as StorageFactoryTrait;
use std::borrow::Cow;
use std::convert::TryInto;
use std::marker::PhantomData;
use std::sync::Arc;
use thiserror::Error;

pub type ExitResult = Result<(), ControllerError>;
pub type SettingHandlerResult = Result<Option<SettingInfo>, ControllerError>;
/// Return type from a controller after handling a state change.
pub type ControllerStateResult = Result<(), ControllerError>;

// The types of data that can be sent to and from a setting controller.
#[derive(Clone, Debug, PartialEq)]
pub enum Payload {
    // Sent to the controller to request an action is taken.
    Command(Command),
    // Sent from the controller adhoc to indicate an event has happened.
    Event(Event),
    // Sent in response to a request.
    Result(SettingHandlerResult),
}

payload_convert!(Controller, Payload);

/// An command sent to the controller to take a particular action.
#[derive(Debug, Clone, PartialEq)]
pub enum Command {
    HandleRequest(Request),
    ChangeState(State),
}

impl TryFrom<crate::handler::setting_handler::Payload> for Command {
    type Error = &'static str;

    fn try_from(value: crate::handler::setting_handler::Payload) -> Result<Self, Self::Error> {
        match value {
            crate::handler::setting_handler::Payload::Command(command) => Ok(command),
            _ => Err("wrong payload type"),
        }
    }
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

pub(crate) trait StorageFactory: StorageFactoryTrait + Send + Sync {}
impl<T: StorageFactoryTrait + Send + Sync> StorageFactory for T {}

#[derive(Error, Debug, Clone, PartialEq)]
pub enum ControllerError {
    #[error("Unimplemented Request:{1:?} for setting type: {0:?}")]
    UnimplementedRequest(SettingType, Request),
    #[error("Write failed. setting type: {0:?}")]
    WriteFailure(SettingType),
    #[error("Initialization failure: cause {0:?}")]
    InitFailure(Cow<'static, str>),
    #[error("Restoration of setting on controller startup failed: cause {0:?}")]
    RestoreFailure(Cow<'static, str>),
    #[error(
        "Call to an external dependency {1:?} for setting type {0:?} failed. \
         Request:{2:?}: Error:{3}"
    )]
    ExternalFailure(SettingType, Cow<'static, str>, Cow<'static, str>, Cow<'static, str>),
    #[error("Invalid input argument for setting type: {0:?} argument:{1:?} value:{2:?}")]
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
    #[error("Unhandled type: {0:?}")]
    UnhandledType(SettingType),
    #[error("Unexpected error: {0:?}")]
    UnexpectedError(Cow<'static, str>),
    #[error("Undeliverable Request:{1:?} for setting type: {0:?}")]
    UndeliverableError(SettingType, Request),
    #[error("Unsupported request for setting type: {0:?}")]
    UnsupportedError(SettingType),
    #[error("Delivery error for type: {0:?} received by: {1:?}")]
    DeliveryError(SettingType, SettingType),
    #[error("Irrecoverable error")]
    IrrecoverableError,
    #[error("Timeout occurred")]
    TimeoutError,
    #[error("Exit occurred")]
    ExitError,
}

pub(crate) type BoxedController = Box<dyn controller::Handle + Send + Sync>;
pub(crate) type BoxedControllerResult = Result<BoxedController, ControllerError>;

pub(crate) type GenerateController =
    Box<dyn Fn(Arc<ClientImpl>) -> BoxFuture<'static, BoxedControllerResult> + Send + Sync>;

pub(crate) mod controller {
    use super::*;

    #[async_trait]
    pub(crate) trait Create: Sized {
        async fn create(client: Arc<ClientImpl>) -> Result<Self, ControllerError>;
    }

    #[async_trait]
    pub(crate) trait Handle: Send {
        /// Handles an incoming request and returns its result. If the request is not supported
        /// by this implementation, then None is returned.
        async fn handle(&self, request: Request) -> Option<SettingHandlerResult>;

        /// Handles a state change, and returns the new state if it was updated, else returns None.
        async fn change_state(&mut self, _state: State) -> Option<ControllerStateResult> {
            None
        }
    }
}

pub struct ClientImpl {
    notify: Mutex<bool>,
    messenger: Messenger,
    notifier_signature: Signature,
    service_context: Arc<ServiceContext>,
    setting_type: SettingType,
}

impl ClientImpl {
    fn new(context: &Context) -> Self {
        Self {
            messenger: context.messenger.clone(),
            setting_type: context.setting_type,
            notifier_signature: context.notifier_signature,
            notify: Mutex::new(false),
            service_context: Arc::clone(&context.environment.service_context),
        }
    }

    /// Test constructor that doesn't require creating a whole [Context].
    #[cfg(test)]
    pub fn for_test(
        notify: Mutex<bool>,
        messenger: Messenger,
        notifier_signature: Signature,
        service_context: Arc<ServiceContext>,
        setting_type: SettingType,
    ) -> Self {
        Self { notify, messenger, notifier_signature, service_context, setting_type }
    }

    async fn process_request(
        setting_type: SettingType,
        controller: &BoxedController,
        request: Request,
    ) -> SettingHandlerResult {
        let result = controller.handle(request.clone()).await;
        match result {
            Some(response_result) => response_result,
            None => Err(ControllerError::UnimplementedRequest(setting_type, request)),
        }
    }

    pub(crate) async fn create(
        mut context: Context,
        generate_controller: GenerateController,
    ) -> ControllerGenerateResult {
        let client = Arc::new(Self::new(&context));

        let mut controller = generate_controller(Arc::clone(&client)).await?;

        // Process MessageHub requests
        fasync::Task::spawn(async move {
            let _ = &context;
            let id = fuchsia_trace::Id::new();
            trace!(
                id,

                "setting handler",
                "setting_type" => format!("{:?}", client.setting_type).as_str()
            );
            while let Ok((payload, message_client)) = context.receptor.next_of::<Payload>().await {
                let setting_type = client.setting_type;

                // Setting handlers should only expect commands
                match Command::try_from(payload).expect("should only receive commands") {
                    // Rebroadcasting requires special handling. The handler will request the
                    // current value from controller and then notify the caller as if it was a
                    // change in value.
                    Command::HandleRequest(Request::Rebroadcast) => {
                        trace!(id, "handle rebroadcast");
                        // Fetch the current value
                        let controller_reply =
                            Self::process_request(setting_type, &controller, Request::Get).await;

                        // notify proxy of value
                        if let Ok(Some(info)) = &controller_reply {
                            client.notify(Event::Changed(info.clone())).await;
                        }

                        reply(message_client, controller_reply);
                    }
                    Command::HandleRequest(request) => {
                        trace!(id, "handle request");
                        reply(
                            message_client,
                            Self::process_request(setting_type, &controller, request.clone()).await,
                        );
                    }
                    Command::ChangeState(state) => {
                        trace!(
                            id,

                            "change state",
                            "state" => format!("{:?}", state).as_str()
                        );
                        match state {
                            State::Startup => {
                                if let Some(Err(e)) = controller.change_state(state).await {
                                    fx_log_err!(
                                        "Failed startup phase for SettingType {:?} {}",
                                        setting_type,
                                        e
                                    );
                                }
                                reply(message_client, Ok(None));
                                continue;
                            }
                            State::Listen => {
                                *client.notify.lock().await = true;
                            }
                            State::EndListen => {
                                *client.notify.lock().await = false;
                            }
                            State::Teardown => {
                                if let Some(Err(e)) = controller.change_state(state).await {
                                    fx_log_err!(
                                        "Failed teardown phase for SettingType {:?} {}",
                                        setting_type,
                                        e
                                    );
                                }
                                reply(message_client, Ok(None));
                                continue;
                            }
                        }

                        // Ignore whether the state change had any effect.
                        let _ = controller.change_state(state).await;
                    }
                }
            }
        })
        .detach();

        Ok(())
    }

    pub(crate) fn get_service_context(&self) -> Arc<ServiceContext> {
        Arc::clone(&self.service_context)
    }

    pub(crate) async fn notify(&self, event: Event) {
        let notify = self.notify.lock().await;
        if *notify {
            // Ignore the receptor result.
            let _ = self
                .messenger
                .message(Payload::Event(event).into(), Audience::Messenger(self.notifier_signature))
                .send();
        }
    }
}

pub(crate) struct Handler<C: controller::Create + controller::Handle + Send + Sync + 'static> {
    _data: PhantomData<C>,
}

impl<C: controller::Create + controller::Handle + Send + Sync + 'static> Handler<C> {
    pub(crate) fn spawn(context: Context) -> BoxFuture<'static, ControllerGenerateResult> {
        Box::pin(async move {
            ClientImpl::create(
                context,
                Box::new(|proxy| {
                    Box::pin(async move {
                        C::create(proxy)
                            .await
                            .map(|controller| Box::new(controller) as BoxedController)
                    })
                }),
            )
            .await
        })
    }
}

/// `IntoHandlerResult` helps with converting a value into the result of a setting request.
pub trait IntoHandlerResult {
    /// Converts `Self` into a `SettingHandlerResult` for use in a `Controller`.
    fn into_handler_result(self) -> SettingHandlerResult;
}

impl IntoHandlerResult for SettingInfo {
    fn into_handler_result(self) -> SettingHandlerResult {
        Ok(Some(self))
    }
}

pub mod persist {
    use super::ClientImpl as BaseProxy;
    use super::*;
    use crate::base::SettingInfo;
    use crate::message::base::{Audience, MessageEvent};
    use crate::service;
    use crate::storage;
    use crate::trace;
    use fuchsia_trace as ftrace;
    use futures::StreamExt;
    use settings_storage::device_storage::DeviceStorageConvertible;
    use settings_storage::UpdateState;

    pub trait Storage: DeviceStorageConvertible + Into<SettingInfo> + Send + Sync {}
    impl<T: DeviceStorageConvertible + Into<SettingInfo> + Send + Sync> Storage for T {}

    pub(crate) mod controller {
        use super::ClientProxy;
        use super::*;

        #[async_trait]
        pub(crate) trait Create: Sized {
            /// Creates the controller.
            async fn create(handler: ClientProxy) -> Result<Self, ControllerError>;
        }
    }

    pub struct ClientProxy {
        base: Arc<BaseProxy>,
        setting_type: SettingType,
    }

    impl Clone for ClientProxy {
        fn clone(&self) -> Self {
            Self { base: Arc::clone(&self.base), setting_type: self.setting_type }
        }
    }

    impl ClientProxy {
        pub(crate) async fn new(base_proxy: Arc<BaseProxy>, setting_type: SettingType) -> Self {
            Self { base: base_proxy, setting_type }
        }

        pub(crate) fn get_service_context(&self) -> Arc<ServiceContext> {
            self.base.get_service_context()
        }

        pub(crate) async fn notify(&self, event: Event) {
            self.base.notify(event).await;
        }

        pub(crate) async fn read_setting_info<T: HasSettingType>(
            &self,
            id: ftrace::Id,
        ) -> SettingInfo {
            let guard = trace_guard!(
                id,

                "read_setting_info send",
                "setting_type" => format!("{:?}", T::SETTING_TYPE).as_str()
            );
            let mut receptor = self
                .base
                .messenger
                .message(
                    storage::Payload::Request(storage::StorageRequest::Read(
                        T::SETTING_TYPE.into(),
                        id,
                    ))
                    .into(),
                    Audience::Address(service::Address::Storage),
                )
                .send();
            drop(guard);

            trace!(
                id,

                "read_setting_info receive",
                "setting_type" => format!("{:?}", T::SETTING_TYPE).as_str()
            );
            while let Ok((payload, _)) = receptor.next_of::<storage::Payload>().await {
                if let storage::Payload::Response(storage::StorageResponse::Read(
                    StorageInfo::SettingInfo(setting_info),
                )) = payload
                {
                    return setting_info;
                } else {
                    panic!("Incorrect response received from storage: {:?}", payload);
                }
            }

            panic!("Did not get a read response");
        }

        pub(crate) async fn read_setting<T: HasSettingType + TryFrom<SettingInfo>>(
            &self,
            id: ftrace::Id,
        ) -> T {
            let setting_info = self.read_setting_info::<T>(id).await;
            if let Ok(info) = setting_info.clone().try_into() {
                info
            } else {
                panic!(
                    "Mismatching type during read. Expected {:?}, but got {:?}",
                    T::SETTING_TYPE,
                    setting_info
                );
            }
        }

        /// The argument `write_through` will block returning until the value has been completely
        /// written to persistent store, rather than any temporary in-memory caching.
        pub(crate) async fn write_setting(
            &self,
            setting_info: SettingInfo,
            id: ftrace::Id,
        ) -> Result<UpdateState, ControllerError> {
            let setting_type = (&setting_info).into();
            let fst = format!("{:?}", setting_type);
            let guard = trace_guard!(
                id,

                "write_setting send",
                "setting_type" => fst.as_str()
            );
            let mut receptor = self
                .base
                .messenger
                .message(
                    storage::Payload::Request(storage::StorageRequest::Write(
                        setting_info.clone().into(),
                        id,
                    ))
                    .into(),
                    Audience::Address(service::Address::Storage),
                )
                .send();
            drop(guard);

            trace!(
                id,

                "write_setting receive",
                "setting_type" => fst.as_str()
            );
            while let Some(response) = receptor.next().await {
                if let MessageEvent::Message(
                    service::Payload::Storage(storage::Payload::Response(
                        storage::StorageResponse::Write(result),
                    )),
                    _,
                ) = response
                {
                    if let Ok(UpdateState::Updated) = result {
                        trace!(
                            id,

                            "write_setting notify",
                            "setting_type" => fst.as_str()
                        );
                        self.notify(Event::Changed(setting_info)).await;
                    }

                    return result.map_err(|e| {
                        fx_log_err!("Failed to write setting: {:?}", e);
                        ControllerError::WriteFailure(setting_type)
                    });
                }
            }

            panic!("Did not get a write response");
        }
    }

    /// A trait for interpreting a `Result` into whether a notification occurred
    /// and converting the `Result` into a `SettingHandlerResult`.
    pub(crate) trait WriteResult: IntoHandlerResult {
        /// Indicates whether a notification occurred as a result of the write.
        fn notified(&self) -> bool;
    }

    impl WriteResult for Result<UpdateState, ControllerError> {
        fn notified(&self) -> bool {
            self.as_ref().map_or(false, |update_state| UpdateState::Updated == *update_state)
        }
    }

    impl IntoHandlerResult for Result<UpdateState, ControllerError> {
        fn into_handler_result(self) -> SettingHandlerResult {
            self.map(|_| None)
        }
    }

    pub(crate) struct Handler<
        C: controller::Create + super::controller::Handle + Send + Sync + 'static,
    > {
        _data: PhantomData<C>,
    }

    impl<C: controller::Create + super::controller::Handle + Send + Sync + 'static> Handler<C> {
        pub(crate) fn spawn(context: Context) -> BoxFuture<'static, ControllerGenerateResult> {
            Box::pin(async move {
                let setting_type = context.setting_type;

                ClientImpl::create(
                    context,
                    Box::new(move |proxy| {
                        Box::pin(async move {
                            let proxy = ClientProxy::new(proxy, setting_type).await;
                            let controller_result = C::create(proxy).await;

                            match controller_result {
                                Err(err) => Err(err),
                                Ok(controller) => Ok(Box::new(controller) as BoxedController),
                            }
                        })
                    }),
                )
                .await
            })
        }
    }
}

pub(crate) fn reply(client: MessageClient, result: SettingHandlerResult) {
    client.reply(Payload::Result(result).into()).send().ack();
}
