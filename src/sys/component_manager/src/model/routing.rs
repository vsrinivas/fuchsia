// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{self, Capability, CapabilityPath, ExposeDecl, ExposeSource, OfferDecl, OfferSource},
    failure::format_err,
    fidl_fuchsia_io::{MODE_TYPE_DIRECTORY, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fuchsia_zircon as zx,
    std::sync::Arc,
};

/// Describes the source of a capability, as determined by `find_capability_source`
enum CapabilitySource {
    /// This capability originates from the component instance for the given Realm.
    /// point.
    Component(Capability, Arc<Realm>),
    /// This capability originates from component manager's namespace.
    ComponentMgrNamespace(Capability),
    /// This capability is an ambient service and originates from component manager itself.
    AmbientService(CapabilityPath, Arc<Realm>),
}

/// `route_directory` will find the source of the directory capability used at `source_path` by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable)
pub async fn route_directory<'a>(
    model: &'a Model,
    capability: &'a Capability,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    await!(route_capability(model, MODE_TYPE_DIRECTORY, capability, abs_moniker, server_chan))
}

/// `route_service` will find the source of the service capability used at `source_path` by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable)
pub async fn route_service<'a>(
    model: &'a Model,
    capability: &'a Capability,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    await!(route_capability(model, MODE_TYPE_SERVICE, capability, abs_moniker, server_chan))
}

/// `route_capability` will find the source of the `capability` used by
/// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
/// directory (or componentmgr's namespace, if applicable) using an open request with
/// `open_mode`.
async fn route_capability<'a>(
    model: &'a Model,
    open_mode: u32,
    capability: &'a Capability,
    abs_moniker: AbsoluteMoniker,
    server_chan: zx::Channel,
) -> Result<(), ModelError> {
    let source = await!(find_capability_source(model, capability, abs_moniker))?;

    let flags = OPEN_RIGHT_READABLE;
    match source {
        CapabilitySource::ComponentMgrNamespace(source_capability) => {
            if let Some(path) = source_capability.path() {
                io_util::connect_in_namespace(&path.to_string(), server_chan)
                    .map_err(|e| ModelError::capability_discovery_error(e))?;
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "storage capabilities are not supported"
                )));
            }
        }
        CapabilitySource::Component(source_capability, realm) => {
            if let Some(path) = source_capability.path() {
                await!(Model::bind_instance_and_open(
                    &model,
                    realm,
                    flags,
                    open_mode,
                    path,
                    server_chan
                ))?;
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "storage capabilities are not supported"
                )));
            }
        }
        CapabilitySource::AmbientService(source_capability_path, realm) => {
            await!(AmbientEnvironment::serve(
                model.ambient.clone(),
                realm,
                &source_capability_path,
                server_chan
            ))?;
        }
    }

    Ok(())
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

/// find_capability_source will walk the component tree to find the originating source of a
/// capability, starting on the given abs_moniker. It returns the absolute moniker of the
/// originating component, a reference to its realm, and the capability exposed or offered at the
/// originating source. If the absolute moniker and realm are None, then the capability originates
/// at the returned path in componentmgr's namespace.
async fn find_capability_source<'a>(
    model: &'a Model,
    used_capability: &'a Capability,
    abs_moniker: AbsoluteMoniker,
) -> Result<CapabilitySource, ModelError> {
    if let Some(ambient_capability) =
        await!(find_ambient_capability(model, used_capability, &abs_moniker))?
    {
        return Ok(ambient_capability);
    }

    // Holds mutable state as we walk the tree
    struct State {
        // The capability as it's represented in the current component
        capability: Capability,
        // The name of the child we came from
        name: Option<ChildMoniker>,
        // The moniker of the component we are currently looking at
        moniker: AbsoluteMoniker,
    }
    let moniker = match abs_moniker.parent() {
        Some(m) => m,
        None => return Ok(CapabilitySource::ComponentMgrNamespace(used_capability.clone())),
    };
    let mut s = State {
        capability: used_capability.clone(),
        name: abs_moniker.path().last().map(|c| c.clone()),
        moniker: moniker,
    };
    // Walk offer chain
    'offerloop: loop {
        let current_realm = await!(model.look_up_realm(&s.moniker))?;
        let realm_state = await!(current_realm.instance.state.lock());
        // This unwrap is safe because look_up_realm populates this field
        let decl = realm_state.decl.as_ref().expect("missing offer decl");

        if let Some(offer) = decl.find_offer_source(&s.capability, &s.name.unwrap().name()) {
            let source = match offer {
                OfferDecl::Service(s) => &s.source,
                OfferDecl::Directory(d) => &d.source,
                OfferDecl::Storage(_) => {
                    return Err(ModelError::capability_discovery_error(format_err!(
                        "storage capabilities are not supported"
                    )))
                }
            };
            match source {
                OfferSource::Realm => {
                    // The offered capability comes from the realm, so follow the
                    // parent
                    s.capability = offer.clone().into();
                    s.name = s.moniker.path().last().map(|c| c.clone());
                    s.moniker = match s.moniker.parent() {
                        Some(m) => m,
                        None => {
                            return Ok(CapabilitySource::ComponentMgrNamespace(
                                s.capability.clone().into(),
                            ))
                        }
                    };
                    continue 'offerloop;
                }
                OfferSource::Self_ => {
                    // The offered capability comes from the current component,
                    // return our current location in the tree.
                    return Ok(CapabilitySource::Component(
                        offer.clone().into(),
                        current_realm.clone(),
                    ));
                }
                OfferSource::Child(child_name) => {
                    // The offered capability comes from a child, break the loop
                    // and begin walking the expose chain.
                    s.capability = offer.clone().into();
                    s.moniker = s.moniker.child(ChildMoniker::new(child_name.to_string(), None));
                    break 'offerloop;
                }
            }
        } else {
            return Err(ModelError::capability_discovery_error(format_err!(
                "no matching offers found for capability {} from component {}",
                s.capability,
                s.moniker
            )));
        }
    }
    // Walk expose chain
    loop {
        let current_realm = await!(model.look_up_realm(&s.moniker))?;
        let realm_state = await!(current_realm.instance.state.lock());
        // This unwrap is safe because look_up_realm populates this field
        let decl = realm_state.decl.as_ref().expect("missing expose decl");

        if let Some(expose) = decl.find_expose_source(&s.capability) {
            let source = match expose {
                ExposeDecl::Service(s) => &s.source,
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
                    s.capability = expose.clone().into();
                    s.moniker = s.moniker.child(ChildMoniker::new(child_name.to_string(), None));
                    continue;
                }
            }
        } else {
            // We didn't find any matching exposes! Oh no!
            return Err(ModelError::capability_discovery_error(format_err!(
                "no matching exposes found for capability {} from component {}",
                s.capability,
                s.moniker
            )));
        }
    }
}
