// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account::{Account, AccountContext};
use crate::common::AccountLifetime;
use crate::inspect;
use account_common::{AccountManagerError, LocalAccountId, ResultExt};
use failure::{format_err, ResultExt as _};
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::AuthenticationContextProviderMarker;
use fidl_fuchsia_identity_account::{AccountMarker, Error as ApiError};
use fidl_fuchsia_identity_internal::{
    AccountHandlerContextMarker, AccountHandlerContextProxy, AccountHandlerControlRequest,
    AccountHandlerControlRequestStream, HASH_SALT_SIZE, HASH_SIZE,
};
use fuchsia_inspect::{Inspector, Node, Property};
use futures::prelude::*;
use identity_common::TaskGroupError;
use log::{error, info, warn};
use mundane::hash::{Digest, Hasher, Sha256};
use parking_lot::RwLock;
use std::sync::Arc;

/// The states of an AccountHandler.
enum Lifecycle {
    /// An account has not yet been created or loaded.
    Uninitialized,

    /// An account is currently loaded and is available.
    Initialized { account: Arc<Account> },

    /// There is no account present, and initialization is not possible.
    Finished,
}

type GlobalIdHash = [u8; HASH_SIZE as usize];
type GlobalIdHashSalt = [u8; HASH_SALT_SIZE as usize];

/// The core state of the AccountHandler, i.e. the Account (once it is known) and references to
/// the execution context and a TokenManager.
pub struct AccountHandler {
    // An optional `Account` that we are handling.
    //
    // This will be Uninitialized until a particular Account is established over the control
    // channel. Then it will be initialized. When the AccountHandler is terminated, or its Account
    // is removed, it reaches its final state, Finished.
    account: RwLock<Lifecycle>,

    /// Lifetime for this account (ephemeral or persistent with a path).
    lifetime: AccountLifetime,

    /// Helper for outputting account handler information via fuchsia_inspect.
    inspect: inspect::AccountHandler,
    // TODO(jsankey): Add TokenManager and AccountHandlerContext.
}

impl AccountHandler {
    /// Constructs a new AccountHandler.
    pub fn new(lifetime: AccountLifetime, inspector: &Inspector) -> AccountHandler {
        let inspect = inspect::AccountHandler::new(inspector.root(), "uninitialized");
        Self { account: RwLock::new(Lifecycle::Uninitialized), lifetime, inspect }
    }

    /// Asynchronously handles the supplied stream of `AccountHandlerControlRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountHandlerControlRequestStream,
    ) -> Result<(), failure::Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    /// Dispatches an `AccountHandlerControlRequest` message to the appropriate handler method
    /// based on its type.
    pub async fn handle_request(
        &self,
        req: AccountHandlerControlRequest,
    ) -> Result<(), fidl::Error> {
        match req {
            AccountHandlerControlRequest::CreateAccount { context, id, responder } => {
                let mut response = self.create_account(id.into(), context).await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::LoadAccount { context, id, responder } => {
                let mut response = self.load_account(id.into(), context).await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::PrepareForAccountTransfer { responder } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
            AccountHandlerControlRequest::PerformAccountTransfer { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
            AccountHandlerControlRequest::FinalizeAccountTransfer { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
            AccountHandlerControlRequest::EncryptAccountData { responder, .. } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
            AccountHandlerControlRequest::RemoveAccount { force, responder } => {
                let mut response = self.remove_account(force).await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::GetAccount {
                auth_context_provider,
                account,
                responder,
            } => {
                let mut response = self.get_account(auth_context_provider, account).await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::GetPublicKey { responder } => {
                responder.send(&mut Err(ApiError::UnsupportedOperation))?;
            }
            AccountHandlerControlRequest::GetGlobalIdHash { salt, responder } => {
                let mut response = self.get_global_id_hash(salt);
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::Terminate { control_handle } => {
                self.terminate().await;
                control_handle.shutdown();
            }
        }
        Ok(())
    }

    /// Helper method which constructs a new account using the supplied function and stores it in
    /// self.account.
    async fn init_account<'a, F, Fut>(
        &'a self,
        construct_account_fn: F,
        id: LocalAccountId,
        context: ClientEnd<AccountHandlerContextMarker>,
    ) -> Result<(), AccountManagerError>
    where
        F: FnOnce(LocalAccountId, AccountLifetime, AccountHandlerContextProxy, &'a Node) -> Fut,
        Fut: Future<Output = Result<Account, AccountManagerError>>,
    {
        let context_proxy = context
            .into_proxy()
            .context("Invalid AccountHandlerContext given")
            .account_manager_error(ApiError::InvalidRequest)?;

        // The function evaluation is front loaded because await is not allowed while holding the
        // lock.
        let account_result =
            construct_account_fn(id, self.lifetime.clone(), context_proxy, self.inspect.get_node())
                .await;
        let mut account_lock = self.account.write();
        match *account_lock {
            Lifecycle::Uninitialized => {
                *account_lock = Lifecycle::Initialized { account: Arc::new(account_result?) };
                self.inspect.lifecycle.set("initialized");
                Ok(())
            }
            _ => Err(AccountManagerError::new(ApiError::Internal)
                .with_cause(format_err!("AccountHandler is already initialized"))),
        }
    }

    /// Creates a new Fuchsia account and attaches it to this handler.
    async fn create_account(
        &self,
        id: LocalAccountId,
        context: ClientEnd<AccountHandlerContextMarker>,
    ) -> Result<(), ApiError> {
        self.init_account(Account::create, id, context).await.map_err(|err| {
            warn!("Failed creating Fuchsia account: {:?}", err);
            err.into()
        })
    }

    /// Loads an existing Fuchsia account and attaches it to this handler.
    async fn load_account(
        &self,
        id: LocalAccountId,
        context: ClientEnd<AccountHandlerContextMarker>,
    ) -> Result<(), ApiError> {
        self.init_account(Account::load, id, context).await.map_err(|err| {
            warn!("Failed loading Fuchsia account: {:?}", err);
            err.into()
        })
    }

    /// Remove the active account. This method should not be retried on failure.
    // TODO(AUTH-212): Implement graceful account removal.
    async fn remove_account(&self, force: bool) -> Result<(), ApiError> {
        if force == false {
            warn!("Graceful (non-force) account removal not yet implemented.");
            return Err(ApiError::Internal);
        }
        let old_lifecycle = {
            let mut account_lock = self.account.write();
            std::mem::replace(&mut *account_lock, Lifecycle::Finished)
        };
        self.inspect.lifecycle.set("finished");
        let account_arc = match old_lifecycle {
            Lifecycle::Initialized { account } => account,
            _ => {
                warn!("No account is initialized");
                return Err(ApiError::InvalidRequest);
            }
        };
        // TODO(AUTH-212): After this point, error recovery might include putting the account back
        // in the lock.
        if let Err(TaskGroupError::AlreadyCancelled) = account_arc.task_group().cancel().await {
            warn!("Task group was already cancelled prior to account removal.");
        }
        // At this point we have exclusive access to the account, so we move it out of the Arc to
        // destroy it.
        let account = Arc::try_unwrap(account_arc).map_err(|_| {
            warn!("Could not acquire exclusive access to account");
            ApiError::Internal
        })?;

        let account_id = account.id().clone();
        match account.remove() {
            Ok(()) => {
                info!("Deleted Fuchsia account {:?}", account_id);
                Ok(())
            }
            Err((_account, err)) => {
                warn!("Could not remove account {:?}: {:?}", account_id, err);
                Err(err.into())
            }
        }
    }

    async fn get_account(
        &self,
        auth_context_provider_client_end: ClientEnd<AuthenticationContextProviderMarker>,
        account_server_end: ServerEnd<AccountMarker>,
    ) -> Result<(), ApiError> {
        let account_arc = match &*self.account.read() {
            Lifecycle::Initialized { account } => Arc::clone(account),
            _ => {
                warn!("AccountHandler is not initialized");
                return Err(ApiError::NotFound);
            }
        };

        let acp = auth_context_provider_client_end.into_proxy().map_err(|err| {
            warn!("Error using AuthenticationContextProvider {:?}", err);
            ApiError::InvalidRequest
        })?;
        let context = AccountContext { auth_ui_context_provider: acp };
        let stream = account_server_end.into_stream().map_err(|err| {
            warn!("Error opening Account channel {:?}", err);
            ApiError::Resource
        })?;

        let account_arc_clone = Arc::clone(&account_arc);
        account_arc
            .task_group()
            .spawn(|cancel| {
                async move {
                    account_arc_clone
                        .handle_requests_from_stream(&context, stream, cancel)
                        .await
                        .unwrap_or_else(|e| error!("Error handling Account channel {:?}", e));
                }
            })
            .await
            .map_err(|_| {
                // Since AccountHandler serves only one channel of requests in serial, this is an
                // inconsistent state rather than a conflict
                ApiError::Internal
            })
    }

    fn get_global_id_hash(&self, salt: GlobalIdHashSalt) -> Result<GlobalIdHash, ApiError> {
        let account_lock = self.account.read();
        let global_id = match &*account_lock {
            Lifecycle::Initialized { account } => account.global_id().as_ref(),
            _ => return Err(ApiError::FailedPrecondition),
        };

        let mut salted_id = Vec::with_capacity(global_id.len() + HASH_SALT_SIZE as usize);
        salted_id.extend_from_slice(&salt);
        salted_id.extend_from_slice(global_id.as_slice());
        Ok(Sha256::hash(&salted_id).bytes())
    }

    async fn terminate(&self) {
        info!("Gracefully shutting down AccountHandler");
        let old_lifecycle = {
            let mut account_lock = self.account.write();
            std::mem::replace(&mut *account_lock, Lifecycle::Finished)
        };
        if let Lifecycle::Initialized { account } = old_lifecycle {
            if account.task_group().cancel().await.is_err() {
                warn!("Task group cancelled but account is still initialized");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_identity_account::{Scenario, ThreatScenario};
    use fidl_fuchsia_identity_internal::{AccountHandlerControlMarker, AccountHandlerControlProxy};
    use fuchsia_async as fasync;
    use fuchsia_inspect::testing::AnyProperty;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use std::sync::Arc;

    const DEFAULT_SCENARIO: Scenario =
        Scenario { include_test: false, threat_scenario: ThreatScenario::None };

    const FORCE_REMOVE_ON: bool = true;
    const FORCE_REMOVE_OFF: bool = false;

    // Will not match a randomly generated account id with high probability.
    const WRONG_ACCOUNT_ID: u64 = 111111;

    /// An enum expressing unexpected errors that may occur during a test.
    #[derive(Debug)]
    enum AccountHandlerTestError {
        FidlError(fidl::Error),
        AccountError(ApiError),
    }

    impl From<fidl::Error> for AccountHandlerTestError {
        fn from(fidl_error: fidl::Error) -> Self {
            AccountHandlerTestError::FidlError(fidl_error)
        }
    }

    impl From<ApiError> for AccountHandlerTestError {
        fn from(err: ApiError) -> Self {
            AccountHandlerTestError::AccountError(err)
        }
    }

    fn request_stream_test<TestFn, Fut>(
        lifetime: AccountLifetime,
        inspector: Arc<Inspector>,
        test_fn: TestFn,
    ) where
        TestFn: FnOnce(AccountHandlerControlProxy, ClientEnd<AccountHandlerContextMarker>) -> Fut,
        Fut: Future<Output = Result<(), AccountHandlerTestError>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let test_object = AccountHandler::new(lifetime, &inspector);
        let fake_context = Arc::new(FakeAccountHandlerContext::new());
        let ahc_client_end = spawn_context_channel(fake_context.clone());

        let (client_end, server_end) = create_endpoints::<AccountHandlerControlMarker>().unwrap();
        let proxy = client_end.into_proxy().unwrap();
        let request_stream = server_end.into_stream().unwrap();

        fasync::spawn(async move {
            test_object
                .handle_requests_from_stream(request_stream)
                .await
                .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err));

            // Check that no more objects are lurking in inspect
            std::mem::drop(test_object);
            assert_inspect_tree!(inspector, root: {});
        });

        executor.run_singlethreaded(test_fn(proxy, ahc_client_end)).expect("Executor run failed.")
    }

    #[test]
    fn test_get_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy, _| {
                async move {
                    let (_, account_server_end) = create_endpoints().unwrap();
                    let (acp_client_end, _) = create_endpoints().unwrap();
                    assert_eq!(
                        proxy.get_account(acp_client_end, account_server_end).await?,
                        Err(ApiError::NotFound)
                    );
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_double_initialize() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy, ahc_client_end| {
                async move {
                    proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().into()).await??;

                    let fake_context_2 = Arc::new(FakeAccountHandlerContext::new());
                    let ahc_client_end_2 = spawn_context_channel(fake_context_2.clone());
                    assert_eq!(
                        proxy
                            .create_account(ahc_client_end_2, TEST_ACCOUNT_ID.clone().into())
                            .await?,
                        Err(ApiError::Internal)
                    );
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_create_and_get_account() {
        let location = TempLocation::new();
        let inspector = Arc::new(Inspector::new());
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::clone(&inspector),
            |account_handler_proxy, ahc_client_end| {
                async move {
                    account_handler_proxy
                        .create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().into())
                        .await??;

                    assert_inspect_tree!(inspector, root: {
                        account_handler: contains {
                            account: contains {
                                open_client_channels: 0u64,
                            },
                        }
                    });

                    let (account_client_end, account_server_end) = create_endpoints().unwrap();
                    let (acp_client_end, _) = create_endpoints().unwrap();
                    account_handler_proxy.get_account(acp_client_end, account_server_end).await??;

                    assert_inspect_tree!(inspector, root: {
                        account_handler: contains {
                            account: contains {
                                open_client_channels: 1u64,
                            },
                        }
                    });

                    // The account channel should now be usable.
                    let account_proxy = account_client_end.into_proxy().unwrap();
                    assert_eq!(
                        account_proxy.get_auth_state(&mut DEFAULT_SCENARIO.clone()).await?,
                        Err(ApiError::UnsupportedOperation)
                    );
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_create_and_load_account() {
        // Check that an account is persisted when account handlers are restarted
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy, ahc_client_end| {
                async move {
                    proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().into()).await??;
                    Ok(())
                }
            },
        );
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy, ahc_client_end| {
                async move {
                    proxy.load_account(ahc_client_end, TEST_ACCOUNT_ID.clone().into()).await??;
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_global_id_hashes_unique() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy, ahc_client_end| {
                async move {
                    proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().into()).await??;

                    // Different given different salts
                    let mut salt_1 = [0u8; 32];
                    let hash_1 = proxy.get_global_id_hash(&mut salt_1).await??;

                    let mut salt_2 = [37u8; 32];
                    let hash_2 = proxy.get_global_id_hash(&mut salt_2).await??;

                    assert_ne!(hash_1[..], hash_2[..]);
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_load_ephemeral_account_fails() {
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy, ahc_client_end| {
                async move {
                    let expected_id = TEST_ACCOUNT_ID.clone();
                    assert_eq!(
                        proxy.load_account(ahc_client_end, expected_id.into()).await?,
                        Err(ApiError::Internal)
                    );
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_remove_account() {
        let location = TempLocation::new();
        let inspector = Arc::new(Inspector::new());
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::clone(&inspector),
            |proxy, ahc_client_end| {
                async move {
                    assert_inspect_tree!(inspector, root: {
                        account_handler: {
                            lifecycle: "uninitialized",
                        }
                    });

                    proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().into()).await??;
                    assert_inspect_tree!(inspector, root: {
                        account_handler: {
                            lifecycle: "initialized",
                            account: {
                                local_account_id: TEST_ACCOUNT_ID_UINT,
                                open_client_channels: 0u64,
                            },
                            default_persona: {
                                local_persona_id: AnyProperty,
                                open_client_channels: 0u64,
                            },
                        }
                    });

                    // Keep an open channel to an account.
                    let (account_client_end, account_server_end) = create_endpoints().unwrap();
                    let (acp_client_end, _) = create_endpoints().unwrap();
                    proxy.get_account(acp_client_end, account_server_end).await??;
                    let account_proxy = account_client_end.into_proxy().unwrap();

                    // Simple check that non-force account removal returns error due to not implemented.
                    assert_eq!(
                        proxy.remove_account(FORCE_REMOVE_OFF).await?,
                        Err(ApiError::Internal)
                    );

                    // Make sure remove_account() can make progress with an open channel.
                    proxy.remove_account(FORCE_REMOVE_ON).await??;

                    assert_inspect_tree!(inspector, root: {
                        account_handler: {
                            lifecycle: "finished",
                        }
                    });

                    // Make sure that the channel is in fact closed.
                    assert!(account_proxy
                        .get_auth_state(&mut DEFAULT_SCENARIO.clone())
                        .await
                        .is_err());

                    // We cannot remove twice.
                    assert_eq!(
                        proxy.remove_account(FORCE_REMOVE_ON).await?,
                        Err(ApiError::InvalidRequest)
                    );
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_remove_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy, _| {
                async move {
                    assert_eq!(
                        proxy.remove_account(FORCE_REMOVE_ON).await?,
                        Err(ApiError::InvalidRequest)
                    );
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_terminate() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy, ahc_client_end| {
                async move {
                    proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().into()).await??;

                    // Keep an open channel to an account.
                    let (account_client_end, account_server_end) = create_endpoints().unwrap();
                    let (acp_client_end, _) = create_endpoints().unwrap();
                    proxy.get_account(acp_client_end, account_server_end).await??;
                    let account_proxy = account_client_end.into_proxy().unwrap();

                    // Terminate the handler
                    proxy.terminate()?;

                    // Check that further operations fail
                    assert!(proxy.remove_account(FORCE_REMOVE_ON).await.is_err());
                    assert!(proxy.terminate().is_err());

                    // Make sure that the channel closed too.
                    assert!(account_proxy
                        .get_auth_state(&mut DEFAULT_SCENARIO.clone())
                        .await
                        .is_err());
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_load_account_not_found() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy, ahc_client_end| {
                async move {
                    assert_eq!(
                        proxy.load_account(ahc_client_end, WRONG_ACCOUNT_ID).await?,
                        Err(ApiError::NotFound)
                    );
                    Ok(())
                }
            },
        );
    }
}
