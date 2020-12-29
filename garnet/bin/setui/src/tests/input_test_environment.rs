// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::BlueprintHandle;
use crate::config::base::AgentType;
use crate::handler::base::{Context, GenerateHandler};
use crate::handler::device_storage::testing::*;
use crate::handler::device_storage::{DeviceStorage, DeviceStorageFactory};
use crate::handler::setting_handler::persist::ClientProxy;
use crate::handler::setting_handler::{BoxedController, ClientImpl};
use crate::input::input_controller::InputController;
use crate::input::input_device_configuration::InputConfiguration;
use crate::switchboard::base::{InputInfoSources, SettingType};
use crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService;
use crate::tests::fakes::service_registry::ServiceRegistry;

use crate::EnvironmentBuilder;

use fidl_fuchsia_settings::{InputMarker, InputProxy};
use futures::lock::Mutex;
use std::sync::Arc;

#[allow(dead_code)]
const ENV_NAME: &str = "settings_service_input_test_environment";
#[allow(dead_code)]
const CONTEXT_ID: u64 = 0;

#[allow(dead_code)]
pub struct TestInputEnvironment {
    /// For sending requests to the input proxy.
    pub input_service: InputProxy,

    /// For sending media buttons changes.
    pub input_button_service: Arc<Mutex<InputDeviceRegistryService>>,

    /// For storing the InputInfoSources.
    pub store: Arc<Mutex<DeviceStorage<InputInfoSources>>>,
}

#[allow(dead_code)]
pub struct TestInputEnvironmentBuilder {
    /// The initial InputInfoSources in the environment.
    starting_input_info_sources: Option<InputInfoSources>,

    /// The config to load from.
    input_device_config: Option<InputConfiguration>,

    /// The list of agents to include.
    agents: Vec<AgentType>,
}

impl TestInputEnvironmentBuilder {
    #[allow(dead_code)]
    pub fn new() -> Self {
        Self {
            starting_input_info_sources: None,
            input_device_config: None,
            agents: vec![AgentType::Restore, AgentType::MediaButtons],
        }
    }

    #[allow(dead_code)]
    pub fn set_input_device_config(mut self, input_device_config: InputConfiguration) -> Self {
        self.input_device_config = Some(input_device_config);
        self
    }

    #[allow(dead_code)]
    pub async fn build(self) -> TestInputEnvironment {
        let service_registry = ServiceRegistry::create();
        let storage_factory = InMemoryStorageFactory::create();

        // Register fake input device registry service.
        let input_button_service_handle = Arc::new(Mutex::new(InputDeviceRegistryService::new()));
        service_registry.lock().await.register_service(input_button_service_handle.clone());

        let store = storage_factory
            .lock()
            .await
            .get_device_storage::<InputInfoSources>(StorageAccessContext::Test, CONTEXT_ID);

        if let Some(info) = self.starting_input_info_sources {
            store.lock().await.write(&info, false).await.expect("write starting values");
        }

        let mut environment_builder = EnvironmentBuilder::new(storage_factory)
            .service(Box::new(ServiceRegistry::serve(service_registry)))
            .agents(&self.agents.into_iter().map(BlueprintHandle::from).collect::<Vec<_>>())
            .settings(&[SettingType::Input]);

        if let Some(config) = self.input_device_config {
            // If hardware configuration was specified, we need a generate_handler to include the
            // specified configuration. This generate_handler method is a copy-paste of
            // persist::controller::spawn from setting_handler.rs, with the innermost controller
            // create method replaced with our custom constructor from input controller.
            // TODO(fxbug.dev/63832): See if we can reduce this rightward drift.
            let generate_handler: GenerateHandler<InMemoryStorageFactory> =
                Box::new(move |context: Context<InMemoryStorageFactory>| {
                    let config_clone = config.clone();
                    Box::pin(async move {
                        let storage = context
                            .environment
                            .storage_factory_handle
                            .lock()
                            .await
                            .get_store::<InputInfoSources>(context.id);

                        let setting_type = context.setting_type;
                        ClientImpl::create(
                            context,
                            Box::new(move |proxy| {
                                let config = config_clone.clone();
                                let storage = storage.clone();
                                Box::pin(async move {
                                    let proxy = ClientProxy::<InputInfoSources>::new(
                                        proxy,
                                        storage,
                                        setting_type,
                                    )
                                    .await;
                                    let controller_result =
                                        InputController::create_with_config(proxy, config.clone())
                                            .await;

                                    controller_result.and_then(|controller| {
                                        Ok(Box::new(controller) as BoxedController)
                                    })
                                })
                            }),
                        )
                        .await
                    })
                });

            environment_builder = environment_builder.handler(SettingType::Input, generate_handler);
        }

        let env = environment_builder.spawn_and_get_nested_environment(ENV_NAME).await.unwrap();

        let input_service = env.connect_to_service::<InputMarker>().unwrap();

        TestInputEnvironment {
            input_service,
            input_button_service: input_button_service_handle,
            store,
        }
    }
}
