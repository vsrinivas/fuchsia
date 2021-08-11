// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::BlueprintHandle;
use crate::base::SettingType;
use crate::config::base::AgentType;
use crate::handler::base::{Context, GenerateHandler};
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::handler::setting_handler::persist::ClientProxy;
use crate::handler::setting_handler::{BoxedController, ClientImpl};
use crate::ingress::fidl::Interface;
use crate::input::input_controller::InputController;
use crate::input::input_device_configuration::InputConfiguration;
use crate::input::types::InputInfoSources;
use crate::service::message::Delegate;
use crate::tests::fakes::camera3_service::Camera3Service;
use crate::tests::fakes::input_device_registry_service::InputDeviceRegistryService;
use crate::tests::fakes::service_registry::ServiceRegistry;

use crate::Environment;
use crate::EnvironmentBuilder;

use fidl_fuchsia_settings::{InputMarker, InputProxy};
use futures::lock::Mutex;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_input_test_environment";

pub(crate) struct TestInputEnvironment {
    /// For sending requests to the input proxy.
    pub(crate) input_service: InputProxy,

    /// For sending media buttons changes.
    pub(crate) input_button_service: Arc<Mutex<InputDeviceRegistryService>>,

    /// For watching, connecting to, and making requests on the camera device.
    pub(crate) camera3_service: Arc<Mutex<Camera3Service>>,

    /// For listening on service messages, particularly media buttons events.
    pub(crate) delegate: Delegate,
}

pub(crate) struct TestInputEnvironmentBuilder {
    /// The initial InputInfoSources in the environment.
    starting_input_info_sources: Option<InputInfoSources>,

    /// The config to load from.
    input_device_config: Option<InputConfiguration>,

    /// The list of agents to include.
    agents: Vec<AgentType>,
}

impl TestInputEnvironmentBuilder {
    pub(crate) fn new() -> Self {
        Self {
            starting_input_info_sources: None,
            input_device_config: None,
            agents: vec![AgentType::Restore, AgentType::MediaButtons],
        }
    }

    pub(crate) fn set_starting_input_info_sources(
        mut self,
        starting_input_info_sources: InputInfoSources,
    ) -> Self {
        self.starting_input_info_sources = Some(starting_input_info_sources);
        self
    }

    pub(crate) fn set_input_device_config(
        mut self,
        input_device_config: InputConfiguration,
    ) -> Self {
        self.input_device_config = Some(input_device_config);
        self
    }

    pub(crate) async fn build(self) -> TestInputEnvironment {
        let service_registry = ServiceRegistry::create();
        let storage_factory = Arc::new(if let Some(info) = self.starting_input_info_sources {
            InMemoryStorageFactory::with_initial_data(&info)
        } else {
            InMemoryStorageFactory::new()
        });

        // Register fake input device registry service.
        let input_button_service_handle = Arc::new(Mutex::new(InputDeviceRegistryService::new()));
        service_registry.lock().await.register_service(input_button_service_handle.clone());

        // Register fake camera3 service.
        let camera3_service_handle = Arc::new(Mutex::new(Camera3Service::new(true)));
        service_registry.lock().await.register_service(camera3_service_handle.clone());

        let mut environment_builder = EnvironmentBuilder::new(Arc::clone(&storage_factory))
            .service(Box::new(ServiceRegistry::serve(service_registry)))
            .agents(&self.agents.into_iter().map(BlueprintHandle::from).collect::<Vec<_>>())
            .fidl_interfaces(&[Interface::Input]);

        if let Some(config) = self.input_device_config {
            // If hardware configuration was specified, we need a generate_handler to include the
            // specified configuration. This generate_handler method is a copy-paste of
            // persist::controller::spawn from setting_handler.rs, with the innermost controller
            // create method replaced with our custom constructor from input controller.
            // TODO(fxbug.dev/63832): See if we can reduce this rightward drift.
            let generate_handler: GenerateHandler = Box::new(move |context: Context| {
                let config = config.clone();
                Box::pin(async move {
                    let setting_type = context.setting_type;
                    ClientImpl::create(
                        context,
                        Box::new(move |proxy| {
                            let config = config.clone();
                            Box::pin(async move {
                                let proxy = ClientProxy::new(proxy, setting_type).await;
                                let controller_result =
                                    InputController::create_with_config(proxy, config.clone())
                                        .await;

                                controller_result
                                    .map(|controller| Box::new(controller) as BoxedController)
                            })
                        }),
                    )
                    .await
                })
            });

            environment_builder = environment_builder.handler(SettingType::Input, generate_handler);
        }

        let Environment { nested_environment: env, delegate, .. } =
            environment_builder.spawn_nested(ENV_NAME).await.unwrap();

        let input_service = env
            .expect("Nested environment should exist")
            .connect_to_protocol::<InputMarker>()
            .unwrap();

        TestInputEnvironment {
            input_service,
            input_button_service: input_button_service_handle,
            camera3_service: camera3_service_handle,
            delegate,
        }
    }
}
