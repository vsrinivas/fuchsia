// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod error;
pub mod open;
pub use error::OpenResourceError;
pub use error::RoutingError;
pub use open::*;

mod service;

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, ComponentCapability, OptionalTask},
        channel,
        model::{
            component::{BindReason, ComponentInstance, ExtendedInstance, WeakComponentInstance},
            error::ModelError,
            hooks::{Event, EventPayload},
            routing::service::serve_collection,
            storage,
        },
    },
    ::routing::{
        component_instance::ComponentInstanceInterface, path::PathBufExt, route_capability,
        route_storage_and_backing_directory,
    },
    async_trait::async_trait,
    cm_rust::{self, CapabilityName, CapabilityPath, ExposeDecl, UseDecl, UseStorageDecl},
    fidl::{endpoints::ServerEnd, epitaph::ChannelEpitaphExt},
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    futures::lock::Mutex,
    log::*,
    moniker::{AbsoluteMonikerBase, ExtendedMoniker, RelativeMoniker},
    std::{path::PathBuf, sync::Arc},
};

pub type RouteRequest = ::routing::RouteRequest;
pub type RouteSource = ::routing::RouteSource<ComponentInstance>;

const SERVICE_OPEN_FLAGS: u32 =
    fio::OPEN_FLAG_DESCRIBE | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;

/// Routes a capability from `target` to its source. Opens the capability if routing succeeds.
///
/// If the capability is not allowed to be routed to the `target`, per the
/// [`crate::model::policy::GlobalPolicyChecker`], the capability is not opened and an error
/// is returned.
pub(super) async fn route_and_open_capability(
    route_request: RouteRequest,
    target: &Arc<ComponentInstance>,
    open_options: OpenOptions<'_>,
) -> Result<(), ModelError> {
    match route_request {
        RouteRequest::UseStorage(use_storage_decl) => {
            let (storage_source_info, relative_moniker) =
                route_storage_and_backing_directory(use_storage_decl, target).await?;
            open_storage_capability(storage_source_info, relative_moniker, target, open_options)
                .await
        }
        _ => {
            let route_source = route_capability(route_request, target).await?;
            open_capability_at_source(OpenRequest::new(route_source, target, open_options)).await
        }
    }
}

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
    let route_request = request_for_namespace_capability_use(use_decl)?;
    let open_options = OpenOptions::for_namespace_capability(
        &route_request,
        flags,
        open_mode,
        relative_path,
        server_chan,
    )?;
    route_and_open_capability(route_request, target, open_options).await
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
    let route_request = request_for_namespace_capability_expose(expose_decl)?;
    let open_options = OpenOptions::for_namespace_capability(
        &route_request,
        flags,
        open_mode,
        relative_path,
        server_chan,
    )?;
    route_and_open_capability(route_request, target, open_options).await
}

/// Create a new `RouteRequest` from a `UseDecl`, checking that the capability type can
/// be installed in a namespace.
fn request_for_namespace_capability_use(use_decl: UseDecl) -> Result<RouteRequest, ModelError> {
    match use_decl {
        UseDecl::Directory(decl) => Ok(RouteRequest::UseDirectory(decl)),
        UseDecl::Protocol(decl) => Ok(RouteRequest::UseProtocol(decl)),
        UseDecl::Service(decl) => Ok(RouteRequest::UseService(decl)),
        UseDecl::Storage(decl) => Ok(RouteRequest::UseStorage(decl)),
        _ => Err(ModelError::unsupported("capability cannot be installed in a namespace")),
    }
}

/// Create a new `RouteRequest` from an `ExposeDecl`, checking that the capability type can
/// be installed in a namespace.
fn request_for_namespace_capability_expose(
    expose_decl: ExposeDecl,
) -> Result<RouteRequest, ModelError> {
    match expose_decl {
        ExposeDecl::Directory(decl) => Ok(RouteRequest::ExposeDirectory(decl)),
        ExposeDecl::Protocol(decl) => Ok(RouteRequest::ExposeProtocol(decl)),
        ExposeDecl::Service(decl) => Ok(RouteRequest::ExposeService(decl)),
        _ => Err(ModelError::unsupported("capability cannot be installed in a namespace")),
    }
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
    ) -> Result<OptionalTask, ModelError> {
        let capability = Arc::new(Mutex::new(Some(channel::take_channel(server_end))));
        let res = async {
            // Start the source component, if necessary
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
            Result::<Arc<ComponentInstance>, ModelError>::Ok(source)
        }
        .await;

        // If the capability transported through the event above wasn't transferred
        // out, then we can open the capability through the component's outgoing directory.
        // If some hook consumes the capability, then we don't bother looking in the outgoing
        // directory.
        let capability = capability.lock().await.take();
        if let Some(mut server_end_in) = capability {
            // Pass back the channel so the caller can set the epitaph, if necessary.
            *server_end = channel::take_channel(&mut server_end_in);
            let path = self.path.to_path_buf().attach(relative_path);
            res?.open_outgoing(flags, open_mode, path, server_end).await?;
        } else {
            let _ = res?;
        }
        Ok(None.into())
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
        | CapabilitySource::Namespace { .. }
        | CapabilitySource::Collection { .. } => {
            // There is no default provider for a framework or builtin capability
            None
        }
    }
}

/// Opens the capability at `source`, triggering a `CapabilityRouted` event and binding
/// to the source component instance if necessary.
///
/// See [`fidl_fuchsia_io::Directory::Open`] for how the `flags`, `open_mode`, `relative_path`,
/// and `server_chan` parameters are used in the open call.
async fn open_capability_at_source(open_request: OpenRequest<'_>) -> Result<(), ModelError> {
    let OpenRequest { flags, open_mode, relative_path, source, target, server_chan } = open_request;
    // When serving a collection, routing hasn't reached the source. The CapabilityRouted event
    // should not fire, nor should hooks be able to modify the provider (which is hosted by
    // component_manager). Once a component is routed to from the collection, CapabilityRouted will
    // fire as usual.
    match source {
        CapabilitySource::Collection { capability_provider, component, .. } => {
            return serve_collection(
                target.as_weak(),
                &component.upgrade()?,
                capability_provider,
                flags,
                open_mode,
                relative_path,
                server_chan,
            )
            .await;
        }
        _ => {}
    }

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
        if let Some(task) =
            capability_provider.open(flags, open_mode, relative_path, server_chan).await?.take()
        {
            let source_instance = source.source_instance().upgrade()?;
            match source_instance {
                ExtendedInstance::AboveRoot(top) => {
                    top.add_task(task).await;
                }
                ExtendedInstance::Component(component) => {
                    component.add_task(task).await;
                }
            }
        }
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
            CapabilitySource::Framework { capability, component } => {
                return Err(RoutingError::capability_from_framework_not_found(
                    &component.moniker.to_partial(),
                    capability.source_name().to_string(),
                )
                .into());
            }
            CapabilitySource::Capability { source_capability, component } => {
                return Err(RoutingError::capability_from_capability_not_found(
                    &component.moniker.to_partial(),
                    source_capability.to_string(),
                )
                .into());
            }
            CapabilitySource::Builtin { capability, .. } => {
                return Err(ModelError::from(
                    RoutingError::capability_from_component_manager_not_found(
                        capability.source_name().to_string(),
                    ),
                ));
            }
            CapabilitySource::Namespace { capability, .. } => match capability.source_path() {
                Some(p) => p.clone(),
                _ => {
                    return Err(ModelError::from(
                        RoutingError::capability_from_component_manager_not_found(
                            capability.source_id(),
                        ),
                    ));
                }
            },
            CapabilitySource::Collection { .. } => {
                return Err(ModelError::unsupported("collections"));
            }
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

/// Routes a storage capability from `target` to its source and deletes its isolated storage.
pub(super) async fn route_and_delete_storage(
    use_storage_decl: UseStorageDecl,
    target: &Arc<ComponentInstance>,
) -> Result<(), ModelError> {
    let (storage_source_info, relative_moniker) =
        route_storage_and_backing_directory(use_storage_decl, target).await?;

    storage::delete_isolated_storage(
        storage_source_info,
        relative_moniker,
        target.instance_id().as_ref(),
    )
    .await
}

/// Sets an epitaph on `server_end` for a capability routing failure, and logs the error. Logs a
/// failure to route a capability. Formats `err` as a `String`, but elides the type if the error is
/// a `RoutingError`, the common case.
pub async fn report_routing_failure(
    target: &Arc<ComponentInstance>,
    cap: &ComponentCapability,
    err: &ModelError,
    server_end: zx::Channel,
) {
    let _ = server_end.close_with_epitaph(err.as_zx_status());
    let err_str = match err {
        ModelError::RoutingError { err } => err.to_string(),
        _ => err.to_string(),
    };
    target
        .log(
            Level::Warn,
            format!(
                "Failed to route {} `{}` with target component `{}`: {}",
                cap.type_name(),
                cap.source_id(),
                &target.abs_moniker.to_partial(),
                &err_str
            ),
        )
        .await
}

/// Routes a storage capability from `target` to its source and opens its backing directory
/// capability, binding to the component instance if necessary.
///
/// See [`fidl_fuchsia_io::Directory::Open`] for how the `flags`, `open_mode`, `relative_path`,
/// and `server_chan` parameters are used in the open call.
async fn open_storage_capability(
    source: storage::StorageCapabilitySource,
    relative_moniker: RelativeMoniker,
    target: &Arc<ComponentInstance>,
    options: OpenOptions<'_>,
) -> Result<(), ModelError> {
    let dir_source = source.storage_provider.clone();
    let relative_moniker_2 = relative_moniker.clone();
    match options {
        OpenOptions::Storage(OpenStorageOptions { open_mode, server_chan, bind_reason }) => {
            let storage_dir_proxy = storage::open_isolated_storage(
                source,
                relative_moniker,
                target.instance_id().as_ref(),
                open_mode,
                &bind_reason,
            )
            .await
            .map_err(|e| ModelError::from(e))?;

            // clone the final connection to connect the channel we're routing to its destination
            let server_chan = channel::take_channel(server_chan);
            storage_dir_proxy
                .clone(fio::CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(server_chan))
                .map_err(|e| {
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
                })?;
            return Ok(());
        }
        _ => unreachable!("expected OpenStorageOptions"),
    }
}
