// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        framework::RealmCapabilityHost,
        model::{
            testing::breakpoints::BreakpointSystem, ComponentManagerConfig, EventType, Hub, Model,
            ModelError, OutgoingBinder,
        },
        process_launcher::ProcessLauncher,
        runner::BuiltinRunner,
        startup::Arguments,
        system_controller::SystemController,
        vmex::VmexService,
        work_scheduler::WorkScheduler,
    },
    fidl::endpoints::{create_endpoints, create_proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::*,
    fuchsia_zircon as zx,
    futures::stream::StreamExt,
    std::sync::{Arc, Weak},
};

/// The built-in environment consists of the set of the root services and framework services.
/// The available built-in capabilities depends on the  configuration provided in Arguments:
/// * If [Arguments::use_builtin_process_launcher] is true, a fuchsia.process.Launcher service
///   is available.
/// * If [Arguments::use_builtin_vmex] is true, a fuchsia.security.resource.Vmex service is
///   available.
pub struct BuiltinEnvironment {
    pub work_scheduler: Arc<WorkScheduler>,
    pub process_launcher: Option<Arc<ProcessLauncher>>,
    pub vmex_service: Option<Arc<VmexService>>,
    pub system_controller: Arc<SystemController>,
    pub realm_capability_host: RealmCapabilityHost,
    pub hub: Hub,
    pub elf_runner: BuiltinRunner,
    pub breakpoint_system: Option<BreakpointSystem>,
}

impl BuiltinEnvironment {
    // TODO(fsamuel): Consider merging Arguments and ComponentManagerConfig.
    pub async fn new(
        args: &Arguments,
        model: &Arc<Model>,
        config: ComponentManagerConfig,
    ) -> Result<BuiltinEnvironment, ModelError> {
        // Set up ProcessLauncher if available.
        let process_launcher = if args.use_builtin_process_launcher {
            let process_launcher = Arc::new(ProcessLauncher::new());
            model.root_realm.hooks.install(process_launcher.hooks()).await;
            Some(process_launcher)
        } else {
            None
        };

        // Set up the vmex service if available.
        let vmex_service = if args.use_builtin_vmex {
            let vmex_service = Arc::new(VmexService::new());
            model.root_realm.hooks.install(vmex_service.hooks()).await;
            Some(vmex_service)
        } else {
            None
        };

        // Set up work scheduler.
        let work_scheduler =
            WorkScheduler::new(Arc::downgrade(model) as Weak<dyn OutgoingBinder>).await;
        model.root_realm.hooks.install(WorkScheduler::hooks(&work_scheduler)).await;

        // Set up system controller.
        let system_controller = Arc::new(SystemController::new());
        model.root_realm.hooks.install(system_controller.hooks()).await;

        // Set up the realm service.
        let realm_capability_host = RealmCapabilityHost::new(model.clone(), config);
        model.root_realm.hooks.install(realm_capability_host.hooks()).await;

        let hub = Hub::new(args.root_component_url.clone())?;

        // Set up the ELF runner.
        let elf_runner = BuiltinRunner::new("elf".into(), Arc::clone(&model.elf_runner));
        model.root_realm.hooks.install(vec![elf_runner.hook()]).await;

        let breakpoint_system = {
            if args.debug {
                Some(BreakpointSystem::new())
            } else {
                None
            }
        };
        Ok(BuiltinEnvironment {
            work_scheduler,
            process_launcher,
            vmex_service,
            system_controller,
            realm_capability_host,
            hub,
            elf_runner,
            breakpoint_system,
        })
    }

    /// Setup a ServiceFs that contains the Hub and (optionally) the breakpoints service
    async fn create_service_fs<'a>(
        model: &Model,
        hub: &Hub,
        breakpoint_system: &Option<BreakpointSystem>,
    ) -> Result<ServiceFs<ServiceObj<'a, ()>>, ModelError> {
        // Create the ServiceFs
        let mut service_fs = ServiceFs::new();

        // Setup the hub
        let (hub_proxy, hub_server_end) = create_proxy::<DirectoryMarker>().unwrap();
        hub.open_root(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, hub_server_end.into_channel())
            .await?;
        model.root_realm.hooks.install(hub.hooks()).await;
        service_fs.add_remote("hub", hub_proxy);

        // Setup the debug breakpoint system (optionally)
        if let Some(breakpoint_system) = breakpoint_system {
            // Install the breakpoint hooks at the root component.
            // This ensures that all events will reach this breakpoint system.
            model.root_realm.hooks.install(breakpoint_system.hooks()).await;

            // Register for RootComponentResolved event to halt the ComponentManager when the
            // root realm is first resolved.
            let root_realm_created_receiver =
                breakpoint_system.register(vec![EventType::RootComponentResolved]).await;

            // Setup a capability provider for external use. This provider is not used
            // by components within component manager, hence there is no associated hook for it.
            // Instead, it is used by external components that wish to debug component manager.
            let breakpoint_capability_provider = breakpoint_system.create_capability_provider();
            service_fs.dir("svc").add_fidl_service(move |stream| {
                breakpoint_capability_provider
                    .serve_async(stream, Some(root_realm_created_receiver.clone()))
                    .expect("failed to serve debug breakpoint capability");
            });
        }

        Ok(service_fs)
    }

    /// Bind ServiceFs to a provided channel
    async fn bind_service_fs(&self, model: &Model, channel: zx::Channel) -> Result<(), ModelError> {
        let mut service_fs =
            Self::create_service_fs(model, &self.hub, &self.breakpoint_system).await?;

        // Bind to the channel
        service_fs
            .serve_connection(channel)
            .map_err(|err| ModelError::namespace_creation_failed(err))?;

        // Start up ServiceFs
        fasync::spawn(async move {
            service_fs.collect::<()>().await;
        });
        Ok(())
    }

    /// Bind ServiceFs to the outgoing directory of this component, if it exists.
    pub async fn bind_service_fs_to_out(&self, model: &Model) -> Result<(), ModelError> {
        if let Some(handle) = fuchsia_runtime::take_startup_handle(
            fuchsia_runtime::HandleType::DirectoryRequest.into(),
        ) {
            self.bind_service_fs(model, zx::Channel::from(handle)).await?;
        }
        Ok(())
    }

    /// Bind ServiceFs to a new channel and return the Hub directory.
    /// Used mainly by integration tests.
    pub async fn bind_service_fs_for_hub(
        &self,
        model: &Model,
    ) -> Result<DirectoryProxy, ModelError> {
        // Create a channel that ServiceFs will operate on
        let (service_fs_proxy, service_fs_server_end) = create_proxy::<DirectoryMarker>().unwrap();

        self.bind_service_fs(model, service_fs_server_end.into_channel()).await?;

        // Open the Hub from within ServiceFs
        let (hub_client_end, hub_server_end) = create_endpoints::<DirectoryMarker>().unwrap();
        service_fs_proxy
            .open(
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                "hub",
                ServerEnd::new(hub_server_end.into_channel()),
            )
            .map_err(|err| ModelError::namespace_creation_failed(err))?;
        let hub_proxy = hub_client_end.into_proxy().unwrap();

        Ok(hub_proxy)
    }
}
