// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod capability_source;
pub mod collection;
pub mod component_id_index;
pub mod component_instance;
pub mod config;
pub mod environment;
pub mod error;
pub mod event;
pub mod path;
pub mod policy;
pub mod rights;
pub mod router;
pub mod walk_state;

use {
    crate::{
        capability_source::{
            CapabilitySourceInterface, ComponentCapability, InternalCapability,
            StorageCapabilitySource,
        },
        component_instance::{ComponentInstanceInterface, ExtendedInstanceInterface},
        environment::DebugRegistration,
        error::RoutingError,
        event::{EventFilter, EventModeSet},
        path::PathBufExt,
        rights::{Rights, READ_RIGHTS, WRITE_RIGHTS},
        router::{
            AllowedSourcesBuilder, CapabilityVisitor, ErrorNotFoundFromParent,
            ErrorNotFoundInChild, ExposeVisitor, OfferVisitor, RoutingStrategy,
        },
        walk_state::WalkState,
    },
    cm_rust::{
        CapabilityName, DirectoryDecl, EventDecl, ExposeDirectoryDecl, ExposeProtocolDecl,
        ExposeResolverDecl, ExposeRunnerDecl, ExposeServiceDecl, ExposeSource, OfferDirectoryDecl,
        OfferEventDecl, OfferProtocolDecl, OfferResolverDecl, OfferRunnerDecl, OfferServiceDecl,
        OfferSource, OfferStorageDecl, ProtocolDecl, RegistrationDeclCommon, RegistrationSource,
        ResolverDecl, ResolverRegistration, RunnerDecl, RunnerRegistration, ServiceDecl,
        SourceName, StorageDecl, StorageDirectorySource, UseDirectoryDecl, UseEventDecl,
        UseProtocolDecl, UseServiceDecl, UseSource, UseStorageDecl,
    },
    fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
    moniker::{
        AbsoluteMonikerBase, PartialAbsoluteMoniker, PartialChildMoniker, RelativeMoniker,
        RelativeMonikerBase,
    },
    std::{
        path::{Path, PathBuf},
        sync::Arc,
    },
};

/// A request to route a capability, together with the data needed to do so.
pub enum RouteRequest {
    // Route a capability from an ExposeDecl.
    ExposeDirectory(ExposeDirectoryDecl),
    ExposeProtocol(ExposeProtocolDecl),
    ExposeService(ExposeServiceDecl),

    // Route a capability from a realm's environment.
    Resolver(ResolverRegistration),
    Runner(CapabilityName),

    // Route the directory capability that backs a storage capability.
    StorageBackingDirectory(StorageDecl),

    // Route a capability from a UseDecl.
    UseDirectory(UseDirectoryDecl),
    UseEvent(UseEventDecl),
    UseProtocol(UseProtocolDecl),
    UseService(UseServiceDecl),
    UseStorage(UseStorageDecl),
}

/// The data returned after successfully routing a capability to its source.
#[derive(Debug)]
pub enum RouteSource<C: ComponentInstanceInterface> {
    Directory(CapabilitySourceInterface<C>, DirectoryState),
    Event(CapabilitySourceInterface<C>),
    Protocol(CapabilitySourceInterface<C>),
    Resolver(CapabilitySourceInterface<C>),
    Runner(CapabilitySourceInterface<C>),
    Service(CapabilitySourceInterface<C>),
    Storage(CapabilitySourceInterface<C>),
    StorageBackingDirectory(StorageCapabilitySource<C>),
}

/// Routes a capability to its source.
///
/// If the capability is not allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], then an error is returned.
pub async fn route_capability<C>(
    request: RouteRequest,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    match request {
        // Route from an ExposeDecl
        RouteRequest::ExposeDirectory(expose_directory_decl) => {
            route_directory_from_expose(expose_directory_decl, target).await
        }
        RouteRequest::ExposeProtocol(expose_protocol_decl) => {
            route_protocol_from_expose(expose_protocol_decl, target).await
        }
        RouteRequest::ExposeService(expose_service_decl) => {
            route_service_from_expose(expose_service_decl, target).await
        }

        // Route a resolver or runner from an environment
        RouteRequest::Resolver(resolver_registration) => {
            route_resolver(resolver_registration, target).await
        }
        RouteRequest::Runner(runner_name) => route_runner(&runner_name, target).await,

        // Route the backing directory for a storage capability
        RouteRequest::StorageBackingDirectory(storage_decl) => {
            route_storage_backing_directory(storage_decl, target).await
        }

        // Route from a UseDecl
        RouteRequest::UseDirectory(use_directory_decl) => {
            route_directory(use_directory_decl, target).await
        }
        RouteRequest::UseEvent(use_event_decl) => route_event(use_event_decl, target).await,
        RouteRequest::UseProtocol(use_protocol_decl) => {
            route_protocol(use_protocol_decl, target).await
        }
        RouteRequest::UseService(use_service_decl) => route_service(use_service_decl, target).await,
        RouteRequest::UseStorage(use_storage_decl) => route_storage(use_storage_decl, target).await,
    }
}

/// Routes a storage capability and its backing directory capability to their sources,
/// returning the data needed to open the storage capability.
///
/// If either capability is not allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], then an error is returned.
pub async fn route_storage_and_backing_directory<C>(
    use_decl: UseStorageDecl,
    target: &Arc<C>,
) -> Result<(StorageCapabilitySource<C>, RelativeMoniker), RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    // First route the storage capability to its source.
    let storage_source = {
        match route_capability(RouteRequest::UseStorage(use_decl), target).await? {
            RouteSource::Storage(source) => source,
            _ => unreachable!("expected RouteSource::Storage"),
        }
    };

    let (storage_decl, storage_component_instance) = match storage_source {
        CapabilitySourceInterface::Component {
            capability: ComponentCapability::Storage(storage_decl),
            component,
        } => (storage_decl, component.upgrade()?),
        _ => unreachable!("unexpected storage source"),
    };
    let relative_moniker = RelativeMoniker::from_absolute(
        storage_component_instance.abs_moniker(),
        target.abs_moniker(),
    );

    // Now route the backing directory capability.
    match route_capability(
        RouteRequest::StorageBackingDirectory(storage_decl),
        &storage_component_instance,
    )
    .await?
    {
        RouteSource::StorageBackingDirectory(storage_source_info) => {
            Ok((storage_source_info, relative_moniker))
        }
        _ => unreachable!("expected RouteSource::StorageBackingDirectory"),
    }
}

make_noop_visitor!(ProtocolVisitor, {
    OfferDecl => OfferProtocolDecl,
    ExposeDecl => ExposeProtocolDecl,
    CapabilityDecl => ProtocolDecl,
});

/// Routes a Protocol capability from `target` to its source, starting from `use_decl`.
async fn route_protocol<C>(
    use_decl: UseProtocolDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources = AllowedSourcesBuilder::new()
        .framework(InternalCapability::Protocol)
        .builtin()
        .namespace()
        .component()
        .capability();
    match use_decl.source {
        UseSource::Debug => {
            // Find the component instance in which the debug capability was registered with the environment.
            let (env_component_instance, env_name, registration_decl) =
                match target.environment().get_debug_capability(&use_decl.source_name)? {
                    Some((
                        ExtendedInstanceInterface::Component(env_component_instance),
                        env_name,
                        reg,
                    )) => (env_component_instance, env_name, reg),
                    Some((ExtendedInstanceInterface::AboveRoot(_), _, _)) => {
                        // Root environment.
                        return Err(RoutingError::UseFromRootEnvironmentNotAllowed {
                            moniker: target.abs_moniker().to_partial(),
                            capability_name: use_decl.source_name.clone(),
                            capability_type: DebugRegistration::TYPE,
                        }
                        .into());
                    }
                    None => {
                        return Err(RoutingError::UseFromEnvironmentNotFound {
                            moniker: target.abs_moniker().to_partial(),
                            capability_name: use_decl.source_name.clone(),
                            capability_type: DebugRegistration::TYPE,
                        }
                        .into());
                    }
                };
            let env_name = env_name.expect(&format!(
                "Environment name in component `{}` not found when routing `{}`.",
                target.abs_moniker().clone(),
                use_decl.source_name
            ));

            let env_moniker = env_component_instance.abs_moniker().clone();

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

            target.try_get_policy_checker()?.can_route_debug_capability(
                &source,
                &env_moniker,
                &env_name,
                target.abs_moniker(),
            )?;
            return Ok(RouteSource::Protocol(source));
        }
        UseSource::Self_ => {
            let allowed_sources = AllowedSourcesBuilder::new().component();
            let source = RoutingStrategy::new()
                .use_::<UseProtocolDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut ProtocolVisitor)
                .await?;
            Ok(RouteSource::Protocol(source))
        }
        _ => {
            let source = RoutingStrategy::new()
                .use_::<UseProtocolDecl>()
                .offer::<OfferProtocolDecl>()
                .expose::<ExposeProtocolDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut ProtocolVisitor)
                .await?;

            target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
            Ok(RouteSource::Protocol(source))
        }
    }
}

/// Routes a Protocol capability from `target` to its source, starting from `expose_decl`.
async fn route_protocol_from_expose<C>(
    expose_decl: ExposeProtocolDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources = AllowedSourcesBuilder::new()
        .framework(InternalCapability::Protocol)
        .builtin()
        .namespace()
        .component()
        .capability();
    let source = RoutingStrategy::new()
        .use_::<UseProtocolDecl>()
        .offer::<OfferProtocolDecl>()
        .expose::<ExposeProtocolDecl>()
        .route_from_expose(expose_decl, target.clone(), allowed_sources, &mut ProtocolVisitor)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Protocol(source))
}

make_noop_visitor!(ServiceVisitor, {
    OfferDecl => OfferServiceDecl,
    ExposeDecl => ExposeServiceDecl,
    CapabilityDecl => ServiceDecl,
});

async fn route_service<C>(
    use_decl: UseServiceDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    match use_decl.source {
        UseSource::Self_ => {
            let allowed_sources = AllowedSourcesBuilder::new().component();
            let source = RoutingStrategy::new()
                .use_::<UseServiceDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut ServiceVisitor)
                .await?;
            Ok(RouteSource::Service(source))
        }
        _ => {
            let allowed_sources = AllowedSourcesBuilder::new().component().collection();
            let source = RoutingStrategy::new()
                .use_::<UseServiceDecl>()
                .offer::<OfferServiceDecl>()
                .expose::<ExposeServiceDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut ServiceVisitor)
                .await?;

            target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
            Ok(RouteSource::Service(source))
        }
    }
}

async fn route_service_from_expose<C>(
    expose_decl: ExposeServiceDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources = AllowedSourcesBuilder::new().component().collection();
    let source = RoutingStrategy::new()
        .use_::<UseServiceDecl>()
        .offer::<OfferServiceDecl>()
        .expose::<ExposeServiceDecl>()
        .route_from_expose(expose_decl, target.clone(), allowed_sources, &mut ServiceVisitor)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Service(source))
}

/// The accumulated state of routing a Directory capability.
#[derive(Clone, Debug)]
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
    ) -> Result<(), RoutingError> {
        self.rights = self.rights.advance(rights.map(Rights::from))?;
        let subdir = subdir.clone().unwrap_or_else(PathBuf::new);
        self.subdir = subdir.attach(&self.subdir);
        Ok(())
    }

    fn finalize(
        &mut self,
        rights: fio2::Operations,
        subdir: Option<PathBuf>,
    ) -> Result<(), RoutingError> {
        self.rights = self.rights.finalize(Some(rights.into()))?;
        let subdir = subdir.clone().unwrap_or_else(PathBuf::new);
        self.subdir = subdir.attach(&self.subdir);
        Ok(())
    }
}

impl OfferVisitor for DirectoryState {
    type OfferDecl = OfferDirectoryDecl;

    fn visit(&mut self, offer: &OfferDirectoryDecl) -> Result<(), RoutingError> {
        match offer.source {
            OfferSource::Framework => self.finalize(*READ_RIGHTS, offer.subdir.clone()),
            _ => self.advance(offer.rights.clone(), offer.subdir.clone()),
        }
    }
}

impl ExposeVisitor for DirectoryState {
    type ExposeDecl = ExposeDirectoryDecl;

    fn visit(&mut self, expose: &ExposeDirectoryDecl) -> Result<(), RoutingError> {
        match expose.source {
            ExposeSource::Framework => self.finalize(*READ_RIGHTS, expose.subdir.clone()),
            _ => self.advance(expose.rights.clone(), expose.subdir.clone()),
        }
    }
}

impl CapabilityVisitor for DirectoryState {
    type CapabilityDecl = DirectoryDecl;

    fn visit(&mut self, capability_decl: &DirectoryDecl) -> Result<(), RoutingError> {
        self.finalize(capability_decl.rights.clone(), None)
    }
}

/// Routes a Directory capability from `target` to its source, starting from `use_decl`.
/// Returns the capability source, along with a `DirectoryState` accumulated from traversing
/// the route.
async fn route_directory<C>(
    use_decl: UseDirectoryDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    match use_decl.source {
        UseSource::Self_ => {
            let allowed_sources = AllowedSourcesBuilder::new().component();
            let source = RoutingStrategy::new()
                .use_::<UseDirectoryDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut ProtocolVisitor)
                .await?;
            Ok(RouteSource::Service(source))
        }
        _ => {
            let mut state = DirectoryState::new(use_decl.rights.clone(), use_decl.subdir.clone());
            if let UseSource::Framework = &use_decl.source {
                state.finalize(*READ_RIGHTS, None)?;
            }
            let allowed_sources = AllowedSourcesBuilder::new()
                .framework(InternalCapability::Directory)
                .namespace()
                .component();
            let source = RoutingStrategy::new()
                .use_::<UseDirectoryDecl>()
                .offer::<OfferDirectoryDecl>()
                .expose::<ExposeDirectoryDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut state)
                .await?;

            target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
            Ok(RouteSource::Directory(source, state))
        }
    }
}

/// Routes a Directory capability from `target` to its source, starting from `expose_decl`.
/// Returns the capability source, along with a `DirectoryState` accumulated from traversing
/// the route.
async fn route_directory_from_expose<C>(
    expose_decl: ExposeDirectoryDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let mut state = DirectoryState { rights: WalkState::new(), subdir: PathBuf::new() };
    let allowed_sources = AllowedSourcesBuilder::new()
        .framework(InternalCapability::Directory)
        .namespace()
        .component();
    let source = RoutingStrategy::new()
        .use_::<UseDirectoryDecl>()
        .offer::<OfferDirectoryDecl>()
        .expose::<ExposeDirectoryDecl>()
        .route_from_expose(expose_decl, target.clone(), allowed_sources, &mut state)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Directory(source, state))
}

make_noop_visitor!(StorageVisitor, {
    OfferDecl => OfferStorageDecl,
    CapabilityDecl => StorageDecl,
});

/// Verifies that the given component is in the index if its `storage_id` is StaticInstanceId.
/// - On success, Ok(()) is returned
/// - RoutingError::ComponentNotInIndex is returned on failure.
pub async fn verify_instance_in_component_id_index<C>(
    source: &CapabilitySourceInterface<C>,
    instance: &Arc<C>,
) -> Result<(), RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let storage_decl = match source {
        CapabilitySourceInterface::Component {
            capability: ComponentCapability::Storage(storage_decl),
            component: _,
        } => storage_decl,
        _ => unreachable!("unexpected storage source"),
    };

    if storage_decl.storage_id == fsys::StorageId::StaticInstanceId
        && instance.try_get_component_id_index()?.look_up_moniker(&instance.abs_moniker()) == None
    {
        return Err(RoutingError::ComponentNotInIdIndex {
            moniker: instance.abs_moniker().to_partial(),
        });
    }
    Ok(())
}

/// Routes a Storage capability from `target` to its source, starting from `use_decl`.
/// Returns the StorageDecl and the storage component's instance.
pub async fn route_to_storage_decl<C>(
    use_decl: UseStorageDecl,
    target: &Arc<C>,
) -> Result<CapabilitySourceInterface<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources = AllowedSourcesBuilder::new().component();
    let source = RoutingStrategy::new()
        .use_::<UseStorageDecl>()
        .offer::<OfferStorageDecl>()
        .route(use_decl, target.clone(), allowed_sources, &mut StorageVisitor)
        .await?;

    Ok(source)
}

/// Routes a Storage capability from `target` to its source, starting from `use_decl`.
/// The backing Directory capability is then routed to its source.
async fn route_storage<C>(
    use_decl: UseStorageDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let source = route_to_storage_decl(use_decl, &target).await?;
    verify_instance_in_component_id_index(&source, target).await?;
    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Storage(source))
}

/// Routes the backing Directory capability of a Storage capability from `target` to its source,
/// starting from `storage_decl`.
async fn route_storage_backing_directory<C>(
    storage_decl: StorageDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    // Storage rights are always READ+WRITE.
    let mut state = DirectoryState::new(*READ_RIGHTS | *WRITE_RIGHTS, None);
    let allowed_sources = AllowedSourcesBuilder::new().component().namespace();
    let source = RoutingStrategy::new()
        .registration::<StorageDeclAsRegistration>()
        .offer::<OfferDirectoryDecl>()
        .expose::<ExposeDirectoryDecl>()
        .route(storage_decl.clone().into(), target.clone(), allowed_sources, &mut state)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;

    let (dir_source_path, dir_source_instance) = match source {
        CapabilitySourceInterface::Component { capability, component } => (
            capability.source_path().expect("directory has no source path?").clone(),
            Some(component.upgrade()?),
        ),
        CapabilitySourceInterface::Namespace { capability, .. } => {
            (capability.source_path().expect("directory has no source path?").clone(), None)
        }
        _ => unreachable!("not valid sources"),
    };

    let dir_subdir = if state.subdir == Path::new("") { None } else { Some(state.subdir) };

    Ok(RouteSource::StorageBackingDirectory(StorageCapabilitySource::<C> {
        storage_provider: dir_source_instance,
        backing_directory_path: dir_source_path,
        backing_directory_subdir: dir_subdir,
        storage_subdir: storage_decl.subdir.clone(),
    }))
}

make_noop_visitor!(RunnerVisitor, {
    OfferDecl => OfferRunnerDecl,
    ExposeDecl => ExposeRunnerDecl,
    CapabilityDecl => RunnerDecl,
});

/// Finds a Runner capability that matches `runner` in the `target`'s environment, and then
/// routes the Runner capability from the environment's component instance to its source.
async fn route_runner<C>(
    runner: &CapabilityName,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    // Find the component instance in which the runner was registered with the environment.
    let (env_component_instance, registration_decl) =
        match target.environment().get_registered_runner(&runner)? {
            Some((
                ExtendedInstanceInterface::Component(env_component_instance),
                registration_decl,
            )) => (env_component_instance, registration_decl),
            Some((ExtendedInstanceInterface::AboveRoot(top_instance), reg)) => {
                // Root environment.
                return Ok(RouteSource::Runner(CapabilitySourceInterface::Builtin {
                    capability: InternalCapability::Runner(reg.source_name.clone()),
                    top_instance: Arc::downgrade(&top_instance),
                }));
            }
            None => {
                return Err(RoutingError::UseFromEnvironmentNotFound {
                    moniker: target.abs_moniker().to_partial(),
                    capability_name: runner.clone(),
                    capability_type: "runner",
                }
                .into());
            }
        };

    let allowed_sources = AllowedSourcesBuilder::new().builtin().component();
    let source = RoutingStrategy::new()
        .registration::<RunnerRegistration>()
        .offer::<OfferRunnerDecl>()
        .expose::<ExposeRunnerDecl>()
        .route(registration_decl, env_component_instance, allowed_sources, &mut RunnerVisitor)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Runner(source))
}

make_noop_visitor!(ResolverVisitor, {
    OfferDecl => OfferResolverDecl,
    ExposeDecl => ExposeResolverDecl,
    CapabilityDecl => ResolverDecl,
});

/// Routes a Resolver capability from `target` to its source, starting from `registration_decl`.
async fn route_resolver<C>(
    registration: ResolverRegistration,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources = AllowedSourcesBuilder::new().builtin().component();

    let source = RoutingStrategy::new()
        .registration::<ResolverRegistration>()
        .offer::<OfferResolverDecl>()
        .expose::<ExposeResolverDecl>()
        .route(registration, target.clone(), allowed_sources, &mut ResolverVisitor)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Resolver(source))
}

/// State accumulated from routing an Event capability to its source.
struct EventState {
    filter_state: WalkState<EventFilter>,
    modes_state: WalkState<EventModeSet>,
}

impl OfferVisitor for EventState {
    type OfferDecl = OfferEventDecl;

    fn visit(&mut self, offer: &OfferEventDecl) -> Result<(), RoutingError> {
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
    type CapabilityDecl = EventDecl;
}

/// Routes an Event capability from `target` to its source, starting from `use_decl`.
async fn route_event<C>(
    use_decl: UseEventDecl,
    target: &Arc<C>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let mut state = EventState {
        filter_state: WalkState::at(EventFilter::new(use_decl.filter.clone())),
        modes_state: WalkState::at(EventModeSet::new(use_decl.mode.clone())),
    };

    let allowed_sources =
        AllowedSourcesBuilder::new().framework(InternalCapability::Event).builtin();
    let source = RoutingStrategy::new()
        .use_::<UseEventDecl>()
        .offer::<OfferEventDecl>()
        .route(use_decl, target.clone(), allowed_sources, &mut state)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Event(source))
}

/// Intermediate type to masquerade as Registration-style routing start point for the storage
/// backing directory capability.
pub struct StorageDeclAsRegistration {
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

// Error trait impls

impl ErrorNotFoundFromParent for UseProtocolDecl {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for DebugRegistration {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
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
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for UseProtocolDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromChildExposeNotFound {
            child_moniker,
            moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for OfferProtocolDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseServiceDecl {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferServiceDecl {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for UseServiceDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromChildExposeNotFound {
            child_moniker,
            moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for OfferServiceDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeServiceDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferDirectoryDecl {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for UseDirectoryDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromChildExposeNotFound {
            child_moniker,
            moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for OfferDirectoryDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferStorageDecl {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for StorageDeclAsRegistration {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::StorageFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for RunnerRegistration {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
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
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferRunnerDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
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
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for OfferResolverDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
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
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for UseEventDecl {
    fn error_not_found_in_child(
        moniker: PartialAbsoluteMoniker,
        child_moniker: PartialChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromChildExposeNotFound {
            child_moniker,
            moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundFromParent for UseEventDecl {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferEventDecl {
    fn error_not_found_from_parent(
        moniker: PartialAbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}
