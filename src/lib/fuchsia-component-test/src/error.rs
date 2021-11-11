// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{event, Moniker},
    anyhow, cm_rust, fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_test as ftest,
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

    #[error("an internal error was encountered while working with the realm builder server")]
    FidlError(#[from] fidl::Error),

    #[error("failed to set component decl for {0}: {1:?}")]
    FailedToSetDecl(Moniker, ftest::RealmBuilderError),

    #[error("failed to retrieve component decl for {0}: {1:?}")]
    FailedToGetDecl(Moniker, ftest::RealmBuilderError),

    #[error("failed to mark component {0} as eager: {1:?}")]
    FailedToMarkAsEager(Moniker, ftest::RealmBuilderError),

    #[error("failed to commit realm: {0:?}")]
    FailedToCommit(ftest::RealmBuilderError),

    #[error("failed to route capability: {0:?}")]
    FailedToRoute(ftest::RealmBuilderError),

    #[error("failed to open \"/pkg\": {0:?}")]
    FailedToOpenPkgDir(anyhow::Error),

    #[error("failed to set package directory: {0:?}")]
    FailedToSetPkgDir(ftest::RealmBuilderError),

    #[error("unable to destroy realm, the destroy waiter for root has already been taken")]
    DestroyWaiterTaken,

    #[error("routes for event capabilities must be provided to a RealmBuilder, not a Realm")]
    EventRoutesOnlySupportedOnBuilder,

    #[error("failed to bind to realm: {0:?}")]
    FailedToBind(anyhow::Error),
}

#[derive(Debug, Error)]
pub enum BuilderError {
    #[error("can't add {} to the realm as a component with that name already exists", _0)]
    ComponentAlreadyExists(Moniker),

    #[error("can't override {} in the realm because that component doesn't exist", _0)]
    ComponentDoesNotExist(Moniker),
}

#[derive(Debug, Error)]
pub enum RealmError {
    #[error("failed to bind to the realm builder server: {:?}", _0)]
    FailedBindToRealmBuilder(fcomponent::Error),

    #[error("failed to use fuchsia.component.Realm: {:?}", _0)]
    FailedToUseRealm(fidl::Error),

    #[error("failed to create proxy: {:?}", _0)]
    CreateProxy(fidl::Error),

    #[error("failed to connect to fuchsia.component.Realm: {:?}", _0)]
    ConnectToRealmService(anyhow::Error),

    #[error("failed to connect to the realm builder server: {:?}", _0)]
    ConnectToRealmBuilderService(anyhow::Error),

    #[error("failed to use the realm builder server: {:?}", _0)]
    FailedToUseRealmBuilder(fidl::Error),

    #[error("failed to create child component: {:?}", _0)]
    FailedToCreateChild(anyhow::Error),

    #[error("failed to destroy child: {0:?}")]
    FailedToDestroyChild(anyhow::Error),

    #[error("failed to set mock component for {0}: {1:?}")]
    FailedToSetMock(Moniker, ftest::RealmBuilderError),
}

#[derive(Debug, Error)]
pub enum EventError {
    #[error("route source {} doesn't exist", _0)]
    MissingRouteSource(Moniker),

    #[error("route targets cannot be empty")]
    EmptyRouteTargets,

    #[error("can't route a capability to the same place it comes from: {:?}", _0)]
    RouteSourceAndTargetMatch(event::CapabilityRoute),

    #[error("route target {} doesn't exist", _0)]
    MissingRouteTarget(Moniker),

    #[error(
        "failed to add event route because an event {0} cannot be offered from a child: {0:?}"
    )]
    EventCannotBeOfferedFromChild(String, event::CapabilityRoute),

    #[error("failed to add route because {:?} is already being offered by {:?} to {:?} from {:?}", _0.capability, _1, _2, _3)]
    ConflictingOffers(event::CapabilityRoute, Moniker, cm_rust::OfferTarget, String),

    #[error("failed to add route because event capabilities cannot be exposed")]
    EventsCannotBeExposed,
}

// TODO: Define an error type for ScopedInstance
