// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate tempfile;

use account_common::{
    AccountAuthState, AccountManagerError, FidlAccountAuthState, FidlLocalAccountId, LocalAccountId,
};
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::AuthProviderConfig;
use fidl_fuchsia_auth::{AuthState, AuthStateSummary, AuthenticationContextProviderMarker};
use fidl_fuchsia_auth_account::{
    AccountListenerMarker, AccountListenerOptions, AccountManagerRequest,
    AccountManagerRequestStream, AccountMarker, Status,
};
use futures::lock::Mutex;
use futures::prelude::*;
use log::{info, warn};
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::sync::Arc;

use crate::account_event_emitter::{AccountEvent, AccountEventEmitter};
use crate::account_handler_connection::AccountHandlerConnection;
use crate::account_handler_context::AccountHandlerContext;
use crate::stored_account_list::{StoredAccountList, StoredAccountMetadata};

/// (Temporary) A fixed AuthState that is used for all accounts until authenticators are
/// available.
const DEFAULT_AUTH_STATE: AuthState = AuthState { summary: AuthStateSummary::Unknown };

type AccountMap = BTreeMap<LocalAccountId, Option<Arc<AccountHandlerConnection>>>;

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
    ids_to_handlers: Mutex<AccountMap>,

    /// An object to service requests for contextual information from AccountHandlers.
    context: Arc<AccountHandlerContext>,

    /// Contains the client ends of all AccountListeners which are subscribed to account events.
    event_emitter: AccountEventEmitter,

    /// Root directory containing persistent resources for an AccountManager instance.
    data_dir: PathBuf,
}

impl AccountManager {
    /// Constructs a new AccountManager, loading existing set of accounts from `data_dir`, and an
    /// auth provider configuration. The directory must exist at construction.
    pub fn new(
        data_dir: PathBuf,
        auth_provider_config: &[AuthProviderConfig],
    ) -> Result<AccountManager, Error> {
        let context = Arc::new(AccountHandlerContext::new(auth_provider_config));
        let mut ids_to_handlers = AccountMap::new();
        let account_list = StoredAccountList::load(&data_dir)?;
        for account in account_list.accounts().into_iter() {
            ids_to_handlers.insert(account.account_id().clone(), None);
        }
        Ok(Self {
            ids_to_handlers: Mutex::new(ids_to_handlers),
            context,
            event_emitter: AccountEventEmitter::new(),
            data_dir,
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
                responder.send(await!(self.register_account_listener(listener, options)))
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

    async fn get_account_auth_states(&self) -> (Status, Vec<FidlAccountAuthState>) {
        // TODO(jsankey): Collect authentication state from AccountHandler instances rather than
        // returning a fixed value. This will involve opening account handler connections (in
        // parallel) for all of the accounts where encryption keys for the account's data partition
        // are available.
        let ids_to_handlers_lock = await!(self.ids_to_handlers.lock());
        (
            Status::Ok,
            ids_to_handlers_lock
                .keys()
                .map(|id| FidlAccountAuthState {
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

    async fn register_account_listener(
        &self,
        listener: ClientEnd<AccountListenerMarker>,
        options: AccountListenerOptions,
    ) -> Status {
        let ids_to_handlers_lock = await!(self.ids_to_handlers.lock());
        let account_auth_states: Vec<AccountAuthState> = ids_to_handlers_lock
            .keys()
            // TODO(dnordstrom): Get the real auth states
            .map(|id| AccountAuthState { account_id: id.clone() })
            .collect();
        std::mem::drop(ids_to_handlers_lock);
        let proxy = match listener.into_proxy() {
            Ok(proxy) => proxy,
            Err(err) => {
                warn!("Could not convert AccountListener client end to proxy {:?}", err);
                return Status::InvalidRequest;
            }
        };
        match await!(self.event_emitter.add_listener(proxy, options, &account_auth_states)) {
            Ok(()) => Status::Ok,
            Err(err) => {
                warn!("Could not instantiate AccountListener client {:?}", err);
                Status::UnknownError
            }
        }
    }

    async fn remove_account(&self, id: LocalAccountId) -> Status {
        // TODO(jsankey): Open an account handler if necessary and ask it to remove persistent
        // storage for the account.
        let mut ids_to_handlers = await!(self.ids_to_handlers.lock());
        match ids_to_handlers.get(&id) {
            None => return Status::NotFound,
            Some(None) => info!("Removing account without open handler: {:?}", id),
            Some(Some(account_handler)) => {
                info!("Removing account and terminating its handler: {:?}", id);
                await!(account_handler.terminate());
            }
        };
        let account_ids = ids_to_handlers
            .keys()
            .filter(|&x| x != &id)
            .map(|id| StoredAccountMetadata::new(id.clone()))
            .collect();
        if let Err(err) = StoredAccountList::new(account_ids).save(&self.data_dir) {
            return err.status;
        }
        let event = AccountEvent::AccountRemoved(id.clone());
        await!(self.event_emitter.publish(&event));
        ids_to_handlers.remove(&id);
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
        let mut account_ids: Vec<StoredAccountMetadata> =
            ids_to_handlers_lock.keys().map(|id| StoredAccountMetadata::new(id.clone())).collect();
        account_ids.push(StoredAccountMetadata::new(account_id.clone()));
        if let Err(err) = StoredAccountList::new(account_ids).save(&self.data_dir) {
            // TODO(dnordstrom): When AccountHandler uses persistent storage, clean up its state.
            return (err.status, None);
        }
        ids_to_handlers_lock.insert(account_id.clone(), Some(account_handler));
        let event = AccountEvent::AccountAdded(account_id.clone());
        await!(self.event_emitter.publish(&event));
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
    use crate::stored_account_list::{StoredAccountList, StoredAccountMetadata};
    use fidl::endpoints::{create_request_stream, RequestStream};
    use fidl_fuchsia_auth::AuthChangeGranularity;
    use fidl_fuchsia_auth_account::{
        AccountListenerRequest, AccountManagerProxy, AccountManagerRequestStream,
    };
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use lazy_static::lazy_static;
    use std::path::Path;
    use tempfile::TempDir;

    lazy_static! {
        /// Configuration for a set of fake auth providers used for testing.
        /// This can be populated later if needed.
        static ref AUTH_PROVIDER_CONFIG: Vec<AuthProviderConfig> = {vec![]};
    }

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

    fn create_test_object(existing_ids: Vec<u64>, data_dir: &Path) -> AccountManager {
        let stored_account_list = existing_ids
            .iter()
            .map(|&id| StoredAccountMetadata::new(LocalAccountId::new(id)))
            .collect();
        StoredAccountList::new(stored_account_list)
            .save(data_dir)
            .expect("Couldn't write account list");
        AccountManager {
            ids_to_handlers: Mutex::new(
                existing_ids.into_iter().map(|id| (LocalAccountId::new(id), None)).collect(),
            ),
            context: Arc::new(AccountHandlerContext::new(&vec![])),
            event_emitter: AccountEventEmitter::new(),
            data_dir: data_dir.to_path_buf(),
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
        let data_dir = TempDir::new().unwrap();
        request_stream_test(
            AccountManager::new(data_dir.path().into(), &AUTH_PROVIDER_CONFIG).unwrap(),
            async move |proxy| {
                assert_eq!(await!(proxy.get_account_ids())?, vec![]);
                assert_eq!(await!(proxy.get_account_auth_states())?, (Status::Ok, vec![]));
                Ok(())
            },
        );
    }

    #[test]
    fn test_remove_missing_account() {
        // Manually create an account manager with one account.
        let data_dir = TempDir::new().unwrap();
        let test_object = create_test_object(vec![1], data_dir.path());
        request_stream_test(test_object, async move |proxy| {
            // Try to delete a very different account from the one we added.
            assert_eq!(
                await!(proxy.remove_account(LocalAccountId::new(42).as_mut()))?,
                Status::NotFound
            );
            Ok(())
        });
    }

    #[test]
    fn test_remove_present_account() {
        let data_dir = TempDir::new().unwrap();
        let stored_account_list = StoredAccountList::new(vec![
            StoredAccountMetadata::new(LocalAccountId::new(1)),
            StoredAccountMetadata::new(LocalAccountId::new(2)),
        ]);
        stored_account_list.save(data_dir.path()).unwrap();
        // Manually create an account manager from an account_list_dir with two pre-populated dirs.
        let account_manager =
            AccountManager::new(data_dir.path().to_owned(), &AUTH_PROVIDER_CONFIG).unwrap();

        request_stream_test(account_manager, async move |proxy| {
            // Try to remove the first account.
            assert_eq!(await!(proxy.remove_account(LocalAccountId::new(1).as_mut()))?, Status::Ok);

            // Verify that the second account is present.
            assert_eq!(await!(proxy.get_account_ids())?, fidl_local_id_vec(vec![2]));
            Ok(())
        });
        // Now create another account manager using the same directory, which should pick up the new
        // state from the operations of the first account manager.
        let account_manager =
            AccountManager::new(data_dir.path().to_owned(), &AUTH_PROVIDER_CONFIG).unwrap();
        request_stream_test(account_manager, async move |proxy| {
            // Verify the only the second account is present.
            assert_eq!(await!(proxy.get_account_ids())?, fidl_local_id_vec(vec![2]));
            Ok(())
        });
    }

    /// Sets up an AccountListener which receives two events, init and remove.
    #[test]
    fn test_account_listener() {
        let mut options = AccountListenerOptions {
            initial_state: true,
            add_account: true,
            remove_account: true,
            granularity: AuthChangeGranularity { summary_changes: false },
        };

        let data_dir = TempDir::new().unwrap();
        let test_object = create_test_object(vec![1, 2], data_dir.path());
        // TODO(dnordstrom): Use run_until_stalled macro instead.
        request_stream_test(test_object, async move |proxy| {
            let (client_end, mut stream) =
                create_request_stream::<AccountListenerMarker>().unwrap();
            let serve_fut = async move {
                let request = await!(stream.try_next()).expect("stream error");
                if let Some(AccountListenerRequest::OnInitialize {
                    account_auth_states,
                    responder,
                }) = request
                {
                    assert_eq!(
                        account_auth_states,
                        vec![
                            FidlAccountAuthState::from(&AccountAuthState {
                                account_id: LocalAccountId::new(1)
                            }),
                            FidlAccountAuthState::from(&AccountAuthState {
                                account_id: LocalAccountId::new(2)
                            }),
                        ]
                    );
                    responder.send().unwrap();
                } else {
                    panic!("Unexpected message received");
                };
                let request = await!(stream.try_next()).expect("stream error");
                if let Some(AccountListenerRequest::OnAccountRemoved { id, responder }) = request {
                    assert_eq!(LocalAccountId::from(id), LocalAccountId::new(1));
                    responder.send().unwrap();
                } else {
                    panic!("Unexpected message received");
                };
                if let Some(_) = await!(stream.try_next()).expect("stream error") {
                    panic!("Unexpected message, channel should be closed");
                }
            };
            let request_fut = async move {
                // The registering itself triggers the init event.
                assert_eq!(
                    await!(proxy.register_account_listener(client_end, &mut options)).unwrap(),
                    Status::Ok
                );

                // Non-existing account removal shouldn't trigger any event.
                assert_eq!(
                    await!(proxy.remove_account(LocalAccountId::new(42).as_mut())).unwrap(),
                    Status::NotFound
                );
                // Removal of existing account triggers the remove event.
                assert_eq!(
                    await!(proxy.remove_account(LocalAccountId::new(1).as_mut())).unwrap(),
                    Status::Ok
                );
            };
            await!(request_fut.join(serve_fut));
            Ok(())
        });
    }

}
