// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::accessibility::spawn_accessibility_controller,
    crate::accessibility::spawn_accessibility_fidl_handler,
    crate::account::spawn_account_controller,
    crate::agent::authority_impl::AuthorityImpl,
    crate::agent::base::{AgentHandle, Authority, Lifespan},
    crate::audio::spawn_audio_controller,
    crate::audio::spawn_audio_fidl_handler,
    crate::device::spawn_device_controller,
    crate::device::spawn_device_fidl_handler,
    crate::display::spawn_display_controller,
    crate::display::spawn_display_fidl_handler,
    crate::display::spawn_light_sensor_controller,
    crate::do_not_disturb::spawn_do_not_disturb_controller,
    crate::do_not_disturb::spawn_do_not_disturb_fidl_handler,
    crate::internal::core::create_message_hub as create_registry_message_hub,
    crate::intl::intl_controller::IntlController,
    crate::intl::intl_fidl_handler::spawn_intl_fidl_handler,
    crate::night_mode::night_mode_controller::NightModeController,
    crate::night_mode::spawn_night_mode_fidl_handler,
    crate::power::spawn_power_controller,
    crate::privacy::privacy_controller::PrivacyController,
    crate::privacy::spawn_privacy_fidl_handler,
    crate::registry::base::GenerateHandler,
    crate::registry::device_storage::DeviceStorageFactory,
    crate::registry::registry_impl::RegistryImpl,
    crate::registry::setting_handler_factory_impl::SettingHandlerFactoryImpl,
    crate::service_context::GenerateService,
    crate::service_context::ServiceContext,
    crate::service_context::ServiceContextHandle,
    crate::setup::setup_controller::SetupController,
    crate::setup::spawn_setup_fidl_handler,
    crate::switchboard::base::SettingType,
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    crate::system::spawn_setui_fidl_handler,
    crate::system::spawn_system_controller,
    crate::system::spawn_system_fidl_handler,
    anyhow::{format_err, Error},
    fidl_fuchsia_settings::*,
    fidl_fuchsia_setui::SetUiServiceRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::{NestedEnvironment, ServiceFs, ServiceFsDir, ServiceObj},
    fuchsia_syslog::fx_log_err,
    futures::channel::oneshot::Receiver,
    futures::lock::Mutex,
    futures::StreamExt,
    serde_derive::{Deserialize, Serialize},
    std::collections::HashMap,
    std::collections::HashSet,
    std::iter::FromIterator,
    std::sync::Arc,
};

mod accessibility;
mod account;
mod audio;
mod device;
mod display;
mod do_not_disturb;
mod fidl_clone;
mod fidl_processor;
mod input;
mod internal;
mod intl;
mod night_mode;
mod power;
mod privacy;
mod setup;
mod system;

pub mod agent;
pub mod config;
pub mod message;
pub mod registry;
pub mod service_context;
pub mod switchboard;

/// Runtime defines where the environment will exist. Service is meant for
/// production environments and will hydrate components to be discoverable as
/// an environment service. Nested creates a service only usable in the scope
/// of a test.
enum Runtime {
    Service,
    Nested(&'static str),
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct ServiceConfiguration {
    pub services: HashSet<SettingType>,
}

/// Environment is handed back when an environment is spawned from the
/// EnvironmentBuilder. A nested environment (if available) is returned,
/// along with a receiver to be notified when initialization/setup is
/// complete.
pub struct Environment {
    pub nested_environment: Option<NestedEnvironment>,
    pub completion_rx: Receiver<Result<(), Error>>,
}

impl Environment {
    pub fn new(
        nested_environment: Option<NestedEnvironment>,
        completion_rx: Receiver<Result<(), Error>>,
    ) -> Environment {
        Environment { nested_environment: nested_environment, completion_rx: completion_rx }
    }
}

/// The EnvironmentBuilder aggregates the parameters surrounding an environment
/// and ultimately spawns an environment based on them.
pub struct EnvironmentBuilder<T: DeviceStorageFactory + Send + Sync + 'static> {
    configuration: Option<ServiceConfiguration>,
    agents: Vec<AgentHandle>,
    storage_factory: Arc<Mutex<T>>,
    generate_service: Option<GenerateService>,
    handlers: HashMap<SettingType, GenerateHandler<T>>,
}

macro_rules! register_handler {
    ($handler_factory:ident, $setting_type:expr, $spawn_method:expr) => {
        $handler_factory.register($setting_type, Box::new($spawn_method));
    };
}

impl<T: DeviceStorageFactory + Send + Sync + 'static> EnvironmentBuilder<T> {
    pub fn new(storage_factory: Arc<Mutex<T>>) -> EnvironmentBuilder<T> {
        EnvironmentBuilder {
            configuration: None,
            agents: vec![],
            storage_factory: storage_factory,
            generate_service: None,
            handlers: HashMap::new(),
        }
    }

    pub fn handler(
        mut self,
        setting_type: SettingType,
        generate_handler: GenerateHandler<T>,
    ) -> EnvironmentBuilder<T> {
        self.handlers.insert(setting_type, generate_handler);
        self
    }

    /// A service generator to be used as an overlay on the ServiceContext.
    pub fn service(mut self, generate_service: GenerateService) -> EnvironmentBuilder<T> {
        self.generate_service = Some(generate_service);
        self
    }

    /// A preset configuration to load preset parameters as a base.
    pub fn configuration(mut self, configuration: ServiceConfiguration) -> EnvironmentBuilder<T> {
        self.configuration = Some(configuration);
        self
    }

    /// Setting types to participate.
    pub fn settings(self, settings: &[SettingType]) -> EnvironmentBuilder<T> {
        self.configuration(ServiceConfiguration {
            services: settings.to_vec().into_iter().collect(),
        })
    }

    /// Agents to participate
    pub fn agents(mut self, agents: &[AgentHandle]) -> EnvironmentBuilder<T> {
        self.agents.append(&mut agents.to_vec());
        self
    }

    async fn prepare_env(
        self,
        runtime: Runtime,
    ) -> (ServiceFs<ServiceObj<'static, ()>>, Receiver<Result<(), Error>>) {
        let mut fs = ServiceFs::new();
        let service_dir =
            if let Runtime::Service = runtime { fs.dir("svc") } else { fs.root_dir() };

        let settings = match self.configuration {
            Some(configuration) => HashSet::from_iter(configuration.services),
            _ => HashSet::new(),
        };

        let service_context = ServiceContext::create(self.generate_service);

        let mut handler_factory = SettingHandlerFactoryImpl::new(
            settings.clone(),
            service_context.clone(),
            self.storage_factory.clone(),
        );

        for (setting_type, handler) in self.handlers {
            handler_factory.register(setting_type, handler);
        }

        EnvironmentBuilder::get_configuration_handlers(&mut handler_factory);

        let rx = create_environment(
            service_dir,
            settings,
            self.agents,
            service_context,
            Arc::new(Mutex::new(handler_factory)),
        )
        .await;

        (fs, rx)
    }

    pub fn spawn(self, mut executor: fasync::Executor) {
        let env_future = self.prepare_env(Runtime::Service);
        let (mut fs, _) = executor.run_singlethreaded(env_future);
        fs.take_and_serve_directory_handle().expect("could not service directory handle");
        let () = executor.run_singlethreaded(fs.collect());
    }

    pub async fn spawn_nested(self, env_name: &'static str) -> Result<Environment, Error> {
        let (mut fs, rx) = self.prepare_env(Runtime::Nested(env_name)).await;
        let nested_environment = Some(fs.create_salted_nested_environment(&env_name)?);
        fasync::spawn(fs.collect());

        Ok(Environment::new(nested_environment, rx))
    }

    /// Spawns a nested environment and returns the associated
    /// NestedEnvironment. Note that this is a helper function that provides a
    /// shortcut for calling EnvironmentBuilder::name() and
    /// EnvironmentBuilder::spawn().
    pub async fn spawn_and_get_nested_environment(
        self,
        env_name: &'static str,
    ) -> Result<NestedEnvironment, Error> {
        let environment = self.spawn_nested(env_name).await?;
        // Wait for environment to be setup.
        let _ = environment.completion_rx.await??;

        if let Some(env) = environment.nested_environment {
            return Ok(env);
        }

        return Err(format_err!("nested environment not created"));
    }

    fn get_configuration_handlers(factory_handle: &mut SettingHandlerFactoryImpl<T>) {
        // Power
        register_handler!(factory_handle, SettingType::Power, spawn_power_controller);
        // Accessibility
        register_handler!(
            factory_handle,
            SettingType::Accessibility,
            spawn_accessibility_controller
        );
        // Account
        register_handler!(factory_handle, SettingType::Account, spawn_account_controller);
        // Audio
        register_handler!(factory_handle, SettingType::Audio, spawn_audio_controller);
        // Device
        register_handler!(factory_handle, SettingType::Device, spawn_device_controller);
        // Display
        register_handler!(factory_handle, SettingType::Display, spawn_display_controller);
        // Light sensor
        register_handler!(factory_handle, SettingType::LightSensor, spawn_light_sensor_controller);
        // Intl
        register_handler!(factory_handle, SettingType::Intl, IntlController::spawn);
        // Do not disturb
        register_handler!(
            factory_handle,
            SettingType::DoNotDisturb,
            spawn_do_not_disturb_controller
        );
        // Night mode
        register_handler!(factory_handle, SettingType::NightMode, NightModeController::spawn);
        // Privacy
        register_handler!(factory_handle, SettingType::Privacy, PrivacyController::spawn);
        // System
        register_handler!(factory_handle, SettingType::System, spawn_system_controller);
        // Setup
        register_handler!(factory_handle, SettingType::Setup, SetupController::spawn);
    }
}

/// Brings up the settings service environment.
///
/// This method generates the necessary infrastructure to support the settings
/// service (switchboard, registry, etc.) and brings up the components necessary
/// to support the components specified in the components HashSet.
async fn create_environment<'a, T: DeviceStorageFactory + Send + Sync + 'static>(
    mut service_dir: ServiceFsDir<'_, ServiceObj<'a, ()>>,
    components: HashSet<switchboard::base::SettingType>,
    agents: Vec<AgentHandle>,
    service_context_handle: ServiceContextHandle,
    handler_factory: Arc<Mutex<SettingHandlerFactoryImpl<T>>>,
) -> Receiver<Result<(), Error>> {
    let registry_messenger_factory = create_registry_message_hub();

    // Creates switchboard, handed to interface implementations to send messages
    // to handlers.
    let switchboard_handle = SwitchboardImpl::create(registry_messenger_factory.clone())
        .await
        .expect("could not create switchboard");

    let mut agent_authority = AuthorityImpl::new();

    // Creates registry, used to register handlers for setting types.
    let _ = RegistryImpl::create(handler_factory.clone(), registry_messenger_factory.clone())
        .await
        .expect("could not create registry");
    if components.contains(&SettingType::Accessibility) {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: AccessibilityRequestStream| {
            spawn_accessibility_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Audio) {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: AudioRequestStream| {
            spawn_audio_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Device) {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: DeviceRequestStream| {
            spawn_device_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Display) || components.contains(&SettingType::LightSensor)
    {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: DisplayRequestStream| {
            spawn_display_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::DoNotDisturb) {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: DoNotDisturbRequestStream| {
            spawn_do_not_disturb_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Intl) {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: IntlRequestStream| {
            spawn_intl_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::NightMode) {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: NightModeRequestStream| {
            spawn_night_mode_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::Privacy) {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: PrivacyRequestStream| {
            spawn_privacy_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    if components.contains(&SettingType::System) {
        {
            let switchboard_handle_clone = switchboard_handle.clone();
            service_dir.add_fidl_service(move |stream: SystemRequestStream| {
                spawn_system_fidl_handler(switchboard_handle_clone.clone(), stream);
            });
        }
        {
            let switchboard_handle_clone = switchboard_handle.clone();
            service_dir.add_fidl_service(move |stream: SetUiServiceRequestStream| {
                spawn_setui_fidl_handler(switchboard_handle_clone.clone(), stream);
            });
        }
    }

    if components.contains(&SettingType::Setup) {
        let switchboard_handle_clone = switchboard_handle.clone();
        service_dir.add_fidl_service(move |stream: SetupRequestStream| {
            spawn_setup_fidl_handler(switchboard_handle_clone.clone(), stream);
        });
    }

    let (response_tx, response_rx) = futures::channel::oneshot::channel::<Result<(), Error>>();
    let switchboard_handle_clone = switchboard_handle.clone();
    fasync::spawn(async move {
        // Register agents
        for agent in agents {
            if agent_authority.register(agent.clone()).is_err() {
                fx_log_err!("failed to register agent: {:?}", agent.clone().lock().await);
            }
        }

        // Execute initialization agents sequentially
        if let Ok(Ok(())) = agent_authority
            .execute_lifespan(
                Lifespan::Initialization,
                components.clone(),
                switchboard_handle_clone.clone(),
                service_context_handle.clone(),
                true,
            )
            .await
        {
            response_tx.send(Ok(())).ok();
        } else {
            response_tx.send(Err(format_err!("Agent initialization failed"))).ok();
        }

        // Execute service agents concurrently
        agent_authority
            .execute_lifespan(
                Lifespan::Service,
                components.clone(),
                switchboard_handle_clone.clone(),
                service_context_handle.clone(),
                false,
            )
            .await
            .ok();
    });

    return response_rx;
}

#[cfg(test)]
mod tests;
