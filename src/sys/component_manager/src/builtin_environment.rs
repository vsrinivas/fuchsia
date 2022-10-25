// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        bootfs::BootfsSvc,
        builtin::{
            arguments::Arguments as BootArguments,
            capability::BuiltinCapability,
            cpu_resource::CpuResource,
            crash_introspect::{CrashIntrospectSvc, CrashRecords},
            debug_resource::DebugResource,
            factory_items::FactoryItems,
            fuchsia_boot_resolver::{FuchsiaBootResolver, SCHEME as BOOT_SCHEME},
            hypervisor_resource::HypervisorResource,
            info_resource::InfoResource,
            ioport_resource::IoportResource,
            irq_resource::IrqResource,
            items::Items,
            kernel_stats::KernelStats,
            log::{ReadOnlyLog, WriteOnlyLog},
            mexec_resource::MexecResource,
            mmio_resource::MmioResource,
            power_resource::PowerResource,
            process_launcher::ProcessLauncher,
            realm_builder::{
                RealmBuilderResolver, RealmBuilderRunner, RUNNER_NAME as REALM_BUILDER_RUNNER_NAME,
                SCHEME as REALM_BUILDER_SCHEME,
            },
            root_job::{RootJob, ROOT_JOB_CAPABILITY_NAME, ROOT_JOB_FOR_INSPECT_CAPABILITY_NAME},
            root_resource::RootResource,
            runner::{BuiltinRunner, BuiltinRunnerFactory},
            smc_resource::SmcResource,
            svc_stash_provider::SvcStashCapability,
            system_controller::SystemController,
            time::{create_utc_clock, UtcTimeMaintainer},
            vmex_resource::VmexResource,
        },
        collection::CollectionCapabilityHost,
        diagnostics::{cpu::ComponentTreeStats, startup::ComponentEarlyStartupTimeStats},
        directory_ready_notifier::DirectoryReadyNotifier,
        elf_runner::ElfRunner,
        framework::{
            binder::BinderCapabilityHost, hub::Hub, lifecycle_controller::LifecycleController,
            pkg_dir::PkgDirectory, realm::RealmCapabilityHost, realm_explorer::RealmExplorer,
            realm_query::RealmQuery, route_validator::RouteValidator,
        },
        model::{
            component::ComponentManagerInstance,
            environment::Environment,
            error::ModelError,
            event_logger::EventLogger,
            events::{
                registry::{EventRegistry, EventSubscription},
                serve::serve_event_stream_v2_as_stream,
                source_factory::EventSourceFactory,
                stream_provider::EventStreamProvider,
            },
            hooks::EventType,
            model::{Model, ModelParams},
            resolver::{BuiltinResolver, Resolver, ResolverRegistry},
            storage::admin_protocol::StorageAdmin,
        },
        root_stop_notifier::RootStopNotifier,
    },
    ::routing::{
        config::RuntimeConfig,
        environment::{DebugRegistry, RunnerRegistry},
    },
    anyhow::{anyhow, format_err, Context as _, Error},
    cm_rust::{CapabilityName, EventMode, RunnerRegistration},
    cm_types::Url,
    fidl::{
        endpoints::{create_proxy, ServerEnd},
        AsHandleRef,
    },
    fidl_fuchsia_component_internal::BuiltinBootResolver,
    fidl_fuchsia_diagnostics_types::Task as DiagnosticsTask,
    fidl_fuchsia_io as fio, fuchsia_async as fasync,
    fuchsia_component::server::*,
    fuchsia_inspect::{self as inspect, component, health::Reporter, Inspector},
    fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType},
    fuchsia_zbi::{ZbiParser, ZbiType},
    fuchsia_zircon::{self as zx, Clock, HandleBased, Resource},
    futures::prelude::*,
    lazy_static::lazy_static,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    std::{collections::HashMap, sync::Arc},
    tracing::{info, warn},
};

// Allow shutdown to take up to an hour.
pub static SHUTDOWN_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(60 * 60);

fn take_vdso_vmos() -> Result<HashMap<String, zx::Vmo>, Error> {
    let mut vmos = HashMap::new();
    let mut i = 0;
    while let Some(handle) = take_startup_handle(HandleInfo::new(HandleType::VdsoVmo, i)) {
        let vmo = zx::Vmo::from(handle);
        let name = vmo.get_name()?.into_string()?;
        vmos.insert(name, vmo);
        i += 1;
    }
    Ok(vmos)
}

fn get_vdso_vmo(name: &str) -> Result<zx::Vmo, Error> {
    lazy_static! {
        static ref VMOS: HashMap<String, zx::Vmo> =
            take_vdso_vmos().expect("Failed to take vDSO VMOs");
    }
    if let Some(vmo) = VMOS.get(name) {
        vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)
            .map_err(|e| anyhow!("Failed to duplicate vDSO VMO {}: {}", name, e))
    } else {
        Err(anyhow!("Failed to get vDSO VMO {}", name))
    }
}

/// Returns an owned VMO handle to the stable vDSO, duplicated from the handle
/// provided to this process through its processargs bootstrap message.
pub fn get_stable_vdso_vmo() -> Result<zx::Vmo, Error> {
    get_vdso_vmo("vdso/stable")
}

/// Returns an owned VMO handle to the next vDSO, duplicated from the handle
/// provided to this process through its processargs bootstrap message.
pub fn get_next_vdso_vmo() -> Result<zx::Vmo, Error> {
    get_vdso_vmo("vdso/next")
}

/// Returns an owned VMO handle to the direct vDSO, duplicated from the handle
/// provided to this process through its processargs bootstrap message.
pub fn get_direct_vdso_vmo() -> Result<zx::Vmo, Error> {
    get_vdso_vmo("vdso/direct")
}

pub struct BuiltinEnvironmentBuilder {
    // TODO(60804): Make component manager's namespace injectable here.
    runtime_config: Option<RuntimeConfig>,
    bootfs_svc: Option<BootfsSvc>,
    runners: Vec<(CapabilityName, Arc<dyn BuiltinRunnerFactory>)>,
    resolvers: ResolverRegistry,
    utc_clock: Option<Arc<Clock>>,
    add_environment_resolvers: bool,
    inspector: Option<Inspector>,
    crash_records: CrashRecords,
}

impl Default for BuiltinEnvironmentBuilder {
    fn default() -> Self {
        Self {
            runtime_config: None,
            bootfs_svc: None,
            runners: vec![],
            resolvers: ResolverRegistry::default(),
            utc_clock: None,
            add_environment_resolvers: false,
            inspector: None,
            crash_records: CrashRecords::new(),
        }
    }
}

impl BuiltinEnvironmentBuilder {
    pub fn new() -> Self {
        BuiltinEnvironmentBuilder::default()
    }

    pub fn use_default_config(self) -> Self {
        self.set_runtime_config(RuntimeConfig::default())
    }

    pub fn set_runtime_config(mut self, runtime_config: RuntimeConfig) -> Self {
        self.runtime_config = Some(runtime_config);
        self
    }

    pub fn set_bootfs_svc(mut self, bootfs_svc: Option<BootfsSvc>) -> Self {
        self.bootfs_svc = bootfs_svc;
        self
    }

    pub fn set_inspector(mut self, inspector: Inspector) -> Self {
        self.inspector = Some(inspector);
        self
    }

    /// Create a UTC clock if required.
    /// Not every instance of component_manager running on the system maintains a
    /// UTC clock. Only the root component_manager should have the `maintain-utc-clock`
    /// config flag set.
    pub async fn create_utc_clock(mut self, bootfs: &Option<BootfsSvc>) -> Result<Self, Error> {
        let runtime_config = self
            .runtime_config
            .as_ref()
            .ok_or(format_err!("Runtime config should be set to create utc clock."))?;
        self.utc_clock = if runtime_config.maintain_utc_clock {
            Some(Arc::new(create_utc_clock(&bootfs).await.context("failed to create UTC clock")?))
        } else {
            None
        };
        Ok(self)
    }

    pub fn set_utc_clock(mut self, clock: Arc<Clock>) -> Self {
        self.utc_clock = Some(clock);
        self
    }

    pub fn add_elf_runner(self) -> Result<Self, Error> {
        let runtime_config = self
            .runtime_config
            .as_ref()
            .ok_or(format_err!("Runtime config should be set to add elf runner."))?;

        let runner = Arc::new(ElfRunner::new(
            &runtime_config,
            self.utc_clock.clone(),
            self.crash_records.clone(),
        ));
        Ok(self.add_runner("elf".into(), runner))
    }

    pub fn add_runner(
        mut self,
        name: CapabilityName,
        runner: Arc<dyn BuiltinRunnerFactory>,
    ) -> Self {
        // We don't wrap these in a BuiltinRunner immediately because that requires the
        // RuntimeConfig, which may be provided after this or may fall back to the default.
        self.runners.push((name, runner));
        self
    }

    pub fn add_resolver(
        mut self,
        scheme: String,
        resolver: Box<dyn Resolver + Send + Sync + 'static>,
    ) -> Self {
        self.resolvers.register(scheme, resolver);
        self
    }

    /// Adds standard resolvers whose dependencies are available in the process's namespace and for
    /// whose scheme no resolver is registered through `add_resolver` by the time `build()` is
    /// is called. This includes:
    ///   - A fuchsia-boot resolver if /boot is available.
    ///   - A fuchsia-pkg resolver, if /svc/fuchsia.sys.Loader is present.
    ///       - This resolver implementation proxies to that protocol (which is the v1 resolver
    ///         equivalent). This is used for tests or other scenarios where component_manager runs
    ///         as a v1 component.
    pub fn include_namespace_resolvers(mut self) -> Self {
        self.add_environment_resolvers = true;
        self
    }

    pub async fn build(mut self) -> Result<BuiltinEnvironment, Error> {
        let system_resource_handle =
            take_startup_handle(HandleType::SystemResource.into()).map(zx::Resource::from);
        if self.bootfs_svc.is_some() {
            // Set up the Rust bootfs VFS, and bind to the '/boot' namespace. This should
            // happen as early as possible when building the component manager as other objects
            // may require reading from '/boot' for configuration, etc.
            self.bootfs_svc
                .unwrap()
                .ingest_bootfs_vmo(&system_resource_handle)?
                .publish_kernel_vmo(get_stable_vdso_vmo()?)?
                .publish_kernel_vmo(get_next_vdso_vmo()?)?
                .publish_kernel_vmo(get_direct_vdso_vmo()?)?
                .publish_kernel_vmo(get_vdso_vmo("vdso/test1")?)?
                .publish_kernel_vmo(get_vdso_vmo("vdso/test2")?)?
                .publish_kernel_vmos(HandleType::KernelFileVmo, 0)?
                .create_and_bind_vfs()?;
        }

        let runtime_config = self
            .runtime_config
            .ok_or(format_err!("Runtime config is required for BuiltinEnvironment."))?;

        let root_component_url = match runtime_config.root_component_url.as_ref() {
            Some(url) => url.clone(),
            None => {
                return Err(format_err!("Root component url is required from RuntimeConfig."));
            }
        };

        let boot_resolver = if self.add_environment_resolvers {
            register_boot_resolver(&mut self.resolvers, &runtime_config).await?
        } else {
            None
        };

        let realm_builder_resolver = match runtime_config.realm_builder_resolver_and_runner {
            fidl_fuchsia_component_internal::RealmBuilderResolverAndRunner::Namespace => {
                self.runners
                    .push((REALM_BUILDER_RUNNER_NAME.into(), Arc::new(RealmBuilderRunner::new()?)));
                Some(register_realm_builder_resolver(&mut self.resolvers)?)
            }
            fidl_fuchsia_component_internal::RealmBuilderResolverAndRunner::None => None,
        };

        let runner_map = self
            .runners
            .iter()
            .map(|(name, _)| {
                (
                    name.clone(),
                    RunnerRegistration {
                        source_name: name.clone(),
                        target_name: name.clone(),
                        source: cm_rust::RegistrationSource::Self_,
                    },
                )
            })
            .collect();

        let runtime_config = Arc::new(runtime_config);
        let top_instance = Arc::new(ComponentManagerInstance::new(
            runtime_config.namespace_capabilities.clone(),
            runtime_config.builtin_capabilities.clone(),
        ));

        let params = ModelParams {
            root_component_url: root_component_url.as_str().to_owned(),
            root_environment: Environment::new_root(
                &top_instance,
                RunnerRegistry::new(runner_map),
                self.resolvers,
                DebugRegistry::default(),
            ),
            runtime_config: Arc::clone(&runtime_config),
            top_instance,
        };
        let model = Model::new(params).await?;

        // Wrap BuiltinRunnerFactory in BuiltinRunner now that we have the definite RuntimeConfig.
        let builtin_runners = self
            .runners
            .into_iter()
            .map(|(name, runner)| {
                Arc::new(BuiltinRunner::new(name, runner, Arc::downgrade(&runtime_config)))
            })
            .collect();

        Ok(BuiltinEnvironment::new(
            model,
            root_component_url,
            runtime_config,
            system_resource_handle,
            builtin_runners,
            boot_resolver,
            realm_builder_resolver,
            self.utc_clock,
            self.inspector.unwrap_or(component::inspector().clone()),
            self.crash_records,
        )
        .await?)
    }
}

/// The built-in environment consists of the set of the root services and framework services. Use
/// BuiltinEnvironmentBuilder to construct one.
///
/// The available built-in capabilities depends on the configuration provided in Arguments:
/// * If [RuntimeConfig::use_builtin_process_launcher] is true, a fuchsia.process.Launcher service
///   is available.
/// * If [RuntimeConfig::maintain_utc_clock] is true, a fuchsia.time.Maintenance service is
///   available.
pub struct BuiltinEnvironment {
    pub model: Arc<Model>,

    // Framework capabilities.
    pub boot_args: Arc<BootArguments>,
    pub cpu_resource: Option<Arc<CpuResource>>,
    pub debug_resource: Option<Arc<DebugResource>>,
    pub hypervisor_resource: Option<Arc<HypervisorResource>>,
    pub info_resource: Option<Arc<InfoResource>>,
    #[cfg(target_arch = "x86_64")]
    pub ioport_resource: Option<Arc<IoportResource>>,
    pub irq_resource: Option<Arc<IrqResource>>,
    pub kernel_stats: Option<Arc<KernelStats>>,
    pub process_launcher: Option<Arc<ProcessLauncher>>,
    pub root_job: Arc<RootJob>,
    pub root_job_for_inspect: Arc<RootJob>,
    pub read_only_log: Option<Arc<ReadOnlyLog>>,
    pub write_only_log: Option<Arc<WriteOnlyLog>>,
    pub factory_items_service: Option<Arc<FactoryItems>>,
    pub items_service: Option<Arc<Items>>,
    pub mexec_resource: Option<Arc<MexecResource>>,
    pub mmio_resource: Option<Arc<MmioResource>>,
    pub power_resource: Option<Arc<PowerResource>>,
    pub root_resource: Option<Arc<RootResource>>,
    #[cfg(target_arch = "aarch64")]
    pub smc_resource: Option<Arc<SmcResource>>,
    pub system_controller: Arc<SystemController>,
    pub utc_time_maintainer: Option<Arc<UtcTimeMaintainer>>,
    pub vmex_resource: Option<Arc<VmexResource>>,
    pub crash_records_svc: Arc<CrashIntrospectSvc>,
    pub svc_stash_provider: Option<Arc<SvcStashCapability>>,

    pub binder_capability_host: Arc<BinderCapabilityHost>,
    pub realm_capability_host: Arc<RealmCapabilityHost>,
    pub collection_capability_host: Arc<CollectionCapabilityHost>,
    pub storage_admin_capability_host: Arc<StorageAdmin>,
    pub hub: Option<Arc<Hub>>,
    pub realm_explorer: Option<Arc<RealmExplorer>>,
    pub realm_query: Option<Arc<RealmQuery>>,
    pub lifecycle_controller: Option<Arc<LifecycleController>>,
    pub route_validator: Option<Arc<RouteValidator>>,
    pub pkg_directory: Arc<PkgDirectory>,
    pub builtin_runners: Vec<Arc<BuiltinRunner>>,
    pub event_registry: Arc<EventRegistry>,
    pub event_source_factory: Arc<EventSourceFactory>,
    pub stop_notifier: Arc<RootStopNotifier>,
    pub directory_ready_notifier: Arc<DirectoryReadyNotifier>,
    pub event_stream_provider: Arc<EventStreamProvider>,
    pub event_logger: Option<Arc<EventLogger>>,
    pub component_tree_stats: Arc<ComponentTreeStats<DiagnosticsTask>>,
    pub component_startup_time_stats: Arc<ComponentEarlyStartupTimeStats>,
    pub debug: bool,
    pub num_threads: usize,
    pub inspector: Inspector,
    pub realm_builder_resolver: Option<Arc<RealmBuilderResolver>>,
    _service_fs_task: Option<fasync::Task<()>>,
}

impl BuiltinEnvironment {
    async fn new(
        model: Arc<Model>,
        root_component_url: Url,
        runtime_config: Arc<RuntimeConfig>,
        system_resource_handle: Option<Resource>,
        builtin_runners: Vec<Arc<BuiltinRunner>>,
        boot_resolver: Option<Arc<FuchsiaBootResolver>>,
        realm_builder_resolver: Option<Arc<RealmBuilderResolver>>,
        utc_clock: Option<Arc<Clock>>,
        inspector: Inspector,
        crash_records: CrashRecords,
    ) -> Result<BuiltinEnvironment, Error> {
        let debug = runtime_config.debug;

        let num_threads = runtime_config.num_threads.clone();

        let event_logger = if runtime_config.log_all_events {
            let event_logger = Arc::new(EventLogger::new());
            model.root().hooks.install(event_logger.hooks()).await;
            Some(event_logger)
        } else {
            None
        };
        // Set up ProcessLauncher if available.
        let process_launcher = if runtime_config.use_builtin_process_launcher {
            let process_launcher = Arc::new(ProcessLauncher::new());
            model.root().hooks.install(process_launcher.hooks()).await;
            Some(process_launcher)
        } else {
            None
        };

        // Set up RootJob service.
        let root_job = RootJob::new(&ROOT_JOB_CAPABILITY_NAME, zx::Rights::SAME_RIGHTS);
        model.root().hooks.install(root_job.hooks()).await;

        // Set up RootJobForInspect service.
        let root_job_for_inspect = RootJob::new(
            &ROOT_JOB_FOR_INSPECT_CAPABILITY_NAME,
            zx::Rights::INSPECT
                | zx::Rights::ENUMERATE
                | zx::Rights::DUPLICATE
                | zx::Rights::TRANSFER
                | zx::Rights::GET_PROPERTY,
        );
        model.root().hooks.install(root_job_for_inspect.hooks()).await;

        let mmio_resource_handle =
            take_startup_handle(HandleType::MmioResource.into()).map(zx::Resource::from);

        let irq_resource_handle =
            take_startup_handle(HandleType::IrqResource.into()).map(zx::Resource::from);

        let root_resource_handle =
            take_startup_handle(HandleType::Resource.into()).map(zx::Resource::from);

        let zbi_vmo_handle = take_startup_handle(HandleType::BootdataVmo.into()).map(zx::Vmo::from);
        let mut zbi_parser = match zbi_vmo_handle {
            Some(zbi_vmo) => Some(
                ZbiParser::new(zbi_vmo)
                    .set_store_item(ZbiType::Cmdline)
                    .set_store_item(ZbiType::ImageArgs)
                    .set_store_item(ZbiType::Crashlog)
                    .set_store_item(ZbiType::KernelDriver)
                    .set_store_item(ZbiType::PlatformId)
                    .set_store_item(ZbiType::StorageBootfsFactory)
                    .set_store_item(ZbiType::StorageRamdisk)
                    .set_store_item(ZbiType::SerialNumber)
                    .set_store_item(ZbiType::BootloaderFile)
                    .set_store_item(ZbiType::DeviceTree)
                    .set_store_item(ZbiType::DriverMetadata)
                    .set_store_item(ZbiType::CpuTopology)
                    .parse()?,
            ),
            None => None,
        };

        // Set up fuchsia.boot.SvcStashProvider service.
        let svc_stash_provider = take_startup_handle(HandleInfo::new(HandleType::User0, 0))
            .map(zx::Channel::from)
            .map(SvcStashCapability::new);
        if let Some(svc_stash_provider) = svc_stash_provider.as_ref() {
            model.root().hooks.install(svc_stash_provider.hooks()).await;
        }

        // Set up BootArguments service.
        let boot_args = BootArguments::new(&mut zbi_parser).await?;
        model.root().hooks.install(boot_args.hooks()).await;

        let (factory_items_service, items_service) = match zbi_parser {
            None => (None, None),
            Some(mut zbi_parser) => {
                let factory_items = FactoryItems::new(&mut zbi_parser)?;
                model.root().hooks.install(factory_items.hooks()).await;

                let items = Items::new(zbi_parser)?;
                model.root().hooks.install(items.hooks()).await;

                (Some(factory_items), Some(items))
            }
        };

        // Set up CrashRecords service.
        let crash_records_svc = CrashIntrospectSvc::new(crash_records);
        model.root().hooks.install(crash_records_svc.hooks()).await;

        // Set up KernelStats service.
        let info_resource_handle = system_resource_handle
            .as_ref()
            .map(|handle| {
                match handle.create_child(
                    zx::ResourceKind::SYSTEM,
                    None,
                    zx::sys::ZX_RSRC_SYSTEM_INFO_BASE,
                    1,
                    b"info",
                ) {
                    Ok(resource) => Some(resource),
                    Err(_) => None,
                }
            })
            .flatten();
        let kernel_stats = info_resource_handle.map(KernelStats::new);
        if let Some(kernel_stats) = kernel_stats.as_ref() {
            model.root().hooks.install(kernel_stats.hooks()).await;
        }

        // Set up ReadOnlyLog service.
        let read_only_log = root_resource_handle.as_ref().map(|handle| {
            ReadOnlyLog::new(
                handle
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .expect("Failed to duplicate root resource handle"),
            )
        });
        if let Some(read_only_log) = read_only_log.as_ref() {
            model.root().hooks.install(read_only_log.hooks()).await;
        }

        // Set up WriteOnlyLog service.
        let write_only_log = root_resource_handle.as_ref().map(|handle| {
            WriteOnlyLog::new(zx::DebugLog::create(handle, zx::DebugLogOpts::empty()).unwrap())
        });
        if let Some(write_only_log) = write_only_log.as_ref() {
            model.root().hooks.install(write_only_log.hooks()).await;
        }

        // Register the UTC time maintainer.
        let utc_time_maintainer = if let Some(clock) = utc_clock {
            let utc_time_maintainer = Arc::new(UtcTimeMaintainer::new(clock));
            model.root().hooks.install(utc_time_maintainer.hooks()).await;
            Some(utc_time_maintainer)
        } else {
            None
        };

        // Set up the MmioResource service.
        let mmio_resource = mmio_resource_handle.map(MmioResource::new);
        if let Some(mmio_resource) = mmio_resource.as_ref() {
            model.root().hooks.install(mmio_resource.hooks()).await;
        }

        let _ioport_resource: Option<Arc<IoportResource>>;
        #[cfg(target_arch = "x86_64")]
        {
            let ioport_resource_handle =
                take_startup_handle(HandleType::IoportResource.into()).map(zx::Resource::from);
            _ioport_resource = ioport_resource_handle.map(IoportResource::new);
            if let Some(_ioport_resource) = _ioport_resource.as_ref() {
                model.root().hooks.install(_ioport_resource.hooks()).await;
            }
        }

        // Set up the IrqResource service.
        let irq_resource = irq_resource_handle.map(IrqResource::new);
        if let Some(irq_resource) = irq_resource.as_ref() {
            model.root().hooks.install(irq_resource.hooks()).await;
        }

        // Set up RootResource service.
        let root_resource = root_resource_handle.map(RootResource::new);
        if let Some(root_resource) = root_resource.as_ref() {
            model.root().hooks.install(root_resource.hooks()).await;
        }

        // Set up the SMC resource.
        let _smc_resource: Option<Arc<SmcResource>>;
        #[cfg(target_arch = "aarch64")]
        {
            let smc_resource_handle =
                take_startup_handle(HandleType::SmcResource.into()).map(zx::Resource::from);
            _smc_resource = smc_resource_handle.map(SmcResource::new);
            if let Some(_smc_resource) = _smc_resource.as_ref() {
                model.root().hooks.install(_smc_resource.hooks()).await;
            }
        }

        // Set up the CpuResource service.
        let cpu_resource = system_resource_handle
            .as_ref()
            .and_then(|handle| {
                handle
                    .create_child(
                        zx::ResourceKind::SYSTEM,
                        None,
                        zx::sys::ZX_RSRC_SYSTEM_CPU_BASE,
                        1,
                        b"cpu",
                    )
                    .ok()
            })
            .map(CpuResource::new)
            .and_then(Result::ok);
        if let Some(cpu_resource) = cpu_resource.as_ref() {
            model.root().hooks.install(cpu_resource.hooks()).await;
        }

        // Set up the DebugResource service.
        let debug_resource = system_resource_handle
            .as_ref()
            .and_then(|handle| {
                handle
                    .create_child(
                        zx::ResourceKind::SYSTEM,
                        None,
                        zx::sys::ZX_RSRC_SYSTEM_DEBUG_BASE,
                        1,
                        b"debug",
                    )
                    .ok()
            })
            .map(DebugResource::new)
            .and_then(Result::ok);
        if let Some(debug_resource) = debug_resource.as_ref() {
            model.root().hooks.install(debug_resource.hooks()).await;
        }

        // Set up the HypervisorResource service.
        let hypervisor_resource = system_resource_handle
            .as_ref()
            .and_then(|handle| {
                handle
                    .create_child(
                        zx::ResourceKind::SYSTEM,
                        None,
                        zx::sys::ZX_RSRC_SYSTEM_HYPERVISOR_BASE,
                        1,
                        b"hypervisor",
                    )
                    .ok()
            })
            .map(HypervisorResource::new)
            .and_then(Result::ok);
        if let Some(hypervisor_resource) = hypervisor_resource.as_ref() {
            model.root().hooks.install(hypervisor_resource.hooks()).await;
        }

        // Set up the InfoResource service.
        let info_resource = system_resource_handle
            .as_ref()
            .and_then(|handle| {
                handle
                    .create_child(
                        zx::ResourceKind::SYSTEM,
                        None,
                        zx::sys::ZX_RSRC_SYSTEM_INFO_BASE,
                        1,
                        b"info",
                    )
                    .ok()
            })
            .map(InfoResource::new)
            .and_then(Result::ok);
        if let Some(info_resource) = info_resource.as_ref() {
            model.root().hooks.install(info_resource.hooks()).await;
        }

        // Set up the MexecResource service.
        let mexec_resource = system_resource_handle
            .as_ref()
            .and_then(|handle| {
                handle
                    .create_child(
                        zx::ResourceKind::SYSTEM,
                        None,
                        zx::sys::ZX_RSRC_SYSTEM_MEXEC_BASE,
                        1,
                        b"mexec",
                    )
                    .ok()
            })
            .map(MexecResource::new)
            .and_then(Result::ok);
        if let Some(mexec_resource) = mexec_resource.as_ref() {
            model.root().hooks.install(mexec_resource.hooks()).await;
        }

        // Set up the PowerResource service.
        let power_resource = system_resource_handle
            .as_ref()
            .and_then(|handle| {
                handle
                    .create_child(
                        zx::ResourceKind::SYSTEM,
                        None,
                        zx::sys::ZX_RSRC_SYSTEM_POWER_BASE,
                        1,
                        b"power",
                    )
                    .ok()
            })
            .map(PowerResource::new)
            .and_then(Result::ok);
        if let Some(power_resource) = power_resource.as_ref() {
            model.root().hooks.install(power_resource.hooks()).await;
        }

        // Set up the VmexResource service.
        let vmex_resource = system_resource_handle
            .as_ref()
            .and_then(|handle| {
                handle
                    .create_child(
                        zx::ResourceKind::SYSTEM,
                        None,
                        zx::sys::ZX_RSRC_SYSTEM_VMEX_BASE,
                        1,
                        b"vmex",
                    )
                    .ok()
            })
            .map(VmexResource::new)
            .and_then(Result::ok);
        if let Some(vmex_resource) = vmex_resource.as_ref() {
            model.root().hooks.install(vmex_resource.hooks()).await;
        }

        // Set up System Controller service.
        let system_controller =
            Arc::new(SystemController::new(Arc::downgrade(&model), SHUTDOWN_TIMEOUT));
        model.root().hooks.install(system_controller.hooks()).await;

        // Set up the realm service.
        let realm_capability_host =
            Arc::new(RealmCapabilityHost::new(Arc::downgrade(&model), runtime_config.clone()));
        model.root().hooks.install(realm_capability_host.hooks()).await;

        // Set up the binder service.
        let binder_capability_host = Arc::new(BinderCapabilityHost::new(Arc::downgrade(&model)));
        model.root().hooks.install(binder_capability_host.hooks()).await;

        // Set up the provider of capabilities that originate from collections.
        let collection_capability_host =
            Arc::new(CollectionCapabilityHost::new(Arc::downgrade(&model)));
        model.root().hooks.install(collection_capability_host.hooks()).await;

        // Set up the storage admin protocol
        let storage_admin_capability_host = Arc::new(StorageAdmin::new(Arc::downgrade(&model)));
        model.root().hooks.install(storage_admin_capability_host.hooks()).await;

        // Set up the builtin runners.
        for runner in &builtin_runners {
            model.root().hooks.install(runner.hooks()).await;
        }

        // Set up the boot resolver so it is routable from "above root".
        if let Some(boot_resolver) = boot_resolver {
            model.root().hooks.install(boot_resolver.hooks()).await;
        }

        // Set up the root realm stop notifier.
        let stop_notifier = Arc::new(RootStopNotifier::new());
        model.root().hooks.install(stop_notifier.hooks()).await;

        let realm_explorer = if runtime_config.enable_introspection {
            let realm_explorer = Arc::new(RealmExplorer::new(model.clone()));
            model.root().hooks.install(realm_explorer.hooks()).await;
            Some(realm_explorer)
        } else {
            None
        };

        let realm_query = if runtime_config.enable_introspection {
            let realm_query = Arc::new(RealmQuery::new(model.clone()));
            model.root().hooks.install(realm_query.hooks()).await;
            Some(realm_query)
        } else {
            None
        };

        let lifecycle_controller = if runtime_config.enable_introspection {
            let realm_control = Arc::new(LifecycleController::new(model.clone()));
            model.root().hooks.install(realm_control.hooks()).await;
            Some(realm_control)
        } else {
            None
        };

        let hub = if runtime_config.enable_introspection {
            let hub = Arc::new(Hub::new(root_component_url.as_str().to_owned())?);
            model.root().hooks.install(hub.hooks()).await;
            Some(hub)
        } else {
            None
        };

        let route_validator = if runtime_config.enable_introspection {
            let route_validator = Arc::new(RouteValidator::new(model.clone()));
            model.root().hooks.install(route_validator.hooks()).await;
            Some(route_validator)
        } else {
            None
        };

        // Set up the handler for routes involving the "pkg" directory
        let pkg_directory = Arc::new(PkgDirectory {});
        model.root().hooks.install(pkg_directory.hooks()).await;

        // Set up the Component Tree Diagnostics runtime statistics.
        let component_tree_stats =
            ComponentTreeStats::new(inspector.root().create_child("cpu_stats")).await;
        component_tree_stats.track_component_manager_stats().await;
        component_tree_stats.start_measuring().await;
        model.root().hooks.install(component_tree_stats.hooks()).await;

        let component_startup_time_stats = Arc::new(ComponentEarlyStartupTimeStats::new(
            inspector.root().create_child("early_start_times"),
            zx::Time::get_monotonic(),
        ));
        model.root().hooks.install(component_startup_time_stats.hooks()).await;

        // Serve stats about inspect in a lazy node.
        let node = inspect::stats::Node::new(&inspector, inspector.root());
        inspector.root().record(node.take());

        // Set up the directory ready notifier.
        let directory_ready_notifier =
            Arc::new(DirectoryReadyNotifier::new(Arc::downgrade(&model)));
        model.root().hooks.install(directory_ready_notifier.hooks()).await;

        // Set up the event registry.
        let event_registry = {
            let mut event_registry = EventRegistry::new(Arc::downgrade(&model));
            event_registry.register_synthesis_provider(
                EventType::DirectoryReady,
                directory_ready_notifier.clone(),
            );
            Arc::new(event_registry)
        };
        model.root().hooks.install(event_registry.hooks()).await;

        let event_stream_provider =
            Arc::new(EventStreamProvider::new(Arc::downgrade(&event_registry)));

        // Set up the event source factory.
        let event_source_factory = Arc::new(EventSourceFactory::new(
            Arc::downgrade(&event_registry),
            Arc::downgrade(&event_stream_provider),
        ));
        model.root().hooks.install(event_source_factory.hooks()).await;
        model.root().hooks.install(event_stream_provider.hooks()).await;

        Ok(BuiltinEnvironment {
            model,
            boot_args,
            process_launcher,
            root_job,
            root_job_for_inspect,
            kernel_stats,
            read_only_log,
            write_only_log,
            factory_items_service,
            items_service,
            cpu_resource,
            debug_resource,
            hypervisor_resource,
            info_resource,
            #[cfg(target_arch = "x86_64")]
            ioport_resource: _ioport_resource,
            irq_resource,
            mexec_resource,
            mmio_resource,
            power_resource,
            #[cfg(target_arch = "aarch64")]
            smc_resource: _smc_resource,
            vmex_resource,
            crash_records_svc,
            svc_stash_provider,
            root_resource,
            system_controller,
            utc_time_maintainer,
            binder_capability_host,
            realm_capability_host,
            collection_capability_host,
            storage_admin_capability_host,
            hub,
            realm_explorer,
            realm_query,
            lifecycle_controller,
            route_validator,
            pkg_directory,
            builtin_runners,
            event_registry,
            event_source_factory,
            stop_notifier,
            directory_ready_notifier,
            event_stream_provider,
            event_logger,
            component_tree_stats,
            component_startup_time_stats,
            debug,
            num_threads,
            inspector,
            realm_builder_resolver,
            _service_fs_task: None,
        })
    }

    /// Setup a ServiceFs that contains debug capabilities like the root Hub, LifecycleController,
    /// and EventSource.
    async fn create_service_fs<'a>(&self) -> Result<ServiceFs<ServiceObj<'a, ()>>, Error> {
        // Create the ServiceFs
        let mut service_fs = ServiceFs::new();

        // Install the root fuchsia.sys2.LifecycleController
        if let Some(lifecycle_controller) = &self.lifecycle_controller {
            let lifecycle_controller = lifecycle_controller.clone();
            let scope = self.model.top_instance().task_scope().clone();
            service_fs.dir("svc").add_fidl_service(move |stream| {
                let lifecycle_controller = lifecycle_controller.clone();
                let scope = scope.clone();
                // Spawn a short-lived task that adds the lifecycle controller serve to
                // component manager's task scope.
                fasync::Task::spawn(async move {
                    scope
                        .add_task(async move {
                            lifecycle_controller.serve(AbsoluteMoniker::root(), stream).await;
                        })
                        .await;
                })
                .detach();
            });
        }

        // Install the root fuchsia.sys2.RealmQuery
        if let Some(realm_query) = &self.realm_query {
            let realm_query = realm_query.clone();
            let scope = self.model.top_instance().task_scope().clone();
            service_fs.dir("svc").add_fidl_service(move |stream| {
                let realm_query = realm_query.clone();
                let scope = scope.clone();
                // Spawn a short-lived task that adds the realm query serve to
                // component manager's task scope.
                fasync::Task::spawn(async move {
                    scope
                        .add_task(async move {
                            realm_query.serve(AbsoluteMoniker::root(), stream).await;
                        })
                        .await;
                })
                .detach();
            });
        }

        // If component manager is in debug mode, create an event source scoped at the
        // root and offer it via ServiceFs to the outside world.
        if self.debug {
            let event_source = self.event_source_factory.create_for_above_root().await?;
            let event_source_v2 = self.event_source_factory.create_v2_for_above_root().await?;
            let scope = self.model.top_instance().task_scope().clone();
            service_fs.dir("svc").add_fidl_service(move |stream| {
                let event_source = event_source.clone();
                let scope = scope.clone();
                // Spawn a short-lived task that adds the EventSource serve to
                // component manager's task scope.
                fasync::Task::spawn(async move {
                    scope
                        .add_task(async move {
                            event_source.serve(stream).await;
                        })
                        .await;
                })
                .detach();
            });

            service_fs.dir("svc").add_fidl_service(move |stream| {
                let mut event_source_v2 = event_source_v2.clone();
                // Spawn a short-lived task that adds the EventSource serve to
                // component manager's task scope.
                fasync::Task::spawn(async move {
                    serve_event_stream_v2_as_stream(
                        event_source_v2
                            .subscribe(vec![
                                EventSubscription {
                                    event_name: CapabilityName::from("started"),
                                    mode: EventMode::Async,
                                },
                                EventSubscription {
                                    event_name: CapabilityName::from("stopped"),
                                    mode: EventMode::Async,
                                },
                                EventSubscription {
                                    event_name: CapabilityName::from("capability_routed"),
                                    mode: EventMode::Async,
                                },
                                EventSubscription {
                                    event_name: CapabilityName::from("running"),
                                    mode: EventMode::Async,
                                },
                                EventSubscription {
                                    event_name: CapabilityName::from("destroyed"),
                                    mode: EventMode::Async,
                                },
                                EventSubscription {
                                    event_name: CapabilityName::from("discovered"),
                                    mode: EventMode::Async,
                                },
                                EventSubscription {
                                    event_name: CapabilityName::from("resolved"),
                                    mode: EventMode::Async,
                                },
                                EventSubscription {
                                    event_name: CapabilityName::from("unresolved"),
                                    mode: EventMode::Async,
                                },
                            ])
                            .await
                            .unwrap(),
                        stream,
                    )
                    .await;
                })
                .detach();
            });
        }

        inspect_runtime::serve(component::inspector(), &mut service_fs)
            .map_err(|err| ModelError::Inspect { err })
            .unwrap_or_else(|error| {
                warn!(%error, "Failed to serve inspect");
            });

        Ok(service_fs)
    }

    /// Bind ServiceFs to a provided channel
    async fn bind_service_fs(
        &mut self,
        channel: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
    ) -> Result<(), Error> {
        let mut service_fs = self.create_service_fs().await?;

        // Bind to the channel
        service_fs
            .serve_connection(channel)
            .map_err(|err| ModelError::namespace_creation_failed(err))?;

        self.emit_diagnostics(&mut service_fs).unwrap_or_else(|error| {
            warn!(%error, "Failed to serve diagnostics");
        });

        // Start up ServiceFs
        self._service_fs_task = Some(fasync::Task::spawn(async move {
            service_fs.collect::<()>().await;
        }));
        Ok(())
    }

    /// Bind ServiceFs to the outgoing directory of this component, if it exists.
    pub async fn bind_service_fs_to_out(&mut self) -> Result<(), Error> {
        let server_end = match fuchsia_runtime::take_startup_handle(
            fuchsia_runtime::HandleType::DirectoryRequest.into(),
        ) {
            Some(handle) => fidl::endpoints::ServerEnd::new(zx::Channel::from(handle)),
            None => {
                // The component manager running on startup does not get a directory handle. If it was
                // to run as a component itself, it'd get one. When we don't have a handle to the out
                // directory, create one.
                let (_client, server) = fidl::endpoints::create_endpoints().unwrap();
                server
            }
        };
        self.bind_service_fs(server_end).await
    }

    fn emit_diagnostics<'a>(
        &self,
        service_fs: &mut ServiceFs<ServiceObj<'a, ()>>,
    ) -> Result<(), ModelError> {
        let (service_fs_proxy, service_fs_server_end) =
            create_proxy::<fio::DirectoryMarker>().unwrap();
        service_fs
            .serve_connection(service_fs_server_end)
            .map_err(|err| ModelError::namespace_creation_failed(err))?;

        let (node, server_end) = fidl::endpoints::create_proxy::<fio::NodeMarker>().unwrap();
        service_fs_proxy
            .open(
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                "diagnostics",
                ServerEnd::new(server_end.into_channel()),
            )
            .map_err(|err| ModelError::namespace_creation_failed(err))?;

        self.directory_ready_notifier.register_component_manager_capability("diagnostics", node);

        Ok(())
    }

    #[cfg(test)]
    pub(crate) fn emit_diagnostics_for_test<'a>(
        &self,
        service_fs: &mut ServiceFs<ServiceObj<'a, ()>>,
    ) -> Result<(), ModelError> {
        self.emit_diagnostics(service_fs)
    }

    pub async fn wait_for_root_stop(&self) {
        self.stop_notifier.wait_for_root_stop().await;
    }

    pub async fn run_root(&mut self) -> Result<(), Error> {
        self.bind_service_fs_to_out().await?;
        self.model.start().await;
        component::health().set_ok();
        self.wait_for_root_stop().await;

        // Stop serving the out directory, so that more connections to debug capabilities
        // cannot be made.
        drop(self._service_fs_task.take());
        Ok(())
    }
}

// Creates a FuchsiaBootResolver if the /boot directory is installed in component_manager's
// namespace, and registers it with the ResolverRegistry. The resolver is returned to so that
// it can be installed as a Builtin capability.
async fn register_boot_resolver(
    resolvers: &mut ResolverRegistry,
    runtime_config: &RuntimeConfig,
) -> Result<Option<Arc<FuchsiaBootResolver>>, Error> {
    let path = match &runtime_config.builtin_boot_resolver {
        BuiltinBootResolver::Boot => "/boot",
        BuiltinBootResolver::None => return Ok(None),
    };
    let boot_resolver =
        FuchsiaBootResolver::new(path).await.context("Failed to create boot resolver")?;
    match boot_resolver {
        None => {
            info!(%path, "fuchsia-boot resolver unavailable, not in namespace");
            Ok(None)
        }
        Some(boot_resolver) => {
            let resolver = Arc::new(boot_resolver);
            resolvers
                .register(BOOT_SCHEME.to_string(), Box::new(BuiltinResolver(resolver.clone())));
            Ok(Some(resolver))
        }
    }
}

fn register_realm_builder_resolver(
    resolvers: &mut ResolverRegistry,
) -> Result<Arc<RealmBuilderResolver>, Error> {
    let realm_builder_resolver =
        RealmBuilderResolver::new().context("Failed to create realm builder resolver")?;
    let resolver = Arc::new(realm_builder_resolver);
    resolvers
        .register(REALM_BUILDER_SCHEME.to_string(), Box::new(BuiltinResolver(resolver.clone())));
    Ok(resolver)
}
