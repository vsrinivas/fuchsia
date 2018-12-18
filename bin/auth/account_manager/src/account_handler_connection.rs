// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::{AccountManagerError, LocalAccountId};
use failure::{format_err, ResultExt};
use fidl_fuchsia_auth_account::Status;
use fidl_fuchsia_auth_account_internal::{AccountHandlerControlMarker, AccountHandlerControlProxy};
use fuchsia_app::client::{App, Launcher};
use futures::prelude::*;
use log::{info, warn};

/// The information necessary to maintain a connection to an AccountHandler component instance.
pub struct AccountHandlerConnection {
    /// An `App` object for the launched AccountHandler.
    ///
    /// Note: This must remain in scope for the component to remain running, but never needs to be
    /// read.
    _app: App,

    /// A `Proxy` connected to the AccountHandlerControl interface on the launched AccountHandler.
    proxy: AccountHandlerControlProxy,
}

/// The url used to launch new AccountHandler component instances.
const ACCOUNT_HANDLER_URL: &str =
    "fuchsia-pkg://fuchsia.com/account_handler#meta/account_handler.cmx";

impl AccountHandlerConnection {
    /// Launches a new AccountHandler component instance and establishes a connection to its
    /// control channel.
    pub fn new() -> Result<Self, AccountManagerError> {
        info!("Launching new AccountHandler instance");

        let launcher = Launcher::new()
            .context("Failed to start launcher")
            .map_err(|err| AccountManagerError::new(Status::IoError).with_cause(err))?;
        let app = launcher
            .launch(ACCOUNT_HANDLER_URL.to_string(), None)
            .context("Failed to launch AccountHandler")
            .map_err(|err| AccountManagerError::new(Status::IoError).with_cause(err))?;
        let proxy = app
            .connect_to_service(AccountHandlerControlMarker)
            .context("Failed to connect to AccountHandlerControl")
            .map_err(|err| AccountManagerError::new(Status::InternalError).with_cause(err))?;
        Ok(AccountHandlerConnection { _app: app, proxy })
    }

    /// Launches a new AccountHandler component instance, establishes a connection to its control
    /// channel, and requests that it loads an existing account.
    pub async fn new_for_account(account_id: &LocalAccountId) -> Result<Self, AccountManagerError> {
        let connection = Self::new()?;
        let mut fidl_account_id = account_id.clone().into();
        match await!(connection.proxy.load_account(&mut fidl_account_id))
            .map_err(|err| AccountManagerError::new(Status::IoError).with_cause(err))?
        {
            Status::Ok => Ok(connection),
            stat => Err(AccountManagerError::new(stat)
                .with_cause(format_err!("Error loading existing account"))),
        }
    }

    /// Returns the AccountHandlerControlProxy for this connection
    pub fn proxy(&self) -> &AccountHandlerControlProxy {
        &self.proxy
    }

    /// Requests that the AccountHandler component instance terminate gracefully.
    ///
    /// Any subsequent operations that attempt to use `proxy()` will fail after this call. The
    /// resources associated with the connection when only be freed once the
    /// `AccountHandlerConnection` is dropped.
    pub async fn terminate(&self) {
        let mut event_stream = self.proxy.take_event_stream();
        if let Err(err) = self.proxy.terminate() {
            warn!("Error gracefully terminating account handler {:?}", err);
        } else {
            while let Ok(Some(_)) = await!(event_stream.try_next()) {}
            info!("Gracefully terminated AccountHandler instance");
        }
    }
}
