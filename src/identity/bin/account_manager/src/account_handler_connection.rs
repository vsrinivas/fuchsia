// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account_handler_context::AccountHandlerContext;
use account_common::{AccountManagerError, LocalAccountId, ResultExt as AccountResultExt};
use anyhow::Context as _;
use async_trait::async_trait;
use core::fmt::Debug;
use fidl_fuchsia_identity_account::{Error as ApiError, Lifetime};
use fidl_fuchsia_identity_internal::{AccountHandlerControlMarker, AccountHandlerControlProxy};
use fidl_fuchsia_stash::StoreMarker;
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fuchsia_async as fasync;
use fuchsia_component::client::App;
use fuchsia_component::fuchsia_single_component_package_url;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use log::{error, info, warn};
use std::fmt;
use std::sync::Arc;

/// The url used to launch new AccountHandler component instances.
const ACCOUNT_HANDLER_URL: &str = fuchsia_single_component_package_url!("account_handler");

/// The url used to launch new ephemeral AccountHandler component instances.
const ACCOUNT_HANDLER_EPHEMERAL_URL: &str =
    "fuchsia-pkg://fuchsia.com/account_handler#meta/account_handler_ephemeral.cmx";

/// This trait is an abstraction over a connection to an account handler
/// component. It contains both static methods for creating connections
/// either by loading or creating a new account as well as instance methods for
/// calling the active account handler.
#[async_trait]
pub trait AccountHandlerConnection: Send + Sized + Debug {
    /// Create a new uninitialized AccountHandlerConnection.
    fn new(
        account_id: LocalAccountId,
        lifetime: Lifetime,
        context: Arc<AccountHandlerContext>,
    ) -> Result<Self, AccountManagerError>;

    /// Returns the lifetime of the account.
    fn get_account_id(&self) -> &LocalAccountId;

    /// Returns the lifetime of the account.
    fn get_lifetime(&self) -> &Lifetime;

    /// An AccountHandlerControlProxy for this connection.
    fn proxy(&self) -> &AccountHandlerControlProxy;

    /// Requests that the AccountHandler component instance terminate gracefully.
    ///
    /// Any subsequent operations that attempt to use `proxy()` will fail after this call. The
    /// resources associated with the connection when only be freed once the
    /// `AccountHandlerConnectionImpl` is dropped.
    async fn terminate(&self);
}

/// Implementation of an AccountHandlerConnection which creates real component
/// instances.
pub struct AccountHandlerConnectionImpl {
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

    /// The local account id of the account.
    account_id: LocalAccountId,

    /// The lifetime of the account.
    lifetime: Lifetime,

    /// A `Proxy` connected to the AccountHandlerControl interface on the launched AccountHandler.
    proxy: AccountHandlerControlProxy,
}

impl fmt::Debug for AccountHandlerConnectionImpl {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "AccountHandlerConnectionImpl {{ lifetime: {:?} }}", self.lifetime)
    }
}

#[async_trait]
impl AccountHandlerConnection for AccountHandlerConnectionImpl {
    fn new(
        account_id: LocalAccountId,
        lifetime: Lifetime,
        context: Arc<AccountHandlerContext>,
    ) -> Result<Self, AccountManagerError> {
        let account_handler_url = if lifetime == Lifetime::Ephemeral {
            info!("Launching new ephemeral AccountHandler instance");
            ACCOUNT_HANDLER_EPHEMERAL_URL
        } else {
            info!("Launching new persistent AccountHandler instance");
            ACCOUNT_HANDLER_URL
        };

        // Note: The combination of component URL and environment label determines the location of
        // the data directory for the launched component. It is critical that the label is unique
        // and stable per-account, which we achieve through using the local account id as
        // the environment name. We also pass in the account id as a flag, because the environment
        // name is not known to the launched component.
        let account_id_string = account_id.to_canonical_string();
        let mut fs_for_account_handler = ServiceFs::new();
        if lifetime == Lifetime::Persistent {
            fs_for_account_handler.add_proxy_service::<StoreMarker, _>();
        }
        fs_for_account_handler.add_fidl_service(move |stream| {
            let context_clone = context.clone();
            fasync::spawn(async move {
                context_clone
                    .handle_requests_from_stream(stream)
                    .await
                    .unwrap_or_else(|err| error!("Error handling AccountHandlerContext: {:?}", err))
            });
        });
        let (env_controller, app) = fs_for_account_handler
            .launch_component_in_nested_environment(
                account_handler_url.to_string(),
                Some(vec![format!("--account_id={}", account_id_string)]),
                account_id_string.as_ref(),
            )
            .context("Failed to start launcher")
            .account_manager_error(ApiError::Resource)?;
        fasync::spawn(fs_for_account_handler.collect());
        let proxy = app
            .connect_to_service::<AccountHandlerControlMarker>()
            .context("Failed to connect to AccountHandlerControl")
            .account_manager_error(ApiError::Resource)?;

        Ok(AccountHandlerConnectionImpl {
            _app: app,
            _env_controller: env_controller,
            account_id,
            lifetime,
            proxy,
        })
    }

    fn get_account_id(&self) -> &LocalAccountId {
        &self.account_id
    }

    fn get_lifetime(&self) -> &Lifetime {
        &self.lifetime
    }

    fn proxy(&self) -> &AccountHandlerControlProxy {
        &self.proxy
    }

    async fn terminate(&self) {
        let mut event_stream = self.proxy.take_event_stream();
        if let Err(err) = self.proxy.terminate() {
            warn!("Error gracefully terminating account handler {:?}", err);
        } else {
            while let Ok(Some(_)) = event_stream.try_next().await {}
            info!("Gracefully terminated AccountHandler instance");
        }
    }
}
