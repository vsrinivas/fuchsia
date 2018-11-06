// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use account_common::{FidlLocalAccountId, LocalAccountId};
use fidl::encoding::OutOfLine;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl::Error;
use fidl_fuchsia_auth::{AuthState, AuthStateSummary, AuthenticationContextProviderMarker};
use fidl_fuchsia_auth_account::{
    AccountAuthState, AccountListenerMarker, AccountListenerOptions, AccountManagerRequest,
    AccountManagerRequestStream, AccountMarker, Status,
};
use futures::prelude::*;
use log::info;
use parking_lot::Mutex;
use std::collections::BTreeSet;

/// The core component of the account system for Fuchsia.
///
/// The AccountManager maintains the set of Fuchsia accounts that are provisioned on the device,
/// launches and configures AuthenticationProvider components to perform authentication via
/// service providers, and launches and delegates to AccountHandler component instances to
/// determine the detailed state and authentication for each account.
pub struct AccountManager {
    /// (Temporary) The next unused local account identifier.
    // TODO(jsankey): Replace this temporary sequential ID assignment with randomness.
    next_id: Mutex<u64>,

    /// An ordered set of LocalAccountIds for accounts provisioned on the current device.
    ids: Mutex<BTreeSet<LocalAccountId>>,
}

impl AccountManager {
    /// (Temporary) A fixed AuthState that is used for all accounts until authenticators are
    /// available.
    const DEFAULT_AUTH_STATE: AuthState = AuthState {
        summary: AuthStateSummary::Unknown,
    };

    /// Constructs a new AccountManager with no accounts.
    pub fn new() -> AccountManager {
        AccountManager {
            next_id: Mutex::new(1),
            ids: Mutex::new(BTreeSet::new()),
        }
    }

    /// Asynchronously handles the supplied stream of `AccountManagerRequest` messages.
    pub async fn handle_requests_from_stream(
        &self, mut stream: AccountManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            self.handle_request(req)?;
        }
        Ok(())
    }

    /// Handles a single request to the AccountManager.
    pub fn handle_request(&self, req: AccountManagerRequest) -> Result<(), Error> {
        match req {
            AccountManagerRequest::GetAccountIds { responder } => {
                responder.send(&mut self.get_account_ids().iter_mut())
            }
            AccountManagerRequest::GetAccountAuthStates { responder } => {
                let mut response = self.get_account_auth_states();
                responder.send(response.0, &mut response.1.iter_mut())
            }
            AccountManagerRequest::GetAccount {
                id,
                auth_context_provider,
                account,
                responder,
            } => responder.send(self.get_account(id.into(), auth_context_provider, account)),
            AccountManagerRequest::RegisterAccountListener {
                listener,
                options,
                responder,
            } => responder.send(self.register_account_listener(listener, options)),
            AccountManagerRequest::RemoveAccount { id, responder } => {
                responder.send(self.remove_account(id.into()))
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
                let mut response = self.provision_new_account();
                responder.send(response.0, response.1.as_mut().map(OutOfLine))
            }
        }
    }

    fn get_account_ids(&self) -> Vec<FidlLocalAccountId> {
        self.ids.lock().iter().map(|id| id.clone().into()).collect()
    }

    fn get_account_auth_states(&self) -> (Status, Vec<AccountAuthState>) {
        // TODO(jsankey): Collect authentication state from AccountHandler instances
        // rather than returning a fixed value.
        (
            Status::Ok,
            self.ids
                .lock()
                .iter()
                .map(|id| AccountAuthState {
                    account_id: id.clone().into(),
                    auth_state: Self::DEFAULT_AUTH_STATE,
                })
                .collect(),
        )
    }

    fn get_account(
        &self, _id: LocalAccountId,
        _auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        _account: ServerEnd<AccountMarker>,
    ) -> Status {
        // TODO(jsankey): Implement this method
        Status::InternalError
    }

    fn register_account_listener(
        &self, _listener: ClientEnd<AccountListenerMarker>, _options: AccountListenerOptions,
    ) -> Status {
        // TODO(jsankey): Implement this method
        Status::InternalError
    }

    fn remove_account(&self, id: LocalAccountId) -> Status {
        let mut ids_lock = self.ids.lock();
        if ids_lock.contains(&id) {
            ids_lock.remove(&id);
            info!("Removing account {:?}", id);
            // TODO(jsankey): Persist the change in installed accounts.
            Status::Ok
        } else {
            Status::NotFound
        }
    }

    fn provision_new_account(&self) -> (Status, Option<FidlLocalAccountId>) {
        let mut next_id_lock = self.next_id.lock();
        let new_id = LocalAccountId::new(*next_id_lock);
        self.ids.lock().insert(new_id.clone());
        *next_id_lock += 1;
        info!("Adding new local account {:?}", new_id);
        // TODO(jsankey): Persist the change in installed accounts.
        (Status::Ok, Some(new_id.into()))
    }

    fn provision_from_auth_provider(
        &self, _auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
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

        executor
            .run_singlethreaded(test_fn(proxy))
            .expect("Executor run failed.")
    }

    fn create_test_object(next_id: u64, existing_ids: Vec<u64>) -> AccountManager {
        AccountManager {
            next_id: Mutex::new(next_id),
            ids: Mutex::new(existing_ids.into_iter().map(LocalAccountId::new).collect()),
        }
    }

    fn fidl_local_id_vec(ints: Vec<u64>) -> Vec<FidlLocalAccountId> {
        ints.into_iter()
            .map(|i| FidlLocalAccountId { id: i })
            .collect()
    }

    fn fidl_local_id_box(i: u64) -> Box<FidlLocalAccountId> {
        Box::new(FidlLocalAccountId { id: i })
    }

    #[test]
    fn test_initially_empty() {
        request_stream_test(AccountManager::new(), async move |proxy| {
            assert_eq!(await!(proxy.get_account_ids())?, vec![]);
            assert_eq!(
                await!(proxy.get_account_auth_states())?,
                (Status::Ok, vec![])
            );
            Ok(())
        });
    }

    #[test]
    fn test_provision_new_account() {
        request_stream_test(AccountManager::new(), async move |proxy| {
            // Add two accounts.
            assert_eq!(
                await!(proxy.provision_new_account())?,
                (Status::Ok, Some(fidl_local_id_box(1)))
            );
            assert_eq!(
                await!(proxy.provision_new_account())?,
                (Status::Ok, Some(fidl_local_id_box(2)))
            );

            // Verify both are visible in the ID and AuthState getters.
            assert_eq!(
                await!(proxy.get_account_ids())?,
                fidl_local_id_vec(vec![1, 2])
            );

            let expected_account_auth_states = vec![1, 2]
                .into_iter()
                .map(|id| AccountAuthState {
                    account_id: FidlLocalAccountId { id },
                    auth_state: AccountManager::DEFAULT_AUTH_STATE,
                })
                .collect();
            assert_eq!(
                await!(proxy.get_account_auth_states())?,
                (Status::Ok, expected_account_auth_states)
            );
            Ok(())
        });
    }

    #[test]
    fn test_remove_missing_account() {
        request_stream_test(
            // Manually create an account manager with one account.
            create_test_object(2, vec![1]),
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
            create_test_object(3, vec![1, 2]),
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
