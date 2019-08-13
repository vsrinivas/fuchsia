// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{
        self, CapabilityPath, ExposeDecl, ExposeSource, FrameworkCapabilityDecl, OfferDecl,
        OfferDirectorySource, OfferServiceSource, OfferStorageSource, StorageDecl,
        StorageDirectorySource, UseDecl,
    },
    failure::format_err,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    std::{convert::TryFrom, path::PathBuf, sync::Arc},
};
const FLAGS: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

/// Describes the source of a capability, for any type of capability.
enum OfferSource<'a> {
    // TODO(CF-908): Enable this once unified services are implemented.
    #[allow(dead_code)]
    Service(&'a OfferServiceSource),
    LegacyService(&'a OfferServiceSource),
    Directory(&'a OfferDirectorySource),
    Storage(&'a OfferStorageSource),
}

/// Describes the source of a capability, as determined by `find_capability_source`
enum CapabilitySource {
    /// This capability originates from the component instance for the given Realm.
    /// point.
    Component(RoutedCapability, Arc<Realm>),
    /// This capability originates from component manager's namespace.
    ComponentManagerNamespace(CapabilityPath),
    /// This capability originates from component manager itself.
    Framework(FrameworkCapabilityDecl, Arc<Realm>),
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
    if let UseDecl::Storage(_) = use_decl {
        return route_and_open_storage_capability(
            model,
            use_decl,
            open_mode,
            abs_moniker,
            server_chan,
        ).await;
    }
    let source = find_used_capability_source(model, use_decl, &abs_moniker).await?;
    open_capability_at_source(model, flags, open_mode, relative_path, source, server_chan).await
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
        CapabilitySource::ComponentManagerNamespace(path) => {
            io_util::connect_in_namespace(&path.to_string(), server_chan, FLAGS)
                .map_err(|e| ModelError::capability_discovery_error(e))?;
        }
        CapabilitySource::Component(source_capability, realm) => {
            if let Some(path) = source_capability.source_path() {
                Model::bind_instance_open_outgoing(
                    &model,
                    realm,
                    flags,
                    open_mode,
                    path,
                    server_chan,
                ).await?;
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "invalid capability type to come from a component"
                )));
            }
        }
        CapabilitySource::Framework(capability_decl, realm) => {
            open_framework_capability(
                model.clone(),
                FLAGS,
                open_mode,
                relative_path,
                realm,
                &capability_decl,
                server_chan,
            ).await?;
        }
        CapabilitySource::StorageDecl(..) => {
            panic!("storage capabilities must be separately routed and opened");
        }
    }
    Ok(())
}

async fn open_framework_capability<'a>(
    model: Model,
    flags: u32,
    open_mode: u32,
    relative_path: String,
    realm: Arc<Realm>,
    capability_decl: &'a FrameworkCapabilityDecl,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let mut capability = None;

    for hook in model.hooks.iter() {
        capability = hook.on_route_framework_capability(
            realm.clone(),
            &capability_decl,
            capability,
        ).await?;
    }

    if let Some(capability) = capability {
        capability.open(flags, open_mode, relative_path, server_chan).await?;
    }

    Ok(())
}

pub async fn route_and_open_storage_capability<'a>(
    model: &'a Model,
    use_decl: &'a UseDecl,
    open_mode: u32,
    use_abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
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
        capability: RoutedCapability::Use(use_decl.clone()),
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
    let (source_path, dir_source_realm) =
        match storage_decl.source {
            StorageDirectorySource::Self_ => (storage_decl.source_path, storage_decl_realm.clone()),
            StorageDirectorySource::Realm => {
                let capability = RoutedCapability::Storage(storage_decl);
                let source = find_offered_capability_source(
                    model,
                    capability,
                    &storage_decl_realm.abs_moniker,
                ).await?;
                match source {
                    CapabilitySource::Component(source_capability, realm) => {
                        (source_capability.source_path().unwrap().clone(), realm)
                    }
                    _ => return Err(ModelError::capability_discovery_error(format_err!(
                        "storage capability backing directories must be provided by a component"
                    ))),
                }
            }
            StorageDirectorySource::Child(ref name) => {
                let moniker = Some(
                    storage_decl_realm.abs_moniker.child(ChildMoniker::new(name.to_string(), None)),
                );
                let capability = RoutedCapability::Storage(storage_decl);
                let mut pos = WalkPosition { capability, last_child_moniker: None, moniker };
                match walk_expose_chain(model, &mut pos).await? {
                    CapabilitySource::Component(source_capability, realm) => {
                        (source_capability.source_path().unwrap().clone(), realm)
                    }
                    _ => return Err(ModelError::capability_discovery_error(format_err!(
                        "storage capability backing directories must be provided by a component"
                    ))),
                }
            }
        };

    // Bind with a local proxy, so we can create and open the relevant sub-directory for
    // this component.
    let (dir_proxy, local_server_end) =
        create_proxy::<DirectoryMarker>().expect("failed to create proxy");
    Model::bind_instance_open_outgoing(
        &model,
        dir_source_realm,
        FLAGS,
        open_mode,
        &source_path,
        local_server_end.into_channel(),
    ).await?;

    // Open each node individually, so it can be created if it doesn't exist
    let relative_moniker =
        RelativeMoniker::from_absolute(&storage_decl_realm.abs_moniker, &use_abs_moniker);
    let storage_type = match use_decl {
        UseDecl::Storage(d) => d.type_(),
        _ => panic!("non-storage capability"),
    };
    let sub_dir_proxy = io_util::create_sub_directories(
        dir_proxy,
        &generate_storage_path(storage_type, relative_moniker),
    )
    .map_err(|e| {
        ModelError::capability_discovery_error(format_err!(
            "failed to create new directories: {}",
            e
        ))
    })?;

    // clone the final connection to connect the channel we're routing to its destination
    sub_dir_proxy
        .clone(FLAGS, ServerEnd::new(server_chan))
        .map_err(|e| ModelError::capability_discovery_error(format_err!("failed clone {}", e)))?;
    Ok(())
}

/// Generates the path into a directory the provided component will be afforded for storage
///
/// The path of the sub-directory for a component that uses a storage capability is based on:
///
/// - Each component instance's name as given in the `children` section of its parent's manifest,
/// for each component instance in the path from the [`storage` declaration][storage-syntax] to the
/// [`use` declaration][use-syntax].
/// - The storage type.
///
/// These names are used as path elements, separated by elements of the name "children". The
/// storage type is then appended to this path.
///
/// For example, if the following component instance tree exists, with `a` declaring storage
/// capabilities, and then cache storage being offered down the chain to `d`:
///
/// ```
///  a  <- declares storage, offers cache storage to b
///  |
///  b  <- offers cache storage to c
///  |
///  c  <- offers cache storage to d
///  |
///  d  <- uses cache storage as `/my_cache`
/// ```
///
/// When `d` attempts to access `/my_cache` the framework creates the sub-directory
/// `b/children/c/children/d/cache` in the directory used by `a` to declare storage capabilities.
/// Then, the framework gives 'd' access to this new directory.
pub fn generate_storage_path(
    type_: fsys::StorageType,          // The type of storage being used
    relative_moniker: RelativeMoniker, // The relative moniker from the storage decl to the usage
) -> PathBuf {
    assert!(
        !relative_moniker.down_path().is_empty(),
        "storage capability appears to have been exposed or used by its source"
    );

    let mut down_path = relative_moniker.down_path().iter();

    let mut dir_path = vec![down_path.next().unwrap().as_str().to_string()];
    while let Some(p) = down_path.next() {
        dir_path.push("children".to_string());
        dir_path.push(p.as_str().to_string());
    }
    match type_ {
        fsys::StorageType::Data => dir_path.push("data".to_string()),
        fsys::StorageType::Cache => dir_path.push("cache".to_string()),
        fsys::StorageType::Meta => dir_path.push("meta".to_string()),
    }
    dir_path.into_iter().collect()
}

/// Check if a used capability is a framework service, and if so return a framework `CapabilitySource`.
async fn find_framework_capability<'a>(
    model: &'a Model,
    use_decl: &'a UseDecl,
    abs_moniker: &'a AbsoluteMoniker,
) -> Result<Option<CapabilitySource>, ModelError> {
    if let Ok(capability_decl) = FrameworkCapabilityDecl::try_from(use_decl) {
        let realm = model.look_up_realm(abs_moniker).await?;
        return Ok(Some(CapabilitySource::Framework(capability_decl, realm)));
    }
    return Ok(None);
}

/// Holds state about the current position when walking the tree.
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
            // We are at component manager's own realm, so pull the capability from its namespace.
            if let Some(path) = pos.capability.source_path() {
                return Ok(Some(CapabilitySource::ComponentManagerNamespace(path.clone())));
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "invalid capability type to come from component manager's namespace",
                )));
            }
        }
        let current_realm = model.look_up_realm(&pos.moniker()).await?;
        let realm_state = current_realm.state.lock().await;
        // This get_decl() is safe because `look_up_realm` populates this field
        let decl = realm_state.get_decl();
        let last_child_moniker = pos.last_child_moniker.as_ref().unwrap();
        if let Some(offer) = pos.capability.find_offer_source(decl, last_child_moniker) {
            let source = match offer {
                OfferDecl::Service(_) => return Err(ModelError::unsupported("Service capability")),
                OfferDecl::LegacyService(s) => OfferSource::LegacyService(&s.source),
                OfferDecl::Directory(d) => OfferSource::Directory(&d.source),
                OfferDecl::Storage(s) => OfferSource::Storage(s.source()),
            };
            match source {
                OfferSource::Directory(OfferDirectorySource::Framework) => {
                    let capability_decl =
                        FrameworkCapabilityDecl::try_from(offer).map_err(|_| {
                            ModelError::capability_discovery_error(format_err!(
                                "no matching offers found for capability {:?} from component {}",
                                pos.capability,
                                pos.moniker(),
                            ))
                        })?;
                    return Ok(Some(CapabilitySource::Framework(
                        capability_decl,
                        current_realm.clone(),
                    )));
                }
                OfferSource::Service(OfferServiceSource::Realm)
                | OfferSource::LegacyService(OfferServiceSource::Realm)
                | OfferSource::Directory(OfferDirectorySource::Realm)
                | OfferSource::Storage(OfferStorageSource::Realm) => {
                    // The offered capability comes from the realm, so follow the
                    // parent
                    pos.capability = RoutedCapability::Offer(offer.clone());
                    pos.last_child_moniker = pos.moniker().path().last().map(|c| c.clone());
                    pos.moniker = pos.moniker().parent();
                    continue 'offerloop;
                }
                OfferSource::Service(OfferServiceSource::Self_)
                | OfferSource::LegacyService(OfferServiceSource::Self_)
                | OfferSource::Directory(OfferDirectorySource::Self_) => {
                    // The offered capability comes from the current component,
                    // return our current location in the tree.
                    return Ok(Some(CapabilitySource::Component(
                        RoutedCapability::Offer(offer.clone()),
                        current_realm.clone(),
                    )));
                }
                OfferSource::Service(OfferServiceSource::Child(child_name))
                | OfferSource::LegacyService(OfferServiceSource::Child(child_name))
                | OfferSource::Directory(OfferDirectorySource::Child(child_name)) => {
                    // The offered capability comes from a child, break the loop
                    // and begin walking the expose chain.
                    pos.capability = RoutedCapability::Offer(offer.clone());
                    pos.moniker =
                        Some(pos.moniker().child(ChildMoniker::new(child_name.to_string(), None)));
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
        } else {
            return Err(ModelError::capability_discovery_error(format_err!(
                "no matching offers found for capability {:?} from component {}",
                pos.capability,
                pos.moniker(),
            )));
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
        let realm_state = current_realm.state.lock().await;
        // This get_decl() is safe because look_up_realm populates this field
        let decl = realm_state.get_decl();
        let first_expose = first_expose.take();
        let expose = match first_expose {
            Some(_) => first_expose.as_ref(),
            None => pos.capability.find_expose_source(decl),
        };
        if let Some(expose) = expose {
            let source = match expose {
                ExposeDecl::Service(_) => {
                    return Err(ModelError::unsupported("Service capability"))
                }
                ExposeDecl::LegacyService(d) => &d.source,
                ExposeDecl::Directory(d) => &d.source,
            };
            match source {
                ExposeSource::Self_ => {
                    // The offered capability comes from the current component, return our
                    // current location in the tree.
                    return Ok(CapabilitySource::Component(
                        RoutedCapability::Expose(expose.clone()),
                        current_realm.clone(),
                    ));
                }
                ExposeSource::Child(child_name) => {
                    // The offered capability comes from a child, so follow the child.
                    pos.capability = RoutedCapability::Expose(expose.clone());
                    pos.moniker =
                        Some(pos.moniker().child(ChildMoniker::new(child_name.to_string(), None)));
                    continue;
                }
                ExposeSource::Framework => {
                    let capability_decl =
                        FrameworkCapabilityDecl::try_from(expose).map_err(|_| {
                            ModelError::capability_discovery_error(format_err!(
                                "no matching offers found for capability {:?} from component {}",
                                pos.capability,
                                pos.moniker(),
                            ))
                        })?;
                    return Ok(CapabilitySource::Framework(capability_decl, current_realm.clone()));
                }
            }
        } else {
            // We didn't find any matching exposes! Oh no!
            return Err(ModelError::capability_discovery_error(format_err!(
                "no matching exposes found for capability {:?} from component {}",
                pos.capability,
                pos.moniker()
            )));
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_sys2::StorageType};

    #[test]
    fn generate_storage_path_test() {
        for (type_, relative_moniker, expected_output) in vec![
            (StorageType::Data, RelativeMoniker::new(vec![], vec!["a".into()]), "a/data"),
            (StorageType::Cache, RelativeMoniker::new(vec![], vec!["a".into()]), "a/cache"),
            (StorageType::Meta, RelativeMoniker::new(vec![], vec!["a".into()]), "a/meta"),
            (
                StorageType::Data,
                RelativeMoniker::new(vec![], vec!["a".into(), "b".into()]),
                "a/children/b/data",
            ),
            (
                StorageType::Cache,
                RelativeMoniker::new(vec![], vec!["a".into(), "b".into()]),
                "a/children/b/cache",
            ),
            (
                StorageType::Meta,
                RelativeMoniker::new(vec![], vec!["a".into(), "b".into()]),
                "a/children/b/meta",
            ),
            (
                StorageType::Data,
                RelativeMoniker::new(vec![], vec!["a".into(), "b".into(), "c".into()]),
                "a/children/b/children/c/data",
            ),
            (
                StorageType::Cache,
                RelativeMoniker::new(vec![], vec!["a".into(), "b".into(), "c".into()]),
                "a/children/b/children/c/cache",
            ),
            (
                StorageType::Meta,
                RelativeMoniker::new(vec![], vec!["a".into(), "b".into(), "c".into()]),
                "a/children/b/children/c/meta",
            ),
        ] {
            assert_eq!(
                generate_storage_path(type_, relative_moniker),
                PathBuf::from(expected_output)
            )
        }
    }
}
