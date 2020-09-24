// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod error;
pub use error::RoutingError;

use {
    crate::{
        capability::{
            CapabilityProvider, CapabilitySource, ComponentCapability, EnvironmentCapability,
            InternalCapability,
        },
        channel,
        model::{
            error::ModelError,
            events::filter::EventFilter,
            hooks::{Event, EventPayload},
            moniker::{
                AbsoluteMoniker, ChildMoniker, ExtendedMoniker, PartialMoniker, RelativeMoniker,
            },
            realm::{BindReason, Realm, WeakRealm},
            rights::{Rights, READ_RIGHTS, WRITE_RIGHTS},
            storage,
            walk_state::WalkState,
        },
        path::PathBufExt,
    },
    async_trait::async_trait,
    cm_rust::{
        self, CapabilityNameOrPath, CapabilityPath, ComponentDecl, ExposeDecl, ExposeDirectoryDecl,
        ExposeSource, ExposeTarget, OfferDecl, OfferDirectoryDecl, OfferDirectorySource,
        OfferEventDecl, OfferEventSource, OfferRunnerSource, OfferServiceSource,
        OfferStorageSource, StorageDirectorySource, UseDecl, UseDirectoryDecl, UseEventDecl,
        UseStorageDecl,
    },
    fidl::{endpoints::ServerEnd, epitaph::ChannelEpitaphExt},
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    futures::lock::Mutex,
    log::*,
    std::{path::PathBuf, sync::Arc},
};
const SERVICE_OPEN_FLAGS: u32 =
    fio::OPEN_FLAG_DESCRIBE | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;

/// Describes the source of a capability, for any type of capability.
#[derive(Debug)]
enum OfferSource<'a> {
    // TODO(fxbug.dev/4776): Enable this once unified services are implemented.
    #[allow(dead_code)]
    Service(&'a OfferServiceSource),
    Protocol(&'a OfferServiceSource),
    Directory(&'a OfferDirectorySource),
    Storage(&'a OfferStorageSource),
    Runner(&'a OfferRunnerSource),
    Event(&'a OfferEventSource),
}

/// Describes the source of a capability, for any type of capability.
#[derive(Debug)]
enum CapabilityExposeSource<'a> {
    Protocol(&'a ExposeSource),
    Directory(&'a ExposeSource),
    Runner(&'a ExposeSource),
}

/// Finds the source of the `capability` used by `absolute_moniker`, and pass along the
/// `server_chan` to the hosting component's out directory (or componentmgr's namespace, if
/// applicable) using an open request with `open_mode`.
pub(super) async fn route_use_capability<'a>(
    flags: u32,
    open_mode: u32,
    relative_path: String,
    use_decl: &'a UseDecl,
    target_realm: &'a Arc<Realm>,
    server_chan: &mut zx::Channel,
) -> Result<(), ModelError> {
    match use_decl {
        UseDecl::Service(_) | UseDecl::Protocol(_) | UseDecl::Directory(_) | UseDecl::Runner(_) => {
            let (source, cap_state) = find_used_capability_source(use_decl, target_realm).await?;
            let relative_path = cap_state.make_relative_path(relative_path);
            open_capability_at_source(
                flags,
                open_mode,
                relative_path,
                source,
                target_realm,
                server_chan,
            )
            .await
        }
        UseDecl::Storage(storage_decl) => {
            // TODO(fxbug.dev/50716): This BindReason is wrong. We need to refactor the Storage
            // capability to plumb through the correct BindReason.
            route_and_open_storage_capability(
                storage_decl,
                open_mode,
                target_realm,
                server_chan,
                &BindReason::Eager,
            )
            .await
        }
        UseDecl::Event(_) | UseDecl::EventStream(_) => {
            // Events are logged separately through route_use_event_capability.
            Ok(())
        }
    }
}

pub(super) async fn route_use_event_capability<'a>(
    use_decl: &'a UseDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<CapabilitySource, ModelError> {
    let (source, _cap_state) = find_used_capability_source(use_decl, target_realm).await?;
    Ok(source)
}

/// Finds the source of the expose capability used at `source_path` by `target_realm`, and pass
/// along the `server_chan` to the hosting component's out directory (or componentmgr's namespace,
/// if applicable)
pub(super) async fn route_expose_capability<'a>(
    flags: u32,
    open_mode: u32,
    relative_path: String,
    expose_decl: &'a ExposeDecl,
    target_realm: &'a Arc<Realm>,
    server_chan: &mut zx::Channel,
) -> Result<(), ModelError> {
    let capability = ComponentCapability::UsedExpose(expose_decl.clone());
    let cap_state = CapabilityState::new(&capability);
    let mut pos = WalkPosition {
        capability,
        cap_state,
        last_child_moniker: None,
        realm: Some(target_realm.clone()),
    };
    let source = walk_expose_chain(&mut pos).await?;
    let relative_path = pos.cap_state.make_relative_path(relative_path);
    open_capability_at_source(flags, open_mode, relative_path, source, target_realm, server_chan)
        .await
}

/// The default provider for a ComponentCapability.
/// This provider will bind to the source moniker's realm and then open the service
/// from the realm's outgoing directory.
struct DefaultComponentCapabilityProvider {
    target_realm: WeakRealm,
    source_realm: WeakRealm,
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
        let source_realm = self
            .source_realm
            .upgrade()?
            .bind(&BindReason::AccessCapability {
                target: ExtendedMoniker::ComponentInstance(self.target_realm.moniker.clone()),
                path: self.path.clone(),
            })
            .await?;

        let event = Event::new(
            &self.target_realm.upgrade()?,
            Ok(EventPayload::CapabilityRequested {
                source_moniker: source_realm.abs_moniker.clone(),
                path: path.as_path().display().to_string(),
                capability: capability.clone(),
            }),
        );
        source_realm.hooks.dispatch(&event).await?;

        // If the capability transported through the event above wasn't transferred
        // out, then we can open the capability through the realm's outgoing directory.
        // If some hook consumes the capability, then we don't bother looking in the outgoing
        // directory.
        let capability = capability.lock().await.take();
        if let Some(mut server_end_for_event) = capability {
            if let Err(e) =
                source_realm.open_outgoing(flags, open_mode, path, &mut server_end_for_event).await
            {
                // Pass back the channel to propagate the epitaph.
                *server_end = channel::take_channel(&mut server_end_for_event);
                return Err(e);
            }
        }
        Ok(())
    }
}

/// This method gets an optional default capability provider based on the
/// capability source.
fn get_default_provider(
    target_realm: WeakRealm,
    source: &CapabilitySource,
) -> Option<Box<dyn CapabilityProvider>> {
    match source {
        CapabilitySource::Component { capability, realm } => {
            // Route normally for a component capability with a source path
            match capability.source_path() {
                Some(path) => Some(Box::new(DefaultComponentCapabilityProvider {
                    target_realm,
                    source_realm: realm.clone(),
                    path: path.clone(),
                })),
                _ => None,
            }
        }
        CapabilitySource::Framework { .. } | CapabilitySource::AboveRoot { .. } => {
            // There is no default provider for a framework or builtin capability
            None
        }
    }
}

/// Open the capability at the given source, binding to its component instance if necessary.
pub async fn open_capability_at_source(
    flags: u32,
    open_mode: u32,
    relative_path: PathBuf,
    source: CapabilitySource,
    target_realm: &Arc<Realm>,
    server_chan: &mut zx::Channel,
) -> Result<(), ModelError> {
    let capability_provider =
        Arc::new(Mutex::new(get_default_provider(target_realm.as_weak(), &source)));

    let event = Event::new(
        &target_realm,
        Ok(EventPayload::CapabilityRouted {
            source: source.clone(),
            capability_provider: capability_provider.clone(),
        }),
    );
    // Get a capability provider from the tree
    target_realm.hooks.dispatch(&event).await?;

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
                return Err(
                    RoutingError::capability_from_framework_not_found(&m, capability.id()).into()
                );
            }
            CapabilitySource::AboveRoot { capability } => {
                match capability.name_or_path() {
                    Some(CapabilityNameOrPath::Path(path)) => path.clone(),
                    Some(CapabilityNameOrPath::Name(name)) => {
                        // Capabilities backed by the namespace are served at
                        // "/svc/{capability-name}".
                        let path_str = format!("/svc/{}", name);
                        path_str.parse().map_err(|_| ModelError::path_invalid(path_str))?
                    }
                    None => {
                        return Err(ModelError::from(
                            RoutingError::capability_from_component_manager_not_found(
                                capability.id(),
                            ),
                        ));
                    }
                }
            }
        };
        let namespace_path = namespace_path.to_path_buf().attach(relative_path);
        let namespace_path = namespace_path
            .to_str()
            .ok_or_else(|| ModelError::path_is_not_utf8(namespace_path.clone()))?;
        let server_chan = channel::take_channel(server_chan);
        io_util::connect_in_namespace(namespace_path, server_chan, flags).map_err(|e| {
            RoutingError::open_component_manager_namespace_failed(namespace_path, e).into()
        })
    }
}

/// Routes a `UseDecl::Storage` to the component instance providing the backing directory and
/// opens its isolated storage with `server_chan`.
pub async fn route_and_open_storage_capability<'a>(
    use_decl: &'a UseStorageDecl,
    open_mode: u32,
    target_realm: &'a Arc<Realm>,
    server_chan: &mut zx::Channel,
    bind_reason: &BindReason,
) -> Result<(), ModelError> {
    // TODO: Actually use `CapabilityState` to apply rights.
    let (dir_source_realm, dir_source_path, relative_moniker, _) =
        route_storage_capability(use_decl, target_realm).await?;
    let storage_dir_proxy = storage::open_isolated_storage(
        dir_source_realm.clone(),
        &dir_source_path,
        use_decl.type_(),
        &relative_moniker,
        open_mode,
        bind_reason,
    )
    .await
    .map_err(|e| ModelError::from(e))?;

    // clone the final connection to connect the channel we're routing to its destination
    let server_chan = channel::take_channel(server_chan);
    storage_dir_proxy.clone(fio::CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(server_chan)).map_err(
        |e| {
            let moniker = match &dir_source_realm {
                Some(r) => ExtendedMoniker::ComponentInstance(r.abs_moniker.clone()),
                None => ExtendedMoniker::ComponentManager,
            };
            ModelError::from(RoutingError::open_storage_failed(&moniker, &relative_moniker, "", e))
        },
    )?;
    Ok(())
}

/// Routes a `UseDecl::Storage` to the component instance providing the backing directory and
/// deletes its isolated storage.
pub(super) async fn route_and_delete_storage<'a>(
    use_decl: &'a UseStorageDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<(), ModelError> {
    let (dir_source_realm, dir_source_path, relative_moniker, _) =
        route_storage_capability(use_decl, target_realm).await?;
    storage::delete_isolated_storage(dir_source_realm, &dir_source_path, &relative_moniker)
        .await
        .map_err(|e| ModelError::from(e))?;
    Ok(())
}

/// Assuming `use_decl` is a UseStorage declaration, returns information about the source of the
/// storage capability, including:
/// - AbsoluteMoniker of the component hosting the backing directory capability
/// - Path to the backing directory capability
/// - Relative moniker between the backing directory component and the consumer, which identifies
///   the isolated storage directory.
async fn route_storage_capability<'a>(
    use_decl: &'a UseStorageDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<(Option<Arc<Realm>>, CapabilityPath, RelativeMoniker, CapabilityState), ModelError> {
    // Walk the offer chain to find the storage decl
    let parent_realm = target_realm.try_get_parent()?.ok_or_else(|| {
        ModelError::from(RoutingError::storage_source_is_not_component(
            "component manager's namespace",
        ))
    })?;

    let capability = ComponentCapability::Use(UseDecl::Storage(use_decl.clone()));
    let cap_state = CapabilityState::new(&capability);
    let mut pos = WalkPosition {
        capability,
        cap_state,
        last_child_moniker: target_realm.abs_moniker.path().last().map(|c| c.clone()),
        realm: Some(parent_realm),
    };

    let source = walk_offer_chain(&mut pos).await?;

    let (storage_decl, source_realm) = match source {
        Some(CapabilitySource::Component {
            capability: ComponentCapability::Storage(decl),
            realm,
        }) => (decl, realm.upgrade()?),
        _ => {
            unreachable!("Storage capability must come from a storage declaration.");
        }
    };
    let relative_moniker =
        RelativeMoniker::from_absolute(&source_realm.abs_moniker, &target_realm.abs_moniker);

    // Find the path and source of the directory consumed by the storage capability.
    let (dir_source_path, dir_source_realm, cap_state) = match storage_decl.source {
        StorageDirectorySource::Self_ => {
            let source_path =
                if let CapabilityNameOrPath::Path(source_path) = storage_decl.source_path {
                    source_path.clone()
                } else {
                    let realm_state = source_realm.lock_resolved_state().await?;
                    let decl = realm_state.decl();
                    let capability = ComponentCapability::Storage(storage_decl.clone());
                    let capability = pos.finalize_directory(&capability, decl, None, None)?;
                    capability.source_path().expect("directory has no source path?").clone()
                };
            (source_path, Some(source_realm), pos.cap_state)
        }
        StorageDirectorySource::Parent => {
            let capability = ComponentCapability::Storage(storage_decl);
            let (source, cap_state) = find_capability_source(capability, &source_realm).await?;
            match source {
                CapabilitySource::Component { capability, realm } => {
                    let source_path =
                        capability.source_path().expect("directory has no source path?").clone();
                    (source_path, Some(realm.upgrade()?), cap_state)
                }
                CapabilitySource::Framework { .. } => {
                    return Err(RoutingError::storage_directory_source_is_not_component(
                        "framework",
                        &source_realm.abs_moniker,
                    )
                    .into());
                }
                CapabilitySource::AboveRoot { capability } => {
                    let source_path = match capability.name_or_path() {
                        Some(CapabilityNameOrPath::Path(path)) => path.clone(),
                        Some(CapabilityNameOrPath::Name(_)) => {
                            // TODO(56604): Come up with a solution to route name-based
                            // capabilities from component manager's namespace.
                            return Err(RoutingError::storage_directory_source_is_not_component(
                                "component manager's namespace",
                                &source_realm.abs_moniker,
                            )
                            .into());
                        }
                        None => {
                            panic!("Invalid capability source for storage");
                        }
                    };
                    (source_path, None, cap_state)
                }
            }
        }
        StorageDirectorySource::Child(ref name) => {
            let mut pos = {
                let partial = PartialMoniker::new(name.to_string(), None);
                let realm_state = source_realm.lock_resolved_state().await?;
                let child_realm = realm_state.get_live_child_realm(&partial).ok_or_else(|| {
                    ModelError::from(RoutingError::storage_directory_source_child_not_found(
                        &source_realm.abs_moniker,
                        &partial,
                    ))
                })?;
                let capability = ComponentCapability::Storage(storage_decl);
                WalkPosition {
                    capability,
                    cap_state: pos.cap_state.clone(),
                    last_child_moniker: None,
                    realm: Some(child_realm),
                }
            };
            let source = walk_expose_chain(&mut pos).await?;
            match source {
                CapabilitySource::Component { capability, realm } => {
                    let source_path =
                        capability.source_path().expect("directory has no source path?").clone();
                    (source_path, Some(realm.upgrade()?), pos.cap_state)
                }
                CapabilitySource::Framework { .. } | CapabilitySource::AboveRoot { .. } => {
                    return Err(RoutingError::storage_directory_source_is_not_component(
                        "framework",
                        &source_realm.abs_moniker,
                    )
                    .into());
                }
            }
        }
    };
    Ok((dir_source_realm, dir_source_path, relative_moniker, cap_state))
}

/// Check if a used capability is a framework service, and if so return a framework `CapabilitySource`.
async fn find_scoped_framework_capability_source<'a>(
    use_decl: &'a UseDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<Option<CapabilitySource>, ModelError> {
    if let Ok(capability) = InternalCapability::framework_from_use_decl(use_decl) {
        return Ok(Some(CapabilitySource::Framework {
            capability,
            scope_moniker: target_realm.abs_moniker.clone(),
        }));
    }
    return Ok(None);
}

/// Holds state about the current position when walking the tree.
#[derive(Debug)]
struct WalkPosition {
    /// The capability declaration as it's represented in the current component.
    capability: ComponentCapability,
    /// Holds any capability-specific state.
    cap_state: CapabilityState,
    /// The moniker of the child we came from.
    last_child_moniker: Option<ChildMoniker>,
    /// The realm of the component we are currently looking at. `None` for component manager's
    /// realm.
    realm: Option<Arc<Realm>>,
}

impl WalkPosition {
    fn realm(&self) -> &Arc<Realm> {
        &self.realm.as_ref().expect("no realm in WalkPosition")
    }

    fn moniker(&self) -> &AbsoluteMoniker {
        &self.realm.as_ref().expect("no moniker in WalkPosition").abs_moniker
    }

    fn abs_last_child_moniker(&self) -> AbsoluteMoniker {
        self.moniker().child(self.last_child_moniker.as_ref().expect("no child moniker").clone())
    }

    fn at_componentmgr_realm(&self) -> bool {
        self.realm.is_none()
    }

    /// Finalize the directory state according to `capability`. Returns a `ComponentCapability` for
    /// the end of routing, or an error if rights did not match.
    ///
    /// REQUIRES: `capability` is a directory expose or offer from `self`.
    fn finalize_directory(
        &mut self,
        capability: &ComponentCapability,
        decl: &ComponentDecl,
        dir_rights: Option<Rights>,
        subdir_decl: Option<PathBuf>,
    ) -> Result<ComponentCapability, ModelError> {
        if let CapabilityState::Directory { rights_state, subdir } = &mut self.cap_state {
            *rights_state = rights_state.advance(dir_rights)?;
            CapabilityState::update_subdir(subdir, subdir_decl);
        }

        let directory_decl = capability
            .find_directory_source(decl)
            .expect("directory offer references nonexistent section")
            .clone();
        let dir_rights = Some(Rights::from(directory_decl.rights));
        let capability = ComponentCapability::Directory(directory_decl);
        if let CapabilityState::Directory { rights_state, subdir: _subdir } = &mut self.cap_state {
            *rights_state = rights_state.finalize(dir_rights)?;
        }
        Ok(capability)
    }
}

/// Holds state related to a capability when walking the tree
#[derive(Debug, Clone)]
enum CapabilityState {
    Directory {
        /// Holds the state of the rights. This is used to enforce directory rights.
        rights_state: WalkState<Rights>,
        /// Holds the subdirectory path to open.
        subdir: PathBuf,
    },
    Event {
        filter_state: WalkState<EventFilter>,
    },
    Other,
}

impl CapabilityState {
    fn new(cap: &ComponentCapability) -> Self {
        match cap {
            ComponentCapability::Use(UseDecl::Directory(UseDirectoryDecl { subdir, .. }))
            | ComponentCapability::Expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                subdir,
                ..
            }))
            | ComponentCapability::Offer(OfferDecl::Directory(OfferDirectoryDecl {
                subdir, ..
            })) => CapabilityState::Directory {
                rights_state: WalkState::new(),
                subdir: subdir.as_ref().map_or(PathBuf::new(), |s| PathBuf::from(s)),
            },
            ComponentCapability::Use(UseDecl::Event(UseEventDecl { filter, .. }))
            | ComponentCapability::Offer(OfferDecl::Event(OfferEventDecl { filter, .. })) => {
                CapabilityState::Event {
                    filter_state: WalkState::at(EventFilter::new(filter.clone())),
                }
            }
            ComponentCapability::UsedExpose(ExposeDecl::Directory(ExposeDirectoryDecl {
                ..
            })) => CapabilityState::Directory {
                rights_state: WalkState::new(),
                subdir: PathBuf::new(),
            },
            // Directories backing storage must provide read and write rights.
            ComponentCapability::Use(UseDecl::Storage { .. }) | ComponentCapability::Storage(_) => {
                CapabilityState::Directory {
                    rights_state: WalkState::at(Rights::from(*READ_RIGHTS | *WRITE_RIGHTS)),
                    subdir: PathBuf::new(),
                }
            }
            _ => CapabilityState::Other,
        }
    }

    fn make_relative_path(&self, in_relative_path: String) -> PathBuf {
        match self {
            Self::Directory { subdir, .. } => subdir.clone().attach(in_relative_path),
            _ => PathBuf::from(in_relative_path),
        }
    }

    fn update_subdir(subdir: &mut PathBuf, subdir_decl: Option<PathBuf>) {
        let subdir_decl = subdir_decl.unwrap_or(PathBuf::new());
        let old_subdir = subdir.clone();
        *subdir = subdir_decl.attach(old_subdir);
    }
}

async fn find_used_capability_source<'a>(
    use_decl: &'a UseDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<(CapabilitySource, CapabilityState), ModelError> {
    let capability = ComponentCapability::Use(use_decl.clone());
    if let Some(framework_capability) =
        find_scoped_framework_capability_source(use_decl, target_realm).await?
    {
        let cap_state = CapabilityState::new(&capability);
        return Ok((framework_capability, cap_state));
    }
    if let Some((cap_source, cap_state)) =
        find_environment_capability_source(use_decl, target_realm).await?
    {
        return Ok((cap_source, cap_state));
    }
    find_capability_source(capability, target_realm).await
}

/// Attempts to perform capability routing starting from a `use` of a capability that could be
/// provided by an environment.  Returns `None` if `use_decl` is not the type of capability
/// provided by an environment.
async fn find_environment_capability_source<'a>(
    use_decl: &'a UseDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<Option<(CapabilitySource, CapabilityState)>, ModelError> {
    let cap_state = CapabilityState::new(&ComponentCapability::Use(use_decl.clone()));
    match use_decl {
        UseDecl::Runner(cm_rust::UseRunnerDecl { source_name }) => {
            match target_realm.environment.get_registered_runner(source_name)? {
                Some((Some(env_realm), reg)) => {
                    let capability =
                        ComponentCapability::Environment(EnvironmentCapability::Runner {
                            source_name: reg.source_name,
                            source: reg.source.clone(),
                        });
                    Ok(Some(
                        find_environment_component_capability_source(
                            &env_realm,
                            capability,
                            cap_state,
                            &reg.source,
                        )
                        .await?,
                    ))
                }
                Some((None, reg)) => {
                    // Root environment.
                    let cap_source = CapabilitySource::AboveRoot {
                        capability: InternalCapability::Runner(reg.source_name.clone()),
                    };
                    Ok(Some((cap_source, cap_state)))
                }
                None => Err(ModelError::from(RoutingError::use_from_environment_not_found(
                    &target_realm.abs_moniker,
                    "runner",
                    source_name.to_string(),
                ))),
            }
        }
        _ => Ok(None),
    }
}

async fn find_environment_component_capability_source<'a>(
    env_realm: &Arc<Realm>,
    capability: ComponentCapability,
    cap_state: CapabilityState,
    source: &cm_rust::RegistrationSource,
) -> Result<(CapabilitySource, CapabilityState), ModelError> {
    let env_realm_state = env_realm.lock_resolved_state().await?;
    let decl = env_realm_state.decl();
    match &source {
        cm_rust::RegistrationSource::Self_ => {
            let cap_source = find_capability_source_from_self(&env_realm, &capability, decl);
            Ok((cap_source, cap_state))
        }
        cm_rust::RegistrationSource::Parent => {
            let starting_realm = env_realm.try_get_parent()?;
            let mut pos = WalkPosition {
                capability,
                cap_state,
                last_child_moniker: env_realm.abs_moniker.leaf().cloned(),
                realm: starting_realm,
            };
            if let Some(cap_source) = walk_offer_chain(&mut pos).await? {
                return Ok((cap_source, pos.cap_state));
            }
            let cap_source = walk_expose_chain(&mut pos).await?;
            Ok((cap_source, pos.cap_state))
        }
        cm_rust::RegistrationSource::Child(child_name) => {
            let partial = PartialMoniker::new(child_name.into(), None);
            let realm = Some(env_realm_state.get_live_child_realm(&partial).ok_or_else(|| {
                ModelError::from(RoutingError::environment_from_child_expose_not_found(
                    &partial,
                    &env_realm.abs_moniker,
                    capability.type_name(),
                    capability.source_id(),
                ))
            })?);
            let mut pos = WalkPosition { capability, cap_state, last_child_moniker: None, realm };
            let cap_source = walk_expose_chain(&mut pos).await?;
            Ok((cap_source, pos.cap_state))
        }
    }
}

fn find_capability_source_from_self(
    env_realm: &Arc<Realm>,
    capability: &ComponentCapability,
    decl: &cm_rust::ComponentDecl,
) -> CapabilitySource {
    match capability {
        ComponentCapability::Environment(EnvironmentCapability::Runner { .. }) => {
            let runner_decl = capability.find_runner_source(decl).expect("missing runner").clone();
            CapabilitySource::Component {
                capability: ComponentCapability::Runner(runner_decl),
                realm: env_realm.as_weak(),
            }
        }
        _ => {
            panic!("Capability has invalid type: {:?}", capability);
        }
    }
}

/// Finds the providing realm and path of a directory exposed by the root realm to component
/// manager.
pub async fn find_exposed_root_directory_capability(
    root_realm: &Arc<Realm>,
    path: CapabilityPath,
) -> Result<(CapabilityPath, Arc<Realm>), ModelError> {
    let expose_dir_decl = {
        let realm_state = root_realm.lock_state().await;
        let root_decl = realm_state
            .as_ref()
            .expect("find_exposed_root_directory_capability: not resolved")
            .decl();
        root_decl
            .exposes
            .iter()
            .find_map(|e| match e {
                ExposeDecl::Directory(dir_decl) => match &dir_decl.target_path {
                    CapabilityNameOrPath::Path(target_path) if target_path == &path => {
                        Some(dir_decl)
                    }
                    _ => None,
                },
                _ => None,
            })
            .ok_or_else(|| {
                ModelError::from(RoutingError::used_expose_not_found(
                    &AbsoluteMoniker::root(),
                    path.to_string(),
                ))
            })?
            .clone()
    };
    match &expose_dir_decl.source {
        ExposeSource::Framework => {
            return Err(ModelError::unsupported(
                "find_exposed_root_directory_capability does not support capabilities exposed from framework"));
        }
        ExposeSource::Self_ => {
            let source_path = match expose_dir_decl.source_path {
                CapabilityNameOrPath::Path(source_path) => source_path.clone(),
                _ => {
                    return Err(ModelError::unsupported(
                        "name-based directory capabilities are not supported yet",
                    ));
                }
            };
            return Ok((source_path, Arc::clone(root_realm)));
        }
        ExposeSource::Child(_) => {
            let capability =
                ComponentCapability::UsedExpose(ExposeDecl::Directory(expose_dir_decl.clone()));
            let cap_state = CapabilityState::new(&capability);
            let mut wp = WalkPosition {
                capability,
                cap_state,
                last_child_moniker: None,
                realm: Some(Arc::clone(root_realm)),
            };
            let capability_source = walk_expose_chain(&mut wp).await?;
            match capability_source {
                CapabilitySource::Component {
                    capability:
                        ComponentCapability::Expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                            source_path,
                            ..
                        })),
                    realm,
                } => {
                    let source_path = match source_path {
                        CapabilityNameOrPath::Path(source_path) => source_path.clone(),
                        _ => {
                            return Err(ModelError::unsupported(
                                "name-based directory capabilities are not supported yet",
                            ));
                        }
                    };
                    return Ok((source_path, realm.upgrade()?));
                }
                CapabilitySource::Framework {
                    capability: InternalCapability::Directory(_),
                    ..
                } => {
                    return Err(ModelError::unsupported(
                        "find_exposed_root_directory_capability does not support \
                        capabilities exposed from framework",
                    ));
                }
                _ => {
                    unreachable!(
                        "Capability source was not an exposed directory, which is impossible"
                    );
                }
            }
        }
    }
}

/// Walks the component tree to return the originating source of a capability, starting on the given
/// abs_moniker, as well as the final capability state.
async fn find_capability_source<'a>(
    capability: ComponentCapability,
    target_realm: &'a Arc<Realm>,
) -> Result<(CapabilitySource, CapabilityState), ModelError> {
    let starting_realm = target_realm.try_get_parent()?;
    let cap_state = CapabilityState::new(&capability);
    let mut pos = WalkPosition {
        capability,
        cap_state,
        last_child_moniker: target_realm.abs_moniker.leaf().cloned(),
        realm: starting_realm,
    };
    if let Some(source) = walk_offer_chain(&mut pos).await? {
        return Ok((source, pos.cap_state));
    }
    let source = walk_expose_chain(&mut pos).await?;
    Ok((source, pos.cap_state))
}

/// Follows `offer` declarations up the component tree, starting at `pos`. The algorithm looks
/// for a matching `offer` in the parent, as long as the `offer` is from `realm`.
///
/// Returns the source of the capability if found, or `None` if `expose` declarations must be
/// walked.
async fn walk_offer_chain<'a>(
    pos: &'a mut WalkPosition,
) -> Result<Option<CapabilitySource>, ModelError> {
    'offerloop: loop {
        if pos.at_componentmgr_realm() {
            // This is a built-in capability because the routing path was traced to the component
            // manager's realm.
            let capability = match &pos.capability {
                ComponentCapability::Use(use_decl) => {
                    InternalCapability::builtin_from_use_decl(use_decl).map_err(|_| {
                        ModelError::from(RoutingError::use_from_component_manager_not_found(
                            pos.capability.source_id(),
                        ))
                    })
                }
                ComponentCapability::Offer(offer_decl) => {
                    InternalCapability::builtin_from_offer_decl(offer_decl).map_err(|_| {
                        ModelError::from(RoutingError::offer_from_component_manager_not_found(
                            pos.capability.source_id(),
                        ))
                    })
                }
                ComponentCapability::Storage(storage_decl) => {
                    InternalCapability::builtin_from_storage_decl(storage_decl).map_err(|_| {
                        ModelError::from(RoutingError::storage_from_component_manager_not_found(
                            pos.capability.source_id(),
                        ))
                    })
                }
                _ => Err(ModelError::unsupported(format!(
                    "Built-in capability not supported: {:?}",
                    pos.capability,
                ))),
            }?;
            return Ok(Some(CapabilitySource::AboveRoot { capability }));
        }
        let cur_realm = pos.realm().clone();
        let cur_realm_state = cur_realm.lock_resolved_state().await?;
        let decl = cur_realm_state.decl();
        let last_child_moniker = pos.last_child_moniker.as_ref().expect("no child moniker");
        let offer =
            pos.capability.find_offer_source(decl, last_child_moniker).ok_or_else(|| match pos
                .capability
            {
                ComponentCapability::Use(_) => {
                    ModelError::from(RoutingError::use_from_parent_not_found(
                        &pos.abs_last_child_moniker(),
                        pos.capability.source_id(),
                    ))
                }
                ComponentCapability::Environment(_) => {
                    ModelError::from(RoutingError::environment_from_parent_not_found(
                        &pos.abs_last_child_moniker(),
                        pos.capability.type_name(),
                        pos.capability.source_id(),
                    ))
                }
                ComponentCapability::Offer(_) => {
                    ModelError::from(RoutingError::offer_from_parent_not_found(
                        &pos.abs_last_child_moniker(),
                        pos.capability.source_id(),
                    ))
                }
                ComponentCapability::Storage(_) => {
                    ModelError::from(RoutingError::storage_from_parent_not_found(
                        &pos.abs_last_child_moniker(),
                        pos.capability.source_id(),
                    ))
                }
                _ => {
                    panic!("Invalid offer target: {:?}", pos.capability);
                }
            })?;
        let source = match offer {
            OfferDecl::Service(_) => return Err(ModelError::unsupported("Service capability")),
            OfferDecl::Protocol(s) => OfferSource::Protocol(&s.source),
            OfferDecl::Directory(d) => OfferSource::Directory(&d.source),
            OfferDecl::Storage(s) => OfferSource::Storage(s.source()),
            OfferDecl::Runner(r) => OfferSource::Runner(&r.source),
            OfferDecl::Resolver(_) => return Err(ModelError::unsupported("Resolver capability")),
            OfferDecl::Event(e) => OfferSource::Event(&e.source),
        };
        let (dir_rights, subdir_decl) = match offer {
            OfferDecl::Directory(OfferDirectoryDecl { rights, subdir, .. }) => {
                (rights.map(Rights::from), subdir.clone())
            }
            _ => (None, None),
        };
        let event_filter = EventFilter::new(match offer {
            OfferDecl::Event(OfferEventDecl { filter, .. }) => filter.clone(),
            _ => None,
        });
        match source {
            OfferSource::Service(_) => {
                return Err(ModelError::unsupported("Service capability"));
            }
            OfferSource::Directory(OfferDirectorySource::Framework) => {
                // Directories offered or exposed directly from the framework are limited to
                // read-only rights.
                if let CapabilityState::Directory { rights_state, subdir } = &mut pos.cap_state {
                    *rights_state = rights_state.finalize(Some(Rights::from(*READ_RIGHTS)))?;
                    CapabilityState::update_subdir(subdir, subdir_decl);
                }
                let capability = InternalCapability::framework_from_offer_decl(offer)
                    .expect("not a framework offer declaration");
                return Ok(Some(CapabilitySource::Framework {
                    capability,
                    scope_moniker: pos.moniker().clone(),
                }));
            }
            OfferSource::Event(OfferEventSource::Framework) => {
                // An event offered from framework is scoped to the current realm.
                if let CapabilityState::Event { filter_state } = &mut pos.cap_state {
                    *filter_state = filter_state.finalize(Some(event_filter))?;
                }
                let capability = InternalCapability::framework_from_offer_decl(offer)
                    .expect("not a framework offer declaration");
                return Ok(Some(CapabilitySource::Framework {
                    capability,
                    scope_moniker: pos.moniker().clone(),
                }));
            }
            OfferSource::Protocol(OfferServiceSource::Parent)
            | OfferSource::Storage(OfferStorageSource::Parent)
            | OfferSource::Runner(OfferRunnerSource::Parent) => {
                // The offered capability comes from the parent, so follow the
                // parent
                pos.capability = ComponentCapability::Offer(offer.clone());
                pos.last_child_moniker = pos.moniker().path().last().map(|c| c.clone());
                pos.realm = cur_realm.try_get_parent()?;
                continue 'offerloop;
            }
            OfferSource::Event(OfferEventSource::Parent) => {
                // The offered capability comes from the parent, so follow the
                // parent
                if let CapabilityState::Event { filter_state } = &mut pos.cap_state {
                    *filter_state = filter_state.advance(Some(event_filter))?;
                }
                pos.capability = ComponentCapability::Offer(offer.clone());
                pos.last_child_moniker = pos.moniker().path().last().map(|c| c.clone());
                pos.realm = cur_realm.try_get_parent()?;
                continue 'offerloop;
            }
            OfferSource::Directory(OfferDirectorySource::Parent) => {
                if let CapabilityState::Directory { rights_state, subdir } = &mut pos.cap_state {
                    *rights_state = rights_state.advance(dir_rights)?;
                    CapabilityState::update_subdir(subdir, subdir_decl);
                }
                pos.capability = ComponentCapability::Offer(offer.clone());
                pos.last_child_moniker = pos.moniker().path().last().map(|c| c.clone());
                pos.realm = cur_realm.try_get_parent()?;
                continue 'offerloop;
            }
            OfferSource::Protocol(OfferServiceSource::Self_) => {
                match pos.capability.source_path() {
                    None => {
                        // The offered capability comes from the current component.
                        // Find the current component's Protocol declaration.
                        let cap = ComponentCapability::Offer(offer.clone());
                        return Ok(Some(CapabilitySource::Component {
                            capability: ComponentCapability::Protocol(
                                cap.find_protocol_source(decl)
                                    .expect("protocol offer references nonexistent section")
                                    .clone(),
                            ),
                            realm: cur_realm.as_weak(),
                        }));
                    }
                    Some(_) => {
                        // Legacy path: protocol is offered by path, get its info from the
                        // OfferDecl.
                        return Ok(Some(CapabilitySource::Component {
                            capability: ComponentCapability::Offer(offer.clone()),
                            realm: cur_realm.as_weak(),
                        }));
                    }
                }
            }
            OfferSource::Directory(OfferDirectorySource::Self_) => {
                match pos.capability.source_path() {
                    None => {
                        // The offered capability comes from the current component.  Update state
                        // and return a capability corresponding to the current component's
                        // Directory declaration.
                        let capability = ComponentCapability::Offer(offer.clone());
                        let capability =
                            pos.finalize_directory(&capability, decl, dir_rights, subdir_decl)?;
                        return Ok(Some(CapabilitySource::Component {
                            capability,
                            realm: cur_realm.as_weak(),
                        }));
                    }
                    Some(_) => {
                        // Legacy path: directory is offered by path, get its info from the
                        // OfferDecl.
                        if let CapabilityState::Directory { rights_state, subdir } =
                            &mut pos.cap_state
                        {
                            *rights_state = rights_state.finalize(dir_rights)?;
                            CapabilityState::update_subdir(subdir, subdir_decl);
                        }
                        return Ok(Some(CapabilitySource::Component {
                            capability: ComponentCapability::Offer(offer.clone()),
                            realm: cur_realm.as_weak(),
                        }));
                    }
                }
            }
            OfferSource::Runner(OfferRunnerSource::Self_) => {
                // The offered capability comes from the current component.
                // Find the current component's Runner declaration.
                let cap = ComponentCapability::Offer(offer.clone());
                return Ok(Some(CapabilitySource::Component {
                    capability: ComponentCapability::Runner(
                        cap.find_runner_source(decl)
                            .expect("runner offer references nonexistent section")
                            .clone(),
                    ),
                    realm: cur_realm.as_weak(),
                }));
            }
            OfferSource::Protocol(OfferServiceSource::Child(child_name))
            | OfferSource::Runner(OfferRunnerSource::Child(child_name)) => {
                // The offered capability comes from a child, break the loop
                // and begin walking the expose chain.
                pos.capability = ComponentCapability::Offer(offer.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.realm =
                    Some(cur_realm_state.get_live_child_realm(&partial).ok_or_else(|| {
                        ModelError::from(RoutingError::offer_from_child_expose_not_found(
                            &partial,
                            pos.moniker(),
                            pos.capability.source_id(),
                        ))
                    })?);
                return Ok(None);
            }
            OfferSource::Directory(OfferDirectorySource::Child(child_name)) => {
                if let CapabilityState::Directory { rights_state, subdir } = &mut pos.cap_state {
                    *rights_state = rights_state.advance(dir_rights)?;
                    CapabilityState::update_subdir(subdir, subdir_decl);
                }
                pos.capability = ComponentCapability::Offer(offer.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.realm = Some(cur_realm_state.get_live_child_realm(&partial).ok_or(
                    ModelError::from(RoutingError::offer_from_child_instance_not_found(
                        &partial,
                        pos.moniker(),
                        pos.capability.source_id(),
                    )),
                )?);
                return Ok(None);
            }
            OfferSource::Storage(OfferStorageSource::Storage(storage_name)) => {
                let storage = decl
                    .find_storage_source(&storage_name)
                    .expect("storage offer references nonexistent section");
                return Ok(Some(CapabilitySource::Component {
                    capability: ComponentCapability::Storage(storage.clone()),
                    realm: cur_realm.as_weak(),
                }));
            }
        }
    }
}

/// Follows `expose` declarations down the component tree, starting at `pos`. The algorithm looks
/// for a matching `expose` in the child, as long as the `expose` is not from `self`.
///
/// Returns the source of the capability.
async fn walk_expose_chain<'a>(pos: &'a mut WalkPosition) -> Result<CapabilitySource, ModelError> {
    loop {
        // TODO(xbhatnag): See if the locking needs to be over the entire loop
        // Consider -> let current_decl = { .. };
        let cur_realm = pos.realm().clone();
        let cur_realm_state = cur_realm.lock_resolved_state().await?;
        let decl = cur_realm_state.decl();
        let expose =
            pos.capability.find_expose_source(decl).ok_or_else(|| match pos.capability {
                ComponentCapability::UsedExpose(_) => ModelError::from(
                    RoutingError::used_expose_not_found(pos.moniker(), pos.capability.source_id()),
                ),
                ComponentCapability::Environment(_) => {
                    let partial =
                        pos.moniker().leaf().expect("impossible source above root").to_partial();
                    ModelError::from(RoutingError::environment_from_child_expose_not_found(
                        &partial,
                        pos.moniker().parent().as_ref().expect("impossible source above root"),
                        pos.capability.type_name(),
                        pos.capability.source_id(),
                    ))
                }
                ComponentCapability::Expose(_) => {
                    let partial =
                        pos.moniker().leaf().expect("impossible source above root").to_partial();
                    ModelError::from(RoutingError::expose_from_child_expose_not_found(
                        &partial,
                        pos.moniker().parent().as_ref().expect("impossible source above root"),
                        pos.capability.source_id(),
                    ))
                }
                ComponentCapability::Offer(_) => {
                    let partial =
                        pos.moniker().leaf().expect("impossible source above root").to_partial();
                    ModelError::from(RoutingError::offer_from_child_expose_not_found(
                        &partial,
                        pos.moniker().parent().as_ref().expect("impossible source above root"),
                        pos.capability.source_id(),
                    ))
                }
                ComponentCapability::Storage(_) => {
                    let partial =
                        pos.moniker().leaf().expect("impossible source above root").to_partial();
                    ModelError::from(RoutingError::storage_from_child_expose_not_found(
                        &partial,
                        pos.moniker().parent().as_ref().expect("impossible source above root"),
                        pos.capability.source_id(),
                    ))
                }
                _ => {
                    unreachable!(
                        "Searched for an expose declaration at `{}` for `{}`, but the \
                            source doesn't seem like it should map to an expose declaration",
                        pos.moniker().parent().expect("impossible source above root"),
                        pos.capability.source_id()
                    );
                }
            })?;
        let (source, target) = match expose {
            ExposeDecl::Service(_) => return Err(ModelError::unsupported("Service capability")),
            ExposeDecl::Protocol(ls) => (CapabilityExposeSource::Protocol(&ls.source), &ls.target),
            ExposeDecl::Directory(d) => (CapabilityExposeSource::Directory(&d.source), &d.target),
            ExposeDecl::Runner(r) => (CapabilityExposeSource::Runner(&r.source), &r.target),
            ExposeDecl::Resolver(_) => return Err(ModelError::unsupported("Resolver capability")),
        };
        if target != &ExposeTarget::Parent {
            let partial = pos.moniker().leaf().expect("impossible source above root").to_partial();
            return Err(RoutingError::expose_from_child_expose_not_found(
                &partial,
                pos.moniker().parent().as_ref().expect("impossible source above root"),
                pos.capability.source_id(),
            )
            .into());
        }
        let (dir_rights, subdir_decl) = match expose {
            ExposeDecl::Directory(ExposeDirectoryDecl { rights, subdir, .. }) => {
                (rights.map(Rights::from), subdir.clone())
            }
            _ => (None, None),
        };
        match source {
            CapabilityExposeSource::Protocol(ExposeSource::Self_) => {
                match pos.capability.source_path() {
                    None => {
                        // The exposed capability comes from the current component.
                        // Find the current component's Protocol declaration.
                        let cap = ComponentCapability::Expose(expose.clone());
                        return Ok(CapabilitySource::Component {
                            capability: ComponentCapability::Protocol(
                                cap.find_protocol_source(decl)
                                    .expect("protocol offer references nonexistent section")
                                    .clone(),
                            ),
                            realm: cur_realm.as_weak(),
                        });
                    }
                    Some(_) => {
                        // Legacy path: protocol is exposed by path, get its info from the
                        // ExposeDecl.
                        return Ok(CapabilitySource::Component {
                            capability: ComponentCapability::Expose(expose.clone()),
                            realm: cur_realm.as_weak(),
                        });
                    }
                }
            }
            CapabilityExposeSource::Directory(ExposeSource::Self_) => {
                match pos.capability.source_path() {
                    None => {
                        // The offered capability comes from the current component.  Update state
                        // and return a capability corresponding to the current component's
                        // Directory declaration.
                        let capability = ComponentCapability::Expose(expose.clone());
                        let capability =
                            pos.finalize_directory(&capability, decl, dir_rights, subdir_decl)?;
                        return Ok(CapabilitySource::Component {
                            capability,
                            realm: cur_realm.as_weak(),
                        });
                    }
                    Some(_) => {
                        // Legacy path: directory is exposed by path, get its info from the
                        // ExposeDecl.
                        if let CapabilityState::Directory { rights_state, subdir } =
                            &mut pos.cap_state
                        {
                            *rights_state = rights_state.finalize(dir_rights)?;
                            CapabilityState::update_subdir(subdir, subdir_decl);
                        }
                        return Ok(CapabilitySource::Component {
                            capability: ComponentCapability::Expose(expose.clone()),
                            realm: cur_realm.as_weak(),
                        });
                    }
                }
            }
            CapabilityExposeSource::Runner(ExposeSource::Self_) => {
                // The exposed capability comes from the current component.
                // Find the current component's Runner declaration.
                let cap = ComponentCapability::Expose(expose.clone());
                return Ok(CapabilitySource::Component {
                    capability: ComponentCapability::Runner(
                        cap.find_runner_source(decl)
                            .expect(&format!(
                                "An `expose from runner` declaration was found at `{}` for `{}`
                                with no corresponding runner declaration. This ComponentDecl should
                                not have passed validation.",
                                pos.moniker(),
                                cap.source_id()
                            ))
                            .clone(),
                    ),
                    realm: cur_realm.as_weak(),
                });
            }
            CapabilityExposeSource::Protocol(ExposeSource::Child(child_name))
            | CapabilityExposeSource::Runner(ExposeSource::Child(child_name)) => {
                // The offered capability comes from a child, so follow the child.
                pos.capability = ComponentCapability::Expose(expose.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.realm = Some(cur_realm_state.get_live_child_realm(&partial).ok_or(
                    ModelError::from(RoutingError::expose_from_child_instance_not_found(
                        &partial,
                        pos.moniker(),
                        pos.capability.source_id(),
                    )),
                )?);
                continue;
            }
            CapabilityExposeSource::Directory(ExposeSource::Child(child_name)) => {
                if let CapabilityState::Directory { rights_state, subdir } = &mut pos.cap_state {
                    *rights_state = rights_state.advance(dir_rights)?;
                    CapabilityState::update_subdir(subdir, subdir_decl);
                }
                // The offered capability comes from a child, so follow the child.
                pos.capability = ComponentCapability::Expose(expose.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.realm = Some(cur_realm_state.get_live_child_realm(&partial).ok_or(
                    ModelError::from(RoutingError::expose_from_child_instance_not_found(
                        &partial,
                        pos.moniker(),
                        pos.capability.source_id(),
                    )),
                )?);
                continue;
            }
            CapabilityExposeSource::Protocol(ExposeSource::Framework) => {
                let capability =
                    InternalCapability::framework_from_expose_decl(expose).map_err(|_| {
                        ModelError::from(RoutingError::expose_from_framework_not_found(
                            pos.moniker(),
                            pos.capability.source_id(),
                        ))
                    })?;
                return Ok(CapabilitySource::Framework {
                    capability,
                    scope_moniker: pos.moniker().clone(),
                });
            }
            CapabilityExposeSource::Directory(ExposeSource::Framework) => {
                // Directories offered or exposed directly from the framework are limited to
                // read-only rights.
                if let CapabilityState::Directory { rights_state, subdir } = &mut pos.cap_state {
                    *rights_state = rights_state.finalize(Some(Rights::from(*READ_RIGHTS)))?;
                    CapabilityState::update_subdir(subdir, subdir_decl);
                }
                let capability =
                    InternalCapability::framework_from_expose_decl(expose).map_err(|_| {
                        ModelError::from(RoutingError::expose_from_framework_not_found(
                            pos.moniker(),
                            pos.capability.source_id(),
                        ))
                    })?;
                return Ok(CapabilitySource::Framework {
                    capability,
                    scope_moniker: pos.moniker().clone(),
                });
            }
            CapabilityExposeSource::Runner(ExposeSource::Framework) => {
                // Currently we don't expose any runners from `framework`.
                // TODO: This error should be caught by validation. We shouldn't have to handle
                // this case here.
                return Err(RoutingError::expose_from_framework_not_found(
                    pos.moniker(),
                    pos.capability.source_id(),
                )
                .into());
            }
        }
    }
}

/// Sets an epitaph on `server_end` for a capability routing failure, and logs the error. Logs a failure to route a capability. Formats `err` as a `String`, but elides the type if the
/// error is a `RoutingError`, the common case.
pub(super) fn report_routing_failure(
    target_moniker: &AbsoluteMoniker,
    cap: &ComponentCapability,
    err: &ModelError,
    server_end: zx::Channel,
) {
    let _ = server_end.close_with_epitaph(routing_epitaph(err));
    let err_str = match err {
        ModelError::RoutingError { err } => format!("{}", err),
        _ => format!("{}", err),
    };
    error!(
        "Failed to route {} `{}` from component `{}`: {}",
        cap.type_name(),
        cap.source_id(),
        target_moniker,
        err_str,
    );
}

/// Converts `err` to a `zx::Status` to use as an epitaph on a routed channel.
fn routing_epitaph(err: &ModelError) -> zx::Status {
    match err {
        ModelError::RoutingError { err } => err.as_zx_status(),
        ModelError::RightsError { err } => err.as_zx_status(),
        // Any other type of error is not expected.
        _ => zx::Status::INTERNAL,
    }
}
