// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account_handler_context::AccountHandlerContext;
use account_common::{AccountManagerError, LocalAccountId, ResultExt as AccountResultExt};
use failure::{format_err, ResultExt};
use fidl::endpoints::{ClientEnd, RequestStream};
use fidl_fuchsia_auth_account::Status;
use fidl_fuchsia_auth_account_internal::{
    AccountHandlerContextMarker, AccountHandlerContextRequestStream, AccountHandlerControlMarker,
    AccountHandlerControlProxy,
};
use fuchsia_app::client::{App, Launcher};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::prelude::*;
use log::{error, info, warn};
use std::sync::Arc;

/// The url used to launch new AccountHandler component instances.
const ACCOUNT_HANDLER_URL: &str =
    "fuchsia-pkg://fuchsia.com/account_handler#meta/account_handler.cmx";

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

impl AccountHandlerConnection {
    /// Launches a new AccountHandler component instance and establishes a connection to its
    /// control channel.
    ///
    /// Note: This method is not public. Callers should use one of the factory methods that also
    /// sends an initialization call to the AccountHandler after connection, such as `load_account`
    /// or `create_account`
    fn new() -> Result<Self, AccountManagerError> {
        info!("Launching new AccountHandler instance");

        let launcher = Launcher::new()
            .context("Failed to start launcher")
            .account_manager_status(Status::IoError)?;
        let app = launcher
            .launch(ACCOUNT_HANDLER_URL.to_string(), None)
            .context("Failed to launch AccountHandler")
            .account_manager_status(Status::IoError)?;
        let proxy = app
            .connect_to_service(AccountHandlerControlMarker)
            .context("Failed to connect to AccountHandlerControl")
            .account_manager_status(Status::InternalError)?;
        Ok(AccountHandlerConnection { _app: app, proxy })
    }

    /// Creates a new `AccountHandlerContext` channel, spawns a task to handle requests received on
    /// this channel using the supplied `AccountHandlerContext`, and returns the `ClientEnd`.
    fn spawn_context_channel(
        context: Arc<AccountHandlerContext>,
    ) -> Result<ClientEnd<AccountHandlerContextMarker>, AccountManagerError> {
        let (server_chan, client_chan) = zx::Channel::create()
            .context("Failed to create channel")
            .account_manager_status(Status::IoError)?;
        let server_async_chan = fasync::Channel::from_channel(server_chan)
            .context("Failed to create async channel")
            .account_manager_status(Status::IoError)?;
        let request_stream = AccountHandlerContextRequestStream::from_channel(server_async_chan);
        let context_clone = Arc::clone(&context);
        fasync::spawn(
            async move {
                await!(context_clone.handle_requests_from_stream(request_stream))
                    .unwrap_or_else(|err| error!("Error handling AccountHandlerContext: {:?}", err))
            },
        );
        Ok(ClientEnd::new(client_chan))
    }

    /// Launches a new AccountHandler component instance, establishes a connection to its control
    /// channel, and requests that it loads an existing account.
    pub async fn load_account(
        account_id: &LocalAccountId,
        context: Arc<AccountHandlerContext>,
    ) -> Result<Self, AccountManagerError> {
        let connection = Self::new()?;
        let context_client_end = Self::spawn_context_channel(context)?;
        let mut fidl_account_id = account_id.clone().into();
        match await!(connection.proxy.load_account(context_client_end, &mut fidl_account_id))
            .account_manager_status(Status::IoError)?
        {
            Status::Ok => Ok(connection),
            stat => Err(AccountManagerError::new(stat)
                .with_cause(format_err!("Error loading existing account"))),
        }
    }

    /// Launches a new AccountHandler component instance, establishes a connection to its control
    /// channel, and requests that it create a new account.
    pub async fn create_account(
        context: Arc<AccountHandlerContext>,
    ) -> Result<(Self, LocalAccountId), AccountManagerError> {
        let connection = Self::new()?;
        let context_client_end = Self::spawn_context_channel(context)?;
        let account_id = match await!(connection.proxy().create_account(context_client_end))
            .account_manager_status(Status::IoError)?
        {
            (Status::Ok, Some(account_id)) => LocalAccountId::from(*account_id),
            (Status::Ok, None) => {
                return Err(AccountManagerError::new(Status::InternalError)
                    .with_cause(format_err!("Account handler returned success without ID")));
            }
            (stat, _) => {
                return Err(AccountManagerError::new(stat)
                    .with_cause(format_err!("Account handler returned error")));
            }
        };

        Ok((connection, account_id))
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
