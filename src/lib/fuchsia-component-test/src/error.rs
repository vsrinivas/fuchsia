// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::new::Ref,
    anyhow, fidl_fuchsia_component_test as ftest,
    thiserror::{self, Error},
};

#[derive(Debug, Error)]
pub enum Error {
    #[error("route is missing source")]
    MissingSource,

    #[error("the realm builder server returned an error: {0:?}")]
    ServerError(ftest::RealmBuilderError2),

    #[error("an internal error was encountered while working with the realm builder server")]
    FidlError(#[from] fidl::Error),

    #[error("failed to open \"/pkg\": {0:?}")]
    FailedToOpenPkgDir(anyhow::Error),

    #[error("failed to connect to realm builder server: {0:?}")]
    ConnectToServer(anyhow::Error),

    #[error("unable to destroy realm, the destroy waiter for root has already been taken")]
    DestroyWaiterTaken,

    #[error("failed to bind to realm: {0:?}")]
    FailedToBind(anyhow::Error),

    #[error("failed to create child: {0:?}")]
    FailedToCreateChild(anyhow::Error),

    #[error("failed to destroy child: {0:?}")]
    FailedToDestroyChild(anyhow::Error),

    #[error("unable to use reference {0} in realm {1:?}")]
    RefUsedInWrongRealm(Ref, String),

    #[error(
        "realms built in a nested component manager are not allowed to contain legacy children"
    )]
    LegacyChildrenUnsupportedInNestedComponentManager,
}

impl From<ftest::RealmBuilderError2> for Error {
    fn from(err: ftest::RealmBuilderError2) -> Self {
        Self::ServerError(err)
    }
}

// TODO: Define an error type for ScopedInstance
