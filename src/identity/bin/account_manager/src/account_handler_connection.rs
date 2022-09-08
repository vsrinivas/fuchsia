// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    account_common::{AccountId, AccountManagerError, ResultExt},
    anyhow::Context,
    async_trait::async_trait,
    core::fmt::Debug,
    fidl_fuchsia_component::{CreateChildArgs, RealmMarker},
    fidl_fuchsia_component_decl::{Child, CollectionRef, StartupMode},
    fidl_fuchsia_identity_account::{Error as ApiError, Lifetime},
    fidl_fuchsia_identity_internal::{AccountHandlerControlMarker, AccountHandlerControlProxy},
    fidl_fuchsia_stash::StoreMarker,
    fuchsia_async as fasync,
    fuchsia_component::{client, server::ServiceFs},
    futures::prelude::*,
    std::fmt,
    tracing::{info, warn},
};

/// The url used to launch new AccountHandler component instances.
///
/// Note that account_handler and account_manager are packaged together in
/// the integration test and in production so the same relative URL works
/// in both cases. In the future, we could use structured configuration to
/// set the account_handler url to allow more flexibility in packaging.
const ACCOUNT_HANDLER_URL: &str = "#meta/account_handler.cm";

/// The url used to launch new ephemeral AccountHandler component instances.
///
/// Note that account_handler and account_manager are packaged together in
/// the integration test and in production so the same relative URL works
/// in both cases. In the future, we could use structured configuration to
/// set the account_handler url to allow more flexibility in packaging.
const ACCOUNT_HANDLER_EPHEMERAL_URL: &str = "#meta/account_handler_ephemeral.cm";

/// The collection where we spawn new AccountHandler component instances.
const ACCOUNT_HANDLER_COLLECTION_NAME: &str = "account_handlers";

/// The prefix which, after appending account id is used for
/// all AccountHandler instance names.
const ACCOUNT_PREFIX: &str = "account_";

/// This trait is an abstraction over a connection to an account handler
/// component. It contains both static methods for creating connections
/// either by loading or creating a new account as well as instance methods for
/// calling the active account handler.
#[async_trait]
pub trait AccountHandlerConnection: Send + Sized + Debug {
    /// Create a new uninitialized AccountHandlerConnection.
    async fn new(account_id: AccountId, lifetime: Lifetime) -> Result<Self, AccountManagerError>;

    /// Returns the lifetime of the account.
    fn get_account_id(&self) -> &AccountId;

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
    /// The account id of the account.
    account_id: AccountId,

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
    async fn new(account_id: AccountId, lifetime: Lifetime) -> Result<Self, AccountManagerError> {
        let account_handler_url = if lifetime == Lifetime::Ephemeral {
            info!("Launching new ephemeral AccountHandler instance");
            ACCOUNT_HANDLER_EPHEMERAL_URL
        } else {
            info!("Launching new persistent AccountHandler instance");
            ACCOUNT_HANDLER_URL
        };

        // We append account id to the account_prefix to get a unique instance
        // name for each account.
        let account_handler_name = ACCOUNT_PREFIX.to_string() + &account_id.to_canonical_string();
        let mut fs_for_account_handler = ServiceFs::new();
        if lifetime == Lifetime::Persistent {
            fs_for_account_handler.add_proxy_service::<StoreMarker, _>();
        }

        let realm = client::connect_to_protocol::<RealmMarker>()
            .context("failed to connect to fuchsia.component.Realm")
            .account_manager_error(ApiError::Resource)?;

        let mut collection_ref =
            CollectionRef { name: String::from(ACCOUNT_HANDLER_COLLECTION_NAME) };

        let child_decl = Child {
            name: Some(account_handler_name.clone()),
            url: Some(account_handler_url.to_string()),
            startup: Some(StartupMode::Lazy),
            ..Child::EMPTY
        };

        realm
            .create_child(&mut collection_ref, child_decl, CreateChildArgs::EMPTY)
            .await
            .map_err(|err| {
                warn!("Failed to create account_handler component instance: {:?}", err);
                AccountManagerError::new(ApiError::Resource)
            })?
            .map_err(|err| {
                warn!("Failed to create account_handler component instance: {:?}", err);
                AccountManagerError::new(ApiError::Resource)
            })?;

        let exposed_dir = client::open_childs_exposed_directory(
            account_handler_name,
            Some(ACCOUNT_HANDLER_COLLECTION_NAME.to_string()),
        )
        .await
        .context("failed to open exposed directory")
        .account_manager_error(ApiError::Resource)?;

        let proxy =
            client::connect_to_protocol_at_dir_root::<AccountHandlerControlMarker>(&exposed_dir)
                .context("Failed to connect to AccountHandlerControl")
                .account_manager_error(ApiError::Resource)?;
        fasync::Task::spawn(fs_for_account_handler.collect()).detach();

        Ok(AccountHandlerConnectionImpl { account_id, lifetime, proxy })
    }

    fn get_account_id(&self) -> &AccountId {
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
            warn!("Error gracefully terminating AccountHandler {:?}", err);
        } else {
            while let Ok(Some(_)) = event_stream.try_next().await {}
            info!("Gracefully terminated AccountHandler instance");
        }
    }
}
