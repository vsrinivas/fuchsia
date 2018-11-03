// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::{FidlLocalAccountId, LocalAccountId};
use crate::account::Account;
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AuthState, AuthStateSummary, AuthenticationContextProviderMarker};
use fidl_fuchsia_auth_account::{AccountMarker, Status};
use fidl_fuchsia_auth_account_internal::{
    AccountHandlerControlRequest, AccountHandlerControlRequestStream,
};
use fuchsia_async as fasync;
use futures::prelude::*;
use log::{error, info, warn};
use parking_lot::RwLock;
use std::sync::Arc;

/// The core state of the AccountHandler, i.e. the Account (once it is known) and references to
/// the execution context and a TokenManager.
pub struct AccountHandler {
    // An optional `Account` that we are handling.
    //
    // This will be None until a particular Account is established over the control channel. Once
    // set, the account will never be cleared or modified.
    account: RwLock<Option<Arc<Account>>>,
    // TODO(jsankey): Add TokenManager and AccountHandlerContext.
}

impl AccountHandler {
    /// (Temporary) A fixed AuthState that is used for all accounts until authenticators are
    /// available.
    pub const DEFAULT_AUTH_STATE: AuthState = AuthState {
        summary: AuthStateSummary::Unknown,
    };

    /// Constructs a new AccountHandler.
    pub fn new() -> AccountHandler {
        Self {
            account: RwLock::new(None),
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
            AccountHandlerControlRequest::CreateAccount { responder } => {
                let response = self.create_account();
                responder.send(
                    response.0,
                    response
                        .1
                        .map(FidlLocalAccountId::from)
                        .as_mut()
                        .map(OutOfLine),
                )?;
            }
            AccountHandlerControlRequest::LoadAccount { id, responder } => {
                let response = self.load_account(id.into());
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

    fn create_account(&self) -> (Status, Option<LocalAccountId>) {
        let mut account_lock = self.account.write();
        if account_lock.is_some() {
            warn!("AccountHandler is already initialized");
            (Status::InvalidRequest, None)
        } else {
            // TODO(jsankey): Longer term, local ID may need to be related to the global ID rather
            // than just a random number.
            let local_account_id = LocalAccountId::new(rand::random::<u64>());
            *account_lock = Some(Arc::new(Account::new(local_account_id.clone())));
            // TODO(jsankey): Persist the account to disk.
            info!("Created new Fuchsia account");
            (Status::Ok, Some(local_account_id))
        }
    }

    fn load_account(&self, _id: LocalAccountId) -> Status {
        // TODO(jsankey): Implement this method once accounts are persisted on disk.
        warn!("LoadAccount method not yet implemented");
        Status::InternalError
    }

    fn get_account(
        &self, _auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        account_server_end: ServerEnd<AccountMarker>,
    ) -> Status {
        let account = if let Some(account) = &*self.account.read() {
            Arc::clone(account)
        } else {
            warn!("AccountHandler not yet initialized");
            return Status::NotFound;
        };

        let stream = match account_server_end.into_stream() {
            Ok(stream) => stream,
            Err(e) => {
                error!("Error opening Account channel {:?}", e);
                return Status::IoError;
            }
        };

        fasync::spawn(
            async move {
                await!(account.handle_requests_from_stream(stream))
                    .unwrap_or_else(|e| error!("Error handling Account channel {:?}", e))
            },
        );
        Status::Ok
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::RequestStream;
    use fidl_fuchsia_auth_account::AccountProxy;
    use fidl_fuchsia_auth_account_internal::{
        AccountHandlerControlProxy, AccountHandlerControlRequestStream,
    };
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    const TEST_ACCOUNT_ID: u64 = 111111;

    fn request_stream_test<TestFn, Fut>(test_object: AccountHandler, test_fn: TestFn)
    where
        TestFn: FnOnce(AccountHandlerControlProxy) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().expect("Failed to create executor");
        let (server_chan, client_chan) = zx::Channel::create().expect("Failed to create channel");
        let proxy =
            AccountHandlerControlProxy::new(fasync::Channel::from_channel(client_chan).unwrap());
        let request_stream = AccountHandlerControlRequestStream::from_channel(
            fasync::Channel::from_channel(server_chan).unwrap(),
        );

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
        request_stream_test(AccountHandler::new(), async move |proxy| {
            let (account_server_chan, _) = zx::Channel::create().unwrap();
            let (_, acp_client_chan) = zx::Channel::create().unwrap();
            assert_eq!(
                await!(proxy.get_account(
                    ClientEnd::new(acp_client_chan),
                    ServerEnd::new(account_server_chan)
                ))?,
                Status::NotFound
            );
            Ok(())
        });
    }

    #[test]
    fn test_double_initialize() {
        request_stream_test(AccountHandler::new(), async move |proxy| {
            let (status, account_id_optional) = await!(proxy.create_account())?;
            assert_eq!(status, Status::Ok);
            assert!(account_id_optional.is_some());
            assert_eq!(
                await!(proxy.create_account())?,
                (Status::InvalidRequest, None)
            );
            Ok(())
        });
    }

    #[test]
    fn test_create_and_get_account() {
        request_stream_test(AccountHandler::new(), async move |account_handler_proxy| {
            let (status, account_id_optional) = await!(account_handler_proxy.create_account())?;
            assert_eq!(status, Status::Ok);
            assert!(account_id_optional.is_some());

            let (account_server_chan, account_client_chan) = zx::Channel::create().unwrap();
            let (_, acp_client_chan) = zx::Channel::create().unwrap();
            assert_eq!(
                await!(account_handler_proxy.get_account(
                    ClientEnd::new(acp_client_chan),
                    ServerEnd::new(account_server_chan)
                ))?,
                Status::Ok
            );

            // The account channel should now be usable.
            let account_proxy =
                AccountProxy::new(fasync::Channel::from_channel(account_client_chan).unwrap());
            assert_eq!(
                await!(account_proxy.get_auth_state())?,
                (
                    Status::Ok,
                    Some(Box::new(AccountHandler::DEFAULT_AUTH_STATE))
                )
            );
            Ok(())
        });
    }

    #[test]
    fn test_load_account() {
        request_stream_test(AccountHandler::new(), async move |proxy| {
            assert_eq!(
                await!(proxy.load_account(&mut FidlLocalAccountId {
                    id: TEST_ACCOUNT_ID
                }))?,
                Status::InternalError
            );
            Ok(())
        });
    }

}
