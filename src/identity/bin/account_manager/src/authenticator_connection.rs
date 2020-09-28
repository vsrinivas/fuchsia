// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::{AccountManagerError, ResultExt};
use anyhow::{format_err, Context as _};
use fidl::endpoints::{DiscoverableService, ServerEnd};
use fidl_fuchsia_identity_account::Error as ApiError;
use fidl_fuchsia_identity_authentication::StorageUnlockMechanismMarker;
use fuchsia_component::client::{launch, launcher, App};
use log::info;
use parking_lot::Mutex;
use std::sync::Arc;

/// A type capable of launching and connecting to a component that implements
/// one or more authentication mechanism protocols. Launching is performed on demand.
/// Currently, an AuthenticatorConnection provides a single default authentication
/// mechanism.
pub struct AuthenticatorConnection {
    /// The URL that should be used to launch the component.
    component_url: String,

    /// The state needed to retain a connection, once one has been created.
    connection_state: Mutex<Option<Arc<App>>>,
}

// TODO(fxbug.dev/427): Add test coverage through integration tests.
impl AuthenticatorConnection {
    /// Creates a new `AuthenticatorConnection` from the supplied Fuchsia component url.
    pub fn from_url(component_url: &str) -> Self {
        AuthenticatorConnection {
            component_url: component_url.to_string(),
            connection_state: Mutex::new(None),
        }
    }

    /// Returns the component_url supplied at construction
    #[allow(dead_code)]
    pub fn component_url(&self) -> &str {
        &self.component_url
    }

    /// Returns an `App` for opening new authenticator connections. If a
    /// component instance has previously been launched this is used, otherwise
    /// a fresh instance is launched and its proxy is stored for future use.
    fn get_app(&self) -> Result<Arc<App>, AccountManagerError> {
        let mut connection_state_lock = self.connection_state.lock();
        if let Some(connection_state) = &*connection_state_lock {
            return Ok(Arc::clone(&connection_state));
        }

        info!("Launching Authenticator component: {}", self.component_url);
        let launcher = launcher()
            .context("Failed to start launcher")
            .account_manager_error(ApiError::Unknown)?;
        let app = launch(&launcher, self.component_url.clone(), None)
            .context("Failed to launch Authenticator")
            .account_manager_error(ApiError::NotFound)?;
        let app_arc = Arc::new(app);
        connection_state_lock.get_or_insert(Arc::clone(&app_arc));
        Ok(app_arc)
    }

    /// Connects the supplied channel to the authenticator. If a component has previously been
    /// launched this is used, otherwise a fresh component is launched.
    pub fn connect<S>(&self, server_end: ServerEnd<S>) -> Result<(), AccountManagerError>
    where
        S: AuthenticatorService,
    {
        let app = self.get_app()?;
        app.pass_to_service::<S>(server_end.into_channel()).map_err(|err| {
            AccountManagerError::new(ApiError::Unknown)
                .with_cause(format_err!("Getting auth mechanism failed with {:?}", err))
        })
    }
}

/// A marker trait for identifying services that may be provided by an authenticator.
pub trait AuthenticatorService: DiscoverableService {}

impl AuthenticatorService for StorageUnlockMechanismMarker {}
