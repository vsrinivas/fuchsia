// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::registry::base::{Command, Context, Notifier, SettingHandlerResult, State};
use crate::registry::device_storage::DeviceStorageFactory;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::{
    SettingRequest, SettingResponseResult, SettingType, SwitchboardError,
};
use async_trait::async_trait;
use fuchsia_async as fasync;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use futures::StreamExt;
use std::marker::PhantomData;
use std::sync::Arc;
use thiserror::Error;

pub trait StorageFactory: DeviceStorageFactory + Send + Sync {}
impl<T: DeviceStorageFactory + Send + Sync> StorageFactory for T {}

#[derive(Error, Debug, Clone)]
pub enum ControllerError {
    #[error("Unimplemented Request")]
    Unimplemented {},
    #[error("Write failed. setting type: {setting_type:?}")]
    WriteFailure { setting_type: SettingType },
    #[error("Initialization failure: cause {description:?}")]
    InitFailure { description: String },
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
    pub trait Handle {
        async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult>;
        async fn change_state(&mut self, state: State);
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
    listen_notifier: Option<Notifier>,
    service_context: ServiceContextHandle,
    setting_type: SettingType,
}

impl ClientImpl {
    fn new<S: StorageFactory + 'static>(context: Context<S>) -> Self {
        Self {
            setting_type: context.setting_type,
            listen_notifier: None,
            service_context: context.environment.service_context_handle,
        }
    }

    async fn create<S: StorageFactory + 'static>(
        context: Context<S>,
        generate_controller: GenerateController,
    ) -> SettingHandlerResult {
        let (ctrl_tx, mut ctrl_rx) = futures::channel::mpsc::unbounded::<Command>();
        let client = Arc::new(Mutex::new(Self::new(context)));
        let controller_result = generate_controller(ClientProxy::new(client.clone())).await;

        if let Err(error) = controller_result {
            return Err(anyhow::Error::new(error));
        }

        let controller = controller_result.unwrap();

        fasync::spawn(async move {
            while let Some(command) = ctrl_rx.next().await {
                match command {
                    Command::HandleRequest(request, responder) => {
                        match controller.handle(request.clone()).await {
                            Some(response_result) => {
                                responder.send(response_result).ok();
                            }
                            None => {
                                responder
                                    .send(Err(SwitchboardError::UnimplementedRequest {
                                        setting_type: SettingType::Intl,
                                        request: request,
                                    }))
                                    .ok();
                            }
                        }
                    }
                    Command::ChangeState(state) => match state {
                        State::Listen(notifier) => {
                            client.lock().await.set_notifier(Some(notifier)).await;
                        }
                        State::EndListen => {
                            client.lock().await.set_notifier(None).await;
                        }
                    },
                }
            }
        });

        Ok(ctrl_tx)
    }
    async fn set_notifier(&mut self, notifier: Option<Notifier>) {
        self.listen_notifier = notifier;
    }

    async fn get_service_context(&self) -> ServiceContextHandle {
        self.service_context.clone()
    }

    async fn notify(&self) {
        if let Some(notifier) = &self.listen_notifier {
            notifier.unbounded_send(self.setting_type).ok();
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
        async fn new<F: StorageFactory + 'static>(
            base_proxy: BaseProxy,
            context: Context<F>,
        ) -> Self {
            let storage = context.environment.storage_factory_handle.lock().await.get_store::<S>();
            Self { base: base_proxy, storage: storage, setting_type: context.setting_type }
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

        pub async fn write(&self, value: S, write_through: bool) -> Result<(), ControllerError> {
            if value == self.read().await {
                return Ok(());
            }

            match self.storage.lock().await.write(&value, write_through).await {
                Ok(_) => {
                    self.notify().await;
                    Ok(())
                }
                Err(_) => Err(ControllerError::WriteFailure { setting_type: self.setting_type }),
            }
        }
    }

    pub async fn write<S: Storage + 'static>(
        client: &ClientProxy<S>,
        value: S,
        write_through: bool,
    ) -> SettingResponseResult {
        match client.write(value, write_through).await {
            Ok(_) => Ok(None),
            Err(ControllerError::WriteFailure { setting_type }) => {
                Err(SwitchboardError::StorageFailure { setting_type: setting_type })
            }
            _ => Err(SwitchboardError::UnexpectedError {
                description: "client write failure".to_string(),
            }),
        }
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
                let context_clone = context.clone();
                ClientImpl::create(
                    context,
                    Box::new(move |proxy| {
                        let context = context_clone.clone();
                        Box::pin(async move {
                            let proxy = ClientProxy::<S>::new(proxy, context.clone()).await;
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
