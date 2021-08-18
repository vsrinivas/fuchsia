// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{builder, Moniker},
    anyhow, cm_rust, fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_realm_builder as frealmbuilder,
    thiserror::{self, Error},
};

#[derive(Debug, Error)]
pub enum Error {
    #[error("error encountered while loading realm")]
    Builder(#[from] BuilderError),

    #[error("error encountered while assembling realm")]
    Realm(#[from] RealmError),

    #[error("error encountered while assembling realm")]
    Event(#[from] EventError),

    #[error("an internal error was encountered while working with the framework intermediary")]
    FidlError(#[from] fidl::Error),

    #[error("failed to set component decl for {0}: {1:?}")]
    FailedToSetDecl(Moniker, frealmbuilder::RealmBuilderError),

    #[error("failed to retrieve component decl for {0}: {1:?}")]
    FailedToGetDecl(Moniker, frealmbuilder::RealmBuilderError),

    #[error("failed to mark component {0} as eager: {1:?}")]
    FailedToMarkAsEager(Moniker, frealmbuilder::RealmBuilderError),

    #[error("failed to commit realm: {0:?}")]
    FailedToCommit(frealmbuilder::RealmBuilderError),

    #[error("failed to route capability: {0:?}")]
    FailedToRoute(frealmbuilder::RealmBuilderError),

    #[error("failed to open \"/pkg\": {0:?}")]
    FailedToOpenPkgDir(anyhow::Error),

    #[error("failed to set package directory: {0:?}")]
    FailedToSetPkgDir(frealmbuilder::RealmBuilderError),

    #[error("unable to destroy realm, the destroy waiter for root has already been taken")]
    DestroyWaiterTaken,
}

#[derive(Debug, Error)]
pub enum BuilderError {
    #[error("can't add {} to the realm as a component with that name already exists", _0)]
    ComponentAlreadyExists(Moniker),

    #[error("can't override {} in the realm because that component doesn't exist", _0)]
    ComponentDoesNotExists(Moniker),
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

    #[error("failed to create child component: {:?}", _0)]
    FailedToCreateChild(anyhow::Error),
}

#[derive(Debug, Error)]
pub enum EventError {
    #[error("route source {} doesn't exist", _0)]
    MissingRouteSource(Moniker),

    #[error("route targets cannot be empty")]
    EmptyRouteTargets,

    #[error("can't route a capability to the same place it comes from: {:?}", _0)]
    RouteSourceAndTargetMatch(builder::CapabilityRoute),

    #[error("route target {} doesn't exist", _0)]
    MissingRouteTarget(Moniker),

    #[error(
        "failed to add event route because an event {0} cannot be offered from a child: {0:?}"
    )]
    EventCannotBeOfferedFromChild(String, builder::CapabilityRoute),

    #[error("failed to add route because {:?} is already being offered by {:?} to {:?} from {:?}", _0.capability, _1, _2, _3)]
    ConflictingOffers(builder::CapabilityRoute, Moniker, cm_rust::OfferTarget, String),

    #[error("failed to add route because an event ({0}) cannot be exposed")]
    EventsCannotBeExposed(String),
}

// TODO: Define an error type for ScopedInstance
