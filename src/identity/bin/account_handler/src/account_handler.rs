// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account::{Account, AccountContext};
use crate::common::AccountLifetime;
use crate::inspect;
use account_common::{AccountManagerError, LocalAccountId};
use anyhow::format_err;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::AuthenticationContextProviderMarker;
use fidl_fuchsia_identity_account::{AccountMarker, Error as ApiError};
use fidl_fuchsia_identity_internal::{
    AccountHandlerContextProxy, AccountHandlerControlRequest, AccountHandlerControlRequestStream,
    HASH_SALT_SIZE, HASH_SIZE,
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

    /// The handler is awaiting an account transfer.
    PendingTransfer,

    /// The handler is holding a transferred account and is awaiting finalization.
    Transferred,

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
    /// An AccountHandlerContextProxy for this account.
    context: AccountHandlerContextProxy,

    /// An optional `Account` that we are handling.
    ///
    /// This will be Uninitialized until a particular Account is established over the control
    /// channel. Then it will be initialized. When the AccountHandler is terminated, or its Account
    /// is removed, it reaches its final state, Finished.
    account: RwLock<Lifecycle>,

    /// Lifetime for this account (ephemeral or persistent with a path).
    lifetime: AccountLifetime,

    /// Helper for outputting account handler information via fuchsia_inspect.
    inspect: inspect::AccountHandler,
    // TODO(jsankey): Add TokenManager and AccountHandlerContext.
}

impl AccountHandler {
    /// Constructs a new AccountHandler.
    pub fn new(
        context: AccountHandlerContextProxy,
        account_id: LocalAccountId,
        lifetime: AccountLifetime,
        inspector: &Inspector,
    ) -> AccountHandler {
        let inspect = inspect::AccountHandler::new(inspector.root(), &account_id, "uninitialized");
        Self { context, account: RwLock::new(Lifecycle::Uninitialized), lifetime, inspect }
    }

    /// Asynchronously handles the supplied stream of `AccountHandlerControlRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountHandlerControlRequestStream,
    ) -> Result<(), anyhow::Error> {
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
            AccountHandlerControlRequest::CreateAccount { auth_mechanism_id, responder } => {
                let mut response = self.create_account(auth_mechanism_id).await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::LoadAccount { responder } => {
                let mut response = self.load_account().await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::PrepareForAccountTransfer { responder } => {
                let mut response = self.prepare_for_account_transfer();
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::PerformAccountTransfer {
                encrypted_account_data,
                responder,
            } => {
                let mut response = self.perform_account_transfer(encrypted_account_data);
                responder.send(&mut response)?;
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
    ) -> Result<(), AccountManagerError>
    where
        F: FnOnce(AccountLifetime, AccountHandlerContextProxy, &'a Node) -> Fut,
        Fut: Future<Output = Result<Account, AccountManagerError>>,
    {
        // The function evaluation is front loaded because await is not allowed while holding the
        // lock.
        let account_result = construct_account_fn(
            self.lifetime.clone(),
            self.context.clone(),
            self.inspect.get_node(),
        )
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

    /// Creates a new Fuchsia account and attaches it to this handler.  Moves
    /// the handler from the `Uninitialized` to `Initialized` state.
    async fn create_account(&self, auth_mechanism_id: Option<String>) -> Result<(), ApiError> {
        match (&self.lifetime, &auth_mechanism_id) {
            (AccountLifetime::Persistent { .. }, Some(_)) => {
                return Err(ApiError::UnsupportedOperation)
            }
            (AccountLifetime::Ephemeral, Some(_)) => return Err(ApiError::InvalidRequest),
            _ => {}
        };
        self.init_account(Account::create).await.map_err(|err| {
            warn!("Failed creating Fuchsia account: {:?}", err);
            err.into()
        })
    }

    /// Loads an existing Fuchsia account and attaches it to this handler.  Moves
    /// the handler from the `Uninitialized` to `Initialized` state.
    async fn load_account(&self) -> Result<(), ApiError> {
        self.init_account(Account::load).await.map_err(|err| {
            warn!("Failed loading Fuchsia account: {:?}", err);
            err.into()
        })
    }

    /// Prepares the handler for an account transfer.  Moves the handler from the
    /// `Uninitialized` state to the `PendingTransfer` state.
    fn prepare_for_account_transfer(&self) -> Result<(), ApiError> {
        let mut account_lock = self.account.write();
        match *account_lock {
            Lifecycle::Uninitialized => {
                *account_lock = Lifecycle::PendingTransfer;
                self.inspect.lifecycle.set("pendingTransfer");
                Ok(())
            }
            _ => {
                warn!("PrepareForAccountTransfer called while not in Uninitialized state");
                Err(ApiError::FailedPrecondition)
            }
        }
    }

    /// Loads an encrypted account into memory but does not make it available
    /// for use yet.  Moves the handler from the `PendingTransfer` state to the
    /// `Transferred` state.
    fn perform_account_transfer(&self, _encrypted_account_data: Vec<u8>) -> Result<(), ApiError> {
        let mut account_lock = self.account.write();
        match *account_lock {
            Lifecycle::PendingTransfer => {
                *account_lock = Lifecycle::Transferred;
                self.inspect.lifecycle.set("transferred");
                Ok(())
            }
            _ => {
                warn!("PerformAccountTransfer called while not in PendingTransfer state");
                Err(ApiError::FailedPrecondition)
            }
        }
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
        match account.remove() {
            Ok(()) => {
                info!("Deleted Fuchsia account");
                Ok(())
            }
            Err((_account, err)) => {
                warn!("Could not remove account: {:?}", err);
                Err(err.into())
            }
        }
    }

    /// Connects the provided `account_server_end` to the `Account` protocol
    /// served by this handler.
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
            .spawn(|cancel| async move {
                account_arc_clone
                    .handle_requests_from_stream(&context, stream, cancel)
                    .await
                    .unwrap_or_else(|e| error!("Error handling Account channel {:?}", e));
            })
            .await
            .map_err(|_| {
                // Since AccountHandler serves only one channel of requests in serial, this is an
                // inconsistent state rather than a conflict
                ApiError::Internal
            })
    }

    /// Computes a hash of the global account id of the account held by this
    /// handler using the provided hash.
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
    use fidl::endpoints::{create_endpoints, create_proxy_and_stream};
    use fidl_fuchsia_identity_account::{Scenario, ThreatScenario};
    use fidl_fuchsia_identity_internal::{AccountHandlerControlMarker, AccountHandlerControlProxy};
    use fuchsia_async as fasync;
    use fuchsia_inspect::testing::AnyProperty;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use futures::future::join;
    use std::sync::Arc;

    const DEFAULT_SCENARIO: Scenario =
        Scenario { include_test: false, threat_scenario: ThreatScenario::None };

    const FORCE_REMOVE_ON: bool = true;
    const FORCE_REMOVE_OFF: bool = false;

    const TEST_AUTH_MECHANISM_ID: &str = "<AUTH MECHANISM ID>";

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

    type TestResult = Result<(), AccountHandlerTestError>;

    fn create_account_handler(
        lifetime: AccountLifetime,
        context: AccountHandlerContextProxy,
        inspector: Arc<Inspector>,
    ) -> (AccountHandlerControlProxy, impl Future<Output = ()>) {
        let test_object =
            AccountHandler::new(context, TEST_ACCOUNT_ID.clone().into(), lifetime, &inspector);
        let (proxy, request_stream) = create_proxy_and_stream::<AccountHandlerControlMarker>()
            .expect("Failed to create proxy and stream");

        let server_fut = async move {
            test_object
                .handle_requests_from_stream(request_stream)
                .await
                .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err));

            // Check that no more objects are lurking in inspect
            std::mem::drop(test_object);
            assert_inspect_tree!(inspector, root: {});
        };

        (proxy, server_fut)
    }

    fn request_stream_test<TestFn, Fut>(
        lifetime: AccountLifetime,
        inspector: Arc<Inspector>,
        test_fn: TestFn,
    ) where
        TestFn: FnOnce(AccountHandlerControlProxy) -> Fut,
        Fut: Future<Output = TestResult>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let fake_context = Arc::new(FakeAccountHandlerContext::new());
        let ahc_proxy = spawn_context_channel(fake_context);
        let (proxy, server_fut) = create_account_handler(lifetime, ahc_proxy, inspector);

        let (test_res, _server_result) =
            executor.run_singlethreaded(join(test_fn(proxy), server_fut));

        assert!(test_res.is_ok());
    }

    #[test]
    fn test_get_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                let (_, account_server_end) = create_endpoints().unwrap();
                let (acp_client_end, _) = create_endpoints().unwrap();
                assert_eq!(
                    proxy.get_account(acp_client_end, account_server_end).await?,
                    Err(ApiError::NotFound)
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_double_initialize() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.create_account(None).await??;
                assert_eq!(proxy.create_account(None).await?, Err(ApiError::Internal));
                Ok(())
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
            |account_handler_proxy| {
                async move {
                    account_handler_proxy.create_account(None).await??;

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
            |proxy| async move {
                proxy.create_account(None).await??;
                Ok(())
            },
        );
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.load_account().await??;
                Ok(())
            },
        );
    }

    #[test]
    fn test_global_id_hashes_unique() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy| {
                async move {
                    proxy.create_account(None).await??;

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
    fn test_account_transfer_states() {
        let location = TempLocation::new();
        let inspector = Arc::new(Inspector::new());
        request_stream_test(location.to_persistent_lifetime(), Arc::clone(&inspector), |proxy| {
            async move {
                // TODO(satsukiu): add more meaningful tests once there's some functionality
                // to test
                assert_inspect_tree!(inspector, root: {
                    account_handler: {
                        local_account_id: TEST_ACCOUNT_ID_UINT,
                        lifecycle: "uninitialized",
                    }
                });
                proxy.prepare_for_account_transfer().await??;
                assert_inspect_tree!(inspector, root: {
                    account_handler: {
                        local_account_id: TEST_ACCOUNT_ID_UINT,
                        lifecycle: "pendingTransfer",
                    }
                });
                proxy.perform_account_transfer(&mut vec![].into_iter()).await??;
                assert_inspect_tree!(inspector, root: {
                    account_handler: {
                        local_account_id: TEST_ACCOUNT_ID_UINT,
                        lifecycle: "transferred",
                    }
                });
                Ok(())
            }
        });
    }

    #[test]
    fn test_prepare_for_account_transfer_invalid_states() {
        // Handler in `PendingTransfer` state
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.prepare_for_account_transfer().await??;
                assert_eq!(
                    proxy.prepare_for_account_transfer().await?,
                    Err(ApiError::FailedPrecondition)
                );
                Ok(())
            },
        );

        // Handler in `Transferred` state
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.prepare_for_account_transfer().await??;
                proxy.perform_account_transfer(&mut vec![].into_iter()).await??;
                assert_eq!(
                    proxy.prepare_for_account_transfer().await?,
                    Err(ApiError::FailedPrecondition)
                );
                Ok(())
            },
        );

        // Handler in `Initialized` state
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.create_account(None).await??;
                assert_eq!(
                    proxy.prepare_for_account_transfer().await?,
                    Err(ApiError::FailedPrecondition)
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_perform_account_transfer_invalid_states() {
        // Handler in `Uninitialized` state
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                assert_eq!(
                    proxy.perform_account_transfer(&mut vec![].into_iter()).await?,
                    Err(ApiError::FailedPrecondition)
                );
                Ok(())
            },
        );

        // Handler in `Transferred` state
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.prepare_for_account_transfer().await??;
                proxy.perform_account_transfer(&mut vec![].into_iter()).await??;
                assert_eq!(
                    proxy.perform_account_transfer(&mut vec![].into_iter()).await?,
                    Err(ApiError::FailedPrecondition)
                );
                Ok(())
            },
        );

        // Handler in `Initialized` state
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.create_account(None).await??;
                assert_eq!(
                    proxy.perform_account_transfer(&mut vec![].into_iter()).await?,
                    Err(ApiError::FailedPrecondition)
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_finalize_account_transfer_unimplemented() {
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.prepare_for_account_transfer().await??;
                proxy.perform_account_transfer(&mut vec![].into_iter()).await??;
                assert_eq!(
                    proxy.finalize_account_transfer().await?,
                    Err(ApiError::UnsupportedOperation)
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_load_ephemeral_account_fails() {
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                assert_eq!(proxy.load_account().await?, Err(ApiError::Internal));
                Ok(())
            },
        );
    }

    #[test]
    fn test_remove_account() {
        let location = TempLocation::new();
        let inspector = Arc::new(Inspector::new());
        request_stream_test(location.to_persistent_lifetime(), Arc::clone(&inspector), |proxy| {
            async move {
                assert_inspect_tree!(inspector, root: {
                    account_handler: {
                        local_account_id: TEST_ACCOUNT_ID_UINT,
                        lifecycle: "uninitialized",
                    }
                });

                proxy.create_account(None).await??;
                assert_inspect_tree!(inspector, root: {
                    account_handler: {
                        local_account_id: TEST_ACCOUNT_ID_UINT,
                        lifecycle: "initialized",
                        account: {
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
                assert_eq!(proxy.remove_account(FORCE_REMOVE_OFF).await?, Err(ApiError::Internal));

                // Make sure remove_account() can make progress with an open channel.
                proxy.remove_account(FORCE_REMOVE_ON).await??;

                assert_inspect_tree!(inspector, root: {
                    account_handler: {
                        local_account_id: TEST_ACCOUNT_ID_UINT,
                        lifecycle: "finished",
                    }
                });

                // Make sure that the channel is in fact closed.
                assert!(account_proxy.get_auth_state(&mut DEFAULT_SCENARIO.clone()).await.is_err());

                // We cannot remove twice.
                assert_eq!(
                    proxy.remove_account(FORCE_REMOVE_ON).await?,
                    Err(ApiError::InvalidRequest)
                );
                Ok(())
            }
        });
    }

    #[test]
    fn test_remove_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                assert_eq!(
                    proxy.remove_account(FORCE_REMOVE_ON).await?,
                    Err(ApiError::InvalidRequest)
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_terminate() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy| {
                async move {
                    proxy.create_account(None).await??;

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
            |proxy| async move {
                assert_eq!(proxy.load_account().await?, Err(ApiError::NotFound));
                Ok(())
            },
        );
    }

    #[test]
    fn test_create_account_ephemeral_with_auth_mechanism() {
        request_stream_test(
            AccountLifetime::Ephemeral,
            Arc::new(Inspector::new()),
            |proxy| async move {
                assert_eq!(
                    proxy.create_account(Some(TEST_AUTH_MECHANISM_ID)).await?,
                    Err(ApiError::InvalidRequest)
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_create_account_with_auth_mechanism() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                assert_eq!(
                    proxy.create_account(Some(TEST_AUTH_MECHANISM_ID)).await?,
                    Err(ApiError::UnsupportedOperation)
                );
                Ok(())
            },
        );
    }
}
