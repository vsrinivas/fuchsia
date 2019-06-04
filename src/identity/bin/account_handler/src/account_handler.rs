// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account::{Account, AccountContext};
use account_common::{AccountManagerError, LocalAccountId, ResultExt};
use failure::{format_err, Error, ResultExt as _};
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthState, AuthStateSummary, AuthenticationContextProviderMarker};
use fidl_fuchsia_auth_account::{AccountMarker, Status};
use fidl_fuchsia_auth_account_internal::{
    AccountHandlerContextMarker, AccountHandlerContextProxy, AccountHandlerControlRequest,
    AccountHandlerControlRequestStream,
};
use futures::prelude::*;
use identity_common::TaskGroupError;
use log::{error, info, warn};
use parking_lot::RwLock;
use std::path::PathBuf;
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

/// The core state of the AccountHandler, i.e. the Account (once it is known) and references to
/// the execution context and a TokenManager.
pub struct AccountHandler {
    // An optional `Account` that we are handling.
    //
    // This will be None until a particular Account is established over the control channel. Once
    // set, the account will never be cleared or modified.
    account: RwLock<Lifecycle>,

    /// Root directory containing persistent resources for an AccountHandler instance.
    data_dir: PathBuf,
    // TODO(jsankey): Add TokenManager and AccountHandlerContext.
}

impl AccountHandler {
    /// (Temporary) A fixed AuthState that is used for all accounts until authenticators are
    /// available.
    pub const DEFAULT_AUTH_STATE: AuthState = AuthState { summary: AuthStateSummary::Unknown };

    /// Constructs a new AccountHandler.
    pub fn new(data_dir: PathBuf) -> AccountHandler {
        Self { account: RwLock::new(Lifecycle::Uninitialized), data_dir }
    }

    /// Asynchronously handles the supplied stream of `AccountHandlerControlRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountHandlerControlRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            await!(self.handle_request(req))?;
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
                let response = await!(self.create_account(id.into(), context));
                responder.send(response)?;
            }
            AccountHandlerControlRequest::LoadAccount { context, id, responder } => {
                let response = await!(self.load_account(id.into(), context));
                responder.send(response)?;
            }
            AccountHandlerControlRequest::RemoveAccount { responder } => {
                let response = await!(self.remove_account());
                responder.send(response)?;
            }
            AccountHandlerControlRequest::GetAccount {
                auth_context_provider,
                account,
                responder,
            } => {
                let response = await!(self.get_account(auth_context_provider, account));
                responder.send(response)?;
            }
            AccountHandlerControlRequest::Terminate { control_handle } => {
                await!(self.terminate());
                control_handle.shutdown();
            }
        }
        Ok(())
    }

    /// Helper method which constructs a new account using the supplied function and stores it in
    /// self.account.
    async fn init_account<F, Fut>(
        &self,
        construct_account_fn: F,
        id: LocalAccountId,
        context: ClientEnd<AccountHandlerContextMarker>,
    ) -> Result<(), AccountManagerError>
    where
        F: FnOnce(LocalAccountId, PathBuf, AccountHandlerContextProxy) -> Fut,
        Fut: Future<Output = Result<Account, AccountManagerError>>,
    {
        let context_proxy = context
            .into_proxy()
            .context("Invalid AccountHandlerContext given")
            .account_manager_status(Status::InvalidRequest)?;

        // The function evaluation is front loaded because await is not allowed while holding the
        // lock.
        let account_result = await!(construct_account_fn(id, self.data_dir.clone(), context_proxy));
        let mut account_lock = self.account.write();
        match *account_lock {
            Lifecycle::Uninitialized => {
                *account_lock = Lifecycle::Initialized { account: Arc::new(account_result?) };
                Ok(())
            }
            _ => Err(AccountManagerError::new(Status::InternalError)
                .with_cause(format_err!("AccountHandler is already initialized"))),
        }
    }

    /// Creates a new Fuchsia account and attaches it to this handler.
    async fn create_account(
        &self,
        id: LocalAccountId,
        context: ClientEnd<AccountHandlerContextMarker>,
    ) -> Status {
        match await!(self.init_account(Account::create, id, context)) {
            Ok(()) => Status::Ok,
            Err(err) => {
                warn!("Failed creating Fuchsia account: {:?}", err);
                err.status
            }
        }
    }

    /// Loads an existing Fuchsia account and attaches it to this handler.
    async fn load_account(
        &self,
        id: LocalAccountId,
        context: ClientEnd<AccountHandlerContextMarker>,
    ) -> Status {
        match await!(self.init_account(Account::load, id, context)) {
            Ok(()) => Status::Ok,
            Err(err) => {
                warn!("Failed creating Fuchsia account: {:?}", err);
                err.status
            }
        }
    }

    /// Remove the active account. This method should not be retried on failure.
    async fn remove_account(&self) -> Status {
        let old_lifecycle = {
            let mut account_lock = self.account.write();
            std::mem::replace(&mut *account_lock, Lifecycle::Finished)
        };
        let account_arc = match old_lifecycle {
            Lifecycle::Initialized { account } => account,
            _ => {
                warn!("No account is initialized");
                return Status::InvalidRequest;
            }
        };
        // TODO(AUTH-212): After this point, error recovery might include putting the account back
        // in the lock.
        if let Err(TaskGroupError::AlreadyCancelled) = await!(account_arc.task_group().cancel()) {
            warn!("Task group was already cancelled prior to account removal.");
        }
        // At this point we have exclusive access to the account, so we move it out of the Arc to
        // destroy it.
        let account = match Arc::try_unwrap(account_arc) {
            Ok(account) => account,
            Err(_account_arc) => {
                warn!("Could not acquire exclusive access to account");
                return Status::InternalError;
            }
        };
        let account_id = account.id().clone();
        match account.remove() {
            Ok(()) => {
                info!("Deleted Fuchsia account {:?}", account_id);
                Status::Ok
            }
            Err((_account, err)) => {
                warn!("Could not remove account {:?}: {:?}", account_id, err);
                err.status
            }
        }
    }

    async fn get_account(
        &self,
        auth_context_provider_client_end: ClientEnd<AuthenticationContextProviderMarker>,
        account_server_end: ServerEnd<AccountMarker>,
    ) -> Status {
        let account_arc = match &*self.account.read() {
            Lifecycle::Initialized { account } => Arc::clone(account),
            _ => {
                warn!("AccountHandler is not initialized");
                return Status::NotFound;
            }
        };

        let context = match auth_context_provider_client_end.into_proxy() {
            Ok(acp) => AccountContext { auth_ui_context_provider: acp },
            Err(err) => {
                warn!("Error using AuthenticationContextProvider {:?}", err);
                return Status::InvalidRequest;
            }
        };
        let stream = match account_server_end.into_stream() {
            Ok(stream) => stream,
            Err(e) => {
                warn!("Error opening Account channel {:?}", e);
                return Status::IoError;
            }
        };

        let account_arc_clone = Arc::clone(&account_arc);
        match await!(account_arc.task_group().spawn(|cancel| async move {
            await!(account_arc_clone.handle_requests_from_stream(&context, stream, cancel))
                .unwrap_or_else(|e| error!("Error handling Account channel {:?}", e));
        })) {
            // Since AccountHandler serves only one channel of requests in serial, this is an
            // inconsistent state rather than a conflict.
            Err(_) => Status::InternalError,
            Ok(()) => Status::Ok,
        }
    }

    async fn terminate(&self) {
        info!("Gracefully shutting down AccountHandler");
        let old_lifecycle = {
            let mut account_lock = self.account.write();
            std::mem::replace(&mut *account_lock, Lifecycle::Finished)
        };
        if let Lifecycle::Initialized { account } = old_lifecycle {
            if await!(account.task_group().cancel()).is_err() {
                warn!("Task group cancelled but account is still initialized");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use account_common::FidlLocalAccountId;
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_auth_account_internal::{
        AccountHandlerControlMarker, AccountHandlerControlProxy,
    };
    use fuchsia_async as fasync;
    use std::path::Path;
    use std::sync::Arc;

    // Will not match a randomly generated account id with high probability.
    const WRONG_ACCOUNT_ID: u64 = 111111;

    fn request_stream_test<TestFn, Fut>(tmp_dir: &Path, test_fn: TestFn)
    where
        TestFn: FnOnce(AccountHandlerControlProxy, ClientEnd<AccountHandlerContextMarker>) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let test_object = AccountHandler::new(tmp_dir.into());
        let fake_context = Arc::new(FakeAccountHandlerContext::new());
        let ahc_client_end = spawn_context_channel(fake_context.clone());

        let (client_end, server_end) = create_endpoints::<AccountHandlerControlMarker>().unwrap();
        let proxy = client_end.into_proxy().unwrap();
        let request_stream = server_end.into_stream().unwrap();

        fasync::spawn(async move {
            await!(test_object.handle_requests_from_stream(request_stream))
                .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err))
        });

        executor.run_singlethreaded(test_fn(proxy, ahc_client_end)).expect("Executor run failed.")
    }

    #[test]
    fn test_get_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(&location.path, async move |proxy, _| {
            let (_, account_server_end) = create_endpoints().unwrap();
            let (acp_client_end, _) = create_endpoints().unwrap();
            assert_eq!(
                await!(proxy.get_account(acp_client_end, account_server_end))?,
                Status::NotFound
            );
            Ok(())
        });
    }

    #[test]
    fn test_double_initialize() {
        let location = TempLocation::new();
        let path = &location.path;
        request_stream_test(&path, async move |proxy, ahc_client_end| {
            let status = await!(
                proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().as_mut().into())
            )?;
            assert_eq!(status, Status::Ok);

            let fake_context_2 = Arc::new(FakeAccountHandlerContext::new());
            let ahc_client_end_2 = spawn_context_channel(fake_context_2.clone());
            assert_eq!(
                await!(
                    proxy.create_account(ahc_client_end_2, TEST_ACCOUNT_ID.clone().as_mut().into())
                )?,
                Status::InternalError
            );
            Ok(())
        });
    }

    #[test]
    fn test_create_and_get_account() {
        let location = TempLocation::new();
        request_stream_test(&location.path, async move |account_handler_proxy, ahc_client_end| {
            let status = await!(account_handler_proxy
                .create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().as_mut().into()))?;
            assert_eq!(status, Status::Ok, "wtf");

            let (account_client_end, account_server_end) = create_endpoints().unwrap();
            let (acp_client_end, _) = create_endpoints().unwrap();
            assert_eq!(
                await!(account_handler_proxy.get_account(acp_client_end, account_server_end))?,
                Status::Ok
            );

            // The account channel should now be usable.
            let account_proxy = account_client_end.into_proxy().unwrap();
            assert_eq!(
                await!(account_proxy.get_auth_state())?,
                (Status::Ok, Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE)))
            );
            Ok(())
        });
    }

    #[test]
    fn test_create_and_load_account() {
        // Check that an account is persisted when account handlers are restarted
        let location = TempLocation::new();
        request_stream_test(&location.path, async move |proxy, ahc_client_end| {
            let status = await!(
                proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().as_mut().into())
            )?;
            assert_eq!(status, Status::Ok);
            Ok(())
        });
        request_stream_test(&location.path, async move |proxy, ahc_client_end| {
            assert_eq!(
                await!(proxy.load_account(ahc_client_end, TEST_ACCOUNT_ID.clone().as_mut().into()))?,
                Status::Ok
            );
            Ok(())
        });
    }

    #[test]
    fn test_remove_account() {
        let location = TempLocation::new();
        request_stream_test(&location.path, async move |proxy, ahc_client_end| {
            assert_eq!(
                await!(
                    proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().as_mut().into())
                )?,
                Status::Ok
            );

            // Keep an open channel to an account.
            let (account_client_end, account_server_end) = create_endpoints().unwrap();
            let (acp_client_end, _) = create_endpoints().unwrap();
            await!(proxy.get_account(acp_client_end, account_server_end)).unwrap();
            let account_proxy = account_client_end.into_proxy().unwrap();

            // Make sure remove_account() can make progress with an open channel.
            assert_eq!(await!(proxy.remove_account())?, Status::Ok);

            // Make sure that the channel is in fact closed.
            assert!(await!(account_proxy.get_auth_state()).is_err());

            // We cannot remove twice.
            assert_eq!(await!(proxy.remove_account())?, Status::InvalidRequest);
            Ok(())
        });
    }

    #[test]
    fn test_remove_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(&location.path, async move |proxy, _| {
            assert_eq!(await!(proxy.remove_account())?, Status::InvalidRequest);
            Ok(())
        });
    }

    #[test]
    fn test_terminate() {
        let location = TempLocation::new();
        request_stream_test(&location.path, async move |proxy, ahc_client_end| {
            assert_eq!(
                await!(
                    proxy.create_account(ahc_client_end, TEST_ACCOUNT_ID.clone().as_mut().into())
                )?,
                Status::Ok
            );

            // Keep an open channel to an account.
            let (account_client_end, account_server_end) = create_endpoints().unwrap();
            let (acp_client_end, _) = create_endpoints().unwrap();
            await!(proxy.get_account(acp_client_end, account_server_end)).unwrap();
            let account_proxy = account_client_end.into_proxy().unwrap();

            // Terminate the handler
            assert!(proxy.terminate().is_ok());

            // Check that further operations fail
            assert!(await!(proxy.remove_account()).is_err());
            assert!(proxy.terminate().is_err());

            // Make sure that the channel closed too.
            assert!(await!(account_proxy.get_auth_state()).is_err());
            Ok(())
        });
    }

    #[test]
    fn test_load_account_not_found() {
        let location = TempLocation::new();
        request_stream_test(&location.path, async move |proxy, ahc_client_end| {
            assert_eq!(
                await!(proxy.load_account(
                    ahc_client_end,
                    &mut FidlLocalAccountId { id: WRONG_ACCOUNT_ID }
                ))?,
                Status::NotFound
            );
            Ok(())
        });
    }

}
