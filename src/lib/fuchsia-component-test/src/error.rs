// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{builder, Moniker},
    anyhow, cm_rust, fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon as zx,
    thiserror::{self, Error},
};

#[derive(Debug, Error)]
pub enum Error {
    #[error("error encountered while loading realm")]
    Builder(#[from] BuilderError),

    #[error("error encountered while assembling realm")]
    Realm(#[from] RealmError),
}

#[derive(Debug, Error)]
pub enum BuilderError {
    #[error("can't add {} to the realm because {} already exists, and added components must be leaf nodes in the generated realm", _0, _1)]
    AddedComponentsMustBeLeafNodes(Moniker, Moniker),

    #[error("can't add {} to the realm as a component with that name already exists", _0)]
    ComponentAlreadyExists(Moniker),

    #[error("can't route a capability to the same place it comes from: {:?}", _0)]
    RouteSourceAndTargetMatch(builder::CapabilityRoute),

    #[error("route source {} doesn't exist", _0)]
    MissingRouteSource(Moniker),

    #[error("route targets cannot be empty")]
    EmptyRouteTargets,

    #[error("route target {} doesn't exist", _0)]
    MissingRouteTarget(Moniker),

    #[error("route already exists: {:?}", _0)]
    RouteAlreadyExists(builder::CapabilityRoute),

    #[error("the root component cannot have a url source")]
    RootComponentCantHaveUrl,

    #[error("component name is invalid: {:?}", _0)]
    InvalidName(cm_rust::Error),

    #[error("failed to add route because {:?} is already being exposed by {:?} from {:?}", _0.capability, _1, _2)]
    ConflictingExposes(builder::CapabilityRoute, Moniker, cm_rust::ExposeSource),

    #[error("failed to add route because {:?} is already being offered by {:?} to {:?} from {:?}", _0.capability, _1, _2, _3)]
    ConflictingOffers(builder::CapabilityRoute, Moniker, cm_rust::OfferTarget, String),
    #[error(
        "failed to add storage route because storage {0} cannot be offered from a child: {0:?}"
    )]
    StorageCannotBeOfferedFromChild(String, builder::CapabilityRoute),

    #[error(
        "failed to add event route because an event {0} cannot be offered from a child: {0:?}"
    )]
    EventCannotBeOfferedFromChild(String, builder::CapabilityRoute),

    #[error("failed to add route because an event ({0}) cannot be exposed")]
    EventsCannotBeExposed(String),

    #[error("failed to add route because storage ({0}) cannot be exposed")]
    StorageCannotBeExposed(String),

    #[error("storage ({0}) must come from abvoe root")]
    StorageMustComeFromAboveRoot(String),
}

#[derive(Debug, Error)]
pub enum RealmError {
    #[error("failed to bind to the framework intermediary: {:?}", _0)]
    FailedBindToFrameworkIntermediary(fcomponent::Error),

    #[error("failed to use fuchsia.sys2.Realm: {:?}", _0)]
    FailedToUseRealm(fidl::Error),

    #[error("failed to create proxy: {:?}", _0)]
    CreateProxy(fidl::Error),

    #[error("failed to connect to fuchsia.sys2.Realm: {:?}", _0)]
    ConnectToRealmService(anyhow::Error),

    #[error("failed to connect to the framework intermediary: {:?}", _0)]
    ConnectToFrameworkIntermediaryService(anyhow::Error),

    #[error("failed to use the framework intermediary: {:?}", _0)]
    FailedToUseFrameworkIntermediary(fidl::Error),

    #[error("root component has already been added")]
    RootComponentAlreadyExists,

    #[error("root component has not been set yet")]
    RootComponentNotSetYet,

    #[error("can't add component {}, as one of its parents doesn't exist", _0)]
    ParentComponentDoesntExist(Moniker),

    #[error("can't add {} to the realm as something with that name already exists", _0)]
    ComponentAlreadyExists(Moniker),

    #[error("component doesn't exist: {}", _0)]
    ComponentDoesntExist(Moniker),

    #[error("the parent for the component {} doesn't have the child", _0)]
    MissingChild(Moniker),

    #[error("failed to create child component: {:?}", _0)]
    FailedToCreateChild(anyhow::Error),

    #[error("unable to create component {}, decl is invalid: {:?}, {:?}", _0, _1, _2)]
    InvalidDecl(Moniker, cm_fidl_validator::ErrorList, fsys::ComponentDecl),

    #[error("the framework intermediary rejected a component decl: {:?}", _0)]
    DeclRejectedByRegistry(zx::Status),

    #[error("the root component can not be marked as eager")]
    CantMarkRootAsEager,

    #[error("cannot modify component {}, as it comes from a URL", _0)]
    ComponentNotModifiable(Moniker),
}

// TODO: Define an error type for ScopedInstance
