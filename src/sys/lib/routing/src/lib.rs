// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod availability;
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
pub mod resolving;
pub mod rights;
pub mod router;
pub mod walk_state;

use {
    crate::{
        availability::{
            AvailabilityDirectoryVisitor, AvailabilityEventStreamVisitor,
            AvailabilityProtocolVisitor, AvailabilityServiceVisitor, AvailabilityState,
            AvailabilityStorageVisitor,
        },
        capability_source::{
            CapabilitySourceInterface, ComponentCapability, InternalCapability,
            StorageCapabilitySource,
        },
        component_instance::{
            ComponentInstanceInterface, ExtendedInstanceInterface, TopInstanceInterface,
        },
        environment::DebugRegistration,
        error::RoutingError,
        event::EventFilter,
        path::PathBufExt,
        rights::{Rights, READ_RIGHTS, READ_WRITE_RIGHTS, WRITE_RIGHTS},
        router::{
            AllowedSourcesBuilder, CapabilityVisitor, ErrorNotFoundFromParent,
            ErrorNotFoundInChild, ExposeVisitor, OfferVisitor, RoutingStrategy, Sources,
        },
        walk_state::WalkState,
    },
    cm_moniker::InstancedRelativeMoniker,
    cm_rust::{
        Availability, CapabilityDecl, CapabilityName, DirectoryDecl, EventDecl, ExposeDecl,
        ExposeDirectoryDecl, ExposeEventStreamDecl, ExposeProtocolDecl, ExposeResolverDecl,
        ExposeRunnerDecl, ExposeServiceDecl, ExposeSource, OfferDecl, OfferDirectoryDecl,
        OfferEventDecl, OfferEventStreamDecl, OfferProtocolDecl, OfferResolverDecl,
        OfferRunnerDecl, OfferServiceDecl, OfferSource, OfferStorageDecl, RegistrationDeclCommon,
        RegistrationSource, ResolverDecl, ResolverRegistration, RunnerDecl, RunnerRegistration,
        SourceName, StorageDecl, StorageDirectorySource, UseDecl, UseDirectoryDecl, UseEventDecl,
        UseEventStreamDecl, UseProtocolDecl, UseServiceDecl, UseSource, UseStorageDecl,
    },
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io as fio,
    from_enum::FromEnum,
    moniker::{AbsoluteMoniker, ChildMoniker, RelativeMonikerBase},
    std::{
        path::{Path, PathBuf},
        sync::Arc,
    },
    tracing::warn,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// A request to route a capability, together with the data needed to do so.
#[derive(Debug)]
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
    UseEventStream(UseEventStreamDecl),
    UseProtocol(UseProtocolDecl),
    UseService(UseServiceDecl),
    UseStorage(UseStorageDecl),
}

impl RouteRequest {
    /// Return `true` if the RouteRequest is a `use` capability declaration, and
    /// the `availability` is `optional` (the target declares that it does not
    /// require the capability).
    pub fn target_use_optional(&self) -> bool {
        use crate::RouteRequest::*;
        match self {
            UseDirectory(UseDirectoryDecl { availability, .. })
            | UseEvent(UseEventDecl { availability, .. })
            | UseEventStream(UseEventStreamDecl { availability, .. })
            | UseProtocol(UseProtocolDecl { availability, .. })
            | UseService(UseServiceDecl { availability, .. })
            | UseStorage(UseStorageDecl { availability, .. }) => {
                *availability == Availability::Optional
            }

            ExposeDirectory(_)
            | ExposeProtocol(_)
            | ExposeService(_)
            | Resolver(_)
            | Runner(_)
            | StorageBackingDirectory(_) => false,
        }
    }
}

/// The data returned after successfully routing a capability to its source.
#[derive(Debug)]
pub enum RouteSource<C: ComponentInstanceInterface> {
    Directory(CapabilitySourceInterface<C>, DirectoryState),
    Event(CapabilitySourceInterface<C>),
    EventStream(CapabilitySourceInterface<C>),
    Protocol(CapabilitySourceInterface<C>),
    Resolver(CapabilitySourceInterface<C>),
    Runner(CapabilitySourceInterface<C>),
    Service(CapabilitySourceInterface<C>),
    Storage(CapabilitySourceInterface<C>),
    StorageBackingDirectory(StorageCapabilitySource<C>),
}

/// Provides methods to record and retrieve a summary of a capability route.
pub trait DebugRouteMapper: Send + Sync + Clone {
    type RouteMap: std::fmt::Debug;

    #[allow(unused_variables)]
    fn add_use(&mut self, abs_moniker: AbsoluteMoniker, use_decl: UseDecl) {}

    #[allow(unused_variables)]
    fn add_offer(&mut self, abs_moniker: AbsoluteMoniker, offer_decl: OfferDecl) {}

    #[allow(unused_variables)]
    fn add_expose(&mut self, abs_moniker: AbsoluteMoniker, expose_decl: ExposeDecl) {}

    #[allow(unused_variables)]
    fn add_registration(
        &mut self,
        abs_moniker: AbsoluteMoniker,
        registration_decl: RegistrationDecl,
    ) {
    }

    #[allow(unused_variables)]
    fn add_component_capability(
        &mut self,
        abs_moniker: AbsoluteMoniker,
        capability_decl: CapabilityDecl,
    ) {
    }

    #[allow(unused_variables)]
    fn add_framework_capability(&mut self, capability_name: CapabilityName) {}

    #[allow(unused_variables)]
    fn add_builtin_capability(&mut self, capability_decl: CapabilityDecl) {}

    #[allow(unused_variables)]
    fn add_namespace_capability(&mut self, capability_decl: CapabilityDecl) {}

    fn get_route(self) -> Self::RouteMap;
}

/// Routes a capability to its source.
///
/// If the capability is not allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], then an error is returned.
pub async fn route_capability<C>(
    request: RouteRequest,
    target: &Arc<C>,
) -> Result<(RouteSource<C>, <C::DebugRouteMapper as DebugRouteMapper>::RouteMap), RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let mut mapper = C::new_route_mapper();
    let source = match request {
        // Route from an ExposeDecl
        RouteRequest::ExposeDirectory(expose_directory_decl) => {
            route_directory_from_expose(expose_directory_decl, target, &mut mapper).await?
        }
        RouteRequest::ExposeProtocol(expose_protocol_decl) => {
            route_protocol_from_expose(expose_protocol_decl, target, &mut mapper).await?
        }
        RouteRequest::ExposeService(expose_service_decl) => {
            route_service_from_expose(expose_service_decl, target, &mut mapper).await?
        }

        // Route a resolver or runner from an environment
        RouteRequest::Resolver(resolver_registration) => {
            route_resolver(resolver_registration, target, &mut mapper).await?
        }
        RouteRequest::Runner(runner_name) => {
            route_runner(&runner_name, target, &mut mapper).await?
        }
        // Route the backing directory for a storage capability
        RouteRequest::StorageBackingDirectory(storage_decl) => {
            route_storage_backing_directory(storage_decl, target, &mut mapper).await?
        }

        // Route from a UseDecl
        RouteRequest::UseDirectory(use_directory_decl) => {
            route_directory(use_directory_decl, target, &mut mapper).await?
        }
        RouteRequest::UseEvent(use_event_decl) => {
            route_event(use_event_decl, target, &mut mapper).await?
        }
        RouteRequest::UseEventStream(use_event_stream_decl) => {
            route_event_stream(use_event_stream_decl, target, &mut mapper).await?
        }
        RouteRequest::UseProtocol(use_protocol_decl) => {
            route_protocol(use_protocol_decl, target, &mut mapper).await?
        }
        RouteRequest::UseService(use_service_decl) => {
            route_service(use_service_decl, target, &mut mapper).await?
        }
        RouteRequest::UseStorage(use_storage_decl) => {
            route_storage(use_storage_decl, target, &mut mapper).await?
        }
    };
    Ok((source, mapper.get_route()))
}

/// Routes a capability to its source.
///
/// If the capability is not allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], then an error is returned.
pub async fn route_event_stream_capability<C>(
    request: UseEventStreamDecl,
    target: &Arc<C>,
    route: &mut Vec<Arc<C>>,
) -> Result<(RouteSource<C>, <C::DebugRouteMapper as DebugRouteMapper>::RouteMap), RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let mut mapper = C::new_route_mapper();
    let source = route_event_stream_with_route(request, target, &mut mapper, route).await?;
    Ok((source, mapper.get_route()))
}

/// Routes a storage capability and its backing directory capability to their sources,
/// returning the data needed to open the storage capability.
///
/// If either capability is not allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], then an error is returned.
pub async fn route_storage_and_backing_directory<C>(
    use_decl: UseStorageDecl,
    target: &Arc<C>,
) -> Result<
    (
        StorageCapabilitySource<C>,
        InstancedRelativeMoniker,
        <C::DebugRouteMapper as DebugRouteMapper>::RouteMap,
        <C::DebugRouteMapper as DebugRouteMapper>::RouteMap,
    ),
    RoutingError,
>
where
    C: ComponentInstanceInterface + 'static,
{
    // First route the storage capability to its source.
    let (storage_source, storage_route) = {
        match route_capability(RouteRequest::UseStorage(use_decl), target).await? {
            (RouteSource::Storage(source), route) => (source, route),
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

    // As of today, the storage component instance must contain the target. This is because it is
    // impossible to expose storage declarations up.
    let storage_source_moniker = storage_component_instance.instanced_moniker();
    let target_moniker = target.instanced_moniker();

    let instanced_relative_moniker =
        InstancedRelativeMoniker::scope_down(storage_source_moniker, target_moniker).unwrap();

    // Now route the backing directory capability.
    match route_capability(
        RouteRequest::StorageBackingDirectory(storage_decl),
        &storage_component_instance,
    )
    .await?
    {
        (RouteSource::StorageBackingDirectory(storage_source_info), dir_route) => {
            Ok((storage_source_info, instanced_relative_moniker, storage_route, dir_route))
        }
        _ => unreachable!("expected RouteSource::StorageBackingDirectory"),
    }
}

/// Routes a Protocol capability from `target` to its source, starting from `use_decl`.
async fn route_protocol<C>(
    use_decl: UseProtocolDecl,
    target: &Arc<C>,
    mapper: &mut C::DebugRouteMapper,
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
            let (env_component_instance, env_name, registration_decl) = match target
                .environment()
                .get_debug_capability(&use_decl.source_name)
                .map_err(|err| {
                    warn!(?use_decl, %err, "route_protocol error 1");
                    err
                })? {
                Some((
                    ExtendedInstanceInterface::Component(env_component_instance),
                    env_name,
                    reg,
                )) => (env_component_instance, env_name, reg),
                Some((ExtendedInstanceInterface::AboveRoot(_), _, _)) => {
                    // Root environment.
                    return Err(RoutingError::UseFromRootEnvironmentNotAllowed {
                        moniker: target.abs_moniker().clone(),
                        capability_name: use_decl.source_name.clone(),
                        capability_type: DebugRegistration::TYPE.to_string(),
                    }
                    .into());
                }
                None => {
                    return Err(RoutingError::UseFromEnvironmentNotFound {
                        moniker: target.abs_moniker().clone(),
                        capability_name: use_decl.source_name.clone(),
                        capability_type: DebugRegistration::TYPE.to_string(),
                    }
                    .into());
                }
            };
            let env_name = env_name.expect(&format!(
                "Environment name in component `{}` not found when routing `{}`.",
                target.abs_moniker(),
                use_decl.source_name
            ));

            let env_moniker = env_component_instance.abs_moniker();

            let mut availability_visitor = AvailabilityProtocolVisitor::new(&use_decl);
            let source = RoutingStrategy::new()
                .registration::<DebugRegistration>()
                .offer::<OfferProtocolDecl>()
                .expose::<ExposeProtocolDecl>()
                .route(
                    registration_decl,
                    env_component_instance.clone(),
                    allowed_sources,
                    &mut availability_visitor,
                    mapper,
                )
                .await
                .map_err(|err| {
                    warn!(?use_decl, %err, "route_protocol error 2");
                    err
                })?;

            target
                .try_get_policy_checker()
                .map_err(|err| {
                    warn!(?use_decl, %err, "route_protocol error 3");
                    err
                })?
                .can_route_debug_capability(&source, &env_moniker, &env_name, target.abs_moniker())
                .map_err(|err| {
                    warn!(?use_decl, %err, "route_protocol error 4");
                    err
                })?;
            return Ok(RouteSource::Protocol(source));
        }
        UseSource::Self_ => {
            let mut availability_visitor = AvailabilityProtocolVisitor::new(&use_decl);
            let allowed_sources = AllowedSourcesBuilder::new().component();
            let source = RoutingStrategy::new()
                .use_::<UseProtocolDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut availability_visitor, mapper)
                .await?;
            Ok(RouteSource::Protocol(source))
        }
        _ => {
            let mut availability_visitor = AvailabilityProtocolVisitor::new(&use_decl);
            let source = RoutingStrategy::new()
                .use_::<UseProtocolDecl>()
                .offer::<OfferProtocolDecl>()
                .expose::<ExposeProtocolDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut availability_visitor, mapper)
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
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    // This is a noop visitor for exposes
    let mut availability_visitor = AvailabilityProtocolVisitor::required();
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
        .route_from_expose(
            expose_decl,
            target.clone(),
            allowed_sources,
            &mut availability_visitor,
            mapper,
        )
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Protocol(source))
}

async fn route_service<C>(
    use_decl: UseServiceDecl,
    target: &Arc<C>,
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    match use_decl.source {
        UseSource::Self_ => {
            let mut availability_visitor = AvailabilityServiceVisitor::new(&use_decl);
            let allowed_sources = AllowedSourcesBuilder::new().component();
            let source = RoutingStrategy::new()
                .use_::<UseServiceDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut availability_visitor, mapper)
                .await?;
            Ok(RouteSource::Service(source))
        }
        _ => {
            let mut availability_visitor = AvailabilityServiceVisitor::new(&use_decl);
            let allowed_sources = AllowedSourcesBuilder::new().component().collection();
            let source = RoutingStrategy::new()
                .use_::<UseServiceDecl>()
                .offer::<OfferServiceDecl>()
                .expose::<ExposeServiceDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut availability_visitor, mapper)
                .await?;

            target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
            Ok(RouteSource::Service(source))
        }
    }
}

async fn route_service_from_expose<C>(
    expose_decl: ExposeServiceDecl,
    target: &Arc<C>,
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let mut availability_visitor = AvailabilityServiceVisitor::required();
    let allowed_sources = AllowedSourcesBuilder::new().component().collection();
    let source = RoutingStrategy::new()
        .use_::<UseServiceDecl>()
        .offer::<OfferServiceDecl>()
        .expose::<ExposeServiceDecl>()
        .route_from_expose(
            expose_decl,
            target.clone(),
            allowed_sources,
            &mut availability_visitor,
            mapper,
        )
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Service(source))
}

/// The accumulated state of routing a Directory capability.
#[derive(Clone, Debug)]
pub struct DirectoryState {
    rights: WalkState<Rights>,
    subdir: PathBuf,
    availability_state: AvailabilityState,
}

impl DirectoryState {
    fn new(
        operations: fio::Operations,
        subdir: Option<PathBuf>,
        availability: &Availability,
    ) -> Self {
        DirectoryState {
            rights: WalkState::at(operations.into()),
            subdir: subdir.unwrap_or_else(PathBuf::new),
            availability_state: availability.clone().into(),
        }
    }

    /// Returns a new path with `in_relative_path` appended to the end of this
    /// DirectoryState's accumulated subdirectory path.
    pub fn make_relative_path(&self, in_relative_path: String) -> PathBuf {
        self.subdir.clone().attach(in_relative_path)
    }

    fn advance_with_offer(&mut self, offer: &OfferDirectoryDecl) -> Result<(), RoutingError> {
        self.availability_state.advance_with_offer(&offer.clone())?;
        self.advance(offer.rights.clone(), offer.subdir.clone())
    }

    fn advance(
        &mut self,
        rights: Option<fio::Operations>,
        subdir: Option<PathBuf>,
    ) -> Result<(), RoutingError> {
        self.rights = self.rights.advance(rights.map(Rights::from))?;
        let subdir = subdir.clone().unwrap_or_else(PathBuf::new);
        self.subdir = subdir.attach(&self.subdir);
        Ok(())
    }

    fn finalize(
        &mut self,
        rights: fio::Operations,
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
            OfferSource::Framework => self.finalize(*READ_WRITE_RIGHTS, offer.subdir.clone()),
            _ => self.advance_with_offer(offer),
        }
    }
}

impl ExposeVisitor for DirectoryState {
    type ExposeDecl = ExposeDirectoryDecl;

    fn visit(&mut self, expose: &ExposeDirectoryDecl) -> Result<(), RoutingError> {
        match expose.source {
            ExposeSource::Framework => self.finalize(*READ_WRITE_RIGHTS, expose.subdir.clone()),
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
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    match use_decl.source {
        UseSource::Self_ => {
            let mut availability_visitor = AvailabilityDirectoryVisitor::new(&use_decl);
            let allowed_sources = AllowedSourcesBuilder::new().component();
            let source = RoutingStrategy::new()
                .use_::<UseDirectoryDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut availability_visitor, mapper)
                .await?;
            Ok(RouteSource::Service(source))
        }
        _ => {
            let mut state = DirectoryState::new(
                use_decl.rights.clone(),
                use_decl.subdir.clone(),
                &use_decl.availability.clone(),
            );
            if let UseSource::Framework = &use_decl.source {
                state.finalize(*READ_WRITE_RIGHTS, None)?;
            }
            let allowed_sources = AllowedSourcesBuilder::new()
                .framework(InternalCapability::Directory)
                .namespace()
                .component();
            let source = RoutingStrategy::new()
                .use_::<UseDirectoryDecl>()
                .offer::<OfferDirectoryDecl>()
                .expose::<ExposeDirectoryDecl>()
                .route(use_decl, target.clone(), allowed_sources, &mut state, mapper)
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
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let mut state = DirectoryState {
        rights: WalkState::new(),
        subdir: PathBuf::new(),
        availability_state: Availability::Required.into(),
    };
    let allowed_sources = AllowedSourcesBuilder::new()
        .framework(InternalCapability::Directory)
        .namespace()
        .component();
    let source = RoutingStrategy::new()
        .use_::<UseDirectoryDecl>()
        .offer::<OfferDirectoryDecl>()
        .expose::<ExposeDirectoryDecl>()
        .route_from_expose(expose_decl, target.clone(), allowed_sources, &mut state, mapper)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Directory(source, state))
}

/// Verifies that the given component is in the index if its `storage_id` is StaticInstanceId.
/// - On success, Ok(()) is returned
/// - RoutingError::ComponentNotInIndex is returned on failure.
pub fn verify_instance_in_component_id_index<C>(
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

    if storage_decl.storage_id == fdecl::StorageId::StaticInstanceId
        && instance.try_get_component_id_index()?.look_up_moniker(instance.abs_moniker()) == None
    {
        return Err(RoutingError::ComponentNotInIdIndex {
            moniker: instance.abs_moniker().clone(),
        });
    }
    Ok(())
}

/// Routes a Storage capability from `target` to its source, starting from `use_decl`.
/// Returns the StorageDecl and the storage component's instance.
pub async fn route_to_storage_decl<C>(
    use_decl: UseStorageDecl,
    target: &Arc<C>,
    mapper: &mut C::DebugRouteMapper,
) -> Result<CapabilitySourceInterface<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let mut availability_visitor = AvailabilityStorageVisitor::new(&use_decl);
    let allowed_sources = AllowedSourcesBuilder::new().component();
    let source = RoutingStrategy::new()
        .use_::<UseStorageDecl>()
        .offer::<OfferStorageDecl>()
        .route(use_decl, target.clone(), allowed_sources, &mut availability_visitor, mapper)
        .await?;
    Ok(source)
}

/// Routes a Storage capability from `target` to its source, starting from `use_decl`.
/// The backing Directory capability is then routed to its source.
async fn route_storage<C>(
    use_decl: UseStorageDecl,
    target: &Arc<C>,
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let source = route_to_storage_decl(use_decl, &target, mapper).await?;
    verify_instance_in_component_id_index(&source, target)?;
    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Storage(source))
}

/// Routes the backing Directory capability of a Storage capability from `target` to its source,
/// starting from `storage_decl`.
async fn route_storage_backing_directory<C>(
    storage_decl: StorageDecl,
    target: &Arc<C>,
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    // Storage rights are always READ+WRITE.
    let mut state =
        DirectoryState::new(*READ_RIGHTS | *WRITE_RIGHTS, None, &Availability::Required);
    let allowed_sources = AllowedSourcesBuilder::new().component().namespace();
    let source = RoutingStrategy::new()
        .registration::<StorageDeclAsRegistration>()
        .offer::<OfferDirectoryDecl>()
        .expose::<ExposeDirectoryDecl>()
        .route(storage_decl.clone().into(), target.clone(), allowed_sources, &mut state, mapper)
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
    let dir_subdir = if state.subdir == Path::new("") { None } else { Some(state.subdir.clone()) };
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
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources = AllowedSourcesBuilder::new().builtin().component();
    let source = match target.environment().get_registered_runner(&runner)? {
        // The runner was registered in the environment of some component instance..
        Some((ExtendedInstanceInterface::Component(env_component_instance), registration_decl)) => {
            RoutingStrategy::new()
                .registration::<RunnerRegistration>()
                .offer::<OfferRunnerDecl>()
                .expose::<ExposeRunnerDecl>()
                .route(
                    registration_decl,
                    env_component_instance,
                    allowed_sources,
                    &mut RunnerVisitor,
                    mapper,
                )
                .await
        }
        // The runner was registered in the root environment.
        Some((ExtendedInstanceInterface::AboveRoot(top_instance), reg)) => {
            let internal_capability = allowed_sources
                .find_builtin_source(
                    reg.source_name(),
                    top_instance.builtin_capabilities(),
                    &mut RunnerVisitor,
                    mapper,
                )?
                .ok_or(RoutingError::register_from_component_manager_not_found(
                    reg.source_name().to_string(),
                ))?;
            Ok(CapabilitySourceInterface::Builtin {
                capability: internal_capability,
                top_instance: Arc::downgrade(&top_instance),
            })
        }
        None => Err(RoutingError::UseFromEnvironmentNotFound {
            moniker: target.abs_moniker().clone(),
            capability_name: runner.clone(),
            capability_type: "runner".to_string(),
        }),
    }?;

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
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources = AllowedSourcesBuilder::new().builtin().component();
    let source = RoutingStrategy::new()
        .registration::<ResolverRegistration>()
        .offer::<OfferResolverDecl>()
        .expose::<ExposeResolverDecl>()
        .route(registration, target.clone(), allowed_sources, &mut ResolverVisitor, mapper)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Resolver(source))
}

/// State accumulated from routing an Event capability to its source.
struct EventState {
    filter_state: WalkState<EventFilter>,
    availability_state: AvailabilityState,
}

impl OfferVisitor for EventState {
    type OfferDecl = OfferEventDecl;

    fn visit(&mut self, offer: &OfferEventDecl) -> Result<(), RoutingError> {
        self.availability_state.advance(&offer.availability)?;
        let event_filter = Some(EventFilter::new(offer.filter.clone()));
        match &offer.source {
            OfferSource::Parent => {
                self.filter_state = self.filter_state.advance(event_filter)?;
            }
            OfferSource::Framework => {
                self.filter_state = self.filter_state.finalize(event_filter)?;
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
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources =
        AllowedSourcesBuilder::new().framework(InternalCapability::Event).builtin();
    let mut state = EventState {
        filter_state: WalkState::at(EventFilter::new(use_decl.filter.clone())),
        availability_state: AvailabilityState(use_decl.availability.clone().into()),
    };

    let source = RoutingStrategy::new()
        .use_::<UseEventDecl>()
        .offer::<OfferEventDecl>()
        .route(use_decl, target.clone(), allowed_sources, &mut state, mapper)
        .await?;

    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::Event(source))
}

/// Routes an EventStream capability from `target` to its source, starting from `use_decl`.
async fn route_event_stream<C>(
    use_decl: UseEventStreamDecl,
    target: &Arc<C>,
    mapper: &mut C::DebugRouteMapper,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources = AllowedSourcesBuilder::new().builtin();

    let mut availability_visitor = AvailabilityEventStreamVisitor::new(&use_decl);
    let source = RoutingStrategy::new()
        .use_::<UseEventStreamDecl>()
        .offer::<OfferEventStreamDecl>()
        .expose::<ExposeEventStreamDecl>()
        .route(use_decl, target.clone(), allowed_sources, &mut availability_visitor, mapper)
        .await?;
    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::EventStream(source))
}

/// Routes an EventStream capability from `target` to its source, starting from `use_decl`.
async fn route_event_stream_with_route<C>(
    use_decl: UseEventStreamDecl,
    target: &Arc<C>,
    mapper: &mut C::DebugRouteMapper,
    route: &mut Vec<Arc<C>>,
) -> Result<RouteSource<C>, RoutingError>
where
    C: ComponentInstanceInterface + 'static,
{
    let allowed_sources =
        AllowedSourcesBuilder::new().framework(InternalCapability::EventStream).builtin();
    let mut availability_visitor = AvailabilityEventStreamVisitor::new(&use_decl);
    let source = RoutingStrategy::new()
        .use_::<UseEventStreamDecl>()
        .offer::<OfferEventStreamDecl>()
        .expose::<ExposeEventStreamDecl>()
        .route_extended_strategy(
            use_decl,
            target.clone(),
            allowed_sources,
            &mut availability_visitor,
            mapper,
            route,
        )
        .await?;
    target.try_get_policy_checker()?.can_route_capability(&source, target.abs_moniker())?;
    Ok(RouteSource::EventStream(source))
}

/// Intermediate type to masquerade as Registration-style routing start point for the storage
/// backing directory capability.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Debug, Clone, PartialEq, Eq)]
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

/// An umbrella type for registration decls, making it more convenient to record route
/// maps for debug use.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(FromEnum, Debug, Clone, PartialEq, Eq)]
pub enum RegistrationDecl {
    Resolver(ResolverRegistration),
    Runner(RunnerRegistration),
    Debug(DebugRegistration),
    Storage(StorageDeclAsRegistration),
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
            capability_type: DebugRegistration::TYPE.to_string(),
        }
    }
}

impl ErrorNotFoundInChild for DebugRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name: capability_name,
            capability_type: DebugRegistration::TYPE.to_string(),
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

impl ErrorNotFoundInChild for UseProtocolDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromChildExposeNotFound {
            child_moniker,
            moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for UseEventStreamDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for OfferEventStreamDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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
        child_moniker: ChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::ExposeFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_id: capability_name.into(),
        }
    }
}

impl ErrorNotFoundInChild for ExposeEventStreamDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for OfferServiceDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundInChild for UseServiceDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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

impl ErrorNotFoundInChild for UseDirectoryDecl {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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
        child_moniker: ChildMoniker,
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
        child_moniker: ChildMoniker,
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
            capability_type: "runner".to_string(),
        }
    }
}

impl ErrorNotFoundInChild for RunnerRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name,
            capability_type: "runner".to_string(),
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
        child_moniker: ChildMoniker,
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
        child_moniker: ChildMoniker,
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
            capability_type: "resolver".to_string(),
        }
    }
}

impl ErrorNotFoundInChild for ResolverRegistration {
    fn error_not_found_in_child(
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::EnvironmentFromChildExposeNotFound {
            moniker,
            child_moniker,
            capability_name,
            capability_type: "resolver".to_string(),
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
        child_moniker: ChildMoniker,
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
        child_moniker: ChildMoniker,
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
        moniker: AbsoluteMoniker,
        child_moniker: ChildMoniker,
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
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::UseFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}

impl ErrorNotFoundFromParent for UseEventStreamDecl {
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

impl ErrorNotFoundFromParent for OfferEventStreamDecl {
    fn error_not_found_from_parent(
        moniker: AbsoluteMoniker,
        capability_name: CapabilityName,
    ) -> RoutingError {
        RoutingError::OfferFromParentNotFound { moniker, capability_id: capability_name.into() }
    }
}
