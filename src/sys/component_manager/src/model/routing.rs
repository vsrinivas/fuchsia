// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{capability::*, model::*},
    cm_rust::{
        self, CapabilityPath, ExposeDecl, ExposeDirectoryDecl, ExposeTarget, OfferDecl,
        OfferDirectorySource, OfferRunnerSource, OfferServiceSource, OfferStorageSource,
        StorageDecl, StorageDirectorySource, UseDecl, UseStorageDecl,
    },
    failure::format_err,
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
    LegacyService(&'a OfferServiceSource),
    Directory(&'a OfferDirectorySource),
    Storage(&'a OfferStorageSource),
    Runner(&'a OfferRunnerSource),
}

/// Describes the source of a capability, for any type of capability.
#[derive(Debug)]
enum ExposeSource<'a> {
    LegacyService(&'a cm_rust::ExposeSource),
    Directory(&'a cm_rust::ExposeSource),
    Runner(&'a cm_rust::ExposeSource),
}

/// Describes the source of a capability, as determined by `find_capability_source`
#[derive(Debug)]
enum CapabilitySource {
    /// This capability originates from the component instance for the given Realm.
    /// point.
    Component(RoutedCapability, Arc<Realm>),
    /// This capability originates from the root component's realm.
    Builtin(ComponentManagerCapability),
    /// This capability originates from component manager itself scoped to the requesting
    /// component's realm.
    Framework(ComponentManagerCapability, Arc<Realm>),
    /// This capability originates from a storage declaration in a component's decl.  `StorageDecl`
    /// describes the backing directory capability offered to this realm, into which storage
    /// requests should be fed.
    StorageDecl(StorageDecl, Arc<Realm>),
}

/// Finds the source of the `capability` used by `absolute_moniker`, and pass along the
/// `server_chan` to the hosting component's out directory (or componentmgr's namespace, if
/// applicable) using an open request with `open_mode`.
pub async fn route_use_capability<'a>(
    model: &'a Model,
    flags: u32,
    open_mode: u32,
    relative_path: String,
    use_decl: &'a UseDecl,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    match use_decl {
        UseDecl::Service(_)
        | UseDecl::LegacyService(_)
        | UseDecl::Directory(_)
        | UseDecl::Runner(_) => {
            let source = find_used_capability_source(model, use_decl, &abs_moniker).await?;
            open_capability_at_source(model, flags, open_mode, relative_path, source, server_chan)
                .await
        }
        UseDecl::Storage(storage_decl) => {
            route_and_open_storage_capability(
                model,
                storage_decl,
                open_mode,
                abs_moniker,
                server_chan,
            )
            .await
        }
    }
}

/// Finds the source of the expose capability used at `source_path` by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable)
pub async fn route_expose_capability<'a>(
    model: &'a Model,
    flags: u32,
    open_mode: u32,
    expose_decl: &'a ExposeDecl,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let capability = RoutedCapability::Expose(expose_decl.clone());
    let mut pos =
        WalkPosition { capability, last_child_moniker: None, moniker: Some(abs_moniker.clone()) };
    let source = walk_expose_chain(model, &mut pos).await?;
    open_capability_at_source(model, flags, open_mode, String::new(), source, server_chan).await
}

/// Open the capability at the given source, binding to its component instance if necessary.
async fn open_capability_at_source<'a>(
    model: &'a Model,
    flags: u32,
    open_mode: u32,
    relative_path: String,
    source: CapabilitySource,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    match source {
        CapabilitySource::Builtin(capability) => {
            open_builtin_capability(
                model,
                flags,
                open_mode,
                relative_path,
                &capability,
                server_chan,
            )
            .await?;
        }
        CapabilitySource::Component(source_capability, realm) => {
            if let Some(path) = source_capability.source_path() {
                // TODO(36541): changing the flags for pkgfs is a hack, directory permissions
                // should be recorded in the manifests
                let mut flags = flags;
                if path.to_string().contains("pkgfs") {
                    flags = fio::OPEN_RIGHT_READABLE;
                }
                model.bind_open_outgoing(realm, flags, open_mode, path, server_chan).await?;
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "invalid capability type to come from a component"
                )));
            }
        }
        CapabilitySource::Framework(capability, realm) => {
            open_framework_capability(
                SERVICE_OPEN_FLAGS,
                open_mode,
                relative_path,
                realm,
                &capability,
                server_chan,
            )
            .await?;
        }
        CapabilitySource::StorageDecl(..) => {
            panic!("storage capabilities must be separately routed and opened");
        }
    }
    Ok(())
}

async fn open_builtin_capability<'a>(
    model: &Model,
    flags: u32,
    open_mode: u32,
    relative_path: String,
    capability: &'a ComponentManagerCapability,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let capability_provider = Arc::new(Mutex::new(None));

    let event = Event {
        target_realm: model.root_realm.clone(),
        payload: EventPayload::RouteBuiltinCapability {
            capability: capability.clone(),
            capability_provider: capability_provider.clone(),
        },
    };
    model.root_realm.hooks.dispatch(&event).await?;

    let capability_provider = capability_provider.lock().await.take();
    if let Some(capability_provider) = capability_provider {
        capability_provider.open(flags, open_mode, relative_path, server_chan).await?;
    } else {
        let path = capability.path();
        io_util::connect_in_namespace(&path.to_string(), server_chan, flags)
            .map_err(|e| ModelError::capability_discovery_error(e))?;
    }

    Ok(())
}

async fn open_framework_capability<'a>(
    flags: u32,
    open_mode: u32,
    relative_path: String,
    realm: Arc<Realm>,
    capability: &'a ComponentManagerCapability,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let capability_provider = Arc::new(Mutex::new(None));

    let event = Event {
        target_realm: realm.clone(),
        payload: EventPayload::RouteFrameworkCapability {
            capability: capability.clone(),
            capability_provider: capability_provider.clone(),
        },
    };
    realm.hooks.dispatch(&event).await?;

    let capability_provider = capability_provider.lock().await.take();
    // TODO(fsamuel): We should report an error message if we're trying to open a framework
    // capability that does not exist.
    if let Some(capability_provider) = capability_provider {
        capability_provider.open(flags, open_mode, relative_path, server_chan).await?;
    }

    Ok(())
}

/// Routes a `UseDecl::Storage` to the component instance providing the backing directory and
/// opens its isolated storage with `server_chan`.
pub async fn route_and_open_storage_capability<'a>(
    model: &'a Model,
    use_decl: &'a UseStorageDecl,
    open_mode: u32,
    use_abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let (dir_source_realm, dir_source_path, relative_moniker) =
        route_storage_capability(model, use_decl, use_abs_moniker).await?;
    let storage_dir_proxy = open_isolated_storage(
        &model,
        dir_source_realm,
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
pub async fn route_and_delete_storage<'a>(
    model: &'a Model,
    use_decl: &'a UseStorageDecl,
    use_abs_moniker: AbsoluteMoniker,
) -> Result<(), ModelError> {
    let (dir_source_realm, dir_source_path, relative_moniker) =
        route_storage_capability(model, use_decl, use_abs_moniker).await?;
    delete_isolated_storage(&model, dir_source_realm, &dir_source_path, &relative_moniker)
        .await
        .map_err(|e| ModelError::from(e))?;
    Ok(())
}

/// Assuming `use_decl` is a UseStorage declaration, returns information about the source of the
/// storage capability, including:
/// - Realm hosting the backing directory capability
/// - Path to the backing directory capability
/// - Relative moniker between the backing directory component and the consumer, which identifies
///   the isolated storage directory.
async fn route_storage_capability<'a>(
    model: &'a Model,
    use_decl: &'a UseStorageDecl,
    use_abs_moniker: AbsoluteMoniker,
) -> Result<(Arc<Realm>, CapabilityPath, RelativeMoniker), ModelError> {
    // Walk the offer chain to find the storage decl
    let parent_moniker = match use_abs_moniker.parent() {
        Some(m) => m,
        None => {
            return Err(ModelError::capability_discovery_error(format_err!(
                "storage capabilities cannot come from component manager's namespace"
            )))
        }
    };
    let mut pos = WalkPosition {
        capability: RoutedCapability::Use(UseDecl::Storage(use_decl.clone())),
        last_child_moniker: use_abs_moniker.path().last().map(|c| c.clone()),
        moniker: Some(parent_moniker),
    };

    let source = walk_offer_chain(model, &mut pos).await?;

    let (storage_decl, storage_decl_realm) = match source {
        Some(CapabilitySource::StorageDecl(s, r)) => (s, r),
        _ => {
            return Err(ModelError::capability_discovery_error(format_err!(
                "storage capabilities must come from a storage declaration"
            )))
        }
    };

    // Find the path and source of the directory consumed by the storage capability.
    let (dir_source_path, dir_source_realm) = match storage_decl.source {
        StorageDirectorySource::Self_ => (storage_decl.source_path, storage_decl_realm.clone()),
        StorageDirectorySource::Realm => {
            let capability = RoutedCapability::Storage(storage_decl);
            let source =
                find_offered_capability_source(model, capability, &storage_decl_realm.abs_moniker)
                    .await?;
            match source {
                CapabilitySource::Component(source_capability, realm) => {
                    (source_capability.source_path().unwrap().clone(), realm)
                }
                _ => {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "storage capability backing directories must be provided by a component"
                    )))
                }
            }
        }
        StorageDirectorySource::Child(ref name) => {
            let mut pos = {
                let partial = PartialMoniker::new(name.to_string(), None);
                let realm_state = storage_decl_realm.lock_state().await;
                let realm_state =
                    realm_state.as_ref().expect("route_storage_capability: not resolved");
                let moniker = Some(
                    realm_state
                        .extend_moniker_with(&storage_decl_realm.abs_moniker, &partial)
                        .ok_or(ModelError::capability_discovery_error(format_err!(
                            "no child {} found from component {} for storage directory source",
                            partial,
                            pos.moniker().clone(),
                        )))?,
                );
                let capability = RoutedCapability::Storage(storage_decl);
                WalkPosition { capability, last_child_moniker: None, moniker }
            };
            match walk_expose_chain(model, &mut pos).await? {
                CapabilitySource::Component(source_capability, realm) => {
                    (source_capability.source_path().unwrap().clone(), realm)
                }
                _ => {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "storage capability backing directories must be provided by a component"
                    )))
                }
            }
        }
    };
    let relative_moniker =
        RelativeMoniker::from_absolute(&storage_decl_realm.abs_moniker, &use_abs_moniker);
    Ok((dir_source_realm, dir_source_path, relative_moniker))
}

/// Check if a used capability is a framework service, and if so return a framework `CapabilitySource`.
async fn find_framework_capability<'a>(
    model: &'a Model,
    use_decl: &'a UseDecl,
    abs_moniker: &'a AbsoluteMoniker,
) -> Result<Option<CapabilitySource>, ModelError> {
    if let Ok(capability) = ComponentManagerCapability::framework_from_use_decl(use_decl) {
        let realm = model.look_up_realm(abs_moniker).await?;
        return Ok(Some(CapabilitySource::Framework(capability, realm)));
    }
    return Ok(None);
}

/// Holds state about the current position when walking the tree.
#[derive(Debug)]
struct WalkPosition {
    /// The capability declaration as it's represented in the current component.
    capability: RoutedCapability,
    /// The moniker of the child we came from.
    last_child_moniker: Option<ChildMoniker>,
    /// The moniker of the component we are currently looking at. `None` for component manager's
    /// realm.
    moniker: Option<AbsoluteMoniker>,
}

impl WalkPosition {
    fn moniker(&self) -> &AbsoluteMoniker {
        self.moniker.as_ref().unwrap()
    }

    fn at_componentmgr_realm(&self) -> bool {
        self.moniker.is_none()
    }
}

async fn find_used_capability_source<'a>(
    model: &'a Model,
    use_decl: &'a UseDecl,
    abs_moniker: &'a AbsoluteMoniker,
) -> Result<CapabilitySource, ModelError> {
    if let Some(framework_capability) =
        find_framework_capability(model, use_decl, abs_moniker).await?
    {
        return Ok(framework_capability);
    }
    let capability = RoutedCapability::Use(use_decl.clone());
    find_offered_capability_source(model, capability, abs_moniker).await
}

/// Finds the providing realm and path of a directory exposed by the root realm to component
/// manager.
pub async fn find_exposed_root_directory_capability<'a>(
    model: &'a Model,
    path: CapabilityPath,
) -> Result<(CapabilityPath, Arc<Realm>), ModelError> {
    let expose_dir_decl = {
        let root_moniker = AbsoluteMoniker::new(vec![]);
        let root_realm = model.look_up_realm(&root_moniker).await?;
        let realm_state = root_realm.lock_state().await;
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
            return Ok((expose_dir_decl.source_path.clone(), model.root_realm.clone()))
        }
        cm_rust::ExposeSource::Child(_) => {
            let mut wp = WalkPosition {
                capability: RoutedCapability::Expose(ExposeDecl::Directory(
                    expose_dir_decl.clone(),
                )),
                last_child_moniker: None,
                moniker: Some(vec![].into()),
            };
            let capability_source = walk_expose_chain(model, &mut wp).await?;
            match &capability_source {
                CapabilitySource::Component(
                    RoutedCapability::Expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_path,
                        ..
                    })),
                    realm,
                ) => return Ok((source_path.clone(), realm.clone())),
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
async fn find_offered_capability_source<'a>(
    model: &'a Model,
    capability: RoutedCapability,
    abs_moniker: &'a AbsoluteMoniker,
) -> Result<CapabilitySource, ModelError> {
    let mut pos = WalkPosition {
        capability,
        last_child_moniker: abs_moniker.path().last().map(|c| c.clone()),
        moniker: abs_moniker.parent(),
    };
    if let Some(source) = walk_offer_chain(model, &mut pos).await? {
        return Ok(source);
    }
    walk_expose_chain(model, &mut pos).await
}

/// Follows `offer` declarations up the component tree, starting at `pos`. The algorithm looks
/// for a matching `offer` in the parent, as long as the `offer` is from `realm`.
///
/// Returns the source of the capability if found, or `None` if `expose` declarations must be
/// walked.
async fn walk_offer_chain<'a>(
    model: &'a Model,
    pos: &'a mut WalkPosition,
) -> Result<Option<CapabilitySource>, ModelError> {
    'offerloop: loop {
        if pos.at_componentmgr_realm() {
            // This is a built-in capability because the routing path was traced to the component
            // manager's realm.
            // TODO(fsamuel): We really ought to test the failure cases here.
            let capability = match &pos.capability {
                RoutedCapability::Use(use_decl) => {
                    ComponentManagerCapability::builtin_from_use_decl(use_decl).map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching use found for capability {:?}",
                            pos.capability,
                        ))
                    })
                }
                RoutedCapability::Offer(offer_decl) => {
                    ComponentManagerCapability::builtin_from_offer_decl(offer_decl).map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching offers found for capability {:?}",
                            pos.capability,
                        ))
                    })
                }
                _ => Err(ModelError::capability_discovery_error(format_err!(
                    "Unsupported capability {:?}",
                    pos.capability,
                ))),
            }?;

            return Ok(Some(CapabilitySource::Builtin(capability)));
        }
        let current_realm = model.look_up_realm(&pos.moniker()).await?;
        let realm_state = current_realm.lock_state().await;
        let realm_state = realm_state.as_ref().expect("walk_offer_chain: not resolved");
        // This `get()` is safe because `look_up_realm` populates this field
        let decl = realm_state.decl();
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
            OfferDecl::LegacyService(s) => OfferSource::LegacyService(&s.source),
            OfferDecl::Directory(d) => OfferSource::Directory(&d.source),
            OfferDecl::Storage(s) => OfferSource::Storage(s.source()),
            OfferDecl::Runner(r) => OfferSource::Runner(&r.source),
        };
        match source {
            OfferSource::Service(_) => {
                return Err(ModelError::unsupported("Service capability"));
            }
            OfferSource::Directory(OfferDirectorySource::Framework) => {
                let capability = ComponentManagerCapability::framework_from_offer_decl(offer)
                    .map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching offers found for capability {:?} from component {}",
                            pos.capability,
                            pos.moniker(),
                        ))
                    })?;
                return Ok(Some(CapabilitySource::Framework(capability, current_realm.clone())));
            }
            OfferSource::LegacyService(OfferServiceSource::Realm)
            | OfferSource::Directory(OfferDirectorySource::Realm)
            | OfferSource::Storage(OfferStorageSource::Realm)
            | OfferSource::Runner(OfferRunnerSource::Realm) => {
                // The offered capability comes from the realm, so follow the
                // parent
                pos.capability = RoutedCapability::Offer(offer.clone());
                pos.last_child_moniker = pos.moniker().path().last().map(|c| c.clone());
                pos.moniker = pos.moniker().parent();
                continue 'offerloop;
            }
            OfferSource::LegacyService(OfferServiceSource::Self_)
            | OfferSource::Directory(OfferDirectorySource::Self_) => {
                // The offered capability comes from the current component,
                // return our current location in the tree.
                return Ok(Some(CapabilitySource::Component(
                    RoutedCapability::Offer(offer.clone()),
                    current_realm.clone(),
                )));
            }
            OfferSource::Runner(OfferRunnerSource::Self_) => {
                // The offered capability comes from the current component.
                // Find the current component's Runner declaration.
                let cap = RoutedCapability::Offer(offer.clone());
                return Ok(Some(CapabilitySource::Component(
                    RoutedCapability::Runner(
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
                    current_realm.clone(),
                )));
            }
            OfferSource::LegacyService(OfferServiceSource::Child(child_name))
            | OfferSource::Directory(OfferDirectorySource::Child(child_name))
            | OfferSource::Runner(OfferRunnerSource::Child(child_name)) => {
                // The offered capability comes from a child, break the loop
                // and begin walking the expose chain.
                pos.capability = RoutedCapability::Offer(offer.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.moniker =
                    Some(realm_state.extend_moniker_with(&pos.moniker(), &partial).ok_or(
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
                    current_realm.clone(),
                )));
            }
        }
    }
}

/// Follows `expose` declarations down the component tree, starting at `pos`. The algorithm looks
/// for a matching `expose` in the child, as long as the `expose` is not from `self`.
///
/// Returns the source of the capability.
async fn walk_expose_chain<'a>(
    model: &'a Model,
    pos: &'a mut WalkPosition,
) -> Result<CapabilitySource, ModelError> {
    // If the first capability is an Expose, assume it corresponds to an Expose declared by the
    // first component.
    let mut first_expose = {
        match &pos.capability {
            RoutedCapability::Expose(e) => Some(e.clone()),
            _ => None,
        }
    };
    loop {
        let current_realm = model.look_up_realm(&pos.moniker()).await?;
        let realm_state = current_realm.lock_state().await;
        let realm_state = realm_state.as_ref().expect("walk_expose_chain: not resolved");
        // This `get()` is safe because look_up_realm populates this field.
        let first_expose = first_expose.take();
        let expose = first_expose
            .as_ref()
            .or_else(|| pos.capability.find_expose_source(realm_state.decl()))
            .ok_or(ModelError::capability_discovery_error(format_err!(
                "no matching offers found for capability {:?} from component {}",
                pos.capability,
                pos.moniker(),
            )))?;
        let (source, target) = match expose {
            ExposeDecl::Service(_) => return Err(ModelError::unsupported("Service capability")),
            ExposeDecl::LegacyService(ls) => (ExposeSource::LegacyService(&ls.source), &ls.target),
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
        match source {
            ExposeSource::LegacyService(cm_rust::ExposeSource::Self_)
            | ExposeSource::Directory(cm_rust::ExposeSource::Self_) => {
                // The offered capability comes from the current component, return our
                // current location in the tree.
                return Ok(CapabilitySource::Component(
                    RoutedCapability::Expose(expose.clone()),
                    current_realm.clone(),
                ));
            }
            ExposeSource::Runner(cm_rust::ExposeSource::Self_) => {
                // The exposed capability comes from the current component.
                // Find the current component's Runner declaration.
                let cap = RoutedCapability::Expose(expose.clone());
                return Ok(CapabilitySource::Component(
                    RoutedCapability::Runner(
                        cap.find_runner_source(realm_state.decl())
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
                    current_realm.clone(),
                ));
            }
            ExposeSource::LegacyService(cm_rust::ExposeSource::Child(child_name))
            | ExposeSource::Directory(cm_rust::ExposeSource::Child(child_name))
            | ExposeSource::Runner(cm_rust::ExposeSource::Child(child_name)) => {
                // The offered capability comes from a child, so follow the child.
                pos.capability = RoutedCapability::Expose(expose.clone());
                let partial = PartialMoniker::new(child_name.to_string(), None);
                pos.moniker =
                    Some(realm_state.extend_moniker_with(&pos.moniker(), &partial).ok_or(
                        ModelError::capability_discovery_error(format_err!(
                            "no child {} found from component {} for expose source",
                            partial,
                            pos.moniker().clone(),
                        )),
                    )?);
                continue;
            }
            ExposeSource::LegacyService(cm_rust::ExposeSource::Framework)
            | ExposeSource::Directory(cm_rust::ExposeSource::Framework) => {
                let capability = ComponentManagerCapability::framework_from_expose_decl(expose)
                    .map_err(|_| {
                        ModelError::capability_discovery_error(format_err!(
                            "no matching offers found for capability {:?} from component {}",
                            pos.capability,
                            pos.moniker(),
                        ))
                    })?;
                return Ok(CapabilitySource::Framework(capability, current_realm.clone()));
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
