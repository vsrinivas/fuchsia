// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use std::collections::{HashMap, HashSet};
use std::sync::atomic::AtomicU64;
use std::sync::Arc;

use ::fidl::endpoints::{create_proxy, ServerEnd};
#[cfg(test)]
use anyhow::format_err;
use anyhow::{anyhow, Context, Error};
#[cfg(test)]
use fidl_fuchsia_io::DirectoryMarker;
use fidl_fuchsia_io::DirectoryProxy;
use fidl_fuchsia_metrics::{
    MetricEventLoggerFactoryProxy, MetricEventLoggerMarker, MetricEventLoggerProxy, ProjectSpec,
};
use fidl_fuchsia_stash::StoreProxy;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
#[cfg(test)]
use fuchsia_component::server::ProtocolConnector;
use fuchsia_component::server::{ServiceFs, ServiceFsDir, ServiceObj};
#[cfg(test)]
use fuchsia_fs::OpenFlags;
use fuchsia_inspect::component;
use fuchsia_syslog::{fx_log_info, fx_log_warn};
use fuchsia_zircon::{Duration, DurationNum};
use futures::lock::Mutex;
use futures::StreamExt;
use settings_storage::device_storage::DeviceStorage;
use settings_storage::fidl_storage::FidlStorage;

pub use audio::policy::AudioPolicyConfig;
pub use display::display_configuration::DisplayConfiguration;
pub use display::LightSensorConfig;
use handler::setting_handler::Handler;
pub use handler::setting_proxy_inspect_info::SettingProxyInspectInfo;
pub use input::input_device_configuration::InputConfiguration;
pub use light::light_hardware_configuration::LightHardwareConfiguration;
use serde::Deserialize;
pub use service::{Address, Payload, Role};
use setui_metrics_registry::{CUSTOMER_ID, PROJECT_ID};
#[cfg(test)]
use vfs::directory::entry::DirectoryEntry;
#[cfg(test)]
use vfs::directory::mutable::simple::tree_constructor;
#[cfg(test)]
use vfs::execution_scope::ExecutionScope;
#[cfg(test)]
use vfs::file::vmo::{read_write, simple_init_vmo_with_capacity};
#[cfg(test)]
use vfs::mut_pseudo_directory;

use crate::accessibility::accessibility_controller::AccessibilityController;
use crate::agent::authority::Authority;
use crate::agent::{BlueprintHandle as AgentBlueprintHandle, Lifespan};
use crate::audio::audio_controller::AudioController;
use crate::audio::policy::audio_policy_handler::AudioPolicyHandler;
use crate::base::{Dependency, Entity, SettingType};
use crate::config::base::{AgentType, ControllerFlag};
use crate::display::display_controller::{DisplayController, ExternalBrightnessControl};
use crate::display::light_sensor_controller::LightSensorController;
use crate::do_not_disturb::do_not_disturb_controller::DoNotDisturbController;
use crate::factory_reset::factory_reset_controller::FactoryResetController;
use crate::handler::base::GenerateHandler;
use crate::handler::setting_handler::persist::Handler as DataHandler;
use crate::handler::setting_handler_factory_impl::SettingHandlerFactoryImpl;
use crate::handler::setting_proxy::SettingProxy;
use crate::ingress::fidl;
use crate::ingress::registration::Registrant;
use crate::input::input_controller::InputController;
use crate::inspect::listener_logger::ListenerInspectLogger;
use crate::intl::intl_controller::IntlController;
use crate::job::manager::Manager;
use crate::job::source::Seeder;
use crate::keyboard::keyboard_controller::KeyboardController;
use crate::light::light_controller::LightController;
use crate::message::MessageHubUtil;
use crate::monitor::base as monitor_base;
use crate::night_mode::night_mode_controller::NightModeController;
use crate::policy::policy_handler;
use crate::policy::policy_handler_factory_impl::PolicyHandlerFactoryImpl;
use crate::policy::policy_proxy::PolicyProxy;
use crate::policy::PolicyType;
use crate::privacy::privacy_controller::PrivacyController;
use crate::service::message::Delegate;
use crate::service_context::GenerateService;
use crate::service_context::ServiceContext;
use crate::setup::setup_controller::SetupController;
use settings_storage::storage_factory::{FidlStorageFactory, StorageFactory};

mod accessibility;
mod audio;
mod clock;
mod display;
mod do_not_disturb;
mod event;
mod factory_reset;
mod input;
mod intl;
mod job;
mod keyboard;
mod light;
mod night_mode;
mod policy;
mod privacy;
mod service;
mod setup;
mod storage_migrations;
pub mod task;

pub mod agent;
pub mod base;
pub mod config;
pub mod fidl_common;
pub mod handler;
pub mod ingress;
pub mod inspect;
pub mod message;
pub(crate) mod migration;
pub mod monitor;
pub mod service_context;
pub mod storage;
pub mod trace;

/// This value represents the duration the proxy will wait after the last request
/// before initiating the teardown of the controller. If a request is received
/// before the timeout triggers, then the timeout will be canceled.
// The value of 5 seconds was chosen arbitrarily to allow some time between manual
// button presses that occurs for some settings.
pub(crate) const DEFAULT_TEARDOWN_TIMEOUT: Duration = Duration::from_seconds(5);
const DEFAULT_SETTING_PROXY_MAX_ATTEMPTS: u64 = 3;
const DEFAULT_SETTING_PROXY_RESPONSE_TIMEOUT_MS: i64 = 10_000;

/// A common trigger for exiting.
pub type ExitSender = futures::channel::mpsc::UnboundedSender<()>;

/// Runtime defines where the environment will exist. Service is meant for
/// production environments and will hydrate components to be discoverable as
/// an environment service. Nested creates a service only usable in the scope
/// of a test.
#[derive(PartialEq)]
enum Runtime {
    Service,
    #[cfg(test)]
    Nested(&'static str),
}

#[derive(Debug, Default, Clone, Deserialize)]
pub struct AgentConfiguration {
    pub agent_types: HashSet<AgentType>,
}

#[derive(PartialEq, Debug, Clone, Deserialize)]
pub struct EnabledInterfacesConfiguration {
    pub interfaces: HashSet<fidl::InterfaceSpec>,
}

impl EnabledInterfacesConfiguration {
    pub fn with_interfaces(interfaces: HashSet<fidl::InterfaceSpec>) -> Self {
        Self { interfaces }
    }
}

#[derive(Default, Debug, Clone, Deserialize)]
pub struct ServiceFlags {
    pub controller_flags: HashSet<ControllerFlag>,
}

#[derive(PartialEq, Debug, Default, Clone)]
pub struct ServiceConfiguration {
    agent_types: HashSet<AgentType>,
    fidl_interfaces: HashSet<fidl::Interface>,
    controller_flags: HashSet<ControllerFlag>,
}

impl ServiceConfiguration {
    pub fn from(
        agent_types: AgentConfiguration,
        interfaces: EnabledInterfacesConfiguration,
        flags: ServiceFlags,
    ) -> Self {
        let fidl_interfaces: HashSet<fidl::Interface> =
            interfaces.interfaces.into_iter().map(|x| x.into()).collect();

        Self {
            agent_types: agent_types.agent_types,
            fidl_interfaces,
            controller_flags: flags.controller_flags,
        }
    }

    fn set_fidl_interfaces(&mut self, interfaces: HashSet<fidl::Interface>) {
        self.fidl_interfaces = interfaces;

        let display_subinterfaces = [
            fidl::Interface::Display(fidl::display::InterfaceFlags::LIGHT_SENSOR),
            fidl::Interface::Display(fidl::display::InterfaceFlags::BASE),
        ]
        .into();

        // Consolidate display type.
        // TODO(fxbug.dev/82297): Remove this special handling once light sensor is its own FIDL
        // interface.
        if self.fidl_interfaces.is_superset(&display_subinterfaces) {
            self.fidl_interfaces = &self.fidl_interfaces - &display_subinterfaces;
            let inserted = self.fidl_interfaces.insert(fidl::Interface::Display(
                fidl::display::InterfaceFlags::BASE | fidl::display::InterfaceFlags::LIGHT_SENSOR,
            ));
            assert!(
                inserted,
                "Cannot insert the display interface twice. Please check the interface \
                configuration"
            );
        }
    }

    fn set_controller_flags(&mut self, controller_flags: HashSet<ControllerFlag>) {
        self.controller_flags = controller_flags;
    }
}

/// Environment is handed back when an environment is spawned from the
/// EnvironmentBuilder. A nested environment (if available) is returned,
/// along with a receiver to be notified when initialization/setup is
/// complete.
#[cfg(test)]
pub struct Environment {
    pub connector: Option<ProtocolConnector>,
    pub delegate: Delegate,
    pub entities: HashSet<Entity>,
    pub job_seeder: Seeder,
}

#[cfg(test)]
impl Environment {
    pub fn new(
        connector: Option<ProtocolConnector>,
        delegate: Delegate,
        job_seeder: Seeder,
        entities: HashSet<Entity>,
    ) -> Environment {
        Environment { connector, delegate, job_seeder, entities }
    }
}

#[cfg(test)]
fn init_storage_dir() -> DirectoryProxy {
    let fs_scope = ExecutionScope::build()
        .entry_constructor(tree_constructor(move |_, _| {
            Ok(read_write(simple_init_vmo_with_capacity(b"", 100)))
        }))
        .new();
    let (directory, server) = create_proxy::<DirectoryMarker>().unwrap();
    mut_pseudo_directory! {}.open(
        fs_scope,
        OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
        0,
        vfs::path::Path::dot(),
        ServerEnd::new(server.into_channel()),
    );
    directory
}

#[cfg(not(test))]
fn init_storage_dir() -> DirectoryProxy {
    panic!("migration dir must be specified");
}

/// The [EnvironmentBuilder] aggregates the parameters surrounding an [environment](Environment) and
/// ultimately spawns an environment based on them.
pub struct EnvironmentBuilder<T: StorageFactory<Storage = DeviceStorage> + Send + Sync + 'static> {
    configuration: Option<ServiceConfiguration>,
    agent_blueprints: Vec<AgentBlueprintHandle>,
    agent_mapping_func: Option<Box<dyn Fn(AgentType) -> AgentBlueprintHandle>>,
    event_subscriber_blueprints: Vec<event::subscriber::BlueprintHandle>,
    storage_factory: Arc<T>,
    generate_service: Option<GenerateService>,
    registrants: Vec<Registrant>,
    settings: Vec<SettingType>,
    handlers: HashMap<SettingType, GenerateHandler>,
    resource_monitors: Vec<monitor_base::monitor::Generate>,
    setting_proxy_inspect_info: Option<&'static fuchsia_inspect::Node>,
    active_listener_inspect_logger: Option<Arc<Mutex<ListenerInspectLogger>>>,
    storage_dir: Option<DirectoryProxy>,
    store_proxy: Option<StoreProxy>,
    fidl_storage_factory: Option<Arc<FidlStorageFactory>>,
    metric_event_logger_factory_proxy: Option<MetricEventLoggerFactoryProxy>,
}

impl<T: StorageFactory<Storage = DeviceStorage> + Send + Sync + 'static> EnvironmentBuilder<T> {
    /// Construct a new [EnvironmentBuilder] using `storage_factory` to construct the storage for
    /// the future [Environment].
    pub fn new(storage_factory: Arc<T>) -> EnvironmentBuilder<T> {
        EnvironmentBuilder {
            configuration: None,
            agent_blueprints: vec![],
            agent_mapping_func: None,
            event_subscriber_blueprints: vec![],
            storage_factory,
            generate_service: None,
            handlers: HashMap::new(),
            registrants: vec![],
            settings: vec![],
            resource_monitors: vec![],
            setting_proxy_inspect_info: None,
            active_listener_inspect_logger: None,
            storage_dir: None,
            store_proxy: None,
            fidl_storage_factory: None,
            metric_event_logger_factory_proxy: None,
        }
    }

    /// Overrides the default [GenerateHandler] for a specific [SettingType].
    pub fn handler(
        mut self,
        setting_type: SettingType,
        generate_handler: GenerateHandler,
    ) -> EnvironmentBuilder<T> {
        // Ignore the old handler result.
        let _ = self.handlers.insert(setting_type, generate_handler);
        self
    }

    /// A service generator to be used as an overlay on the ServiceContext.
    pub fn service(mut self, generate_service: GenerateService) -> EnvironmentBuilder<T> {
        self.generate_service = Some(generate_service);
        self
    }

    /// A preset configuration to load preset parameters as a base. Note that this will override
    /// any configuration modifications made by [EnvironmentBuilder::fidl_interface],
    /// [EnvironmentBuilder::policies], and [EnvironmentBuilder::flags].
    pub fn configuration(mut self, configuration: ServiceConfiguration) -> EnvironmentBuilder<T> {
        self.configuration = Some(configuration);
        self
    }

    /// Will override all fidl interfaces in the [ServiceConfiguration].
    pub fn fidl_interfaces(mut self, interfaces: &[fidl::Interface]) -> EnvironmentBuilder<T> {
        if self.configuration.is_none() {
            self.configuration = Some(ServiceConfiguration::default());
        }

        if let Some(c) = self.configuration.as_mut() {
            c.set_fidl_interfaces(interfaces.iter().copied().collect());
        }

        self
    }

    /// Appends the [Registrant]s to the list of registrants already configured.
    pub fn registrants(mut self, mut registrants: Vec<Registrant>) -> EnvironmentBuilder<T> {
        self.registrants.append(&mut registrants);

        self
    }

    /// Setting types to participate.
    pub fn settings(mut self, settings: &[SettingType]) -> EnvironmentBuilder<T> {
        self.settings.extend(settings);

        self
    }

    /// Setting types to participate with customized controllers.
    pub fn flags(mut self, controller_flags: &[ControllerFlag]) -> EnvironmentBuilder<T> {
        if self.configuration.is_none() {
            self.configuration = Some(ServiceConfiguration::default());
        }

        if let Some(c) = self.configuration.as_mut() {
            c.set_controller_flags(controller_flags.iter().copied().collect());
        }

        self
    }

    /// Sets the mapping function used to generate an [AgentBlueprintHandle] from an [AgentType].
    pub fn agent_mapping<F>(mut self, agent_mapping_func: F) -> EnvironmentBuilder<T>
    where
        F: Fn(AgentType) -> AgentBlueprintHandle + 'static,
    {
        self.agent_mapping_func = Some(Box::new(agent_mapping_func));
        self
    }

    /// Appends the supplied [AgentBlueprintHandle]s to the list of agent blueprints.
    pub fn agents(mut self, blueprints: &[AgentBlueprintHandle]) -> EnvironmentBuilder<T> {
        self.agent_blueprints.append(&mut blueprints.to_vec());
        self
    }

    /// Adds the specified monitors to the list of resource monitors.
    pub fn resource_monitors(
        mut self,
        monitors: &[monitor_base::monitor::Generate],
    ) -> EnvironmentBuilder<T> {
        self.resource_monitors.append(&mut monitors.to_vec());
        self
    }

    /// Event subscribers to participate
    pub fn event_subscribers(
        mut self,
        subscribers: &[event::subscriber::BlueprintHandle],
    ) -> EnvironmentBuilder<T> {
        self.event_subscriber_blueprints.append(&mut subscribers.to_vec());
        self
    }

    /// Sets the inspect node for setting proxy inspect information and any required
    /// inspect loggers.
    pub fn setting_proxy_inspect_info(
        mut self,
        setting_proxy_inspect_info: &'static fuchsia_inspect::Node,
        active_listener_inspect_logger: Arc<Mutex<ListenerInspectLogger>>,
    ) -> EnvironmentBuilder<T> {
        self.setting_proxy_inspect_info = Some(setting_proxy_inspect_info);
        self.active_listener_inspect_logger = Some(active_listener_inspect_logger);
        self
    }

    pub fn storage_dir(mut self, storage_dir: DirectoryProxy) -> Self {
        self.storage_dir = Some(storage_dir);
        self
    }

    pub fn store_proxy(mut self, store_proxy: StoreProxy) -> Self {
        self.store_proxy = Some(store_proxy);
        self
    }

    pub fn fidl_storage_factory(mut self, fidl_storage_factory: Arc<FidlStorageFactory>) -> Self {
        self.fidl_storage_factory = Some(fidl_storage_factory);
        self
    }

    pub fn metric_event_logger_factory_proxy(
        mut self,
        metric_event_logger_factory_proxy: Option<MetricEventLoggerFactoryProxy>,
    ) -> Self {
        self.metric_event_logger_factory_proxy = metric_event_logger_factory_proxy;
        self
    }

    /// Prepares an environment so that it may be spawned. This ensures that all necessary
    /// components are spawned and ready to handle events and FIDL requests.
    async fn prepare_env(
        mut self,
        runtime: Runtime,
    ) -> Result<(ServiceFs<ServiceObj<'static, ()>>, Delegate, Seeder, HashSet<Entity>), Error>
    {
        let mut fs = ServiceFs::new();

        let service_dir = if runtime == Runtime::Service {
            // Initialize inspect.
            if let Err(e) = inspect_runtime::serve(component::inspector(), &mut fs) {
                fx_log_warn!("Unable to serve inspect runtime: {:?}", e);
            }

            fs.dir("svc")
        } else {
            fs.root_dir()
        };

        // Define top level MessageHub for service communication.
        let delegate = service::MessageHub::create_hub();

        let (agent_types, fidl_interfaces, flags) = match self.configuration {
            Some(configuration) => (
                configuration.agent_types,
                configuration.fidl_interfaces,
                configuration.controller_flags,
            ),
            _ => (HashSet::new(), HashSet::new(), HashSet::new()),
        };

        self.registrants.extend(fidl_interfaces.into_iter().map(|x| x.registrant()));

        let mut settings = HashSet::new();
        settings.extend(self.settings);

        let mut policies = HashSet::new();

        for registrant in &self.registrants {
            for dependency in registrant.get_dependencies() {
                match dependency {
                    Dependency::Entity(Entity::Handler(setting_type)) => {
                        let _ = settings.insert(*setting_type);
                    }
                    Dependency::Entity(Entity::PolicyHandler(policy_type)) => {
                        let _ = policies.insert(*policy_type);
                    }
                }
            }
        }

        let fidl_storage_factory = if let Some(factory) = self.fidl_storage_factory {
            factory
        } else {
            let (migration_id, storage_dir) = if let Some(storage_dir) = self.storage_dir {
                let metric_event_logger_proxy =
                    if let Some(factory_proxy) = self.metric_event_logger_factory_proxy {
                        get_metric_event_logger_proxy(factory_proxy)
                            .await
                            .map_err(|e| {
                                fx_log_warn!("Unable to connect to metric event logger: {e:?}")
                            })
                            .ok()
                    } else {
                        None
                    };

                let store_proxy = self.store_proxy.unwrap_or_else(|| {
                    let store_proxy = connect_to_protocol::<fidl_fuchsia_stash::StoreMarker>()
                        .expect("failed to connect to stash");
                    store_proxy
                        .identify("setting_service")
                        .expect("should be able to identify to stash");
                    store_proxy
                });

                let migration_manager = storage_migrations::register_migrations(
                    &settings,
                    &policies,
                    Clone::clone(&storage_dir),
                    store_proxy,
                )
                .context("failed to register migrations")?;
                let migration_id = migration_manager
                    .run_tracked_migrations(metric_event_logger_proxy)
                    .await
                    .context("migrations failed")?;
                fx_log_info!("migrated storage to {migration_id:?}");
                (migration_id, storage_dir)
            } else {
                (None, init_storage_dir())
            };

            Arc::new(FidlStorageFactory::new(migration_id.unwrap_or(0), storage_dir))
        };

        let service_context =
            Arc::new(ServiceContext::new(self.generate_service, Some(delegate.clone())));

        let context_id_counter = Arc::new(AtomicU64::new(1));

        let mut handler_factory = SettingHandlerFactoryImpl::new(
            settings.clone(),
            Arc::clone(&service_context),
            context_id_counter.clone(),
        );

        // Create the policy handler factory and register policy handlers.
        let mut policy_handler_factory = PolicyHandlerFactoryImpl::new(
            policies.clone(),
            settings.clone(),
            self.storage_factory.clone(),
            context_id_counter,
        );

        EnvironmentBuilder::register_policy_handlers(
            &policies,
            Arc::clone(&self.storage_factory),
            &mut policy_handler_factory,
        )
        .await;

        EnvironmentBuilder::register_setting_handlers(
            &settings,
            Arc::clone(&self.storage_factory),
            Arc::clone(&fidl_storage_factory),
            &flags,
            &mut handler_factory,
        )
        .await;

        // Override the configuration handlers with any custom handlers specified
        // in the environment.
        for (setting_type, handler) in self.handlers {
            handler_factory.register(setting_type, handler);
        }

        let agent_blueprints = self
            .agent_mapping_func
            .map(|agent_mapping_func| agent_types.into_iter().map(agent_mapping_func).collect())
            .unwrap_or(self.agent_blueprints);

        let job_manager_signature = Manager::spawn(&delegate).await;
        let job_seeder = Seeder::new(&delegate, job_manager_signature).await;

        let entities = create_environment(
            service_dir,
            delegate.clone(),
            job_seeder.clone(),
            settings,
            self.registrants,
            policies,
            agent_blueprints,
            self.resource_monitors,
            self.event_subscriber_blueprints,
            service_context,
            Arc::new(Mutex::new(handler_factory)),
            Arc::new(Mutex::new(policy_handler_factory)),
            self.storage_factory,
            fidl_storage_factory,
            self.setting_proxy_inspect_info.unwrap_or_else(|| component::inspector().root()),
            self.active_listener_inspect_logger
                .unwrap_or_else(|| Arc::new(Mutex::new(ListenerInspectLogger::new()))),
        )
        .await
        .context("Could not create environment")?;

        Ok((fs, delegate, job_seeder, entities))
    }

    /// Spawn an [Environment] on the supplied [fasync::LocalExecutor] so that it may process
    /// incoming FIDL requests.
    pub fn spawn(self, mut executor: fasync::LocalExecutor) -> Result<(), Error> {
        let (mut fs, ..) = executor
            .run_singlethreaded(self.prepare_env(Runtime::Service))
            .context("Failed to prepare env")?;
        let _ = fs.take_and_serve_directory_handle().expect("could not service directory handle");
        executor.run_singlethreaded(fs.collect::<()>());
        Ok(())
    }

    /// Spawn a nested [Environment] so that it can be used for tests.
    #[cfg(test)]
    pub async fn spawn_nested(self, env_name: &'static str) -> Result<Environment, Error> {
        let (mut fs, delegate, job_seeder, entities) =
            self.prepare_env(Runtime::Nested(env_name)).await.context("Failed to prepare env")?;
        let connector = Some(fs.create_protocol_connector()?);
        fasync::Task::spawn(fs.collect()).detach();

        Ok(Environment::new(connector, delegate, job_seeder, entities))
    }

    /// Spawns a nested environment and returns the associated
    /// ProtocolConnector. Note that this is a helper function that provides a
    /// shortcut for calling EnvironmentBuilder::name() and
    /// EnvironmentBuilder::spawn().
    #[cfg(test)]
    pub async fn spawn_and_get_protocol_connector(
        self,
        env_name: &'static str,
    ) -> Result<ProtocolConnector, Error> {
        let environment = self.spawn_nested(env_name).await?;

        environment.connector.ok_or_else(|| format_err!("connector not created"))
    }

    /// Initializes storage and registers handler generation functions for the configured policy
    /// types.
    async fn register_policy_handlers(
        policies: &HashSet<PolicyType>,
        storage_factory: Arc<T>,
        policy_handler_factory: &mut PolicyHandlerFactoryImpl<T>,
    ) {
        // Audio
        if policies.contains(&PolicyType::Audio) {
            storage_factory
                .initialize::<AudioPolicyHandler>()
                .await
                .expect("storage should still be initializing");
            policy_handler_factory.register(
                PolicyType::Audio,
                Box::new(policy_handler::create_handler::<AudioPolicyHandler, _>),
            );
        }
    }

    /// Initializes storage and registers handler generation functions for the configured setting
    /// types.
    async fn register_setting_handlers<F>(
        components: &HashSet<SettingType>,
        device_storage_factory: Arc<T>,
        fidl_storage_factory: Arc<F>,
        controller_flags: &HashSet<ControllerFlag>,
        factory_handle: &mut SettingHandlerFactoryImpl,
    ) where
        F: StorageFactory<Storage = FidlStorage>,
    {
        // Accessibility
        if components.contains(&SettingType::Accessibility) {
            device_storage_factory
                .initialize::<AccessibilityController>()
                .await
                .expect("storage should still be initializing");
            factory_handle.register(
                SettingType::Accessibility,
                Box::new(DataHandler::<AccessibilityController>::spawn),
            );
        }

        // Audio
        if components.contains(&SettingType::Audio) {
            device_storage_factory
                .initialize::<AudioController>()
                .await
                .expect("storage should still be initializing");
            factory_handle
                .register(SettingType::Audio, Box::new(DataHandler::<AudioController>::spawn));
        }

        // Display
        if components.contains(&SettingType::Display) {
            device_storage_factory
                .initialize::<DisplayController>()
                .await
                .expect("storage should still be initializing");
            factory_handle.register(
                SettingType::Display,
                Box::new(
                    if controller_flags.contains(&ControllerFlag::ExternalBrightnessControl) {
                        DataHandler::<DisplayController<ExternalBrightnessControl>>::spawn
                    } else {
                        DataHandler::<DisplayController>::spawn
                    },
                ),
            );
        }

        // Light
        if components.contains(&SettingType::Light) {
            fidl_storage_factory
                .initialize::<LightController>()
                .await
                .expect("storage should still be initializing");
            factory_handle
                .register(SettingType::Light, Box::new(DataHandler::<LightController>::spawn));
        }

        // Light sensor
        if components.contains(&SettingType::LightSensor) {
            device_storage_factory
                .initialize::<LightSensorController>()
                .await
                .expect("storage should still be initializing");
            factory_handle.register(
                SettingType::LightSensor,
                Box::new(Handler::<LightSensorController>::spawn),
            );
        }

        // Input
        if components.contains(&SettingType::Input) {
            device_storage_factory
                .initialize::<InputController>()
                .await
                .expect("storage should still be initializing");
            factory_handle
                .register(SettingType::Input, Box::new(DataHandler::<InputController>::spawn));
        }

        // Intl
        if components.contains(&SettingType::Intl) {
            device_storage_factory
                .initialize::<IntlController>()
                .await
                .expect("storage should still be initializing");
            factory_handle
                .register(SettingType::Intl, Box::new(DataHandler::<IntlController>::spawn));
        }

        // Keyboard
        if components.contains(&SettingType::Keyboard) {
            device_storage_factory
                .initialize::<KeyboardController>()
                .await
                .expect("storage should still be initializing");
            factory_handle.register(
                SettingType::Keyboard,
                Box::new(DataHandler::<KeyboardController>::spawn),
            );
        }

        // Do not disturb
        if components.contains(&SettingType::DoNotDisturb) {
            device_storage_factory
                .initialize::<DoNotDisturbController>()
                .await
                .expect("storage should still be initializing");
            factory_handle.register(
                SettingType::DoNotDisturb,
                Box::new(DataHandler::<DoNotDisturbController>::spawn),
            );
        }

        // Factory Reset
        if components.contains(&SettingType::FactoryReset) {
            device_storage_factory
                .initialize::<FactoryResetController>()
                .await
                .expect("storage should still be initializing");
            factory_handle.register(
                SettingType::FactoryReset,
                Box::new(DataHandler::<FactoryResetController>::spawn),
            );
        }

        // Night mode
        if components.contains(&SettingType::NightMode) {
            device_storage_factory
                .initialize::<NightModeController>()
                .await
                .expect("storage should still be initializing");
            factory_handle.register(
                SettingType::NightMode,
                Box::new(DataHandler::<NightModeController>::spawn),
            );
        }

        // Privacy
        if components.contains(&SettingType::Privacy) {
            device_storage_factory
                .initialize::<PrivacyController>()
                .await
                .expect("storage should still be initializing");
            factory_handle
                .register(SettingType::Privacy, Box::new(DataHandler::<PrivacyController>::spawn));
        }

        // Setup
        if components.contains(&SettingType::Setup) {
            device_storage_factory
                .initialize::<SetupController>()
                .await
                .expect("storage should still be initializing");
            factory_handle
                .register(SettingType::Setup, Box::new(DataHandler::<SetupController>::spawn));
        }
    }
}

async fn get_metric_event_logger_proxy(
    factory_proxy: MetricEventLoggerFactoryProxy,
) -> Result<MetricEventLoggerProxy, Error> {
    let (metric_event_logger_proxy, server_end) = create_proxy::<MetricEventLoggerMarker>()
        .context("failed to create metric event logger proxy")?;

    factory_proxy
        .create_metric_event_logger(
            ProjectSpec {
                customer_id: Some(CUSTOMER_ID),
                project_id: Some(PROJECT_ID),
                ..ProjectSpec::EMPTY
            },
            ServerEnd::new(server_end.into_channel()),
        )
        .await
        .context("failed fidl call")?
        .map_err(|e| anyhow!("creating metric event logger: {:?}", e))?;
    Ok(metric_event_logger_proxy)
}

/// Brings up the settings service environment.
///
/// This method generates the necessary infrastructure to support the settings
/// service (handlers, agents, etc.) and brings up the components necessary to
/// support the components specified in the components HashSet.
#[allow(clippy::too_many_arguments)]
async fn create_environment<'a, T, F>(
    mut service_dir: ServiceFsDir<'_, ServiceObj<'a, ()>>,
    delegate: service::message::Delegate,
    job_seeder: Seeder,
    components: HashSet<SettingType>,
    registrants: Vec<Registrant>,
    policies: HashSet<PolicyType>,
    agent_blueprints: Vec<AgentBlueprintHandle>,
    resource_monitor_generators: Vec<monitor_base::monitor::Generate>,
    event_subscriber_blueprints: Vec<event::subscriber::BlueprintHandle>,
    service_context: Arc<ServiceContext>,
    handler_factory: Arc<Mutex<SettingHandlerFactoryImpl>>,
    policy_handler_factory: Arc<Mutex<PolicyHandlerFactoryImpl<T>>>,
    device_storage_factory: Arc<T>,
    fidl_storage_factory: Arc<F>,
    setting_proxies_node: &'static fuchsia_inspect::Node,
    listener_logger: Arc<Mutex<ListenerInspectLogger>>,
) -> Result<HashSet<Entity>, Error>
where
    T: StorageFactory<Storage = DeviceStorage> + Send + Sync + 'static,
    F: StorageFactory<Storage = FidlStorage> + Send + Sync + 'static,
{
    for blueprint in event_subscriber_blueprints {
        blueprint.create(delegate.clone()).await;
    }

    let monitor_actor = if resource_monitor_generators.is_empty() {
        None
    } else {
        Some(monitor::environment::Builder::new().add_monitors(resource_monitor_generators).build())
    };

    let mut entities = HashSet::new();

    for setting_type in &components {
        let _ = SettingProxy::create(
            *setting_type,
            handler_factory.clone(),
            delegate.clone(),
            DEFAULT_SETTING_PROXY_MAX_ATTEMPTS,
            DEFAULT_TEARDOWN_TIMEOUT,
            Some(DEFAULT_SETTING_PROXY_RESPONSE_TIMEOUT_MS.millis()),
            true,
            setting_proxies_node.create_child(format!("{:?}", setting_type)),
            listener_logger.clone(),
        )
        .await?;

        let _ = entities.insert(Entity::Handler(*setting_type));
    }

    for policy_type in &policies {
        let setting_type = policy_type.setting_type();
        if components.contains(&setting_type) {
            PolicyProxy::create(*policy_type, policy_handler_factory.clone(), delegate.clone())
                .await?;
            let _ = entities.insert(Entity::PolicyHandler(*policy_type));
        }
    }

    let mut agent_authority =
        Authority::create(delegate.clone(), components.clone(), policies, monitor_actor).await?;

    // TODO(fxbug.dev/96251) Handle registrants with missing dependencies.
    for registrant in registrants {
        if registrant.get_dependencies().iter().all(|dependency| dependency.is_fulfilled(&entities))
        {
            registrant.register(&delegate, &job_seeder, &mut service_dir);
        }
    }

    // The service does not work without storage, so ensure it is always included first.
    agent_authority
        .register(Arc::new(crate::agent::storage_agent::Blueprint::new(
            device_storage_factory,
            fidl_storage_factory,
        )))
        .await;

    for blueprint in agent_blueprints {
        agent_authority.register(blueprint).await;
    }

    // Execute initialization agents sequentially
    agent_authority
        .execute_lifespan(Lifespan::Initialization, Arc::clone(&service_context), true)
        .await
        .context("Agent initialization failed")?;

    // Execute service agents concurrently
    agent_authority
        .execute_lifespan(Lifespan::Service, Arc::clone(&service_context), false)
        .await
        .context("Agent service start failed")?;

    Ok(entities)
}

#[cfg(test)]
mod tests;
