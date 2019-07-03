// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{
        self, Capability, CapabilityPath, ExposeDecl, ExposeSource, OfferDecl,
        OfferDirectorySource, OfferServiceSource, OfferStorageSource,
    },
    failure::format_err,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    std::path::PathBuf,
    std::sync::Arc,
};
const FLAGS: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

/// Describes the source of a capability, for any type of capability.
enum OfferSource<'a> {
    Service(&'a OfferServiceSource),
    Directory(&'a OfferDirectorySource),
    Storage(&'a OfferStorageSource),
}

/// Describes the source of a capability, as determined by `find_capability_source`
enum CapabilitySource {
    /// This capability originates from the component instance for the given Realm.
    /// point.
    Component(Capability, Arc<Realm>),
    /// This capability originates from component manager's namespace.
    ComponentMgrNamespace(Capability),
    /// This capability is an ambient service and originates from component manager itself.
    AmbientService(CapabilityPath, Arc<Realm>),
    /// This capability originates from a storage declaration in a component's decl. The capability
    /// here is the backing directory capability offered to this realm, into which storage requests
    /// should be fed.
    StorageDecl(Capability, OfferDirectorySource, Arc<Realm>),
}

/// Finds the source of the `capability` used by `absolute_moniker`, and pass along the
/// `server_chan` to the hosting component's out directory (or componentmgr's namespace, if
/// applicable) using an open request with `open_mode`.
pub async fn route_use_capability<'a>(
    model: &'a Model,
    open_mode: u32,
    used_capability: &'a Capability,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    if let Capability::Storage(type_) = used_capability {
        return await!(route_and_open_storage_capability(
            model,
            type_.clone(),
            open_mode,
            abs_moniker,
            server_chan
        ));
    }
    let source = await!(find_capability_source(model, used_capability, &abs_moniker))?;
    await!(open_capability_at_source(model, open_mode, source, server_chan))
}

/// Finds the source of the expose capability used at `source_path` by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable)
pub async fn route_expose_capability<'a>(
    model: &'a Model,
    open_mode: u32,
    exposed_capability: &'a Capability,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let mut pos = WalkPosition {
        capability: exposed_capability.clone(),
        last_child_moniker: None,
        moniker: abs_moniker,
    };
    let source = await!(walk_expose_chain(model, &mut pos))?;
    await!(open_capability_at_source(model, open_mode, source, server_chan))
}

/// Open the capability at the given source, binding to its component instance if necessary.
async fn open_capability_at_source<'a>(
    model: &'a Model,
    open_mode: u32,
    source: CapabilitySource,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    match source {
        CapabilitySource::ComponentMgrNamespace(source_capability) => {
            if let Some(path) = source_capability.path() {
                io_util::connect_in_namespace(&path.to_string(), server_chan, FLAGS)
                    .map_err(|e| ModelError::capability_discovery_error(e))?;
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "invalid capability type to come from component manager's namespace"
                )));
            }
        }
        CapabilitySource::Component(source_capability, realm) => {
            if let Some(path) = source_capability.path() {
                await!(Model::bind_instance_open_outgoing(
                    &model,
                    realm,
                    FLAGS,
                    open_mode,
                    path,
                    server_chan
                ))?;
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "invalid capability type to come from a component"
                )));
            }
        }
        CapabilitySource::AmbientService(source_capability_path, realm) => {
            await!(AmbientEnvironment::serve(
                model.clone(),
                realm,
                &source_capability_path,
                server_chan
            ))?;
        }
        CapabilitySource::StorageDecl(..) => {
            panic!("storage capabilities must be separately routed and opened");
        }
    }
    Ok(())
}

async fn route_and_open_storage_capability(
    model: &Model,
    type_: fsys::StorageType,
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
        capability: Capability::Storage(type_.clone()),
        last_child_moniker: use_abs_moniker.path().last().map(|c| c.clone()),
        moniker: parent_moniker,
    };

    let source = await!(walk_offer_chain(model, &mut pos))?;

    let (dir_capability, dir_source, storage_decl_realm) = match source {
        Some(CapabilitySource::StorageDecl(c, s, r)) => (c, s, r),
        _ => {
            return Err(ModelError::capability_discovery_error(format_err!(
                "storage capabilities must come from a storage declaration"
            )))
        }
    };

    // Find the path and source of the directory capability.
    let (capability_path, dir_source_realm) = match dir_source {
        OfferDirectorySource::Self_ => {
            (dir_capability.path().unwrap().clone(), storage_decl_realm.clone())
        }
        OfferDirectorySource::Realm => {
            let source = await!(find_capability_source(
                model,
                &dir_capability,
                &storage_decl_realm.abs_moniker
            ))?;
            match source {
                CapabilitySource::Component(source_capability, realm) => {
                    (source_capability.path().unwrap().clone(), realm)
                }
                _ => {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "storage capability backing directories must be provided by a component"
                    )))
                }
            }
        }
        OfferDirectorySource::Child(n) => {
            let mut pos = WalkPosition {
                capability: dir_capability,
                last_child_moniker: None,
                moniker: storage_decl_realm.abs_moniker.child(ChildMoniker::new(n, None)),
            };
            match await!(walk_expose_chain(model, &mut pos))? {
                CapabilitySource::Component(source_capability, realm) => {
                    (source_capability.path().unwrap().clone(), realm)
                }
                _ => {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "storage capability backing directories must be provided by a component"
                    )))
                }
            }
        }
    };

    // Bind with a local proxy, so we can create and open the relevant sub-directory for
    // this component.
    let (dir_proxy, local_server_end) =
        create_proxy::<DirectoryMarker>().expect("failed to create proxy");
    await!(Model::bind_instance_open_outgoing(
        &model,
        dir_source_realm,
        FLAGS,
        open_mode,
        &capability_path,
        local_server_end.into_channel()
    ))?;

    // Open each node individually, so it can be created if it doesn't exist
    let relative_moniker =
        RelativeMoniker::from_absolute(&storage_decl_realm.abs_moniker, &use_abs_moniker);
    let sub_dir_proxy =
        io_util::create_sub_directories(dir_proxy, &generate_storage_path(type_, relative_moniker))
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

/// Check if a used capability is ambient, and if so return the ambient `CapabilitySource`.
async fn find_ambient_capability<'a>(
    model: &'a Model,
    used_capability: &'a Capability,
    abs_moniker: &'a AbsoluteMoniker,
) -> Result<Option<CapabilitySource>, ModelError> {
    if let Some(path) = AMBIENT_SERVICES.iter().find(|p| match used_capability {
        Capability::Service(s) => **p == s,
        _ => false,
    }) {
        let realm = await!(model.look_up_realm(abs_moniker))?;
        Ok(Some(CapabilitySource::AmbientService((*path).clone(), realm)))
    } else {
        Ok(None)
    }
}

/// Holds state about the current position when walking the tree.
struct WalkPosition {
    /// The capability as it's represented in the current component.
    capability: Capability,
    /// The moniker of the child we came from.
    last_child_moniker: Option<ChildMoniker>,
    /// The moniker of the component we are currently looking at.
    moniker: AbsoluteMoniker,
}

/// find_capability_source will walk the component tree to find the originating source of a
/// capability, starting on the given abs_moniker. It returns the absolute moniker of the
/// originating component, a reference to its realm, and the capability exposed or offered at the
/// originating source. If the absolute moniker and realm are None, then the capability originates
/// at the returned path in componentmgr's namespace.
async fn find_capability_source<'a>(
    model: &'a Model,
    used_capability: &'a Capability,
    abs_moniker: &'a AbsoluteMoniker,
) -> Result<CapabilitySource, ModelError> {
    if let Some(ambient_capability) =
        await!(find_ambient_capability(model, used_capability, &abs_moniker))?
    {
        return Ok(ambient_capability);
    }

    let moniker = match abs_moniker.parent() {
        Some(m) => m,
        None => return Ok(CapabilitySource::ComponentMgrNamespace(used_capability.clone())),
    };
    let mut pos = WalkPosition {
        capability: used_capability.clone(),
        last_child_moniker: abs_moniker.path().last().map(|c| c.clone()),
        moniker: moniker,
    };

    if let Some(source) = await!(walk_offer_chain(model, &mut pos))? {
        return Ok(source);
    }
    await!(walk_expose_chain(model, &mut pos))
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
        let current_realm = await!(model.look_up_realm(&pos.moniker))?;
        let realm_state = await!(current_realm.state.lock());
        // This unwrap is safe because `look_up_realm` populates this field
        let decl = realm_state.decl.as_ref().expect("missing offer decl");
        let last_child_moniker = pos.last_child_moniker.as_ref().unwrap();
        if let Some(offer) = decl.find_offer_source(
            &pos.capability,
            last_child_moniker.name(),
            last_child_moniker.collection(),
        ) {
            // source is a (Option<&OfferSource>,Option<&OfferStorageSource>), of which exactly one
            // side will be Some and one side will be None. This is the source of offer, whose type
            // varies depending on which variant of offer is present.
            let source = match offer {
                OfferDecl::Service(s) => OfferSource::Service(&s.source),
                OfferDecl::Directory(d) => OfferSource::Directory(&d.source),
                OfferDecl::Storage(s) => OfferSource::Storage(s.source()),
            };
            match source {
                OfferSource::Service(OfferServiceSource::Realm)
                | OfferSource::Directory(OfferDirectorySource::Realm)
                | OfferSource::Storage(OfferStorageSource::Realm) => {
                    // The offered capability comes from the realm, so follow the
                    // parent
                    pos.capability = offer.clone().into();
                    pos.last_child_moniker = pos.moniker.path().last().map(|c| c.clone());
                    pos.moniker = match pos.moniker.parent() {
                        Some(m) => m,
                        None => {
                            return Ok(Some(CapabilitySource::ComponentMgrNamespace(
                                pos.capability.clone().into(),
                            )));
                        }
                    };
                    continue 'offerloop;
                }
                OfferSource::Service(OfferServiceSource::Self_)
                | OfferSource::Directory(OfferDirectorySource::Self_) => {
                    // The offered capability comes from the current component,
                    // return our current location in the tree.
                    return Ok(Some(CapabilitySource::Component(
                        offer.clone().into(),
                        current_realm.clone(),
                    )));
                }
                OfferSource::Service(OfferServiceSource::Child(child_name))
                | OfferSource::Directory(OfferDirectorySource::Child(child_name)) => {
                    // The offered capability comes from a child, break the loop
                    // and begin walking the expose chain.
                    pos.capability = offer.clone().into();
                    pos.moniker =
                        pos.moniker.child(ChildMoniker::new(child_name.to_string(), None));
                    return Ok(None);
                }
                OfferSource::Storage(OfferStorageSource::Storage(storage_name)) => {
                    let storage = decl
                        .find_storage_source(&storage_name)
                        .expect("storage offer references nonexistent section");
                    return Ok(Some(CapabilitySource::StorageDecl(
                        Capability::Directory(storage.source_path.clone()),
                        storage.source.clone(),
                        current_realm.clone(),
                    )));
                }
            }
        } else {
            return Err(ModelError::capability_discovery_error(format_err!(
                "no matching offers found for capability {} from component {}",
                pos.capability,
                pos.moniker
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
    loop {
        let current_realm = await!(model.look_up_realm(&pos.moniker))?;
        let realm_state = await!(current_realm.state.lock());
        // This unwrap is safe because look_up_realm populates this field
        let decl = realm_state.decl.as_ref().expect("missing expose decl");

        if let Some(expose) = decl.find_expose_source(&pos.capability) {
            let source = match expose {
                ExposeDecl::Service(d) => &d.source,
                ExposeDecl::Directory(d) => &d.source,
            };
            match source {
                ExposeSource::Self_ => {
                    // The offered capability comes from the current component, return our
                    // current location in the tree.
                    return Ok(CapabilitySource::Component(
                        expose.clone().into(),
                        current_realm.clone(),
                    ));
                }
                ExposeSource::Child(child_name) => {
                    // The offered capability comes from a child, so follow the child.
                    pos.capability = expose.clone().into();
                    pos.moniker =
                        pos.moniker.child(ChildMoniker::new(child_name.to_string(), None));
                    continue;
                }
            }
        } else {
            // We didn't find any matching exposes! Oh no!
            return Err(ModelError::capability_discovery_error(format_err!(
                "no matching exposes found for capability {} from component {}",
                pos.capability,
                pos.moniker
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
