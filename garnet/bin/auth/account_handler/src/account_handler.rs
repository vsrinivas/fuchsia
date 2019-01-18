// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::account::{Account, AccountContext};
use account_common::{FidlLocalAccountId, LocalAccountId};
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthState, AuthStateSummary, AuthenticationContextProviderMarker};
use fidl_fuchsia_auth_account::{AccountMarker, Status};
use fidl_fuchsia_auth_account_internal::{
    AccountHandlerContextMarker, AccountHandlerControlRequest, AccountHandlerControlRequestStream,
};
use fuchsia_async as fasync;
use futures::prelude::*;
use log::{error, info, warn};
use parking_lot::RwLock;
use std::fs;
use std::path::PathBuf;
use std::sync::Arc;

/// The core state of the AccountHandler, i.e. the Account (once it is known) and references to
/// the execution context and a TokenManager.
pub struct AccountHandler {
    // An optional `Account` that we are handling.
    //
    // This will be None until a particular Account is established over the control channel. Once
    // set, the account will never be cleared or modified.
    account: RwLock<Option<Arc<Account>>>,
    accounts_dir: PathBuf,
    // TODO(jsankey): Add TokenManager and AccountHandlerContext.
}

impl AccountHandler {
    /// (Temporary) A fixed AuthState that is used for all accounts until authenticators are
    /// available.
    pub const DEFAULT_AUTH_STATE: AuthState = AuthState {
        summary: AuthStateSummary::Unknown,
    };

    /// Constructs a new AccountHandler.
    pub fn new(accounts_dir: PathBuf) -> AccountHandler {
        Self {
            account: RwLock::new(None),
            accounts_dir,
        }
    }

    /// Asynchronously handles the supplied stream of `AccountHandlerControlRequest` messages.
    pub async fn handle_requests_from_stream(
        &self, mut stream: AccountHandlerControlRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            self.handle_request(req)?;
        }
        Ok(())
    }

    /// Dispatches an `AccountHandlerControlRequest` message to the appropriate handler method
    /// based on its type.
    pub fn handle_request(&self, req: AccountHandlerControlRequest) -> Result<(), fidl::Error> {
        match req {
            AccountHandlerControlRequest::CreateAccount { context, responder } => {
                let response = self.create_account(context);
                responder.send(
                    response.0,
                    response
                        .1
                        .map(FidlLocalAccountId::from)
                        .as_mut()
                        .map(OutOfLine),
                )?;
            }
            AccountHandlerControlRequest::LoadAccount {
                context,
                id,
                responder,
            } => {
                let response = self.load_account(id.into(), context);
                responder.send(response)?;
            }
            AccountHandlerControlRequest::RemoveAccount { responder } => {
                let response = self.remove_account();
                responder.send(response)?;
            }
            AccountHandlerControlRequest::GetAccount {
                auth_context_provider,
                account,
                responder,
            } => {
                let response = self.get_account(auth_context_provider, account);
                responder.send(response)?;
            }
            AccountHandlerControlRequest::Terminate { control_handle } => {
                // TODO(jsankey): Close any open files once we have them and shutdown dependant
                // channels on the account, personae, and token manager.
                info!("Gracefully shutting down AccountHandler");
                control_handle.shutdown();
            }
        }
        Ok(())
    }

    fn create_account(
        &self, context: ClientEnd<AccountHandlerContextMarker>,
    ) -> (Status, Option<LocalAccountId>) {
        let mut account_lock = self.account.write();
        if account_lock.is_some() {
            warn!("AccountHandler is already initialized");
            (Status::InvalidRequest, None)
        } else {
            // TODO(jsankey): Longer term, local ID may need to be related to the global ID rather
            // than just a random number.
            let local_account_id = LocalAccountId::new(rand::random::<u64>());

            // First create the directory to contain the new account
            let account_dir = self.account_dir(&local_account_id);

            // Construct an Account value to maintain state inside this directory
            let account = match Account::create(local_account_id.clone(), &account_dir, context) {
                Ok(account) => account,
                Err(err) => {
                    warn!("Failed to initialize new Account: {:?}", err);
                    if let Err(err) = fs::remove_dir(&account_dir) {
                        warn!("and failed to remove redundant dir: {:?}", err);
                    }
                    return (err.status, None);
                }
            };
            *account_lock = Some(Arc::new(account));

            info!("Created new Fuchsia account");
            (Status::Ok, Some(local_account_id))
        }
    }

    fn load_account(
        &self, id: LocalAccountId, context: ClientEnd<AccountHandlerContextMarker>,
    ) -> Status {
        let mut account_lock = self.account.write();
        if account_lock.is_some() {
            warn!("AccountHandler is already initialized");
            Status::InvalidRequest
        } else {
            let account_dir = self.account_dir(&id);
            if account_dir.exists() {
                let account = match Account::load(id.clone(), &account_dir, context) {
                    Ok(account) => account,
                    Err(err) => return err.status,
                };
                *account_lock = Some(Arc::new(account));
                Status::Ok
            } else {
                Status::NotFound
            }
        }
    }

    fn remove_account(&self) -> Status {
        let mut account_lock = self.account.write();
        let account = match &*account_lock {
            Some(account) => account,
            None => {
                warn!("No account is initialized or it has already been removed");
                return Status::InvalidRequest;
            }
        };
        let account_dir = self.account_dir(&account.id());
        match fs::remove_dir_all(account_dir) {
            Err(err) => {
                warn!("Could not remove account dir: {:?}", err);
                Status::IoError
            }
            Ok(()) => {
                info!("Deleted Fuchsia account {:?}", &account.id());
                *account_lock = None;
                Status::Ok
            }
        }
    }

    fn get_account(
        &self, auth_context_provider_client_end: ClientEnd<AuthenticationContextProviderMarker>,
        account_server_end: ServerEnd<AccountMarker>,
    ) -> Status {
        let account = if let Some(account) = &*self.account.read() {
            Arc::clone(account)
        } else {
            warn!("AccountHandler not yet initialized");
            return Status::NotFound;
        };

        let context = match auth_context_provider_client_end.into_proxy() {
            Ok(acp) => AccountContext {
                auth_ui_context_provider: acp,
            },
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

        fasync::spawn(
            async move {
                await!(account.handle_requests_from_stream(&context, stream))
                    .unwrap_or_else(|e| error!("Error handling Account channel {:?}", e))
            },
        );
        Status::Ok
    }

    /// Returns the directory that should be used for the specified LocalAccountId
    fn account_dir(&self, account_id: &LocalAccountId) -> PathBuf {
        self.accounts_dir.join(account_id.to_canonical_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_util::*;
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_auth_account_internal::{
        AccountHandlerControlMarker, AccountHandlerControlProxy,
    };
    use fuchsia_async as fasync;
    use parking_lot::Mutex;
    use std::sync::Arc;

    // Will not match a randomly generated account id with high probability.
    const WRONG_ACCOUNT_ID: u64 = 111111;

    fn request_stream_test<TestFn, Fut>(test_object: AccountHandler, test_fn: TestFn)
    where
        TestFn: FnOnce(AccountHandlerControlProxy) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");

        let (client_end, server_end) = create_endpoints::<AccountHandlerControlMarker>().unwrap();
        let proxy = client_end.into_proxy().unwrap();
        let request_stream = server_end.into_stream().unwrap();

        fasync::spawn(
            async move {
                await!(test_object.handle_requests_from_stream(request_stream))
                    .unwrap_or_else(|err| panic!("Fatal error handling test request: {:?}", err))
            },
        );

        executor
            .run_singlethreaded(test_fn(proxy))
            .expect("Executor run failed.")
    }

    #[test]
    fn test_get_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(AccountHandler::new(location.path), async move |proxy| {
            let (_, account_server_end) = create_endpoints().unwrap();
            let (ahc_client_end, _) = create_endpoints().unwrap();
            assert_eq!(
                await!(proxy.get_account(ahc_client_end, account_server_end))?,
                Status::NotFound
            );
            Ok(())
        });
    }

    #[test]
    fn test_double_initialize() {
        let location = TempLocation::new();
        request_stream_test(AccountHandler::new(location.path), async move |proxy| {
            let (ahc_client_end_1, _) = create_endpoints::<AccountHandlerContextMarker>().unwrap();
            let (status, account_id_optional) = await!(proxy.create_account(ahc_client_end_1))?;
            assert_eq!(status, Status::Ok);
            assert!(account_id_optional.is_some());

            let (ahc_client_end_2, _) = create_endpoints::<AccountHandlerContextMarker>().unwrap();
            assert_eq!(
                await!(proxy.create_account(ahc_client_end_2))?,
                (Status::InvalidRequest, None)
            );
            Ok(())
        });
    }

    #[test]
    fn test_create_and_get_account() {
        let location = TempLocation::new();
        request_stream_test(
            AccountHandler::new(location.path),
            async move |account_handler_proxy| {
                let (ahc_client_end, _) =
                    create_endpoints::<AccountHandlerContextMarker>().unwrap();
                let (status, account_id_optional) =
                    await!(account_handler_proxy.create_account(ahc_client_end))?;
                assert_eq!(status, Status::Ok);
                assert!(account_id_optional.is_some());

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
                    (
                        Status::Ok,
                        Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE))
                    )
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_create_and_load_account() {
        // Check that an account is persisted when account handlers are restarted
        let location = TempLocation::new();
        let acc_id_holder: Arc<Mutex<Option<FidlLocalAccountId>>> = Arc::new(Mutex::new(None));
        let acc_id_holder_clone = Arc::clone(&acc_id_holder);
        request_stream_test(
            AccountHandler::new(location.path.clone()),
            async move |proxy| {
                let (ahc_client_end, _) =
                    create_endpoints::<AccountHandlerContextMarker>().unwrap();
                let (status, account_id_optional) = await!(proxy.create_account(ahc_client_end))?;
                assert_eq!(status, Status::Ok);
                *acc_id_holder_clone.lock() = account_id_optional.map(|x| *x);
                Ok(())
            },
        );
        request_stream_test(AccountHandler::new(location.path), async move |proxy| {
            let (ahc_client_end, _) = create_endpoints::<AccountHandlerContextMarker>().unwrap();
            match acc_id_holder.lock().as_mut() {
                Some(mut acc_id) => {
                    assert_eq!(
                        await!(proxy.load_account(ahc_client_end, &mut acc_id))?,
                        Status::Ok
                    );
                }
                None => panic!("Create account did not return a valid account id to get"),
            }
            Ok(())
        });
    }

    #[test]
    fn test_create_and_remove_account() {
        let location = TempLocation::new();
        request_stream_test(
            AccountHandler::new(location.path.clone()),
            async move |proxy| {
                let (ahc_client_end, _) =
                    create_endpoints::<AccountHandlerContextMarker>().unwrap();
                let (status, account_id_optional) = await!(proxy.create_account(ahc_client_end))?;
                assert_eq!(status, Status::Ok);
                assert!(account_id_optional.is_some());
                let account_path = location
                    .path
                    .join(account_id_optional.unwrap().id.to_string());
                assert!(account_path.is_dir());
                assert_eq!(await!(proxy.remove_account())?, Status::Ok);
                assert_eq!(account_path.exists(), false);
                Ok(())
            },
        );
    }

    #[test]
    fn test_remove_account_before_initialization() {
        let location = TempLocation::new();
        request_stream_test(AccountHandler::new(location.path), async move |proxy| {
            assert_eq!(await!(proxy.remove_account())?, Status::InvalidRequest);
            Ok(())
        });
    }

    #[test]
    fn test_create_and_remove_account_twice() {
        let location = TempLocation::new();
        request_stream_test(
            AccountHandler::new(location.path.clone()),
            async move |proxy| {
                let (ahc_client_end, _) =
                    create_endpoints::<AccountHandlerContextMarker>().unwrap();
                let (status, account_id_optional) = await!(proxy.create_account(ahc_client_end))?;
                assert_eq!(status, Status::Ok);
                assert!(account_id_optional.is_some());
                assert_eq!(await!(proxy.remove_account())?, Status::Ok);
                assert_eq!(
                    await!(proxy.remove_account())?,
                    Status::InvalidRequest // You can only remove once
                );
                Ok(())
            },
        );
    }

    #[test]
    fn test_load_account_not_found() {
        let location = TempLocation::new();
        request_stream_test(AccountHandler::new(location.path), async move |proxy| {
            let (ahc_client_end, _) = create_endpoints::<AccountHandlerContextMarker>().unwrap();
            assert_eq!(
                await!(proxy.load_account(
                    ahc_client_end,
                    &mut FidlLocalAccountId {
                        id: WRONG_ACCOUNT_ID
                    }
                ))?,
                Status::NotFound
            );
            Ok(())
        });
    }

}
