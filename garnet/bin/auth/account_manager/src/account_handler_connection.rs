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
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fuchsia_app::client::App;
use fuchsia_app::server::ServicesServer;
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

    /// An `EnvController` object for the launched AccountHandler.
    ///
    /// Note: This must remain in scope for the component to remain running, but never needs to be
    /// read.
    _env_controller: EnvironmentControllerProxy,

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
    fn new(account_id: LocalAccountId) -> Result<Self, AccountManagerError> {
        info!("Launching new AccountHandler instance");

        // Note: The combination of component URL and environment label determines the location of
        // the data directory for the launched component. It is critical that the label is unique
        // and stable per-account, which we achieve through using the local account id as
        // the environment name.
        let env_label = account_id.to_canonical_string();
        let (server, env_controller, app) = ServicesServer::new()
            .launch_component_in_nested_environment(
                ACCOUNT_HANDLER_URL.to_string(),
                None,
                env_label.as_ref(),
            )
            .context("Failed to start launcher")
            .account_manager_status(Status::IoError)?;
        fasync::spawn(server.unwrap_or_else(|err| {
            error!("AccountHandlerConnection terminated unexpectedly: {:?}", err);
        }));
        let proxy = app
            .connect_to_service(AccountHandlerControlMarker)
            .context("Failed to connect to AccountHandlerControl")
            .account_manager_status(Status::IoError)?;

        Ok(AccountHandlerConnection { _app: app, _env_controller: env_controller, proxy })
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
        let connection = Self::new(account_id.clone())?;
        let context_client_end = Self::spawn_context_channel(context)?;
        match await!(connection
            .proxy
            .load_account(context_client_end, account_id.clone().as_mut().into()))
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
        let account_id = LocalAccountId::new(rand::random::<u64>());
        let connection = Self::new(account_id.clone())?;
        let context_client_end = Self::spawn_context_channel(context)?;

        match await!(connection
            .proxy()
            .create_account(context_client_end, account_id.clone().as_mut().into()))
        .account_manager_status(Status::IoError)?
        {
            Status::Ok => {
                // TODO(jsankey): Longer term, local ID may need to be related to the global ID
                // rather than just a random number.
                Ok((connection, account_id))
            }
            status => Err(AccountManagerError::new(status)
                .with_cause(format_err!("Account handler returned error"))),
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
