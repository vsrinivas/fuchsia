// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::{AccountManagerError, FidlLocalAccountId, LocalAccountId};
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::AuthProviderConfig;
use fidl_fuchsia_auth::{AuthState, AuthStateSummary, AuthenticationContextProviderMarker};
use fidl_fuchsia_auth_account::{
    AccountAuthState, AccountListenerMarker, AccountListenerOptions, AccountManagerRequest,
    AccountManagerRequestStream, AccountMarker, Status,
};
use futures::lock::Mutex;
use futures::prelude::*;
use lazy_static::lazy_static;
use log::{info, warn};
use std::collections::BTreeMap;
use std::fs;
use std::path::PathBuf;
use std::sync::Arc;

use crate::account_handler_connection::AccountHandlerConnection;
use crate::account_handler_context::AccountHandlerContext;

/// (Temporary) A fixed AuthState that is used for all accounts until authenticators are
/// available.
const DEFAULT_AUTH_STATE: AuthState = AuthState { summary: AuthStateSummary::Unknown };

lazy_static! {
    /// (Temporary) Configuration for a fixed set of auth providers used until file-based
    /// configuration is available.
    static ref DEFAULT_AUTH_PROVIDER_CONFIG: Vec<AuthProviderConfig> = {
        vec![AuthProviderConfig {
            auth_provider_type: "google".to_string(),
            url: "fuchsia-pkg://fuchsia.com/google_auth_provider#meta/google_auth_provider.cmx"
                .to_string(),
            params: None
        }]
    };
}

/// The core component of the account system for Fuchsia.
///
/// The AccountManager maintains the set of Fuchsia accounts that are provisioned on the device,
/// launches and configures AuthenticationProvider components to perform authentication via
/// service providers, and launches and delegates to AccountHandler component instances to
/// determine the detailed state and authentication for each account.
pub struct AccountManager {
    /// An ordered map from the `LocalAccountId` of all accounts on the device to an
    /// `Option` containing the `AcountHandlerConnection` used to communicate with the associated
    /// AccountHandler if a connecton exists, or None otherwise.
    ids_to_handlers: Mutex<BTreeMap<LocalAccountId, Option<Arc<AccountHandlerConnection>>>>,

    /// An object to service requests for contextual information from AccountHandlers.
    context: Arc<AccountHandlerContext>,
}

impl AccountManager {
    /// Constructs a new AccountManager with no accounts.
    pub fn new(account_dir_parent: &str) -> Result<AccountManager, Error> {
        let dir = PathBuf::from(account_dir_parent);
        if !dir.exists() {
            fs::create_dir(account_dir_parent)?;
            info!("Created account dir parent: {:?}", account_dir_parent);
        }
        Ok(AccountManager {
            ids_to_handlers: Mutex::new(BTreeMap::new()),
            context: Arc::new(AccountHandlerContext::new(
                &DEFAULT_AUTH_PROVIDER_CONFIG,
                account_dir_parent,
            )),
        })
    }

    /// Asynchronously handles the supplied stream of `AccountManagerRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            await!(self.handle_request(req))?;
        }
        Ok(())
    }

    /// Handles a single request to the AccountManager.
    pub async fn handle_request(&self, req: AccountManagerRequest) -> Result<(), fidl::Error> {
        match req {
            AccountManagerRequest::GetAccountIds { responder } => {
                responder.send(&mut await!(self.get_account_ids()).iter_mut())
            }
            AccountManagerRequest::GetAccountAuthStates { responder } => {
                let mut response = await!(self.get_account_auth_states());
                responder.send(response.0, &mut response.1.iter_mut())
            }
            AccountManagerRequest::GetAccount { id, auth_context_provider, account, responder } => {
                responder.send(await!(self.get_account(id.into(), auth_context_provider, account)))
            }
            AccountManagerRequest::RegisterAccountListener { listener, options, responder } => {
                responder.send(self.register_account_listener(listener, options))
            }
            AccountManagerRequest::RemoveAccount { id, responder } => {
                responder.send(await!(self.remove_account(id.into())))
            }
            AccountManagerRequest::ProvisionFromAuthProvider {
                auth_context_provider,
                auth_provider_type,
                responder,
            } => {
                let mut response =
                    self.provision_from_auth_provider(auth_context_provider, auth_provider_type);
                responder.send(response.0, response.1.as_mut().map(OutOfLine))
            }
            AccountManagerRequest::ProvisionNewAccount { responder } => {
                let mut response = await!(self.provision_new_account());
                responder.send(response.0, response.1.as_mut().map(OutOfLine))
            }
        }
    }

    /// Returns an `AccountHandlerConnection` for the specified `LocalAccountId`, either by
    /// returning the existing entry from the map or by creating and adding a new entry to the map.
    async fn get_handler_for_existing_account<'a>(
        &'a self,
        account_id: &'a LocalAccountId,
    ) -> Result<Arc<AccountHandlerConnection>, AccountManagerError> {
        let mut ids_to_handlers_lock = await!(self.ids_to_handlers.lock());
        match ids_to_handlers_lock.get(account_id) {
            None => return Err(AccountManagerError::new(Status::NotFound)),
            Some(Some(existing_handler)) => return Ok(Arc::clone(existing_handler)),
            Some(None) => { /* ID is valid but a handler doesn't exist yet */ }
        }

        let new_handler = Arc::new(await!(AccountHandlerConnection::load_account(
            account_id,
            Arc::clone(&self.context)
        ))?);
        ids_to_handlers_lock.insert(account_id.clone(), Some(Arc::clone(&new_handler)));
        Ok(new_handler)
    }

    async fn get_account_ids(&self) -> Vec<FidlLocalAccountId> {
        await!(self.ids_to_handlers.lock()).keys().map(|id| id.clone().into()).collect()
    }

    async fn get_account_auth_states(&self) -> (Status, Vec<AccountAuthState>) {
        // TODO(jsankey): Collect authentication state from AccountHandler instances rather than
        // returning a fixed value. This will involve opening account handler connections (in
        // parallel) for all of the accounts where encryption keys for the account's data partition
        // are available.
        let ids_to_handlers_lock = await!(self.ids_to_handlers.lock());
        (
            Status::Ok,
            ids_to_handlers_lock
                .keys()
                .map(|id| AccountAuthState {
                    account_id: id.clone().into(),
                    auth_state: DEFAULT_AUTH_STATE,
                })
                .collect(),
        )
    }

    async fn get_account(
        &self,
        id: LocalAccountId,
        auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        account: ServerEnd<AccountMarker>,
    ) -> Status {
        let account_handler = match await!(self.get_handler_for_existing_account(&id)) {
            Ok(account_handler) => account_handler,
            Err(err) => {
                warn!("Failure getting account handler connection: {:?}", err);
                return err.status;
            }
        };

        await!(account_handler.proxy().get_account(auth_context_provider, account)).unwrap_or_else(
            |err| {
                warn!("Failure calling get account: {:?}", err);
                Status::IoError
            },
        )
    }

    fn register_account_listener(
        &self,
        _listener: ClientEnd<AccountListenerMarker>,
        _options: AccountListenerOptions,
    ) -> Status {
        // TODO(jsankey): Implement this method
        Status::InternalError
    }

    async fn remove_account(&self, id: LocalAccountId) -> Status {
        // TODO(jsankey): Open an account handler if necessary and ask it to remove persistent
        // storage for the account.
        match await!(self.ids_to_handlers.lock()).remove(&id) {
            None => return Status::NotFound,
            Some(None) => info!("Removing account without open handler: {:?}", id),
            Some(Some(account_handler)) => {
                info!("Removing account and terminating its handler: {:?}", id);
                await!(account_handler.terminate());
            }
        }
        Status::Ok
    }

    async fn provision_new_account(&self) -> (Status, Option<FidlLocalAccountId>) {
        let (account_handler, account_id) =
            match await!(AccountHandlerConnection::create_account(Arc::clone(&self.context))) {
                Ok((connection, account_id)) => (Arc::new(connection), account_id),
                Err(err) => {
                    warn!("Failure creating account: {:?}", err);
                    return (err.status, None);
                }
            };

        info!("Adding new local account {:?}", account_id);

        // TODO(jsankey): Persist the change in installed accounts, ensuring this has succeeded
        // before adding to our in-memory state.

        let mut ids_to_handlers_lock = await!(self.ids_to_handlers.lock());
        if ids_to_handlers_lock.get(&account_id).is_some() {
            // IDs are 64 bit integers that are meant to be random. Its very unlikely we'll create
            // the same one twice but not impossible.
            // TODO(jsankey): Once account handler is handling persistent state it may be able to
            // detect this condition itself, if not it needs to be told to delete any state it has
            // created for this user we're not going to add.
            warn!("Duplicate ID creating new account");
            return (Status::UnknownError, None);
        }
        ids_to_handlers_lock.insert(account_id.clone(), Some(account_handler));
        (Status::Ok, Some(account_id.into()))
    }

    fn provision_from_auth_provider(
        &self,
        _auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        _auth_provider_type: String,
    ) -> (Status, Option<FidlLocalAccountId>) {
        // TODO(jsankey): Implement this method
        (Status::InternalError, None)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::RequestStream;
    use fidl_fuchsia_auth_account::{AccountManagerProxy, AccountManagerRequestStream};
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    const TEST_ACCOUNT_DIR: &str = "/data/test_account";

    fn request_stream_test<TestFn, Fut>(test_object: AccountManager, test_fn: TestFn)
    where
        TestFn: FnOnce(AccountManagerProxy) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let (server_chan, client_chan) = zx::Channel::create().expect("Failed to create channel");
        let proxy = AccountManagerProxy::new(fasync::Channel::from_channel(client_chan).unwrap());
        let request_stream = AccountManagerRequestStream::from_channel(
            fasync::Channel::from_channel(server_chan).unwrap(),
        );

        fasync::spawn(
            async move {
                await!(test_object.handle_requests_from_stream(request_stream))
                    .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err))
            },
        );

        executor.run_singlethreaded(test_fn(proxy)).expect("Executor run failed.")
    }

    fn create_test_object(existing_ids: Vec<u64>) -> AccountManager {
        AccountManager {
            ids_to_handlers: Mutex::new(
                existing_ids.into_iter().map(|id| (LocalAccountId::new(id), None)).collect(),
            ),
            context: Arc::new(AccountHandlerContext::new(&vec![], TEST_ACCOUNT_DIR)),
        }
    }

    fn fidl_local_id_vec(ints: Vec<u64>) -> Vec<FidlLocalAccountId> {
        ints.into_iter().map(|i| FidlLocalAccountId { id: i }).collect()
    }

    /// Note: Many AccountManager methods launch instances of an AccountHandler. Since its
    /// currently not convenient to mock out this component launching in Rust, we rely on the
    /// hermetic component test to provide coverage for these areas and only cover the in-process
    /// behavior with this unit-test.

    #[test]
    fn test_initially_empty() {
        request_stream_test(AccountManager::new(TEST_ACCOUNT_DIR).unwrap(), async move |proxy| {
            assert_eq!(await!(proxy.get_account_ids())?, vec![]);
            assert_eq!(await!(proxy.get_account_auth_states())?, (Status::Ok, vec![]));
            Ok(())
        });
    }

    #[test]
    fn test_remove_missing_account() {
        request_stream_test(
            // Manually create an account manager with one account.
            create_test_object(vec![1]),
            async move |proxy| {
                // Try to delete a very different account from the one we added.
                assert_eq!(
                    await!(proxy.remove_account(LocalAccountId::new(42).as_mut()))?,
                    Status::NotFound
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_remove_present_account() {
        request_stream_test(
            // Manually create an account manager with two accounts.
            create_test_object(vec![1, 2]),
            async move |proxy| {
                // Try to remove the first one.
                assert_eq!(
                    await!(proxy.remove_account(LocalAccountId::new(1).as_mut()))?,
                    Status::Ok
                );

                // Verify the second account is still present.
                assert_eq!(await!(proxy.get_account_ids())?, fidl_local_id_vec(vec![2]));
                Ok(())
            },
        );
    }

}
