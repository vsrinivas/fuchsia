// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::base::{SettingInfo, SettingType};
use crate::handler::base::{Context, ControllerGenerateResult, Request};
use crate::handler::device_storage::DeviceStorageFactory;
use crate::message::base::Audience;
use crate::payload_convert;
use crate::service::message::{MessageClient, Messenger, Signature};
use crate::service_context::ServiceContextHandle;
use async_trait::async_trait;
use core::convert::TryFrom;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use std::borrow::Cow;
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

pub trait StorageFactory: DeviceStorageFactory + Send + Sync {}
impl<T: DeviceStorageFactory + Send + Sync> StorageFactory for T {}

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
    #[error("Call to an external dependency {1:?} for setting type {0:?} failed. Request:{2:?}")]
    ExternalFailure(SettingType, Cow<'static, str>, Cow<'static, str>),
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

pub type BoxedController = Box<dyn controller::Handle + Send + Sync>;
pub type BoxedControllerResult = Result<BoxedController, ControllerError>;

pub type GenerateController =
    Box<dyn Fn(ClientProxy) -> BoxFuture<'static, BoxedControllerResult> + Send + Sync>;

pub mod controller {
    use super::*;

    #[async_trait]
    pub trait Create: Sized {
        async fn create(client: ClientProxy) -> Result<Self, ControllerError>;
    }

    #[async_trait]
    pub trait Handle: Send {
        async fn handle(&self, request: Request) -> Option<SettingHandlerResult>;
        async fn change_state(&mut self, _state: State) -> Option<ControllerStateResult> {
            None
        }
    }
}

// TODO(fxbug.dev/70633) Convert Arc<Mutex<...>> into interior mutability within ClientImpl.
// Then remove/replace ClientProxy with ClientImpl.
#[derive(Clone)]
pub struct ClientProxy {
    client_handle: Arc<Mutex<ClientImpl>>,
}

impl ClientProxy {
    fn new(handle: Arc<Mutex<ClientImpl>>) -> Self {
        Self { client_handle: handle }
    }

    pub async fn get_service_context(&self) -> ServiceContextHandle {
        self.client_handle.lock().await.get_service_context().await
    }

    pub async fn notify(&self, event: Event) {
        self.client_handle.lock().await.notify(event).await;
    }
}

pub struct ClientImpl {
    notify: bool,
    messenger: Messenger,
    notifier_signature: Signature,
    service_context: ServiceContextHandle,
    setting_type: SettingType,
}

impl ClientImpl {
    fn new<S: StorageFactory + 'static>(context: &Context<S>) -> Self {
        Self {
            messenger: context.messenger.clone(),
            setting_type: context.setting_type,
            notifier_signature: context.notifier_signature.clone(),
            notify: false,
            service_context: context.environment.service_context_handle.clone(),
        }
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

    pub async fn create<S: StorageFactory + 'static>(
        mut context: Context<S>,
        generate_controller: GenerateController,
    ) -> ControllerGenerateResult {
        let client = Arc::new(Mutex::new(Self::new(&context)));
        let controller_result = generate_controller(ClientProxy::new(client.clone())).await;

        if let Err(error) = controller_result {
            return Err(anyhow::Error::new(error));
        }

        let mut controller = controller_result.unwrap();

        // Process MessageHub requests
        fasync::Task::spawn(async move {
            while let Ok((payload, message_client)) = context.receptor.next_payload().await {
                let setting_type = client.lock().await.setting_type;

                // Setting handlers should only expect commands
                match Command::try_from(
                    Payload::try_from(payload).expect("should only receive handler payloads"),
                )
                .expect("should only receive commands")
                {
                    // Rebroadcasting requires special handling. The handler will request the
                    // current value from controller and then notify the caller as if it was a
                    // change in value.
                    Command::HandleRequest(Request::Rebroadcast) => {
                        // Fetch the current value
                        let controller_reply =
                            Self::process_request(setting_type, &controller, Request::Get).await;

                        // notify proxy of value
                        if let Ok(Some(info)) = &controller_reply {
                            client.lock().await.notify(Event::Changed(info.clone())).await;
                        }

                        reply(message_client, controller_reply);
                    }
                    Command::HandleRequest(request) => reply(
                        message_client,
                        Self::process_request(setting_type, &controller, request.clone()).await,
                    ),
                    Command::ChangeState(state) => {
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
                                client.lock().await.notify = true;
                            }
                            State::EndListen => {
                                client.lock().await.notify = false;
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
                        controller.change_state(state).await;
                    }
                }
            }
        })
        .detach();

        Ok(())
    }

    async fn get_service_context(&self) -> ServiceContextHandle {
        self.service_context.clone()
    }

    async fn notify(&self, event: Event) {
        if self.notify {
            self.messenger
                .message(Payload::Event(event).into(), Audience::Messenger(self.notifier_signature))
                .send()
                .ack();
        }
    }
}

pub struct Handler<C: controller::Create + controller::Handle + Send + Sync + 'static> {
    _data: PhantomData<C>,
}

impl<C: controller::Create + controller::Handle + Send + Sync + 'static> Handler<C> {
    pub fn spawn<S: StorageFactory + 'static>(
        context: Context<S>,
    ) -> BoxFuture<'static, ControllerGenerateResult> {
        Box::pin(async move {
            ClientImpl::create(
                context,
                Box::new(|proxy| {
                    Box::pin(async move {
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

pub mod persist {
    use super::ClientProxy as BaseProxy;
    use super::*;
    use crate::base::SettingInfo;
    use crate::handler::device_storage::{DeviceStorage, DeviceStorageConvertible};
    use std::borrow::Borrow;

    pub trait Storage: DeviceStorageConvertible + Into<SettingInfo> + Send + Sync {}
    impl<T: DeviceStorageConvertible + Into<SettingInfo> + Send + Sync> Storage for T {}

    #[derive(PartialEq, Clone, Debug)]
    /// Enum for describing whether writing affected persistent value.
    pub enum UpdateState {
        Unchanged,
        Updated,
    }

    pub mod controller {
        use super::ClientProxy;
        use super::*;

        #[async_trait]
        pub trait Create: Sized {
            async fn create(handler: ClientProxy) -> Result<Self, ControllerError>;
        }
    }

    #[derive(Clone)]
    pub struct ClientProxy {
        base: BaseProxy,
        storage: Arc<DeviceStorage>,
        setting_type: SettingType,
    }

    impl ClientProxy {
        pub async fn new(
            base_proxy: BaseProxy,
            storage: Arc<DeviceStorage>,
            setting_type: SettingType,
        ) -> Self {
            Self { base: base_proxy, storage, setting_type }
        }

        pub async fn get_service_context(&self) -> ServiceContextHandle {
            self.base.get_service_context().await
        }

        pub async fn notify(&self, event: Event) {
            self.base.notify(event).await;
        }

        pub async fn read<S>(&self) -> S
        where
            S: Storage + 'static,
        {
            self.storage.get::<S::Storable>().await.into()
        }

        /// Returns a boolean indicating whether the value was written or an
        /// Error if write failed. the argument `write_through` will block
        /// returning until the value has been completely written to persistent
        /// store, rather than any temporary in-memory caching.
        pub async fn write<S>(
            &self,
            value: S,
            write_through: bool,
        ) -> Result<UpdateState, ControllerError>
        where
            S: Storage + 'static,
        {
            let storable_value = value.get_storable();
            let storable_value: &S::Storable = storable_value.borrow();
            if storable_value == &self.storage.get::<S::Storable>().await {
                return Ok(UpdateState::Unchanged);
            }

            match self.storage.write(storable_value, write_through).await {
                Ok(_) => {
                    self.notify(Event::Changed(value.into())).await;
                    Ok(UpdateState::Updated)
                }
                Err(_) => Err(ControllerError::WriteFailure(self.setting_type)),
            }
        }
    }

    /// A trait for interpreting a `Result` into whether a notification occurred
    /// and converting the `Result` into a `SettingHandlerResult`.
    pub trait WriteResult {
        /// Indicates whether a notification occurred as a result of the write.
        fn notified(&self) -> bool;
        /// Converts the result into a `SettingHandlerResult`.
        fn into_handler_result(self) -> SettingHandlerResult;
    }

    impl WriteResult for Result<UpdateState, ControllerError> {
        fn notified(&self) -> bool {
            self.as_ref().map_or(false, |update_state| UpdateState::Updated == *update_state)
        }

        fn into_handler_result(self) -> SettingHandlerResult {
            self.map(|_| None)
        }
    }

    pub async fn write<S: Storage + 'static>(
        client: &ClientProxy,
        value: S,
        write_through: bool,
    ) -> Result<UpdateState, ControllerError> {
        client.write(value, write_through).await
    }

    pub struct Handler<C: controller::Create + super::controller::Handle + Send + Sync + 'static> {
        _data: PhantomData<C>,
    }

    impl<C: controller::Create + super::controller::Handle + Send + Sync + 'static> Handler<C> {
        pub fn spawn<F: StorageFactory + 'static>(
            context: Context<F>,
        ) -> BoxFuture<'static, ControllerGenerateResult> {
            Box::pin(async move {
                let storage = context.environment.storage_factory.get_store(context.id).await;
                let setting_type = context.setting_type;

                ClientImpl::create(
                    context,
                    Box::new(move |proxy| {
                        let storage = storage.clone();
                        Box::pin(async move {
                            let proxy = ClientProxy::new(proxy, storage, setting_type).await;
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

pub fn reply(client: MessageClient, result: SettingHandlerResult) {
    client.reply(Payload::Result(result).into()).send().ack();
}
