// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account::{Account, AccountContext};
use crate::common::AccountLifetime;
use crate::inspect;
use crate::pre_auth;
use account_common::LocalAccountId;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::AuthenticationContextProviderMarker;
use fidl_fuchsia_identity_account::{AccountMarker, Error as ApiError};
use fidl_fuchsia_identity_internal::{
    AccountHandlerContextProxy, AccountHandlerControlRequest, AccountHandlerControlRequestStream,
    HASH_SALT_SIZE, HASH_SIZE,
};
use fuchsia_inspect::{Inspector, Property};
use futures::lock::Mutex;
use futures::prelude::*;
use identity_common::TaskGroupError;
use log::{error, info, warn};
use mundane::hash::{Digest, Hasher, Sha256};
use std::fmt;
use std::sync::Arc;

/// The states of an AccountHandler.
enum Lifecycle {
    /// An account has not yet been created or loaded.
    Uninitialized,

    /// The handler is awaiting an account transfer.
    PendingTransfer,

    /// The handler is holding a transferred account and is awaiting finalization.
    Transferred,

    /// The account is locked.
    Locked { pre_auth_state: Arc<pre_auth::State> },

    /// The account is currently loaded and is available.
    Initialized { account: Arc<Account>, pre_auth_state: Arc<pre_auth::State> },

    /// There is no account present, and initialization is not possible.
    Finished,
}

impl fmt::Debug for Lifecycle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let name = match self {
            &Lifecycle::Uninitialized => "Uninitialized",
            &Lifecycle::PendingTransfer => "PendingTransfer",
            &Lifecycle::Transferred => "Transferred",
            &Lifecycle::Locked { .. } => "Locked",
            &Lifecycle::Initialized { .. } => "Initialized",
            &Lifecycle::Finished => "Finished",
        };
        write!(f, "{}", name)
    }
}

type GlobalIdHash = [u8; HASH_SIZE as usize];
type GlobalIdHashSalt = [u8; HASH_SALT_SIZE as usize];

/// The core state of the AccountHandler, i.e. the Account (once it is known) and references to
/// the execution context and a TokenManager.
pub struct AccountHandler {
    /// An AccountHandlerContextProxy for this account.
    context: AccountHandlerContextProxy,

    /// The current state of the AccountHandler state machine, optionally containing
    /// a reference to the `Account` and its pre-authentication data, depending on the
    /// state. The methods of the AccountHandler drives changes to the state.
    state: Mutex<Lifecycle>,

    /// Lifetime for this account (ephemeral or persistent with a path).
    lifetime: AccountLifetime,

    /// An implementation of the `Manager` trait that is responsible for retrieving and
    /// persisting pre-authentication data of an account. The pre-auth data is cached
    /// and available in the Locked and Initialized states.
    // TODO(dnordstrom): Consider moving the pre_auth_manager into the AccountLifetime enum.
    pre_auth_manager: Arc<dyn pre_auth::Manager>,

    /// Helper for outputting account handler information via fuchsia_inspect.
    inspect: inspect::AccountHandler,
    // TODO(jsankey): Add TokenManager and AccountHandlerContext.
}

impl AccountHandler {
    /// Constructs a new AccountHandler and puts it in the Uninitialized state.
    pub fn new(
        context: AccountHandlerContextProxy,
        account_id: LocalAccountId,
        lifetime: AccountLifetime,
        pre_auth_manager: Arc<dyn pre_auth::Manager>,
        inspector: &Inspector,
    ) -> AccountHandler {
        let inspect = inspect::AccountHandler::new(inspector.root(), &account_id, "uninitialized");
        Self {
            context,
            state: Mutex::new(Lifecycle::Uninitialized),
            lifetime,
            pre_auth_manager,
            inspect,
        }
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
            AccountHandlerControlRequest::Preload { responder } => {
                let mut response = self.preload().await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::UnlockAccount { responder } => {
                let mut response = self.unlock_account().await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::LockAccount { responder } => {
                let mut response = self.lock_account().await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::PrepareForAccountTransfer { responder } => {
                let mut response = self.prepare_for_account_transfer().await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::PerformAccountTransfer {
                encrypted_account_data,
                responder,
            } => {
                let mut response = self.perform_account_transfer(encrypted_account_data).await;
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
                let mut response = self.get_global_id_hash(salt).await;
                responder.send(&mut response)?;
            }
            AccountHandlerControlRequest::Terminate { control_handle } => {
                self.terminate().await;
                control_handle.shutdown();
            }
        }
        Ok(())
    }

    /// Creates a new Fuchsia account and attaches it to this handler.  Moves
    /// the handler from the `Uninitialized` to the `Initialized` state.
    async fn create_account(&self, auth_mechanism_id: Option<String>) -> Result<(), ApiError> {
        let mut state_lock = self.state.lock().await;
        match *state_lock {
            Lifecycle::Uninitialized => {
                let pre_auth_state = match (&self.lifetime, &auth_mechanism_id) {
                    (AccountLifetime::Persistent { .. }, Some(_)) => {
                        // TODO(42885, 42886): Get authenticator channel, enroll.
                        return Err(ApiError::UnsupportedOperation);
                    }
                    (AccountLifetime::Ephemeral, Some(_)) => {
                        warn!(
                            "CreateAccount called with auth_mechanism_id set on an ephemeral \
                              account"
                        );
                        return Err(ApiError::InvalidRequest);
                    }
                    (_, None) => Arc::new(pre_auth::State::NoEnrollments),
                };
                self.pre_auth_manager.put(&pre_auth_state).await.map_err(|err| {
                    warn!("Could not write pre-auth data: {:?}", err);
                    err.api_error
                })?;
                let account = Account::create(
                    self.lifetime.clone(),
                    self.context.clone(),
                    self.inspect.get_node(),
                )
                .await
                .map_err(|err| err.api_error)?;
                *state_lock = Lifecycle::Initialized { account: Arc::new(account), pre_auth_state };
                self.inspect.lifecycle.set("initialized");
                Ok(())
            }
            ref invalid_state @ _ => {
                warn!("CreateAccount was called in the {:?} state", invalid_state);
                Err(ApiError::FailedPrecondition)
            }
        }
    }

    /// Loads pre-authentication state for an account.  Moves the handler from
    /// the `Uninitialized` to the `Locked` state.
    async fn preload(&self) -> Result<(), ApiError> {
        if self.lifetime == AccountLifetime::Ephemeral {
            warn!("Preload was called on an ephemeral account");
            return Err(ApiError::InvalidRequest);
        }
        let mut state_lock = self.state.lock().await;
        match *state_lock {
            Lifecycle::Uninitialized => {
                let pre_auth_state =
                    Arc::new(self.pre_auth_manager.get().await.map_err(|err| err.api_error)?);
                *state_lock = Lifecycle::Locked { pre_auth_state };
                self.inspect.lifecycle.set("locked");
                Ok(())
            }
            ref invalid_state @ _ => {
                warn!("Preload was called in the {:?} state", invalid_state);
                Err(ApiError::FailedPrecondition)
            }
        }
    }

    /// Unlocks an existing Fuchsia account and attaches it to this handler.
    /// If the account is enrolled with an authentication mechanism,
    /// authentication will be performed as part of this call. Moves
    /// the handler to the `Initialized` state.
    async fn unlock_account(&self) -> Result<(), ApiError> {
        let mut state_lock = self.state.lock().await;
        match &*state_lock {
            Lifecycle::Initialized { .. } => {
                info!("UnlockAccount was called in the Initialized state, quietly succeeding.");
                return Ok(());
            }
            Lifecycle::Locked { pre_auth_state } => {
                let new_state = match pre_auth_state.as_ref() {
                    &pre_auth::State::NoEnrollments => {
                        let account = Account::load(
                            self.lifetime.clone(),
                            self.context.clone(),
                            self.inspect.get_node(),
                        )
                        .await
                        .map_err(|err| err.api_error)?;
                        Lifecycle::Initialized {
                            account: Arc::new(account),
                            pre_auth_state: Arc::clone(pre_auth_state),
                        }
                    }
                    &pre_auth::State::SingleEnrollment { .. } => {
                        // TODO(42885, 42886): Get authenticator channel and authenticate.
                        return Err(ApiError::UnsupportedOperation);
                    }
                };
                *state_lock = new_state;
                self.inspect.lifecycle.set("initialized");
                Ok(())
            }
            ref invalid_state @ _ => {
                warn!("UnlockAccount was called in the {:?} state", invalid_state);
                Err(ApiError::FailedPrecondition)
            }
        }
    }

    /// Locks the account, terminating all open Account and Persona channels.  Moves
    /// the handler to the `Locked` state.
    async fn lock_account(&self) -> Result<(), ApiError> {
        let mut state_lock = self.state.lock().await;
        match &*state_lock {
            Lifecycle::Locked { .. } => {
                info!("LockAccount was called in the Locked state, quietly succeeding.");
                return Ok(());
            }
            Lifecycle::Initialized { pre_auth_state, account } => {
                let _ = account.task_group().cancel().await; // Ignore AlreadyCancelled error
                let new_state = Lifecycle::Locked { pre_auth_state: Arc::clone(pre_auth_state) };
                *state_lock = new_state;
                self.inspect.lifecycle.set("locked");
                Ok(())
            }
            ref invalid_state @ _ => {
                warn!("LockAccount was called in the {:?} state", invalid_state);
                Err(ApiError::FailedPrecondition)
            }
        }
    }

    /// Prepares the handler for an account transfer.  Moves the handler from the
    /// `Uninitialized` state to the `PendingTransfer` state.
    async fn prepare_for_account_transfer(&self) -> Result<(), ApiError> {
        let mut state_lock = self.state.lock().await;
        match *state_lock {
            Lifecycle::Uninitialized => {
                *state_lock = Lifecycle::PendingTransfer;
                self.inspect.lifecycle.set("pendingTransfer");
                Ok(())
            }
            ref invalid_state @ _ => {
                warn!("PrepareForAccountTransfer was called in the {:?} state", invalid_state);
                Err(ApiError::FailedPrecondition)
            }
        }
    }

    /// Loads an encrypted account into memory but does not make it available
    /// for use yet.  Moves the handler from the `PendingTransfer` state to the
    /// `Transferred` state.
    async fn perform_account_transfer(
        &self,
        _encrypted_account_data: Vec<u8>,
    ) -> Result<(), ApiError> {
        let mut state_lock = self.state.lock().await;
        match *state_lock {
            Lifecycle::PendingTransfer => {
                *state_lock = Lifecycle::Transferred;
                self.inspect.lifecycle.set("transferred");
                Ok(())
            }
            ref invalid_state @ _ => {
                warn!("PerformAccountTransfer was called in the {:?} state", invalid_state);
                Err(ApiError::FailedPrecondition)
            }
        }
    }

    /// Remove the active account. This method should not be retried on failure.
    // TODO(AUTH-212): Implement graceful account removal.
    async fn remove_account(&self, force: bool) -> Result<(), ApiError> {
        if force == false {
            warn!("Graceful (non-force) account removal not yet implemented.");
            return Err(ApiError::UnsupportedOperation);
        }
        let old_lifecycle = {
            let mut state_lock = self.state.lock().await;
            std::mem::replace(&mut *state_lock, Lifecycle::Finished)
        };
        self.inspect.lifecycle.set("finished");
        let account_arc = match old_lifecycle {
            Lifecycle::Locked { .. } => {
                warn!("Removing a locked account is not yet implemented");
                return Err(ApiError::UnsupportedOperation);
            }
            Lifecycle::Initialized { account, .. } => account,
            _ => {
                warn!("No account is initialized");
                return Err(ApiError::FailedPrecondition);
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
        account.remove().map_err(|(_account, err)| {
            warn!("Could not delete account: {:?}", err);
            err.api_error
        })?;
        self.pre_auth_manager.remove().await.map_err(|err| {
            warn!("Could not remove pre-auth data: {:?}", err);
            err.api_error
        })?;
        info!("Deleted Fuchsia account");
        Ok(())
    }

    /// Connects the provided `account_server_end` to the `Account` protocol
    /// served by this handler.
    async fn get_account(
        &self,
        auth_context_provider_client_end: ClientEnd<AuthenticationContextProviderMarker>,
        account_server_end: ServerEnd<AccountMarker>,
    ) -> Result<(), ApiError> {
        let account_arc = match &*self.state.lock().await {
            Lifecycle::Initialized { account, .. } => Arc::clone(account),
            _ => {
                warn!("AccountHandler is not initialized");
                return Err(ApiError::FailedPrecondition);
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
    async fn get_global_id_hash(&self, salt: GlobalIdHashSalt) -> Result<GlobalIdHash, ApiError> {
        let state_lock = self.state.lock().await;
        let global_id = match &*state_lock {
            Lifecycle::Initialized { account, .. } => account.global_id().as_ref(),
            invalid_state @ _ => {
                warn!("GetGlobalIdHash was called in the {:?} state", invalid_state);
                return Err(ApiError::FailedPrecondition);
            }
        };

        let mut salted_id = Vec::with_capacity(global_id.len() + HASH_SALT_SIZE as usize);
        salted_id.extend_from_slice(&salt);
        salted_id.extend_from_slice(global_id.as_slice());
        Ok(Sha256::hash(&salted_id).bytes())
    }

    async fn terminate(&self) {
        info!("Gracefully shutting down AccountHandler");
        let old_state = {
            let mut state_lock = self.state.lock().await;
            std::mem::replace(&mut *state_lock, Lifecycle::Finished)
        };
        if let Lifecycle::Initialized { account, .. } = old_state {
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
    use lazy_static::lazy_static;
    use std::sync::Arc;

    const DEFAULT_SCENARIO: Scenario =
        Scenario { include_test: false, threat_scenario: ThreatScenario::None };

    const FORCE_REMOVE_ON: bool = true;
    const FORCE_REMOVE_OFF: bool = false;

    const TEST_AUTH_MECHANISM_ID: &str = "<AUTH MECHANISM ID>";

    lazy_static! {
        static ref TEST_ENROLLMENT_DATA: Vec<u8> = vec![13, 37];
        static ref TEST_PRE_AUTH_SINGLE: pre_auth::State = pre_auth::State::SingleEnrollment {
            auth_mechanism_id: TEST_AUTH_MECHANISM_ID.to_string(),
            data: TEST_ENROLLMENT_DATA.clone(),
        };
    }

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
        pre_auth_manager: Arc<dyn pre_auth::Manager>,
        context: AccountHandlerContextProxy,
        inspector: Arc<Inspector>,
    ) -> (AccountHandlerControlProxy, impl Future<Output = ()>) {
        let test_object = AccountHandler::new(
            context,
            TEST_ACCOUNT_ID.clone().into(),
            lifetime,
            pre_auth_manager,
            &inspector,
        );
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
        pre_auth_manager: Arc<dyn pre_auth::Manager>,
        inspector: Arc<Inspector>,
        test_fn: TestFn,
    ) where
        TestFn: FnOnce(AccountHandlerControlProxy) -> Fut,
        Fut: Future<Output = TestResult>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let fake_context = Arc::new(FakeAccountHandlerContext::new());
        let ahc_proxy = spawn_context_channel(fake_context);
        let (proxy, server_fut) =
            create_account_handler(lifetime, pre_auth_manager, ahc_proxy, inspector);

        let (test_res, _server_result) =
            executor.run_singlethreaded(join(test_fn(proxy), server_fut));

        assert!(test_res.is_ok());
    }

    fn create_clean_pre_auth_manager() -> Arc<dyn pre_auth::Manager> {
        Arc::new(pre_auth::InMemoryManager::create(pre_auth::State::NoEnrollments))
    }

    #[test]
    fn test_get_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                let (_, account_server_end) = create_endpoints().unwrap();
                let (acp_client_end, _) = create_endpoints().unwrap();
                assert_eq!(
                    proxy.get_account(acp_client_end, account_server_end).await?,
                    Err(ApiError::FailedPrecondition)
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_get_account_when_locked() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                let (_, account_server_end) = create_endpoints().unwrap();
                let (acp_client_end, _) = create_endpoints().unwrap();
                proxy.preload().await??;
                assert_eq!(
                    proxy.get_account(acp_client_end, account_server_end).await?,
                    Err(ApiError::FailedPrecondition)
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
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.create_account(None).await??;
                assert_eq!(proxy.create_account(None).await?, Err(ApiError::FailedPrecondition));
                Ok(())
            },
        );
    }

    #[test]
    fn test_create_get_and_lock_account() {
        let location = TempLocation::new();
        let inspector = Arc::new(Inspector::new());
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
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
                            lifecycle: "initialized",
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

                    // Lock the account and check that channels are closed
                    account_handler_proxy.lock_account().await??;
                    assert_inspect_tree!(inspector, root: {
                        account_handler: contains {
                            lifecycle: "locked",
                        }
                    });
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
    fn test_preload_and_unlock_existing_account() {
        // Create an account
        let location = TempLocation::new();
        let pre_auth_manager = create_clean_pre_auth_manager();
        let inspector = Arc::new(Inspector::new());
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::clone(&pre_auth_manager),
            Arc::clone(&inspector),
            |proxy| async move {
                proxy.create_account(None).await??;
                assert_inspect_tree!(inspector, root: {
                    account_handler: contains {
                        lifecycle: "initialized",
                    }
                });
                Ok(())
            },
        );

        // Ensure the account is persisted by unlocking it
        let inspector = Arc::new(Inspector::new());
        request_stream_test(
            location.to_persistent_lifetime(),
            pre_auth_manager,
            Arc::clone(&inspector),
            |proxy| async move {
                proxy.preload().await??;
                assert_inspect_tree!(inspector, root: {
                    account_handler: contains {
                        lifecycle: "locked",
                    }
                });
                proxy.unlock_account().await??;
                assert_inspect_tree!(inspector, root: {
                    account_handler: contains {
                        lifecycle: "initialized",
                    }
                });
                Ok(())
            },
        );
    }

    #[test]
    fn test_multiple_unlocks() {
        // Create an account
        let location = TempLocation::new();
        let pre_auth_manager = create_clean_pre_auth_manager();
        let inspector = Arc::new(Inspector::new());
        request_stream_test(
            location.to_persistent_lifetime(),
            Arc::clone(&pre_auth_manager),
            Arc::clone(&inspector),
            |proxy| async move {
                proxy.create_account(None).await??;
                proxy.lock_account().await??;
                proxy.unlock_account().await??;
                proxy.lock_account().await??;
                proxy.unlock_account().await??;
                Ok(())
            },
        );
    }

    #[test]
    fn test_unlock_uninitialized_account() {
        let location = TempLocation::new();
        let pre_auth_manager =
            Arc::new(pre_auth::InMemoryManager::create(TEST_PRE_AUTH_SINGLE.clone()));
        request_stream_test(
            location.to_persistent_lifetime(),
            pre_auth_manager,
            Arc::new(Inspector::new()),
            |proxy| async move {
                assert_eq!(proxy.unlock_account().await?, Err(ApiError::FailedPrecondition));
                Ok(())
            },
        );
    }

    #[test]
    fn test_unlock_protected_account() {
        // Here we omit creating an account prior to loading it, because
        // the unlocking step (which is the failing step) is called
        // before attempting to read the account from disk.
        let location = TempLocation::new();
        let pre_auth_manager =
            Arc::new(pre_auth::InMemoryManager::create(TEST_PRE_AUTH_SINGLE.clone()));
        request_stream_test(
            location.to_persistent_lifetime(),
            pre_auth_manager,
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.preload().await??;
                assert_eq!(proxy.unlock_account().await?, Err(ApiError::UnsupportedOperation));
                Ok(())
            },
        );
    }

    #[test]
    fn test_global_id_hashes_unique() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
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
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::clone(&inspector),
            |proxy| {
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
            },
        );
    }

    #[test]
    fn test_prepare_for_account_transfer_invalid_states() {
        // Handler in `PendingTransfer` state
        request_stream_test(
            AccountLifetime::Ephemeral,
            create_clean_pre_auth_manager(),
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
            create_clean_pre_auth_manager(),
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
            create_clean_pre_auth_manager(),
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

        // Handler in `Locked` state
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.preload().await??;
                proxy.lock_account().await??;
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
            create_clean_pre_auth_manager(),
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
            create_clean_pre_auth_manager(),
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
            create_clean_pre_auth_manager(),
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

        // Handler in `Locked` state
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.preload().await??;
                proxy.lock_account().await??;
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
            create_clean_pre_auth_manager(),
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
    fn test_remove_account() {
        let location = TempLocation::new();
        let inspector = Arc::new(Inspector::new());
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::clone(&inspector),
            |proxy| {
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

                    // Simple check that non-force account removal returns error due to not
                    // implemented.
                    assert_eq!(
                        proxy.remove_account(FORCE_REMOVE_OFF).await?,
                        Err(ApiError::UnsupportedOperation)
                    );

                    // Make sure remove_account() can make progress with an open channel.
                    proxy.remove_account(FORCE_REMOVE_ON).await??;

                    assert_inspect_tree!(inspector, root: {
                        account_handler: {
                            local_account_id: TEST_ACCOUNT_ID_UINT,
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
                        Err(ApiError::FailedPrecondition)
                    );
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_remove_locked_account() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.preload().await??; // Preloading a non-existing account will succeed, for now
                assert_eq!(proxy.remove_account(false).await?, Err(ApiError::UnsupportedOperation));
                Ok(())
            },
        );
    }

    #[test]
    fn test_remove_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                assert_eq!(
                    proxy.remove_account(FORCE_REMOVE_ON).await?,
                    Err(ApiError::FailedPrecondition)
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
            create_clean_pre_auth_manager(),
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
    fn test_terminate_locked_account() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| {
                async move {
                    proxy.create_account(None).await??;
                    proxy.lock_account().await??;
                    proxy.terminate()?;

                    // Check that further operations fail
                    assert!(proxy.unlock_account().await.is_err());
                    assert!(proxy.terminate().is_err());
                    Ok(())
                }
            },
        );
    }

    #[test]
    fn test_load_non_existing_account() {
        let location = TempLocation::new();
        request_stream_test(
            location.to_persistent_lifetime(),
            create_clean_pre_auth_manager(),
            Arc::new(Inspector::new()),
            |proxy| async move {
                proxy.preload().await??; // Preloading a non-existing account will succeed, for now
                assert_eq!(proxy.unlock_account().await?, Err(ApiError::NotFound));
                Ok(())
            },
        );
    }

    #[test]
    fn test_create_account_ephemeral_with_auth_mechanism() {
        request_stream_test(
            AccountLifetime::Ephemeral,
            create_clean_pre_auth_manager(),
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
            create_clean_pre_auth_manager(),
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
