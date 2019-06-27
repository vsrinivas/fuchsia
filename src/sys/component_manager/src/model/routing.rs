// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::{self, Capability, CapabilityPath, ExposeDecl, ExposeSource, OfferDecl, OfferSource},
    failure::format_err,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
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
    let source = await!(find_capability_source(model, used_capability, abs_moniker))?;
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
    let flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;
    match source {
        CapabilitySource::ComponentMgrNamespace(source_capability) => {
            if let Some(path) = source_capability.path() {
                io_util::connect_in_namespace(&path.to_string(), server_chan, flags)
                    .map_err(|e| ModelError::capability_discovery_error(e))?;
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "storage capabilities are not supported"
                )));
            }
        }
        CapabilitySource::Component(source_capability, realm) => {
            if let Some(path) = source_capability.path() {
                await!(Model::bind_instance_open_outgoing(
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
                model.clone(),
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
    abs_moniker: AbsoluteMoniker,
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
            let source = match offer {
                OfferDecl::Service(d) => &d.source,
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
                OfferSource::Self_ => {
                    // The offered capability comes from the current component,
                    // return our current location in the tree.
                    return Ok(Some(CapabilitySource::Component(
                        offer.clone().into(),
                        current_realm.clone(),
                    )));
                }
                OfferSource::Child(child_name) => {
                    // The offered capability comes from a child, break the loop
                    // and begin walking the expose chain.
                    pos.capability = offer.clone().into();
                    pos.moniker =
                        pos.moniker.child(ChildMoniker::new(child_name.to_string(), None));
                    return Ok(None);
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
