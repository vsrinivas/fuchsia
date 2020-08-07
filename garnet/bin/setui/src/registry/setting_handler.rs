// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::internal::handler::{message, reply, Payload};
use crate::message::base::{Audience, MessageEvent};
use crate::registry::base::{Command, Context, SettingHandlerResult, State};
use crate::registry::device_storage::DeviceStorageFactory;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::{
    ControllerStateResult, SettingRequest, SettingResponseResult, SettingType, SwitchboardError,
};
use async_trait::async_trait;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use futures::StreamExt;
use std::borrow::Cow;
use std::marker::PhantomData;
use std::sync::Arc;
use thiserror::Error;

pub trait StorageFactory: DeviceStorageFactory + Send + Sync {}
impl<T: DeviceStorageFactory + Send + Sync> StorageFactory for T {}

#[derive(Error, Debug, Clone, PartialEq)]
pub enum ControllerError {
    #[error("Unimplemented Request")]
    Unimplemented,
    #[error("Write failed. setting type: {0:?}")]
    WriteFailure(SettingType),
    #[error("Initialization failure: cause {0:?}")]
    InitFailure(Cow<'static, str>),
    #[error("Restoration of setting on controller startup failed: cause {0:?}")]
    RestoreFailure(Cow<'static, str>),
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
        async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult>;
        async fn change_state(&mut self, _state: State) -> Option<ControllerStateResult> {
            None
        }
    }
}

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

    pub async fn notify(&self) {
        self.client_handle.lock().await.notify().await;
    }
}

pub struct ClientImpl {
    notify: bool,
    messenger: message::Messenger,
    notifier_signature: message::Signature,
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
        request: SettingRequest,
    ) -> SettingResponseResult {
        let result = controller.handle(request.clone()).await;
        match result {
            Some(response_result) => response_result,
            None => Err(SwitchboardError::UnimplementedRequest(setting_type, request)),
        }
    }

    pub async fn create<S: StorageFactory + 'static>(
        mut context: Context<S>,
        generate_controller: GenerateController,
    ) -> SettingHandlerResult {
        let client = Arc::new(Mutex::new(Self::new(&context)));
        let controller_result = generate_controller(ClientProxy::new(client.clone())).await;

        if let Err(error) = controller_result {
            return Err(anyhow::Error::new(error));
        }

        let mut controller = controller_result.unwrap();

        // Process MessageHub requests
        fasync::Task::spawn(async move {
            while let Some(event) = context.receptor.next().await {
                let setting_type = client.lock().await.setting_type;
                match event {
                    MessageEvent::Message(
                        Payload::Command(Command::HandleRequest(request)),
                        client,
                    ) => reply(
                        client,
                        Self::process_request(setting_type, &controller, request.clone()).await,
                    ),
                    MessageEvent::Message(
                        Payload::Command(Command::ChangeState(state)),
                        receptor_client,
                    ) => {
                        match state {
                            State::Startup => {
                                match controller.change_state(state).await {
                                    Some(Err(e)) => fx_log_err!(
                                        "Failed startup phase for SettingType {:?} {}",
                                        setting_type,
                                        e
                                    ),
                                    _ => {}
                                };
                                reply(receptor_client, Ok(None));
                                continue;
                            }
                            State::Listen => {
                                client.lock().await.notify = true;
                            }
                            State::EndListen => {
                                client.lock().await.notify = false;
                            }
                            State::Teardown => {
                                match controller.change_state(state).await {
                                    Some(Err(e)) => fx_log_err!(
                                        "Failed teardown phase for SettingType {:?} {}",
                                        setting_type,
                                        e
                                    ),
                                    _ => {}
                                };
                                reply(receptor_client, Ok(None));
                                continue;
                            }
                        }
                        controller.change_state(state).await;
                    }
                    _ => {}
                }
            }
        })
        .detach();

        Ok(())
    }

    async fn get_service_context(&self) -> ServiceContextHandle {
        self.service_context.clone()
    }

    async fn notify(&self) {
        if self.notify {
            self.messenger
                .message(
                    Payload::Changed(self.setting_type),
                    Audience::Messenger(self.notifier_signature),
                )
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
    ) -> BoxFuture<'static, SettingHandlerResult> {
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
    use crate::registry::device_storage::{DeviceStorage, DeviceStorageCompatible};

    pub trait Storage: DeviceStorageCompatible + Send + Sync {}
    impl<T: DeviceStorageCompatible + Send + Sync> Storage for T {}

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
        pub trait Create<S: Storage>: Sized {
            async fn create(handler: ClientProxy<S>) -> Result<Self, ControllerError>;
        }
    }

    #[derive(Clone)]
    pub struct ClientProxy<S: Storage + 'static> {
        base: BaseProxy,
        storage: Arc<Mutex<DeviceStorage<S>>>,
        setting_type: SettingType,
    }

    impl<S: Storage + 'static> ClientProxy<S> {
        pub async fn new(
            base_proxy: BaseProxy,
            storage: Arc<Mutex<DeviceStorage<S>>>,
            setting_type: SettingType,
        ) -> Self {
            Self { base: base_proxy, storage, setting_type }
        }

        pub async fn get_service_context(&self) -> ServiceContextHandle {
            self.base.get_service_context().await
        }

        pub async fn notify(&self) {
            self.base.notify().await;
        }

        pub async fn read(&self) -> S {
            self.storage.lock().await.get().await
        }

        /// Returns a boolean indicating whether the value was written or an
        /// Error if write failed. the argument `write_through` will block
        /// returning until the value has been completely written to persistent
        /// store, rather than any temporary in-memory caching.
        pub async fn write(
            &self,
            value: S,
            write_through: bool,
        ) -> Result<UpdateState, ControllerError> {
            if value == self.read().await {
                return Ok(UpdateState::Unchanged);
            }

            match self.storage.lock().await.write(&value, write_through).await {
                Ok(_) => {
                    self.notify().await;
                    Ok(UpdateState::Updated)
                }
                Err(_) => Err(ControllerError::WriteFailure(self.setting_type)),
            }
        }
    }

    /// A trait for interpreting a `Result` into whether a notification occurred
    /// and converting the `Result` into a `SettingResponseResult`.
    pub trait WriteResult {
        /// Indicates whether a notification occurred as a result of the write.
        fn notified(&self) -> bool;
        /// Converts the result into a `SettingResponseResult`.
        fn into_response_result(self) -> SettingResponseResult;
    }

    impl WriteResult for Result<UpdateState, SwitchboardError> {
        fn notified(&self) -> bool {
            self.as_ref().map_or(false, |update_state| UpdateState::Updated == *update_state)
        }

        fn into_response_result(self) -> SettingResponseResult {
            self.map(|_| None)
        }
    }

    pub async fn write<S: Storage + 'static>(
        client: &ClientProxy<S>,
        value: S,
        write_through: bool,
    ) -> Result<UpdateState, SwitchboardError> {
        client.write(value, write_through).await.map_err(|e| {
            if let ControllerError::WriteFailure(setting_type) = e {
                SwitchboardError::StorageFailure(setting_type)
            } else {
                SwitchboardError::UnexpectedError("client write failure".into())
            }
        })
    }

    pub struct Handler<
        S: Storage + 'static,
        C: controller::Create<S> + super::controller::Handle + Send + Sync + 'static,
    > {
        _data: PhantomData<C>,
        _storage: PhantomData<S>,
    }

    impl<
            S: Storage + 'static,
            C: controller::Create<S> + super::controller::Handle + Send + Sync + 'static,
        > Handler<S, C>
    {
        pub fn spawn<F: StorageFactory + 'static>(
            context: Context<F>,
        ) -> BoxFuture<'static, SettingHandlerResult> {
            Box::pin(async move {
                let storage = context
                    .environment
                    .storage_factory_handle
                    .lock()
                    .await
                    .get_store::<S>(context.id);
                let setting_type = context.setting_type.clone();

                ClientImpl::create(
                    context,
                    Box::new(move |proxy| {
                        let storage = storage.clone();
                        Box::pin(async move {
                            let proxy = ClientProxy::<S>::new(proxy, storage, setting_type).await;
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
