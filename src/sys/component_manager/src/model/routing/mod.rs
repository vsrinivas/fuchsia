// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod error;
pub use error::OpenResourceError;
pub use error::RoutingError;

#[macro_use]
mod router;

use {
    crate::{
        capability::{
            CapabilityProvider, CapabilitySource, ComponentCapability, InternalCapability,
        },
        channel,
        model::{
            component::{BindReason, ComponentInstance, WeakComponentInstance},
            environment::DebugRegistration,
            error::ModelError,
            events::{filter::EventFilter, mode_set::EventModeSet},
            hooks::{Event, EventPayload},
            logging::{FmtArgsLogger, LOGGER as MODEL_LOGGER},
            rights::{Rights, READ_RIGHTS, WRITE_RIGHTS},
            routing::router::{
                AllowedSourcesBuilder, CapabilityVisitor, ErrorNotFoundFromParent,
                ErrorNotFoundInChild, ExposeVisitor, OfferVisitor, RoutingStrategy,
            },
            storage,
            walk_state::WalkState,
        },
        path::PathBufExt,
    },
    ::routing::component_instance::ComponentInstanceInterface,
    async_trait::async_trait,
    cm_rust::{
        self, CapabilityName, CapabilityPath, DirectoryDecl, ExposeDecl, ExposeDirectoryDecl,
        ExposeProtocolDecl, ExposeResolverDecl, ExposeRunnerDecl, ExposeSource, OfferDirectoryDecl,
        OfferEventDecl, OfferProtocolDecl, OfferResolverDecl, OfferRunnerDecl, OfferSource,
        OfferStorageDecl, ProtocolDecl, RegistrationDeclCommon, RegistrationSource, ResolverDecl,
        ResolverRegistration, RunnerDecl, RunnerRegistration, SourceName, StorageDecl,
        StorageDirectorySource, UseDecl, UseDirectoryDecl, UseEventDecl, UseProtocolDecl,
        UseSource, UseStorageDecl,
    },
    fidl::{endpoints::ServerEnd, epitaph::ChannelEpitaphExt},
    fidl_fuchsia_io as fio, fidl_fuchsia_io2 as fio2, fuchsia_zircon as zx,
    futures::lock::Mutex,
    log::*,
    moniker::{AbsoluteMoniker, ExtendedMoniker, PartialMoniker, RelativeMoniker},
    std::{
        path::{Path, PathBuf},
        sync::Arc,
    },
};

const SERVICE_OPEN_FLAGS: u32 =
    fio::OPEN_FLAG_DESCRIBE | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;

/// Routes a capability from `target` to its source, starting from a `use_decl`.
///
/// If the capability is allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], the capability is then opened at its source
/// triggering a `CapabilityRouted` event.
///
/// See [`fidl_fuchsia_io::Directory::Open`] for how the `flags`, `open_mode`, `relative_path`,
/// and `server_chan` parameters are used in the open call.
///
/// Only capabilities that can be installed in a namespace are supported: Protocol, Service,
/// Directory, and Storage.
pub(super) async fn route_and_open_namespace_capability(
    flags: u32,
    open_mode: u32,
    relative_path: String,
    use_decl: UseDecl,
    target: &Arc<ComponentInstance>,
    server_chan: &mut zx::Channel,
) -> Result<(), ModelError> {
    match use_decl {
        UseDecl::Protocol(use_protocol_decl) => {
            let source = route_protocol(use_protocol_decl, target).await?;
            open_capability_at_source(flags, open_mode, PathBuf::new(), source, target, server_chan)
                .await
        }
        UseDecl::Directory(use_directory_decl) => {
            let (source, directory_state) = route_directory(use_directory_decl, target).await?;
            open_capability_at_source(
                flags,
                open_mode,
                directory_state.make_relative_path(relative_path),
                source,
                target,
                server_chan,
            )
            .await
        }
        UseDecl::Storage(use_storage_decl) => {
            // TODO(fxbug.dev/50716): This BindReason is wrong. We need to refactor the Storage
            // capability to plumb through the correct BindReason.
            route_and_open_storage_capability(
                use_storage_decl,
                open_mode,
                target,
                server_chan,
                &BindReason::Eager,
            )
            .await
        }
        UseDecl::Service(_) => Err(ModelError::unsupported("service routing")),
        UseDecl::Event(_) | UseDecl::EventStream(_) => {
            // These capabilities are not representable on a VFS.
            Err(ModelError::unsupported(
                "opening a capability that cannot be installed in a namespace",
            ))
        }
    }
}

/// Routes a capability from `target` to its source, starting from an `expose_decl`.
///
/// If the capability is allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], the capability is then opened at its source
/// triggering a `CapabilityRouted` event.
///
/// See [`fidl_fuchsia_io::Directory::Open`] for how the `flags`, `open_mode`, `relative_path`,
/// and `server_chan` parameters are used in the open call.
///
/// Only capabilities that can both be opened from a VFS and be exposed to their parent
/// are supported: Protocol, Service, and Directory.
pub(super) async fn route_and_open_namespace_capability_from_expose(
    flags: u32,
    open_mode: u32,
    relative_path: String,
    expose_decl: ExposeDecl,
    target: &Arc<ComponentInstance>,
    server_chan: &mut zx::Channel,
) -> Result<(), ModelError> {
    let (capability_source, relative_path) = match expose_decl {
        ExposeDecl::Service(_) => panic!("not supported yet"),
        ExposeDecl::Protocol(expose_protocol_decl) => {
            (route_protocol_from_expose(expose_protocol_decl, target).await?, PathBuf::new())
        }
        ExposeDecl::Directory(expose_directory_decl) => {
            let (capability_source, directory_state) =
                route_directory_from_expose(expose_directory_decl, target).await?;
            (capability_source, directory_state.make_relative_path(relative_path))
        }
        ExposeDecl::Runner(_) | ExposeDecl::Resolver(_) => {
            return Err(ModelError::unsupported(
                "opening a capability that cannot be installed in a namespace",
            ))
        }
    };
    open_capability_at_source(
        flags,
        open_mode,
        relative_path,
        capability_source,
        target,
        server_chan,
    )
    .await
}

/// The default provider for a ComponentCapability.
/// This provider will bind to the source component instance and open the capability `name` at
/// `path` under the source component's outgoing namespace.
struct DefaultComponentCapabilityProvider {
    target: WeakComponentInstance,
    source: WeakComponentInstance,
    name: CapabilityName,
    path: CapabilityPath,
}

#[async_trait]
impl CapabilityProvider for DefaultComponentCapabilityProvider {
    async fn open(
        self: Box<Self>,
        flags: u32,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let capability = Arc::new(Mutex::new(Some(channel::take_channel(server_end))));
        // Start the source component, if necessary
        let path = self.path.to_path_buf().attach(relative_path);
        let source = self
            .source
            .upgrade()?
            .bind(&BindReason::AccessCapability {
                target: ExtendedMoniker::ComponentInstance(self.target.moniker.clone()),
                path: self.path.clone(),
            })
            .await?;

        let event = Event::new(
            &self.target.upgrade()?,
            Ok(EventPayload::CapabilityRequested {
                source_moniker: source.abs_moniker.clone(),
                name: self.name.to_string(),
                capability: capability.clone(),
            }),
        );
        source.hooks.dispatch(&event).await?;

        // If the capability transported through the event above wasn't transferred
        // out, then we can open the capability through the component's outgoing directory.
        // If some hook consumes the capability, then we don't bother looking in the outgoing
        // directory.
        let capability = capability.lock().await.take();
        if let Some(mut server_end_for_event) = capability {
            if let Err(e) =
                source.open_outgoing(flags, open_mode, path, &mut server_end_for_event).await
            {
                // Pass back the channel to propagate the epitaph.
                *server_end = channel::take_channel(&mut server_end_for_event);
                return Err(e);
            }
        }
        Ok(())
    }
}

/// Returns an instance of the default capability provider for the capability at `source`, if supported.
fn get_default_provider(
    target: WeakComponentInstance,
    source: &CapabilitySource,
) -> Option<Box<dyn CapabilityProvider>> {
    match source {
        CapabilitySource::Component { capability, component } => {
            // Route normally for a component capability with a source path
            match capability.source_path() {
                Some(path) => Some(Box::new(DefaultComponentCapabilityProvider {
                    target,
                    source: component.clone(),
                    name: capability
                        .source_name()
                        .expect("capability with source path should have a name")
                        .clone(),
                    path: path.clone(),
                })),
                _ => None,
            }
        }
        CapabilitySource::Framework { .. }
        | CapabilitySource::Capability { .. }
        | CapabilitySource::Builtin { .. }
        | CapabilitySource::Namespace { .. } => {
            // There is no default provider for a framework or builtin capability
            None
        }
    }
}

/// Opens the capability at `source`, triggering a `CapabilityRouted` event and binding
/// to the source component instance if necessary.
///
/// If the capability is not allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], the capability is not opened and an error
/// is returned.
///
/// See [`fidl_fuchsia_io::Directory::Open`] for how the `flags`, `open_mode`, `relative_path`,
/// and `server_chan` parameters are used in the open call.
pub async fn open_capability_at_source(
    flags: u32,
    open_mode: u32,
    relative_path: PathBuf,
    source: CapabilitySource,
    target: &Arc<ComponentInstance>,
    server_chan: &mut zx::Channel,
) -> Result<(), ModelError> {
    target.try_get_context()?.policy().can_route_capability(&source, &target.abs_moniker)?;

    let capability_provider = Arc::new(Mutex::new(get_default_provider(target.as_weak(), &source)));

    let event = Event::new(
        &target,
        Ok(EventPayload::CapabilityRouted {
            source: source.clone(),
            capability_provider: capability_provider.clone(),
        }),
    );
    // Get a capability provider from the tree
    target.hooks.dispatch(&event).await?;

    // This hack changes the flags for a scoped framework service
    let mut flags = flags;
    if let CapabilitySource::Framework { .. } = source {
        flags = SERVICE_OPEN_FLAGS;
    }

    let capability_provider = capability_provider.lock().await.take();

    // If a hook in the component tree gave a capability provider, then use it.
    if let Some(capability_provider) = capability_provider {
        capability_provider.open(flags, open_mode, relative_path, server_chan).await?;
        Ok(())
    } else {
        // TODO(fsamuel): This is a temporary hack. If a global path-based framework capability
        // is not provided by a hook in the component tree, then attempt to connect to the service
        // in component manager's namespace. We could have modeled this as a default provider,
        // but several hooks (such as WorkScheduler) require that a provider is not set.
        let namespace_path = match &source {
            CapabilitySource::Component { .. } => {
                unreachable!(
                    "Capability source is a component, which should have been caught by \
                    default_capability_provider: {:?}",
                    source
                );
            }
            CapabilitySource::Framework { capability, scope_moniker: m } => {
                return Err(RoutingError::capability_from_framework_not_found(
                    &m,
                    capability.source_name().to_string(),
                )
                .into());
            }
            CapabilitySource::Capability { source_capability, component } => {
                return Err(RoutingError::capability_from_capability_not_found(
                    &component.moniker,
                    source_capability.to_string(),
                )
                .into());
            }
            CapabilitySource::Builtin { capability } => {
                return Err(ModelError::from(
                    RoutingError::capability_from_component_manager_not_found(
                        capability.source_name().to_string(),
                    ),
                ));
            }
            CapabilitySource::Namespace { capability } => match capability.source_path() {
                Some(p) => p.clone(),
                _ => {
                    return Err(ModelError::from(
                        RoutingError::capability_from_component_manager_not_found(
                            capability.source_id(),
                        ),
                    ));
                }
            },
        };
        let namespace_path = namespace_path.to_path_buf().attach(relative_path);
        let namespace_path = namespace_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(namespace_path.clone()))?;
        let server_chan = channel::take_channel(server_chan);
        io_util::connect_in_namespace(namespace_path, server_chan, flags).map_err(|e| {
            OpenResourceError::open_component_manager_namespace_failed(namespace_path, e).into()
        })
    }
}

/// Sets an epitaph on `server_end` for a capability routing failure, and logs the error. Logs a
/// failure to route a capability. Formats `err` as a `String`, but elides the type if the error is
/// a `RoutingError`, the common case.
pub(super) fn report_routing_failure(
    target_moniker: &AbsoluteMoniker,
    cap: &ComponentCapability,
    err: &ModelError,
    server_end: zx::Channel,
    logger: Option<&dyn FmtArgsLogger>,
) {
    let _ = server_end.close_with_epitaph(err.as_zx_status());
    let err_str = match err {
        ModelError::RoutingError { err } => format!("{}", err),
        _ => format!("{}", err),
    };
    let log_msg = format!(
        "Failed to route {} `{}` with target component `{}`: {}",
        cap.type_name(),
        cap.source_id(),
        target_moniker,
        err_str
    );
    if let Some(l) = logger {
        l.log(Level::Error, format_args!("{}", log_msg));
    } else {
        MODEL_LOGGER.log(Level::Error, format_args!("{}", log_msg))
    }
}

make_noop_visitor!(ProtocolVisitor, {
    OfferDecl => OfferProtocolDecl,
    ExposeDecl => ExposeProtocolDecl,
    CapabilityDecl => ProtocolDecl,
});

impl SourceName for DebugRegistration {
    fn source_name(&self) -> &CapabilityName {
        &self.source_name
    }
}

impl RegistrationDeclCommon for DebugRegistration {
    const TYPE: &'static str = "protocol";

    fn source(&self) -> &RegistrationSource {
        &self.source
    }
}

/// Routes a Protocol capability from `target` to its source, starting from `use_decl`.
pub async fn route_protocol(
    use_decl: UseProtocolDecl,
    target: &Arc<ComponentInstance>,
) -> Result<CapabilitySource, ModelError> {
    let allowed_sources = AllowedSourcesBuilder::new()
        .framework(InternalCapability::Protocol)
        .builtin(InternalCapability::Protocol)
        .namespace()
        .component()
        .capability();
    if let UseSource::Debug = use_decl.source {
        // Find the component instance in which the debug capability was registered with the environment.
        let (env_component_instance, env_name, registration_decl) =
            match target.environment.get_debug_capability(&use_decl.source_name)? {
                Some((Some(env_component_instance), env_name, reg)) => {
                    (env_component_instance, env_name, reg)
                }
                Some((None, _, _)) => {
                    // Root environment.
                    return Err(RoutingError::UseFromRootEnvironmentNotAllowed {
                        moniker: target.abs_moniker.clone(),
                        capability_name: use_decl.source_name.clone(),
                        capability_type: DebugRegistration::TYPE,
                    }
                    .into());
                }
                None => {
                    return Err(RoutingError::UseFromEnvironmentNotFound {
                        moniker: target.abs_moniker.clone(),
                        capability_name: use_decl.source_name.clone(),
                        capability_type: DebugRegistration::TYPE,
                    }
                    .into());
                }
            };
        let env_name = env_name.expect(&format!(
            "Environment name in component `{}` not found when routing `{}`.",
            target.abs_moniker, use_decl.source_name
        ));

        let env_moniker = env_component_instance.abs_moniker.clone();

        let source = RoutingStrategy::new()
            .registration::<DebugRegistration>()
            .offer::<OfferProtocolDecl>()
            .expose::<ExposeProtocolDecl>()
            .route(
                registration_decl,
                env_component_instance.clone(),
                allowed_sources,
                &mut ProtocolVisitor,
            )
            .await?;

        target.try_get_context()?.policy().can_route_debug_capability(
            &source,
            &env_moniker,
            &env_name,
            &target.abs_moniker,
        )?;
        return Ok(source);
    } else {
        RoutingStrategy::new()
            .use_::<UseProtocolDecl>()
            .offer::<OfferProtocolDecl>()
            .expose::<ExposeProtocolDecl>()
            .route(use_decl, target.clone(), allowed_sources, &mut ProtocolVisitor)
            .await
    }
}

/// Routes a Protocol capability from `target` to its source, starting from `expose_decl`.
pub async fn route_protocol_from_expose(
    expose_decl: ExposeProtocolDecl,
    target: &Arc<ComponentInstance>,
) -> Result<CapabilitySource, ModelError> {
    let allowed_sources = AllowedSourcesBuilder::new()
        .framework(InternalCapability::Protocol)
        .builtin(InternalCapability::Protocol)
        .namespace()
        .component()
        .capability();
    RoutingStrategy::new()
        .use_::<UseProtocolDecl>()
        .offer::<OfferProtocolDecl>()
        .expose::<ExposeProtocolDecl>()
        .route_from_expose(expose_decl, target.clone(), allowed_sources, &mut ProtocolVisitor)
        .await
}

/// The accumulated state of routing a Directory capability.
pub struct DirectoryState {
    rights: WalkState<Rights>,
    subdir: PathBuf,
}

impl DirectoryState {
    fn new(operations: fio2::Operations, subdir: Option<PathBuf>) -> Self {
        DirectoryState {
            rights: WalkState::at(operations.into()),
            subdir: subdir.unwrap_or_else(PathBuf::new),
        }
    }

    /// Returns a new path with `in_relative_path` appended to the end of this
    /// DirectoryState's accumulated subdirectory path.
    pub fn make_relative_path(&self, in_relative_path: String) -> PathBuf {
        self.subdir.clone().attach(in_relative_path)
    }

    fn advance(
        &mut self,
        rights: Option<fio2::Operations>,
        subdir: Option<PathBuf>,
    ) -> Result<(), ModelError> {
        self.rights = self.rights.advance(rights.map(Rights::from))?;
        let subdir = subdir.clone().unwrap_or_else(PathBuf::new);
        self.subdir = subdir.attach(&self.subdir);
        Ok(())
    }

    fn finalize(
        &mut self,
        rights: fio2::Operations,
        subdir: Option<PathBuf>,
    ) -> Result<(), ModelError> {
        self.rights = self.rights.finalize(Some(rights.into()))?;
        let subdir = subdir.clone().unwrap_or_else(PathBuf::new);
        self.subdir = subdir.attach(&self.subdir);
        Ok(())
    }
}

impl OfferVisitor for DirectoryState {
    type OfferDecl = OfferDirectoryDecl;

    fn visit(&mut self, offer: &OfferDirectoryDecl) -> Result<(), ModelError> {
        match offer.source {
            OfferSource::Framework => self.finalize(*READ_RIGHTS, offer.subdir.clone()),
            _ => self.advance(offer.rights.clone(), offer.subdir.clone()),
        }
    }
}

impl ExposeVisitor for DirectoryState {
    type ExposeDecl = ExposeDirectoryDecl;

    fn visit(&mut self, expose: &ExposeDirectoryDecl) -> Result<(), ModelError> {
        match expose.source {
            ExposeSource::Framework => self.finalize(*READ_RIGHTS, expose.subdir.clone()),
            _ => self.advance(expose.rights.clone(), expose.subdir.clone()),
        }
    }
}

impl CapabilityVisitor for DirectoryState {
    type CapabilityDecl = DirectoryDecl;

    fn visit(&mut self, capability_decl: &DirectoryDecl) -> Result<(), ModelError> {
        self.finalize(capability_decl.rights.clone(), None)
    }
}

/// Routes a Directory capability from `target` to its source, starting from `use_decl`.
/// Returns the capability source, along with a `DirectoryState` accumulated from traversing
/// the route.
pub async fn route_directory(
    use_decl: UseDirectoryDecl,
    target: &Arc<ComponentInstance>,
) -> Result<(CapabilitySource, DirectoryState), ModelError> {
    let mut state = DirectoryState::new(use_decl.rights.clone(), use_decl.subdir.clone());
    if let UseSource::Framework = &use_decl.source {
        state.finalize(*READ_RIGHTS, None)?;
    }
    let allowed_sources = AllowedSourcesBuilder::new()
        .framework(InternalCapability::Directory)
        .builtin(InternalCapability::Directory)
        .namespace()
        .component();
    let source = RoutingStrategy::new()
        .use_::<UseDirectoryDecl>()
        .offer::<OfferDirectoryDecl>()
        .expose::<ExposeDirectoryDecl>()
        .route(use_decl, target.clone(), allowed_sources, &mut state)
        .await?;
    Ok((source, state))
}

/// Routes a Directory capability from `target` to its source, starting from `expose_decl`.
/// Returns the capability source, along with a `DirectoryState` accumulated from traversing
/// the route.
pub async fn route_directory_from_expose(
    expose_decl: ExposeDirectoryDecl,
    target: &Arc<ComponentInstance>,
) -> Result<(CapabilitySource, DirectoryState), ModelError> {
    let mut state = DirectoryState { rights: WalkState::new(), subdir: PathBuf::new() };
    let allowed_sources = AllowedSourcesBuilder::new()
        .framework(InternalCapability::Directory)
        .builtin(InternalCapability::Directory)
        .namespace()
        .component();
    let source = RoutingStrategy::new()
        .use_::<UseDirectoryDecl>()
        .offer::<OfferDirectoryDecl>()
        .expose::<ExposeDirectoryDecl>()
        .route_from_expose(expose_decl, target.clone(), allowed_sources, &mut state)
        .await?;
    Ok((source, state))
}

/// Routes a storage capability from `target` to its source and opens its backing directory
/// capability, binding to the component instance if necessary.
///
/// If the capability is not allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], the capability is not opened and an error
/// is returned.
///
/// See [`fidl_fuchsia_io::Directory::Open`] for how the `flags`, `open_mode`, `relative_path`,
/// and `server_chan` parameters are used in the open call.
pub async fn route_and_open_storage_capability(
    use_decl: UseStorageDecl,
    open_mode: u32,
    target: &Arc<ComponentInstance>,
    server_chan: &mut zx::Channel,
    bind_reason: &BindReason,
) -> Result<(), ModelError> {
    let (storage_source_info, relative_moniker) = route_storage(use_decl, target).await?;
    let dir_source = storage_source_info.storage_provider.clone();
    let relative_moniker_2 = relative_moniker.clone();
    let storage_dir_proxy = storage::open_isolated_storage(
        storage_source_info,
        relative_moniker,
        target.instance_id().as_ref(),
        open_mode,
        bind_reason,
    )
    .await
    .map_err(|e| ModelError::from(e))?;

    // clone the final connection to connect the channel we're routing to its destination
    let server_chan = channel::take_channel(server_chan);
    storage_dir_proxy.clone(fio::CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(server_chan)).map_err(
        |e| {
            let moniker = match &dir_source {
                Some(r) => ExtendedMoniker::ComponentInstance(r.abs_moniker.clone()),
                None => ExtendedMoniker::ComponentManager,
            };
            ModelError::from(OpenResourceError::open_storage_failed(
                &moniker,
                &relative_moniker_2,
                "",
                e,
            ))
        },
    )?;
    Ok(())
}

/// Routes a storage capability from `target` to its source and deletes its isolated storage.
pub(super) async fn route_and_delete_storage(
    use_decl: UseStorageDecl,
    target: &Arc<ComponentInstance>,
) -> Result<(), ModelError> {
    let (storage_source_info, relative_moniker) = route_storage(use_decl, target).await?;
    storage::delete_isolated_storage(
        storage_source_info,
        relative_moniker,
        target.instance_id().as_ref(),
    )
    .await
}

make_noop_visitor!(StorageVisitor, {
    OfferDecl => OfferStorageDecl,
    CapabilityDecl => StorageDecl,
});

/// Routes a Storage capability from `target` to its source, starting from `use_decl`.
/// The backing Directory capability is then routed to its source.
async fn route_storage(
    use_decl: UseStorageDecl,
    target: &Arc<ComponentInstance>,
) -> Result<(storage::StorageCapabilitySource, RelativeMoniker), ModelError> {
    let allowed_sources = AllowedSourcesBuilder::new().component();
    let storage_source = RoutingStrategy::new()
        .use_::<UseStorageDecl>()
        .offer::<OfferStorageDecl>()
        .route(use_decl, target.clone(), allowed_sources, &mut StorageVisitor)
        .await?;
    target
        .try_get_context()?
        .policy()
        .can_route_capability(&storage_source, &target.abs_moniker)?;
    let (storage_decl, storage_component_instance) = match storage_source {
        CapabilitySource::Component {
            capability: ComponentCapability::Storage(storage_decl),
            component,
        } => (storage_decl, component.upgrade()?),
        _ => unreachable!("unexpected storage source"),
    };
    let relative_moniker = RelativeMoniker::from_absolute(
        &storage_component_instance.abs_moniker,
        &target.abs_moniker,
    );

    // The storage capability was routed to its source. Now route the backing directory capability.
    Ok((
        route_storage_backing_directory(storage_decl, storage_component_instance).await?,
        relative_moniker,
    ))
}

/// Intermediate type to masquerade as Registration-style routing start point for the storage
/// backing directory capability.
struct StorageDeclAsRegistration {
    source: RegistrationSource,
    name: CapabilityName,
}

impl From<StorageDecl> for StorageDeclAsRegistration {
    fn from(decl: StorageDecl) -> Self {
        Self {
            name: decl.backing_dir,
            source: match decl.source {
                StorageDirectorySource::Parent => RegistrationSource::Parent,
                StorageDirectorySource::Self_ => RegistrationSource::Self_,
                StorageDirectorySource::Child(child) => RegistrationSource::Child(child),
            },
        }
    }
}

impl SourceName for StorageDeclAsRegistration {
    fn source_name(&self) -> &CapabilityName {
        &self.name
    }
}

impl RegistrationDeclCommon for StorageDeclAsRegistration {
    const TYPE: &'static str = "storage";

    fn source(&self) -> &RegistrationSource {
        &self.source
    }
}

/// Routes the backing Directory capability of a Storage capability from `target` to its source,
/// starting from `storage_decl`.
pub async fn route_storage_backing_directory(
    storage_decl: StorageDecl,
    target: Arc<ComponentInstance>,
) -> Result<storage::StorageCapabilitySource, ModelError> {
    // Storage rights are always READ+WRITE.
    let mut state = DirectoryState::new(*READ_RIGHTS | *WRITE_RIGHTS, None);
    let allowed_sources = AllowedSourcesBuilder::new().component().namespace();
    let source = RoutingStrategy::new()
        .registration::<StorageDeclAsRegistration>()
        .offer::<OfferDirectoryDecl>()
        .expose::<ExposeDirectoryDecl>()
        .route(storage_decl.clone().into(), target, allowed_sources, &mut state)
        .await?;

    let (dir_source_path, dir_source_instance) = match source {
        CapabilitySource::Component { capability, component } => (
            capability.source_path().expect("directory has no source path?").clone(),
            Some(component.upgrade()?),
        ),
        CapabilitySource::Namespace { capability } => {
            (capability.source_path().expect("directory has no source path?").clone(), None)
        }
        _ => unreachable!("not valid sources"),
    };

    let dir_subdir = if state.subdir == Path::new("") { None } else { Some(state.subdir) };

    Ok(storage::StorageCapabilitySource {
        storage_provider: dir_source_instance,
        backing_directory_path: dir_source_path,
        backing_directory_subdir: dir_subdir,
        storage_subdir: storage_decl.subdir.clone(),
    })
}

make_noop_visitor!(RunnerVisitor, {
    OfferDecl => OfferRunnerDecl,
    ExposeDecl => ExposeRunnerDecl,
    CapabilityDecl => RunnerDecl,
});

/// Finds a Runner capability that matches `runner` in the `target`'s environment, and then
/// routes the Runner capability from the environment's component instance to its source.
pub async fn route_runner(
    runner: &CapabilityName,
    target: &Arc<ComponentInstance>,
) -> Result<CapabilitySource, ModelError> {
    // Find the component instance in which the runner was registered with the environment.
    let (env_component_instance, registration_decl) =
        match target.environment.get_registered_runner(&runner)? {
            Some((Some(env_component_instance), registration_decl)) => {
                (env_component_instance, registration_decl)
            }
            Some((None, reg)) => {
                // Root environment.
                return Ok(CapabilitySource::Builtin {
                    capability: InternalCapability::Runner(reg.source_name.clone()),
                });
            }
            None => {
                return Err(RoutingError::UseFromEnvironmentNotFound {
                    moniker: target.abs_moniker.clone(),
                    capability_name: runner.clone(),
                    capability_type: "runner",
                }
                .into());
            }
        };

    let allowed_sources =
        AllowedSourcesBuilder::new().builtin(InternalCapability::Runner).component();
    RoutingStrategy::new()
        .registration::<RunnerRegistration>()
        .offer::<OfferRunnerDecl>()
        .expose::<ExposeRunnerDecl>()
        .route(registration_decl, env_component_instance, allowed_sources, &mut RunnerVisitor)
        .await
}

make_noop_visitor!(ResolverVisitor, {
    OfferDecl => OfferResolverDecl,
    ExposeDecl => ExposeResolverDecl,
    CapabilityDecl => ResolverDecl,
});

/// Routes a Resolver capability from `target` to its source, starting from `registration_decl`.
pub async fn route_resolver(
    registration: ResolverRegistration,
    target: &Arc<ComponentInstance>,
) -> Result<CapabilitySource, ModelError> {
    let allowed_sources =
        AllowedSourcesBuilder::new().builtin(InternalCapability::Resolver).component();
    RoutingStrategy::new()
        .registration::<ResolverRegistration>()
        .offer::<OfferResolverDecl>()
        .expose::<ExposeResolverDecl>()
        .route(registration, target.clone(), allowed_sources, &mut ResolverVisitor)
        .await
}

/// State accumulated from routing an Event capability to its source.
struct EventState {
    filter_state: WalkState<EventFilter>,
    modes_state: WalkState<EventModeSet>,
}

impl OfferVisitor for EventState {
    type OfferDecl = OfferEventDecl;

    fn visit(&mut self, offer: &OfferEventDecl) -> Result<(), ModelError> {
        let event_filter = Some(EventFilter::new(offer.filter.clone()));
        let modes = Some(EventModeSet::new(offer.mode.clone()));
        match &offer.source {
            OfferSource::Parent => {
                self.filter_state = self.filter_state.advance(event_filter)?;
                self.modes_state = self.modes_state.advance(modes)?;
            }
            OfferSource::Framework => {
                self.filter_state = self.filter_state.finalize(event_filter)?;
                self.modes_state = self.modes_state.finalize(modes)?;
            }
            _ => unreachable!("no other valid sources"),
        }
        Ok(())
    }
}

impl CapabilityVisitor for EventState {
    type CapabilityDecl = ();
}

/// Routes an Event capability from `target` to its source, starting from `use_decl`.
pub async fn route_event(
    use_decl: UseEventDecl,
    target: &Arc<ComponentInstance>,
) -> Result<CapabilitySource, ModelError> {
    let mut state = EventState {
        filter_state: WalkState::at(EventFilter::new(use_decl.filter.clone())),
        modes_state: WalkState::at(EventModeSet::new(use_decl.mode.clone())),
    };

    let allowed_sources = AllowedSourcesBuilder::<()>::new()
        .framework(InternalCapability::Event)
        .builtin(InternalCapability::Event);
    let source = RoutingStrategy::new()
        .use_::<UseEventDecl>()
        .offer::<OfferEventDecl>()
        .route(use_decl, target.clone(), allowed_sources, &mut state)
        .await?;
    target.try_get_context()?.policy().can_route_capability(&source, &target.abs_moniker)?;
    Ok(source)
}

// Error trait impls

impl ErrorNotFoundFromParent for UseProtocolDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for DebugRegistration {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromParentNotFound {
            moniker,
            capability_name: capability_name,
            capability_type: DebugRegistration::TYPE,
        }
    }
}

impl ErrorNotFoundInChild for DebugRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name: capability_name,
            capability_type: DebugRegistration::TYPE,
        }
    }
}

impl ErrorNotFoundFromParent for OfferProtocolDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferProtocolDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeProtocolDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseDirectoryDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferDirectoryDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferDirectoryDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeDirectoryDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseStorageDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferStorageDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for StorageDeclAsRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::StorageFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for StorageDeclAsRegistration {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::StorageFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for RunnerRegistration {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromEnvironmentNotFound {
            moniker,
            capability_name,
            capability_type: "runner",
        }
    }
}

impl ErrorNotFoundInChild for RunnerRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name,
            capability_type: "runner",
        }
    }
}

impl ErrorNotFoundFromParent for OfferRunnerDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferRunnerDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeRunnerDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for ResolverRegistration {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromParentNotFound {
            moniker,
            capability_name,
            capability_type: "resolver",
        }
    }
}

impl ErrorNotFoundInChild for ResolverRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name,
            capability_type: "resolver",
        }
    }
}

impl ErrorNotFoundFromParent for OfferResolverDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferResolverDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeResolverDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: PartialMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseEventDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferEventDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}
