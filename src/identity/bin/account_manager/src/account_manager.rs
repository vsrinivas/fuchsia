// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        account_event_emitter::{
            AccountEvent, AccountEventEmitter, Options as AccountEventEmitterOptions,
        },
        account_handler_connection::AccountHandlerConnection,
        account_map::AccountMap,
        stored_account_list::AccountMetadata,
    },
    account_common::{AccountId, AccountManagerError, FidlAccountId},
    anyhow::Error,
    fidl_fuchsia_identity_account::{
        AccountManagerGetAccountRequest, AccountManagerProvisionNewAccountRequest,
        AccountManagerRegisterAccountListenerRequest, AccountManagerRequest,
        AccountManagerRequestStream, AccountMetadata as FidlAccountMetadata, Error as ApiError,
        Lifetime,
    },
    fidl_fuchsia_identity_internal::{
        AccountHandlerControlCreateAccountRequest, AccountHandlerControlUnlockAccountRequest,
    },
    fuchsia_inspect::Inspector,
    futures::{lock::Mutex, prelude::*},
    std::{convert::TryFrom, path::PathBuf, sync::Arc},
    tracing::{info, warn},
};

/// The core component of the account system for Fuchsia.
///
/// The AccountManager maintains the set of Fuchsia accounts that are provisioned on the device and
/// launches and delegates to AccountHandler component instances to determine the detailed state and
/// authentication for each account.
///
/// `AHC` An AccountHandlerConnection used to spawn new AccountHandler components.
pub struct AccountManager<AHC: AccountHandlerConnection> {
    /// The account map maintains the state of all accounts as well as connections to their account
    /// handlers.
    account_map: Mutex<AccountMap<AHC>>,

    /// Contains the client ends of all AccountListeners which are subscribed to account events.
    event_emitter: AccountEventEmitter,
}

impl<AHC: AccountHandlerConnection> AccountManager<AHC> {
    /// Constructs a new AccountManager and loads an existing set of accounts from `data_dir`.
    pub fn new(data_dir: PathBuf, inspector: &Inspector) -> Result<AccountManager<AHC>, Error> {
        let account_map = AccountMap::load(data_dir, inspector.root())?;

        // Initialize the structs used to output state through the inspect system.
        let event_emitter = AccountEventEmitter::new(inspector.root());

        Ok(Self { account_map: Mutex::new(account_map), event_emitter })
    }

    /// Asynchronously handles the supplied stream of `AccountManagerRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    /// Handles a single request to the AccountManager.
    pub async fn handle_request(&self, req: AccountManagerRequest) -> Result<(), fidl::Error> {
        match req {
            AccountManagerRequest::GetAccountIds { responder } => {
                let response = self.get_account_ids().await;
                responder.send(&response)?;
            }
            AccountManagerRequest::GetAccountMetadata { id, responder } => {
                let mut response = self.get_account_metadata(id).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::GetAccount { payload, responder } => {
                let mut response = self.get_account(payload).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::DeprecatedGetAccount {
                id: _,
                password: _,
                account: _,
                responder: _,
            } => {
                unimplemented!();
            }
            AccountManagerRequest::RegisterAccountListener { payload, responder } => {
                let mut response = self.register_account_listener(payload).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::RemoveAccount { id, responder } => {
                let mut response = self.remove_account(id.into()).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::ProvisionNewAccount { payload, responder } => {
                let mut response = self.provision_new_account(payload).await;
                responder.send(&mut response)?;
            }
            AccountManagerRequest::DeprecatedProvisionNewAccount {
                password: _,
                metadata: _,
                account: _,
                responder: _,
            } => {
                unimplemented!();
            }
            AccountManagerRequest::GetAuthenticationMechanisms { responder } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
        }
        Ok(())
    }

    async fn get_account_ids(&self) -> Vec<FidlAccountId> {
        self.account_map.lock().await.get_account_ids().iter().map(|id| id.clone().into()).collect()
    }

    async fn get_account_metadata(
        &self,
        id: FidlAccountId,
    ) -> Result<FidlAccountMetadata, ApiError> {
        let account_id = id.into();
        self.account_map
            .lock()
            .await
            .get_metadata(&account_id)
            .map_err(|err| {
                warn!("Failure getting account metadata: {:?}", err);
                err.api_error
            })
            .map(|metadata| metadata.into())
    }

    async fn get_account(
        &self,
        AccountManagerGetAccountRequest {
            id,
            interaction,
            account,
            ..
        }: AccountManagerGetAccountRequest,
    ) -> Result<(), ApiError> {
        let id = id.ok_or(ApiError::InvalidRequest)?.into();
        let account = account.ok_or(ApiError::InvalidRequest)?;

        let mut account_map = self.account_map.lock().await;
        let account_handler = account_map.get_handler(&id).await.map_err(|err| {
            warn!("Failure getting account handler connection: {:?}", err);
            err.api_error
        })?;
        let pre_auth_state_opt = account_handler
            .proxy()
            .unlock_account(AccountHandlerControlUnlockAccountRequest {
                interaction,
                ..AccountHandlerControlUnlockAccountRequest::EMPTY
            })
            .await
            .map_err(|_| ApiError::Resource)??;

        // TODO(fxb/105818): Finalize how to handle the case when we fail this operation.
        if let Some(pre_auth_state) = pre_auth_state_opt {
            account_map.update_account(&id, pre_auth_state).await.map_err(|err| {
                warn!("Failure updating account pre_auth_state: {:?}", err);
                err.api_error
            })?;
        }

        account_handler.proxy().get_account(account).await.map_err(|err| {
            warn!("Failure calling get account: {:?}", err);
            ApiError::Resource
        })?
    }

    async fn register_account_listener(
        &self,
        mut payload: AccountManagerRegisterAccountListenerRequest,
    ) -> Result<(), ApiError> {
        let listener = payload.listener.take().ok_or(ApiError::InvalidRequest)?;
        let options = AccountEventEmitterOptions::try_from(payload)?;

        let account_ids = self.account_map.lock().await.get_account_ids();
        let proxy = listener.into_proxy().map_err(|err| {
            warn!("Could not convert AccountListener client end to proxy {:?}", err);
            ApiError::InvalidRequest
        })?;
        self.event_emitter.add_listener(proxy, options, &account_ids).await.map_err(|err| {
            warn!("Could not instantiate AccountListener client {:?}", err);
            ApiError::Unknown
        })?;
        info!("AccountListener established");
        Ok(())
    }

    async fn remove_account(&self, account_id: AccountId) -> Result<(), ApiError> {
        let mut account_map = self.account_map.lock().await;
        let account_handler = account_map.get_handler(&account_id).await.map_err(|err| {
            warn!("Could not get account handler for account removal {:?}", err);
            err.api_error
        })?;
        // TODO(fxbug.dev/43491): Don't unlock accounts before removing them.
        account_handler
            .proxy()
            .unlock_account(AccountHandlerControlUnlockAccountRequest::EMPTY)
            .await
            .map_err(|_| ApiError::Resource)??;
        account_handler.proxy().remove_account().await.map_err(|_| ApiError::Resource)??;
        account_handler.terminate().await;
        // Emphemeral accounts were never included in the StoredAccountList and so it does not need
        // to be modified when they are removed.
        // TODO(fxbug.dev/39455): Handle irrecoverable, corrupt state.
        account_map.remove_account(&account_id).await.map_err(|err| {
            warn!("Could not remove account: {:?}", err);
            // TODO(fxbug.dev/39829): Improve error mapping.
            if err.api_error == ApiError::NotFound {
                // We already checked for existence, so NotFound is unexpected
                ApiError::Internal
            } else {
                err.api_error
            }
        })?;
        let event = AccountEvent::AccountRemoved(account_id.clone());
        self.event_emitter.publish(&event).await;
        Ok(())
    }

    /// Creates a new account handler connection, then creates an account within it,
    /// and finally returns the connection.
    async fn create_account_internal(
        &self,
        lifetime: Lifetime,
        auth_mechanism_id: Option<String>,
    ) -> Result<(Arc<AHC>, Vec<u8>), ApiError> {
        let account_handler =
            self.account_map.lock().await.new_handler(lifetime).await.map_err(|err| {
                warn!("Could not initialize account handler: {:?}", err);
                err.api_error
            })?;
        let account_id = Some(account_handler.get_account_id().clone().into());
        let pre_auth_state = account_handler
            .proxy()
            .create_account(AccountHandlerControlCreateAccountRequest {
                id: account_id,
                auth_mechanism_id,
                ..AccountHandlerControlCreateAccountRequest::EMPTY
            })
            .await
            .map_err(|err| {
                warn!("Could not create account: {:?}", err);
                ApiError::Resource
            })??;
        Ok((account_handler, pre_auth_state))
    }

    async fn provision_new_account(
        &self,
        AccountManagerProvisionNewAccountRequest {
            lifetime, auth_mechanism_id, mut metadata, ..
        }: AccountManagerProvisionNewAccountRequest,
    ) -> Result<FidlAccountId, ApiError> {
        let account_metadata = metadata
            .take()
            .ok_or_else(|| {
                warn!("No metadata found");
                ApiError::InvalidRequest
            })?
            .try_into()?;
        let (account_handler, pre_auth_state) = self
            .create_account_internal(lifetime.ok_or(ApiError::InvalidRequest)?, auth_mechanism_id)
            .await?;
        let account_id = account_handler.get_account_id();

        // Persist the account both in memory and on disk
        if let Err(err) =
            self.add_account(account_handler.clone(), pre_auth_state, account_metadata).await
        {
            warn!("Failure adding account: {:?}", err);
            account_handler.terminate().await;
            Err(err.api_error)
        } else {
            info!(?account_id, "Adding new local account");
            Ok(account_id.clone().into())
        }
    }

    // Add the account to the AccountManager, including persistent state.
    async fn add_account(
        &self,
        account_handler: Arc<AHC>,
        pre_auth_state: Vec<u8>,
        metadata: AccountMetadata,
    ) -> Result<(), AccountManagerError> {
        let mut account_map = self.account_map.lock().await;

        account_map
            .add_account(Arc::clone(&account_handler), pre_auth_state, metadata)
            .await
            .map_err(|err| {
                warn!("Could not add account: {:?}", err);
                // TODO(fxbug.dev/39829): Improve error mapping.
                if err.api_error == ApiError::FailedPrecondition {
                    ApiError::Internal
                } else {
                    err.api_error
                }
            })?;
        let event = AccountEvent::AccountAdded(account_handler.get_account_id().clone());
        self.event_emitter.publish(&event).await;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::account_event_emitter::MINIMUM_AUTH_STATE;
    use crate::account_handler_connection::AccountHandlerConnectionImpl;
    use crate::stored_account_list::{StoredAccount, StoredAccountList};
    use fidl::endpoints::{create_request_stream, RequestStream};
    use fidl_fuchsia_identity_account::{
        AccountAuthState, AccountListenerMarker, AccountListenerRequest, AccountManagerProxy,
        AccountManagerRequestStream, AuthChangeGranularity,
    };
    use fuchsia_async as fasync;
    use fuchsia_inspect::{assert_data_tree, Inspector};
    use fuchsia_zircon as zx;
    use futures::future::join;
    use lazy_static::lazy_static;
    use std::path::Path;
    use tempfile::TempDir;

    type TestAccountManager = AccountManager<AccountHandlerConnectionImpl>;

    lazy_static! {
        static ref TEST_GRANULARITY: AuthChangeGranularity =
            AuthChangeGranularity { summary_changes: Some(true), ..AuthChangeGranularity::EMPTY };
        static ref ACCOUNT_PRE_AUTH_STATE: Vec<u8> = vec![1, 2, 3];
        static ref ACCOUNT_METADATA: AccountMetadata = AccountMetadata::new("test".to_string());
    }

    fn request_stream_test<TestFn, Fut>(account_manager: TestAccountManager, test_fn: TestFn)
    where
        TestFn: FnOnce(AccountManagerProxy, Arc<TestAccountManager>) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::LocalExecutor::new().expect("Failed to create executor");
        let (server_chan, client_chan) = zx::Channel::create().expect("Failed to create channel");
        let proxy = AccountManagerProxy::new(fasync::Channel::from_channel(client_chan).unwrap());
        let request_stream = AccountManagerRequestStream::from_channel(
            fasync::Channel::from_channel(server_chan).unwrap(),
        );

        let account_manager_arc = Arc::new(account_manager);
        let account_manager_clone = Arc::clone(&account_manager_arc);
        // TODO(fxbug.dev/39745): Migrate off of fuchsia_async::spawn.
        fasync::Task::spawn(async move {
            account_manager_clone
                .handle_requests_from_stream(request_stream)
                .await
                .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err))
        })
        .detach();

        executor
            .run_singlethreaded(test_fn(proxy, account_manager_arc))
            .expect("LocalExecutor run failed.")
    }

    // Construct an account manager initialized with the supplied set of accounts.
    fn create_accounts(
        existing_ids: Vec<u64>,
        data_dir: &Path,
        inspector: &Inspector,
    ) -> TestAccountManager {
        let stored_account_list = existing_ids
            .iter()
            .map(|&id| {
                StoredAccount::new(
                    AccountId::new(id),
                    ACCOUNT_PRE_AUTH_STATE.to_vec(),
                    ACCOUNT_METADATA.clone(),
                )
            })
            .collect();
        StoredAccountList::new(data_dir, stored_account_list)
            .save()
            .expect("Couldn't write account list");

        read_accounts(data_dir, inspector)
    }

    // Contructs an account manager that reads its accounts from the supplied directory.
    fn read_accounts(
        data_dir: &Path,
        inspector: &Inspector,
    ) -> AccountManager<AccountHandlerConnectionImpl> {
        AccountManager::new(data_dir.to_path_buf(), inspector).unwrap()
    }

    /// Note: Many AccountManager methods launch instances of an AccountHandler. Since its
    /// currently not convenient to mock out this component launching in Rust, we rely on the
    /// hermetic component test to provide coverage for these areas and only cover the in-process
    /// behavior with this unit-test.

    #[test]
    fn test_new() {
        let inspector = Inspector::new();
        let data_dir = TempDir::new().unwrap();
        request_stream_test(
            AccountManager::new(data_dir.path().into(), &inspector).unwrap(),
            |proxy, _| async move {
                assert_eq!(proxy.get_account_ids().await?.len(), 0);
                Ok(())
            },
        );
    }

    #[test]
    fn test_initially_empty() {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        request_stream_test(
            create_accounts(vec![], data_dir.path(), &inspector),
            |proxy, _test_object| async move {
                assert_eq!(proxy.get_account_ids().await?.len(), 0);
                assert_data_tree!(inspector, root: contains {
                    accounts: {
                        active: 0 as u64,
                        total: 0 as u64,
                    },
                    listeners: {
                        active: 0 as u64,
                        events: 0 as u64,
                        total_opened: 0 as u64,
                    },
                });
                Ok(())
            },
        );
    }

    #[test]
    fn test_remove_missing_account() {
        // Manually create an account manager with one account.
        let data_dir = TempDir::new().unwrap();
        let stored_account_list = StoredAccountList::new(
            data_dir.path(),
            vec![StoredAccount::new(
                AccountId::new(1),
                vec![],
                AccountMetadata::new("test".to_string()),
            )],
        );
        stored_account_list.save().unwrap();
        let inspector = Inspector::new();
        request_stream_test(read_accounts(data_dir.path(), &inspector), |proxy, _test_object| {
            async move {
                // Try to delete a very different account from the one we added.
                assert_eq!(
                    proxy.remove_account(AccountId::new(42).into()).await?,
                    Err(ApiError::NotFound)
                );
                assert_data_tree!(inspector, root: contains {
                    accounts: {
                        total: 1 as u64,
                        active: 0 as u64,
                    },
                });
                Ok(())
            }
        });
    }

    /// Sets up an AccountListener with an init event.
    #[test]
    fn test_account_listener() {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        request_stream_test(
            create_accounts(vec![1, 2], data_dir.path(), &inspector),
            |proxy, _| {
                async move {
                    let (client_end, mut stream) =
                        create_request_stream::<AccountListenerMarker>().unwrap();
                    let serve_fut = async move {
                        let request = stream.try_next().await.expect("stream error");
                        if let Some(AccountListenerRequest::OnInitialize {
                            account_states,
                            responder,
                        }) = request
                        {
                            assert_eq!(
                                account_states,
                                vec![
                                    AccountAuthState {
                                        account_id: 1,
                                        auth_state: MINIMUM_AUTH_STATE
                                    },
                                    AccountAuthState {
                                        account_id: 2,
                                        auth_state: MINIMUM_AUTH_STATE
                                    },
                                ]
                            );
                            responder.send().unwrap();
                        } else {
                            panic!("Unexpected message received");
                        };
                        if let Some(_) = stream.try_next().await.expect("stream error") {
                            panic!("Unexpected message, channel should be closed");
                        }
                    };
                    let request_fut = async move {
                        // The registering itself triggers the init event.
                        assert_eq!(
                            proxy
                                .register_account_listener(
                                    AccountManagerRegisterAccountListenerRequest {
                                        listener: Some(client_end),
                                        initial_state: Some(true),
                                        add_account: Some(true),
                                        remove_account: Some(true),
                                        ..AccountManagerRegisterAccountListenerRequest::EMPTY
                                    }
                                )
                                .await
                                .unwrap(),
                            Ok(())
                        );
                    };
                    join(request_fut, serve_fut).await;
                    Ok(())
                }
            },
        );
    }

    /// Registers an account listener with invalid request arguments.
    #[test]
    fn test_account_listener_invalid_requests() {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        request_stream_test(
            create_accounts(vec![1, 2], data_dir.path(), &inspector),
            |proxy, _| async move {
                let (client_end, _) = create_request_stream::<AccountListenerMarker>().unwrap();
                assert_eq!(
                    proxy
                        .register_account_listener(AccountManagerRegisterAccountListenerRequest {
                            listener: Some(client_end),
                            initial_state: Some(true),
                            add_account: Some(true),
                            remove_account: Some(true),
                            granularity: Some(TEST_GRANULARITY.clone()),
                            ..AccountManagerRegisterAccountListenerRequest::EMPTY
                        })
                        .await?,
                    Err(ApiError::InvalidRequest)
                );
                Ok(())
            },
        );
    }
}
