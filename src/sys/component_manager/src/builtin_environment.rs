// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        builtin::{
            capability::BuiltinCapability,
            log::{ReadOnlyLog, WriteOnlyLog},
            process_launcher::ProcessLauncher,
            root_job::{RootJob, ROOT_JOB_CAPABILITY_PATH, ROOT_JOB_FOR_INSPECT_CAPABILITY_PATH},
            root_resource::RootResource,
            system_controller::SystemController,
            vmex::VmexService,
        },
        framework::RealmCapabilityHost,
        model::{
            binding::Binder,
            breakpoints::core::BreakpointSystem,
            error::ModelError,
            hooks::EventType,
            hub::Hub,
            model::{ComponentManagerConfig, Model},
            moniker::AbsoluteMoniker,
        },
        root_realm_stop_notifier::RootRealmStopNotifier,
        runner::BuiltinRunner,
        startup::Arguments,
        work_scheduler::WorkScheduler,
    },
    cm_rust::CapabilityName,
    fidl::endpoints::{create_endpoints, create_proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, MODE_TYPE_DIRECTORY, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::*,
    fuchsia_runtime::{take_startup_handle, HandleType},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{lock::Mutex, stream::StreamExt},
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
    },
};

/// The built-in environment consists of the set of the root services and framework services.
/// The available built-in capabilities depends on the  configuration provided in Arguments:
/// * If [Arguments::use_builtin_process_launcher] is true, a fuchsia.process.Launcher service
///   is available.
/// * If [Arguments::use_builtin_vmex] is true, a fuchsia.security.resource.Vmex service is
///   available.
pub struct BuiltinEnvironment {
    // Framework capabilities.
    pub process_launcher: Option<Arc<ProcessLauncher>>,
    pub root_job: Arc<RootJob>,
    pub root_job_for_inspect: Arc<RootJob>,
    pub read_only_log: Option<Arc<ReadOnlyLog>>,
    pub write_only_log: Option<Arc<WriteOnlyLog>>,
    pub root_resource: Option<Arc<RootResource>>,
    pub system_controller: Arc<SystemController>,
    pub vmex_service: Option<Arc<VmexService>>,

    pub work_scheduler: Arc<WorkScheduler>,
    pub realm_capability_host: RealmCapabilityHost,
    pub hub: Hub,
    pub builtin_runners: HashMap<CapabilityName, BuiltinRunner>,
    pub breakpoint_system: Option<BreakpointSystem>,
    pub stop_notifier: Mutex<Option<RootRealmStopNotifier>>,
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

        // Set up RootJob service.
        let root_job = RootJob::new(&ROOT_JOB_CAPABILITY_PATH, zx::Rights::SAME_RIGHTS);
        model.root_realm.hooks.install(root_job.hooks()).await;

        // Set up RootJobForInspect service.
        let root_job_for_inspect = RootJob::new(
            &ROOT_JOB_FOR_INSPECT_CAPABILITY_PATH,
            zx::Rights::INSPECT
                | zx::Rights::ENUMERATE
                | zx::Rights::DUPLICATE
                | zx::Rights::TRANSFER
                | zx::Rights::GET_PROPERTY,
        );
        model.root_realm.hooks.install(root_job_for_inspect.hooks()).await;

        let root_resource_handle =
            take_startup_handle(HandleType::Resource.into()).map(zx::Resource::from);

        // Set up ReadOnlyLog service.
        let read_only_log = root_resource_handle.as_ref().map(|handle| {
            ReadOnlyLog::new(handle.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap())
        });
        if let Some(read_only_log) = read_only_log.as_ref() {
            model.root_realm.hooks.install(read_only_log.hooks()).await;
        }

        // Set up WriteOnlyLog service.
        let write_only_log = root_resource_handle.as_ref().map(|handle| {
            WriteOnlyLog::new(zx::DebugLog::create(handle, zx::DebugLogOpts::empty()).unwrap())
        });
        if let Some(write_only_log) = write_only_log.as_ref() {
            model.root_realm.hooks.install(write_only_log.hooks()).await;
        }

        // Set up RootResource service.
        let root_resource = root_resource_handle.map(RootResource::new);
        if let Some(root_resource) = root_resource.as_ref() {
            model.root_realm.hooks.install(root_resource.hooks()).await;
        }

        // Set up System Controller service.
        let system_controller = Arc::new(SystemController::new(model.clone()));
        model.root_realm.hooks.install(system_controller.hooks()).await;

        // Set up the Vmex service if available.
        let vmex_service = if args.use_builtin_vmex {
            let vmex_service = Arc::new(VmexService::new());
            model.root_realm.hooks.install(vmex_service.hooks()).await;
            Some(vmex_service)
        } else {
            None
        };

        // Set up work scheduler.
        let work_scheduler = WorkScheduler::new(Arc::downgrade(model) as Weak<dyn Binder>).await;
        model.root_realm.hooks.install(WorkScheduler::hooks(&work_scheduler)).await;

        // Set up the realm service.
        let realm_capability_host = RealmCapabilityHost::new(model.clone(), config);
        model.root_realm.hooks.install(realm_capability_host.hooks()).await;

        let hub = Hub::new(Arc::downgrade(model), args.root_component_url.clone())?;

        // Set up the builtin runners.
        let mut builtin_runners = HashMap::new();
        for (name, runner) in model.builtin_runners.iter() {
            let runner = BuiltinRunner::new(name.clone(), runner.clone());
            model.root_realm.hooks.install(vec![runner.hook()]).await;
            builtin_runners.insert(name.clone(), runner);
        }

        // Set up the root realm stop notifier.
        let stop_notifier = RootRealmStopNotifier::new();
        model.root_realm.hooks.install(stop_notifier.hooks()).await;

        let breakpoint_system = {
            if args.debug {
                Some(BreakpointSystem::new())
            } else {
                None
            }
        };

        Ok(BuiltinEnvironment {
            process_launcher,
            root_job,
            root_job_for_inspect,
            read_only_log,
            write_only_log,
            root_resource,
            system_controller,
            vmex_service,

            work_scheduler,
            realm_capability_host,
            hub,
            builtin_runners,
            breakpoint_system,
            stop_notifier: Mutex::new(Some(stop_notifier)),
        })
    }

    /// Setup a ServiceFs that contains the Hub and (optionally) the breakpoints service
    async fn create_service_fs<'a>(
        &self,
        model: &Model,
    ) -> Result<ServiceFs<ServiceObj<'a, ()>>, ModelError> {
        // Create the ServiceFs
        let mut service_fs = ServiceFs::new();

        // Setup the hub
        let (hub_proxy, hub_server_end) = create_proxy::<DirectoryMarker>().unwrap();
        self.hub
            .open_root(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, hub_server_end.into_channel())
            .await?;
        model.root_realm.hooks.install(self.hub.hooks()).await;
        service_fs.add_remote("hub", hub_proxy);

        // If component manager is in debug mode, create a breakpoint system scoped at the
        // root and offer it via ServiceFs to the outside world.
        if let Some(breakpoint_system) = &self.breakpoint_system {
            model.root_realm.hooks.install(breakpoint_system.hooks()).await;

            // Indicates whether the ScopedBreakpointSystem should set a `ResolveInstance`
            // breakpoint on the root component. This allows integration tests to set up
            // other breakpoints before starting the root component. However, this is only
            // relevant to the first requester of the breakpoint system and so this flag
            // is reset after the first request.
            let system = breakpoint_system.create_scope(AbsoluteMoniker::root());
            let mut root_instance_resolved_receiver =
                Some(system.set_breakpoints(vec![EventType::ResolveInstance]).await);

            service_fs.dir("svc").add_fidl_service(move |stream| {
                let system = system.clone();
                system.serve_async(stream, root_instance_resolved_receiver.take());
            });
        }

        Ok(service_fs)
    }

    /// Bind ServiceFs to a provided channel
    async fn bind_service_fs(&self, model: &Model, channel: zx::Channel) -> Result<(), ModelError> {
        let mut service_fs = self.create_service_fs(model).await?;

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

    pub async fn wait_for_root_realm_stop(&self) {
        let mut notifier = self.stop_notifier.lock().await;
        if let Some(notifier) = notifier.take() {
            notifier.wait_for_root_realm_stop().await;
        }
    }
}
