// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{
            CapabilityProvider, CapabilitySource, ComponentCapability, FrameworkCapability,
        },
        model::{
            binding::Binder,
            error::ModelError,
            hooks::{Event, EventPayload},
            model::Model,
            moniker::{AbsoluteMoniker, ChildMoniker, PartialMoniker, RelativeMoniker},
            realm::Realm,
            rights::{RightWalkState, Rights, READ_RIGHTS, WRITE_RIGHTS},
            storage,
        },
    },
    anyhow::format_err,
    async_trait::async_trait,
    cm_rust::{
        self, CapabilityPath, ExposeDecl, ExposeDirectoryDecl, ExposeTarget, OfferDecl,
        OfferDirectorySource, OfferRunnerSource, OfferServiceSource, OfferStorageSource,
        StorageDirectorySource, UseDecl, UseStorageDecl,
    },
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::sync::Arc,
};
const SERVICE_OPEN_FLAGS: u32 =
    fio::OPEN_FLAG_DESCRIBE | fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;

/// Describes the source of a capability, for any type of capability.
#[derive(Debug)]
enum OfferSource<'a> {
    // TODO(CF-908): Enable this once unified services are implemented.
    #[allow(dead_code)]
    Service(&'a OfferServiceSource),
    Protocol(&'a OfferServiceSource),
    Directory(&'a OfferDirectorySource),
    Storage(&'a OfferStorageSource),
    Runner(&'a OfferRunnerSource),
}

/// Describes the source of a capability, for any type of capability.
#[derive(Debug)]
enum ExposeSource<'a> {
    Protocol(&'a cm_rust::ExposeSource),
    Directory(&'a cm_rust::ExposeSource),
    Runner(&'a cm_rust::ExposeSource),
}

/// Finds the source of the `capability` used by `absolute_moniker`, and pass along the
/// `server_chan` to the hosting component's out directory (or componentmgr's namespace, if
/// applicable) using an open request with `open_mode`.
pub(super) async fn route_use_capability<'a>(
    model: &'a Model,
    flags: u32,
    open_mode: u32,
    relative_path: String,
    use_decl: &'a UseDecl,
    target_realm: &'a Arc<Realm>,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    match use_decl {
        UseDecl::Service(_) | UseDecl::Protocol(_) | UseDecl::Directory(_) | UseDecl::Runner(_) => {
            let source = find_used_capability_source(use_decl, target_realm).await?;
            open_capability_at_source(
                model,
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
            route_and_open_storage_capability(
                model,
                storage_decl,
                open_mode,
                target_realm,
                server_chan,
            )
            .await
        }
    }
}

/// Finds the source of the expose capability used at `source_path` by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable)
pub(super) async fn route_expose_capability<'a>(
    model: &'a Model,
    flags: u32,
    open_mode: u32,
    expose_decl: &'a ExposeDecl,
    target_realm: &'a Arc<Realm>,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let capability = ComponentCapability::Expose(expose_decl.clone());
    let mut pos = WalkPosition {
        capability,
        last_child_moniker: None,
        realm: Some(target_realm.clone()),
        rights_state: RightWalkState::new(),
    };
    let source = walk_expose_chain(&mut pos).await?;
    open_capability_at_source(
        model,
        flags,
        open_mode,
        String::new(),
        source,
        target_realm,
        server_chan,
    )
    .await
}

/// The default provider for a ComponentCapability.
/// This provider will bind to the source moniker's realm and then open the service
/// from the realm's outgoing directory.
struct DefaultComponentCapabilityProvider {
    model: Model,
    path: CapabilityPath,
    source_moniker: AbsoluteMoniker,
}

#[async_trait]
impl CapabilityProvider for DefaultComponentCapabilityProvider {
    async fn open(
        self: Box<Self>,
        flags: u32,
        open_mode: u32,
        _relative_path: String,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        // Start the source component, if necessary
        let source_realm = self.model.bind(&self.source_moniker).await?;
        source_realm.open_outgoing(flags, open_mode, &self.path, server_end).await?;
        Ok(())
    }
}

/// This method gets an optional default capability provider based on the
/// capability source.
fn get_default_provider(
    model: &Model,
    source: &CapabilitySource,
) -> Option<Box<dyn CapabilityProvider>> {
    match source {
        CapabilitySource::Framework { .. } => {
            // There is no default provider for a Framework capability
            None
        }
        CapabilitySource::Component { capability, source_moniker } => {
            // Route normally for a component capability with a source path
            if let Some(path) = capability.source_path() {
                Some(Box::new(DefaultComponentCapabilityProvider {
                    model: model.clone(),
                    path: path.clone(),
                    source_moniker: source_moniker.clone(),
                }))
            } else {
                None
            }
        }
        _ => None,
    }
}

/// Returns the optional path for a global framework capability, None otherwise.
pub fn get_framework_capability_path(source: &CapabilitySource) -> Option<&CapabilityPath> {
    match source {
        CapabilitySource::Framework { capability, scope_moniker: None } => capability.path(),
        _ => None,
    }
}

/// Open the capability at the given source, binding to its component instance if necessary.
pub async fn open_capability_at_source(
    model: &Model,
    flags: u32,
    open_mode: u32,
    relative_path: String,
    source: CapabilitySource,
    target_realm: &Arc<Realm>,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let capability_provider = Arc::new(Mutex::new(get_default_provider(model, &source)));
    let event = Event::new(
        target_realm.abs_moniker.clone(),
        EventPayload::RouteCapability {
            source: source.clone(),
            capability_provider: capability_provider.clone(),
        },
    );

    // This hack changes the flags for a scoped framework service
    let mut flags = flags;
    if let CapabilitySource::Framework { scope_moniker: Some(_), .. } = source {
        flags = SERVICE_OPEN_FLAGS;
    }

    // Get a capability provider from the tree
    target_realm.hooks.dispatch(&event).await?;
    let capability_provider = capability_provider.lock().await.take();

    // If a hook in the component tree gave a capability provider, then use it.
    if let Some(capability_provider) = capability_provider {
        capability_provider.open(flags, open_mode, relative_path, server_chan).await?;
        Ok(())
    } else if let Some(path) = get_framework_capability_path(&source) {
        // TODO(fsamuel): This is a temporary hack. If a global path-based framework capability
        // is not provided by a hook in the component tree, then attempt to connect to the service
        // in component manager's namespace. We could have modeled this as a default provider,
        // but several hooks (such as WorkScheduler) require that a provider is not set.
        io_util::connect_in_namespace(&path.to_string(), server_chan, flags)
            .map_err(|e| ModelError::capability_discovery_error(e))
    } else {
        Err(ModelError::capability_discovery_error(format_err!(
            "No providers for this capability were found!"
        )))
    }
}

/// Routes a `UseDecl::Storage` to the component instance providing the backing directory and
/// opens its isolated storage with `server_chan`.
pub async fn route_and_open_storage_capability<'a>(
    model: &'a Model,
    use_decl: &'a UseStorageDecl,
    open_mode: u32,
    target_realm: &'a Arc<Realm>,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let (dir_source_moniker, dir_source_path, relative_moniker) =
        route_storage_capability(model, use_decl, target_realm).await?;
    let storage_dir_proxy = storage::open_isolated_storage(
        &model,
        dir_source_moniker,
        &dir_source_path,
        use_decl.type_(),
        &relative_moniker,
        open_mode,
    )
    .await
    .map_err(|e| ModelError::from(e))?;

    // clone the final connection to connect the channel we're routing to its destination
    storage_dir_proxy
        .clone(fio::CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(server_chan))
        .map_err(|e| ModelError::capability_discovery_error(format_err!("failed clone {}", e)))?;
    Ok(())
}

/// Routes a `UseDecl::Storage` to the component instance providing the backing directory and
/// deletes its isolated storage.
pub(super) async fn route_and_delete_storage<'a>(
    model: &'a Model,
    use_decl: &'a UseStorageDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<(), ModelError> {
    let (dir_source_moniker, dir_source_path, relative_moniker) =
        route_storage_capability(model, use_decl, target_realm).await?;
    storage::delete_isolated_storage(
        &model,
        dir_source_moniker,
        &dir_source_path,
        &relative_moniker,
    )
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
    model: &'a Model,
    use_decl: &'a UseStorageDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<(Option<AbsoluteMoniker>, CapabilityPath, RelativeMoniker), ModelError> {
    // Walk the offer chain to find the storage decl
    let parent_realm =
        target_realm.try_get_parent()?.ok_or(ModelError::capability_discovery_error(
            format_err!("storage capabilities cannot come from component manager's namespace"),
        ))?;

    // Storage capabilities are always require rw rights to be valid so this must be
    // explicitly encoded into its starting walk state.
    let mut pos = WalkPosition {
        capability: ComponentCapability::Use(UseDecl::Storage(use_decl.clone())),
        last_child_moniker: target_realm.abs_moniker.path().last().map(|c| c.clone()),
        realm: Some(parent_realm),
        rights_state: RightWalkState::at(Rights::from(*READ_RIGHTS | *WRITE_RIGHTS)),
    };

    let source = walk_offer_chain(&mut pos).await?;

    let (storage_decl, storage_decl_moniker) = match source {
        Some(CapabilitySource::StorageDecl(decl, moniker)) => (decl, moniker),
        _ => {
            return Err(ModelError::capability_discovery_error(format_err!(
                "storage capabilities must come from a storage declaration"
            )))
        }
    };
    let relative_moniker =
        RelativeMoniker::from_absolute(&storage_decl_moniker, &target_realm.abs_moniker);

    // Find the path and source of the directory consumed by the storage capability.
    let (dir_source_path, dir_source_moniker) = match storage_decl.source {
        StorageDirectorySource::Self_ => (storage_decl.source_path, Some(storage_decl_moniker)),
        StorageDirectorySource::Realm => {
            let capability = ComponentCapability::Storage(storage_decl);
            let storage_decl_realm = model.look_up_realm(&storage_decl_moniker).await?;
            let source = find_capability_source(capability, &storage_decl_realm).await?;
            match source {
                CapabilitySource::Component { capability, source_moniker } => {
                    (capability.source_path().unwrap().clone(), Some(source_moniker))
                }
                CapabilitySource::Framework { capability, scope_moniker: None } => {
                    (capability.path().unwrap().clone(), None)
                }
                CapabilitySource::Framework { scope_moniker: Some(_), .. } => panic!(
                    "using scoped framework capabilities for storage declarations is unsupported"
                ),
                CapabilitySource::StorageDecl(..) => {
                    panic!("storage directory sources can't come from storage declarations")
                }
            }
        }
        StorageDirectorySource::Child(ref name) => {
            let mut pos = {
                let partial = PartialMoniker::new(name.to_string(), None);
                let storage_decl_realm = model.look_up_realm(&storage_decl_moniker).await?;
                let realm_state = storage_decl_realm.lock_state().await;
                let realm_state =
                    realm_state.as_ref().expect("route_storage_capability: not resolved");
                let child_realm = realm_state.get_live_child_realm(&partial).ok_or(
                    ModelError::capability_discovery_error(format_err!(
                        "no child {} found from component {} for storage directory source",
                        partial,
                        pos.moniker().clone(),
                    )),
                )?;
                let capability = ComponentCapability::Storage(storage_decl);
                WalkPosition {
                    capability,
                    last_child_moniker: None,
                    realm: Some(child_realm),
                    rights_state: pos.rights_state.clone(),
                }
            };
            match walk_expose_chain(&mut pos).await? {
                CapabilitySource::Component { capability, source_moniker } => {
                    (capability.source_path().unwrap().clone(), Some(source_moniker))
                }
                _ => {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "storage capability backing directories must be provided by a component"
                    )))
                }
            }
        }
    };
    Ok((dir_source_moniker, dir_source_path, relative_moniker))
}

/// Check if a used capability is a framework service, and if so return a framework `CapabilitySource`.
async fn find_scoped_framework_capability_source<'a>(
    use_decl: &'a UseDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<Option<CapabilitySource>, ModelError> {
    if let Ok(capability) = FrameworkCapability::framework_from_use_decl(use_decl) {
        return Ok(Some(CapabilitySource::Framework {
            capability,
            scope_moniker: Some(target_realm.abs_moniker.clone()),
        }));
    }
    return Ok(None);
}

/// Holds state about the current position when walking the tree.
#[derive(Debug)]
struct WalkPosition {
    /// The capability declaration as it's represented in the current component.
    capability: ComponentCapability,
    /// The moniker of the child we came from.
    last_child_moniker: Option<ChildMoniker>,
    /// The realm of the component we are currently looking at. `None` for component manager's
    /// realm.
    realm: Option<Arc<Realm>>,
    /// Holds the state of the rights walk. This is used to enforce directory rights.
    rights_state: RightWalkState,
}

impl WalkPosition {
    fn realm(&self) -> &Arc<Realm> {
        &self.realm.as_ref().unwrap()
    }

    fn moniker(&self) -> &AbsoluteMoniker {
        &self.realm.as_ref().unwrap().abs_moniker
    }

    fn at_componentmgr_realm(&self) -> bool {
        self.realm.is_none()
    }
}

async fn find_used_capability_source<'a>(
    use_decl: &'a UseDecl,
    target_realm: &'a Arc<Realm>,
) -> Result<CapabilitySource, ModelError> {
    if let Some(framework_capability) =
        find_scoped_framework_capability_source(use_decl, target_realm).await?
    {
        return Ok(framework_capability);
    }
    let capability = ComponentCapability::Use(use_decl.clone());
    find_capability_source(capability, target_realm).await
}

/// Finds the providing realm and path of a directory exposed by the root realm to component
/// manager.
pub async fn find_exposed_root_directory_capability(
    model: &Model,
    path: CapabilityPath,
) -> Result<(CapabilityPath, AbsoluteMoniker), ModelError> {
    let expose_dir_decl = {
        let realm_state = model.root_realm.lock_state().await;
        let root_decl = realm_state
            .as_ref()
            .expect("find_exposed_root_directory_capability: not resolved")
            .decl();
        root_decl
            .exposes
            .iter()
            .find_map(|e| match e {
                ExposeDecl::Directory(dir_decl) if dir_decl.target_path == path => Some(dir_decl),
                _ => None,
            })
            .ok_or(ModelError::capability_discovery_error(format_err!(
                "root component does not expose directory {:?}",
                path
            )))?
            .clone()
    };
    match &expose_dir_decl.source {
        cm_rust::ExposeSource::Framework => {
            return Err(ModelError::capability_discovery_error(format_err!(
                "root realm cannot expose framework directories"
            )))
        }
        cm_rust::ExposeSource::Self_ => {
            return Ok((expose_dir_decl.source_path.clone(), AbsoluteMoniker::root()))
        }
        cm_rust::ExposeSource::Child(_) => {
            let mut wp = WalkPosition {
                capability: ComponentCapability::Expose(ExposeDecl::Directory(
                    expose_dir_decl.clone(),
                )),
                last_child_moniker: None,
                realm: Some(model.root_realm.clone()),
                rights_state: RightWalkState::new(),
            };
            let capability_source = walk_expose_chain(&mut wp).await?;
            match capability_source {
                CapabilitySource::Component {
                    capability:
                        ComponentCapability::Expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                            source_path,
                            ..
                        })),
                    source_moniker,
                } => return Ok((source_path, source_moniker)),
                _ => {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "unexpected capability source"
                    )))
                }
            }
        }
    }
}

/// Walks the component tree to find the originating source of a capability, starting on the given
/// abs_moniker. It returns the absolute moniker of the originating component, a reference to its
/// realm, and the capability exposed or offered at the originating source. If the absolute moniker
/// and realm are None, then the capability originates at the returned path in componentmgr's
/// namespace.
async fn find_capability_source<'a>(
    capability: ComponentCapability,
    target_realm: &'a Arc<Realm>,
) -> Result<CapabilitySource, ModelError> {
    let starting_realm = target_realm.try_get_parent()?;
    let mut pos = WalkPosition {
        capability,
        last_child_moniker: target_realm.abs_moniker.path().last().map(|c| c.clone()),
        realm: starting_realm,
        rights_state: RightWalkState::new(),
    };
    if let Some(source) = walk_offer_chain(&mut pos).await? {
        return Ok(source);
    }
    walk_expose_chain(&mut pos).await
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
                    FrameworkCapability::builtin_from_use_decl(use_decl).map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching use found for capability {:?}",
                            pos.capability,
                        ))
                    })
                }
                ComponentCapability::Offer(offer_decl) => {
                    FrameworkCapability::builtin_from_offer_decl(offer_decl).map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching offers found for capability {:?}",
                            pos.capability,
                        ))
                    })
                }
                ComponentCapability::Storage(storage_decl) => {
                    FrameworkCapability::builtin_from_storage_decl(storage_decl).map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching directories found for storage capability {:?}",
                            pos.capability,
                        ))
                    })
                }
                _ => Err(ModelError::capability_discovery_error(format_err!(
                    "Unsupported capability {:?}",
                    pos.capability,
                ))),
            }?;

            return Ok(Some(CapabilitySource::Framework { capability, scope_moniker: None }));
        }
        let cur_realm = pos.realm().clone();
        let cur_realm_state = cur_realm.lock_state().await;
        let cur_realm_state = cur_realm_state.as_ref().expect("walk_offer_chain: not resolved");
        // This `decl()` is safe because the component must have been resolved to get here
        let decl = cur_realm_state.decl();
        let last_child_moniker = pos.last_child_moniker.as_ref().unwrap();
        let offer = pos.capability.find_offer_source(decl, last_child_moniker).ok_or(
            ModelError::capability_discovery_error(format_err!(
                "no matching offers found for capability {:?} from component {}",
                pos.capability,
                pos.moniker(),
            )),
        )?;
        let source = match offer {
            OfferDecl::Service(_) => return Err(ModelError::unsupported("Service capability")),
            OfferDecl::Protocol(s) => OfferSource::Protocol(&s.source),
            OfferDecl::Directory(d) => OfferSource::Directory(&d.source),
            OfferDecl::Storage(s) => OfferSource::Storage(s.source()),
            OfferDecl::Runner(r) => OfferSource::Runner(&r.source),
        };
        let dir_rights = match offer {
            OfferDecl::Directory(offer_dir) => offer_dir.rights.map(Rights::from),
            _ => None,
        };
        match source {
            OfferSource::Service(_) => {
                return Err(ModelError::unsupported("Service capability"));
            }
            OfferSource::Directory(OfferDirectorySource::Framework) => {
                // Directories offered or exposed directly from the framework are limited to
                // read-only rights.
                pos.rights_state = pos.rights_state.finalize(Some(Rights::from(*READ_RIGHTS)))?;
                let capability =
                    FrameworkCapability::framework_from_offer_decl(offer).map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching offers found for capability {:?} from component {}",
                            pos.capability,
                            pos.moniker(),
                        ))
                    })?;
                return Ok(Some(CapabilitySource::Framework {
                    capability,
                    scope_moniker: Some(pos.moniker().clone()),
                }));
            }
            OfferSource::Protocol(OfferServiceSource::Realm)
            | OfferSource::Storage(OfferStorageSource::Realm)
            | OfferSource::Runner(OfferRunnerSource::Realm) => {
                // The offered capability comes from the realm, so follow the
                // parent
                pos.capability = ComponentCapability::Offer(offer.clone());
                pos.last_child_moniker = pos.moniker().path().last().map(|c| c.clone());
                pos.realm = cur_realm.try_get_parent()?;
                continue 'offerloop;
            }
            OfferSource::Directory(OfferDirectorySource::Realm) => {
                pos.rights_state = pos.rights_state.advance(dir_rights)?;
                pos.capability = ComponentCapability::Offer(offer.clone());
                pos.last_child_moniker = pos.moniker().path().last().map(|c| c.clone());
                pos.realm = cur_realm.try_get_parent()?;
                continue 'offerloop;
            }
            OfferSource::Protocol(OfferServiceSource::Self_) => {
                // The offered capability comes from the current component,
                // return our current location in the tree.
                return Ok(Some(CapabilitySource::Component {
                    capability: ComponentCapability::Offer(offer.clone()),
                    source_moniker: pos.moniker().clone(),
                }));
            }
            OfferSource::Directory(OfferDirectorySource::Self_) => {
                pos.rights_state = pos.rights_state.finalize(dir_rights)?;
                return Ok(Some(CapabilitySource::Component {
                    capability: ComponentCapability::Offer(offer.clone()),
                    source_moniker: pos.moniker().clone(),
                }));
            }
            OfferSource::Runner(OfferRunnerSource::Self_) => {
                // The offered capability comes from the current component.
                // Find the current component's Runner declaration.
                let cap = ComponentCapability::Offer(offer.clone());
                return Ok(Some(CapabilitySource::Component {
                    capability: ComponentCapability::Runner(
                        cap.find_runner_source(decl)
                            .ok_or(ModelError::capability_discovery_error(format_err!(
                                concat!(
                                    "component {} attempted to offer runner {:?}, ",
                                    "but no matching runner declaration was found"
                                ),
                                pos.moniker(),
                                cap.source_name().unwrap(),
                            )))?
                            .clone(),
                    ),
                    source_moniker: pos.moniker().clone(),
                }));
            }
            OfferSource::Protocol(OfferServiceSource::Child(child_name))
            | OfferSource::Runner(OfferRunnerSource::Child(child_name)) => {
                // The offered capability comes from a child, break the loop
                // and begin walking the expose chain.
                pos.capability = ComponentCapability::Offer(offer.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.realm = Some(cur_realm_state.get_live_child_realm(&partial).ok_or(
                    ModelError::capability_discovery_error(format_err!(
                        "no child {} found from component {} for offer source",
                        partial,
                        pos.moniker().clone(),
                    )),
                )?);
                return Ok(None);
            }
            OfferSource::Directory(OfferDirectorySource::Child(child_name)) => {
                pos.rights_state = pos.rights_state.advance(dir_rights)?;
                pos.capability = ComponentCapability::Offer(offer.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.realm = Some(cur_realm_state.get_live_child_realm(&partial).ok_or(
                    ModelError::capability_discovery_error(format_err!(
                        "no child {} found from component {} for offer source",
                        partial,
                        pos.moniker().clone(),
                    )),
                )?);
                return Ok(None);
            }
            OfferSource::Storage(OfferStorageSource::Storage(storage_name)) => {
                let storage = decl
                    .find_storage_source(&storage_name)
                    .expect("storage offer references nonexistent section");
                return Ok(Some(CapabilitySource::StorageDecl(
                    storage.clone(),
                    pos.moniker().clone(),
                )));
            }
        }
    }
}

/// Follows `expose` declarations down the component tree, starting at `pos`. The algorithm looks
/// for a matching `expose` in the child, as long as the `expose` is not from `self`.
///
/// Returns the source of the capability.
async fn walk_expose_chain<'a>(pos: &'a mut WalkPosition) -> Result<CapabilitySource, ModelError> {
    // If the first capability is an Expose, assume it corresponds to an Expose declared by the
    // first component.
    let mut first_expose = {
        match &pos.capability {
            ComponentCapability::Expose(e) => Some(e.clone()),
            _ => None,
        }
    };
    loop {
        // TODO(xbhatnag): See if the locking needs to be over the entire loop
        // Consider -> let current_decl = { .. };
        let cur_realm = pos.realm().clone();
        Realm::resolve_decl(&cur_realm).await?;
        let cur_realm_state = cur_realm.lock_state().await;
        let cur_realm_state = cur_realm_state.as_ref().expect("walk_expose_chain: not resolved");
        let first_expose = first_expose.take();
        let expose = first_expose
            .as_ref()
            .or_else(|| pos.capability.find_expose_source(cur_realm_state.decl()))
            .ok_or(ModelError::capability_discovery_error(format_err!(
                "no matching exposes found for capability {:?} from component {}",
                pos.capability,
                pos.moniker(),
            )))?;
        let (source, target) = match expose {
            ExposeDecl::Service(_) => return Err(ModelError::unsupported("Service capability")),
            ExposeDecl::Protocol(ls) => (ExposeSource::Protocol(&ls.source), &ls.target),
            ExposeDecl::Directory(d) => (ExposeSource::Directory(&d.source), &d.target),
            ExposeDecl::Runner(r) => (ExposeSource::Runner(&r.source), &r.target),
        };
        if target != &ExposeTarget::Realm {
            return Err(ModelError::capability_discovery_error(format_err!(
                "matching exposed capability {:?} from component {} has non-realm target",
                pos.capability,
                pos.moniker()
            )));
        }
        let dir_rights = match expose {
            ExposeDecl::Directory(expose_dir) => expose_dir.rights.map(Rights::from),
            _ => None,
        };
        match source {
            ExposeSource::Protocol(cm_rust::ExposeSource::Self_) => {
                // The offered capability comes from the current component, return our
                // current location in the tree.
                return Ok(CapabilitySource::Component {
                    capability: ComponentCapability::Expose(expose.clone()),
                    source_moniker: pos.moniker().clone(),
                });
            }
            ExposeSource::Directory(cm_rust::ExposeSource::Self_) => {
                pos.rights_state = pos.rights_state.finalize(dir_rights)?;
                return Ok(CapabilitySource::Component {
                    capability: ComponentCapability::Expose(expose.clone()),
                    source_moniker: pos.moniker().clone(),
                });
            }
            ExposeSource::Runner(cm_rust::ExposeSource::Self_) => {
                // The exposed capability comes from the current component.
                // Find the current component's Runner declaration.
                let cap = ComponentCapability::Expose(expose.clone());
                return Ok(CapabilitySource::Component {
                    capability: ComponentCapability::Runner(
                        cap.find_runner_source(cur_realm_state.decl())
                            .ok_or(ModelError::capability_discovery_error(format_err!(
                                concat!(
                                    "component {} attempted to expose runner {:?}, ",
                                    "but no matching runner declaration was found"
                                ),
                                pos.moniker(),
                                cap.source_name().unwrap(),
                            )))?
                            .clone(),
                    ),
                    source_moniker: pos.moniker().clone(),
                });
            }
            ExposeSource::Protocol(cm_rust::ExposeSource::Child(child_name))
            | ExposeSource::Runner(cm_rust::ExposeSource::Child(child_name)) => {
                // The offered capability comes from a child, so follow the child.
                pos.capability = ComponentCapability::Expose(expose.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.realm = Some(cur_realm_state.get_live_child_realm(&partial).ok_or(
                    ModelError::capability_discovery_error(format_err!(
                        "no child {} found from component {} for offer source",
                        partial,
                        pos.moniker().clone(),
                    )),
                )?);
                continue;
            }
            ExposeSource::Directory(cm_rust::ExposeSource::Child(child_name)) => {
                pos.rights_state = pos.rights_state.advance(dir_rights)?;
                // The offered capability comes from a child, so follow the child.
                pos.capability = ComponentCapability::Expose(expose.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.realm = Some(cur_realm_state.get_live_child_realm(&partial).ok_or(
                    ModelError::capability_discovery_error(format_err!(
                        "no child {} found from component {} for offer source",
                        partial,
                        pos.moniker().clone(),
                    )),
                )?);
                continue;
            }
            ExposeSource::Protocol(cm_rust::ExposeSource::Framework) => {
                let capability =
                    FrameworkCapability::framework_from_expose_decl(expose).map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching exposes found for capability {:?} from component {}",
                            pos.capability,
                            pos.moniker(),
                        ))
                    })?;
                return Ok(CapabilitySource::Framework {
                    capability,
                    scope_moniker: Some(pos.moniker().clone()),
                });
            }
            ExposeSource::Directory(cm_rust::ExposeSource::Framework) => {
                // Directories offered or exposed directly from the framework are limited to
                // read-only rights.
                pos.rights_state = pos.rights_state.finalize(Some(Rights::from(*READ_RIGHTS)))?;
                let capability =
                    FrameworkCapability::framework_from_expose_decl(expose).map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching exposes found for capability {:?} from component {}",
                            pos.capability,
                            pos.moniker(),
                        ))
                    })?;
                return Ok(CapabilitySource::Framework {
                    capability,
                    scope_moniker: Some(pos.moniker().clone()),
                });
            }
            ExposeSource::Runner(cm_rust::ExposeSource::Framework) => {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "component {} attempted to use runner from framework",
                    pos.moniker().clone()
                )));
            }
        }
    }
}
