// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io_util,
    crate::model::*,
    cm_rust::{self, CapabilityPath, RelativeId},
    failure::format_err,
    fidl_fuchsia_io::{MODE_TYPE_DIRECTORY, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::sync::Arc,
};

/// Describes the source of a capability, as determined by `find_capability_source`
#[derive(Debug)]
enum CapabilitySource {
    /// This capability source comes from the component described by this AbsoluteMoniker at
    /// this path. The Realm is provided as well, as it has already been looked up by this
    /// point.
    Component(CapabilityPath, Arc<Mutex<Realm>>),
    /// This capability source comes from component manager's namespace, at this path
    ComponentMgrNamespace(CapabilityPath),
}

/// `route_directory` will find the source of the directory capability used at `source_path` by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable)
pub async fn route_directory(
    model: &Model,
    source_path: CapabilityPath,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    await!(route_capability(
        model,
        fsys::CapabilityType::Directory,
        MODE_TYPE_DIRECTORY,
        source_path,
        abs_moniker,
        server_chan
    ))
}

/// `route_service` will find the source of the service capability used at `source_path` by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable)
pub async fn route_service(
    model: &Model,
    source_path: CapabilityPath,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    await!(route_capability(
        model,
        fsys::CapabilityType::Service,
        MODE_TYPE_SERVICE,
        source_path,
        abs_moniker,
        server_chan
    ))
}

/// `route_capability` will find the source of the capability `type_` used at `source_path` by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable) using an open request with
/// `open_mode`.
async fn route_capability(
    model: &Model,
    type_: fsys::CapabilityType,
    open_mode: u32,
    source_path: CapabilityPath,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let source = await!(find_capability_source(model, type_, source_path, abs_moniker))?;

    let flags = OPEN_RIGHT_READABLE;
    match source {
        CapabilitySource::ComponentMgrNamespace(path) => {
            io_util::connect_in_namespace(&path.to_string(), server_chan)
                .map_err(|e| ModelError::capability_discovery_error(e))?
        }
        CapabilitySource::Component(path, realm) => {
            await!(Model::bind_instance_and_open(
                &model,
                realm,
                flags,
                open_mode,
                path,
                server_chan
            ))?;
        }
    }

    Ok(())
}

/// find_capability_source will walk the component tree to find the originating source of a
/// capability, starting on the given abs_moniker and source_path. It returns the absolute
/// moniker of the originating component, a reference to its realm, and the path that the
/// component is exposing the capability from. If the absolute moniker and realm are None, then
/// the capability originates at the returned path in componentmgr's namespace.
async fn find_capability_source(
    model: &Model,
    type_: fsys::CapabilityType,
    source_path: CapabilityPath,
    abs_moniker: AbsoluteMoniker,
) -> Result<CapabilitySource, ModelError> {
    // Holds mutable state as we walk the tree
    struct State {
        // The current path of the capability
        path: CapabilityPath,
        // The name of the child we came from
        name: Option<ChildMoniker>,
        // The moniker of the component we are currently looking at
        moniker: AbsoluteMoniker,
    }
    let moniker = match abs_moniker.parent() {
        Some(m) => m,
        None => return Ok(CapabilitySource::ComponentMgrNamespace(source_path)),
    };
    let mut s = State {
        path: source_path,
        name: abs_moniker.path().last().map(|c| c.clone()),
        moniker: moniker,
    };
    // Walk offer chain
    'offerloop: loop {
        let current_realm_mutex = await!(model.look_up_realm(&s.moniker))?;
        let current_realm = await!(current_realm_mutex.lock());
        // This unwrap is safe because look_up_realm populates this field
        let decl = current_realm.instance.decl.as_ref().expect("missing offer decl");

        if let Some(offer) = decl.find_offer_source(&s.path, &type_, &s.name.unwrap().name()) {
            match &offer.source {
                RelativeId::Realm => {
                    // The offered capability comes from the realm, so follow the
                    // parent
                    s.path = offer.source_path.clone();
                    s.name = s.moniker.path().last().map(|c| c.clone());
                    s.moniker = match s.moniker.parent() {
                        Some(m) => m,
                        None => return Ok(CapabilitySource::ComponentMgrNamespace(s.path)),
                    };
                    continue 'offerloop;
                }
                RelativeId::Myself => {
                    // The offered capability comes from the current component,
                    // return our current location in the tree.
                    return Ok(CapabilitySource::Component(
                        offer.source_path.clone(),
                        current_realm_mutex.clone(),
                    ));
                }
                RelativeId::Child(child_name) => {
                    // The offered capability comes from a child, break the loop
                    // and begin walking the expose chain.
                    s.path = offer.source_path.clone();
                    s.moniker = s.moniker.child(ChildMoniker::new(child_name.to_string()));
                    break 'offerloop;
                }
            }
        } else {
            return Err(ModelError::capability_discovery_error(format_err!(
                "no matching offers found for capability {} from component {}",
                s.path,
                s.moniker
            )));
        }
    }
    // Walk expose chain
    loop {
        let current_realm_mutex = await!(model.look_up_realm(&s.moniker))?;
        let current_realm = await!(current_realm_mutex.lock());
        // This unwrap is safe because look_up_realm populates this field
        let decl = current_realm.instance.decl.as_ref().expect("missing expose decl");

        if let Some(expose) = decl.find_expose_source(&s.path, &type_) {
            match &expose.source {
                RelativeId::Myself => {
                    // The offered capability comes from the current component, return our
                    // current location in the tree.
                    return Ok(CapabilitySource::Component(
                        expose.source_path.clone(),
                        current_realm_mutex.clone(),
                    ));
                }
                RelativeId::Child(child_name) => {
                    // The offered capability comes from a child, so follow the child.
                    s.path = expose.source_path.clone();
                    s.moniker = s.moniker.child(ChildMoniker::new(child_name.to_string()));
                    continue;
                }
                _ => panic!("relation on an expose wasn't self or child"),
            }
        } else {
            // We didn't find any matching exposes! Oh no!
            return Err(ModelError::capability_discovery_error(format_err!(
                "no matching exposes found for capability {} from component {}",
                s.path,
                s.moniker
            )));
        }
    }
}
