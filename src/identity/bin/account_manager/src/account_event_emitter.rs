// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation for the event emitting client end of the AccountListener FIDL interface,
//! sending events to listeners, optionally configured with filters, about changes in the accounts
//! presence and states during their lifetime.

use {
    crate::inspect,
    account_common::AccountId,
    fidl::endpoints::Proxy,
    fidl_fuchsia_identity_account::{
        AccountAuthState, AccountListenerProxy, AccountManagerRegisterAccountListenerRequest,
        AuthState, AuthStateSummary,
    },
    fuchsia_inspect::{Node, NumericProperty, Property},
    futures::{future::*, lock::Mutex},
    std::{convert::TryFrom, pin::Pin},
};

// TODO(jsankey): Add a mechanism to publicize auth state changes rather than this fixed minimum
// possible value.
pub const MINIMUM_AUTH_STATE: AuthState =
    AuthState { summary: Some(AuthStateSummary::StorageLocked), ..AuthState::EMPTY };

/// Events emitted on account listeners
pub enum AccountEvent {
    /// AccountAdded is emitted after an account has been added.
    AccountAdded(AccountId),

    /// AccountRemoved is emitted after an account has been removed.
    AccountRemoved(AccountId),
}

/// The set of conditions under which events should be emitted.
pub struct Options {
    initial_state: bool,
    add_account: bool,
    remove_account: bool,
}

impl TryFrom<AccountManagerRegisterAccountListenerRequest> for Options {
    type Error = fidl_fuchsia_identity_account::Error;

    fn try_from(
        request: AccountManagerRegisterAccountListenerRequest,
    ) -> Result<Self, fidl_fuchsia_identity_account::Error> {
        // Auth state is not yet supported, return an error if its requested.
        if request.granularity.map(|g| g.summary_changes).is_some() {
            return Err(fidl_fuchsia_identity_account::Error::InvalidRequest);
        }
        Ok(Self {
            initial_state: request.initial_state.unwrap_or(false),
            add_account: request.add_account.unwrap_or(false),
            remove_account: request.remove_account.unwrap_or(false),
        })
    }
}

/// The client end of an account listener.
struct Client {
    listener: AccountListenerProxy,
    options: Options,
}

impl Client {
    /// Create a new client, given the listener's client end and options used for filtering.
    fn new(listener: AccountListenerProxy, options: Options) -> Self {
        Self { listener, options }
    }

    fn should_send(&self, event: &AccountEvent) -> bool {
        match event {
            AccountEvent::AccountAdded(_) => self.options.add_account,
            AccountEvent::AccountRemoved(_) => self.options.remove_account,
        }
    }

    fn send<'a>(
        &'a self,
        event: &'a AccountEvent,
    ) -> impl Future<Output = Result<(), fidl::Error>> {
        match event {
            AccountEvent::AccountAdded(id) => {
                let mut account_state = AccountAuthState {
                    account_id: id.clone().into(),
                    auth_state: MINIMUM_AUTH_STATE,
                };
                self.listener.on_account_added(&mut account_state)
            }
            AccountEvent::AccountRemoved(id) => self.listener.on_account_removed(id.clone().into()),
        }
    }

    /// Emit a given event on the channel if it passes the filter in options. Returns a future
    /// which will return once the listening end has responded.
    fn possibly_send<'a>(
        &'a self,
        event: &'a AccountEvent,
    ) -> impl Future<Output = Result<(), fidl::Error>> {
        if self.should_send(event) {
            FutureObj::new(Box::pin(self.send(event)))
        } else {
            FutureObj::new(Box::pin(ok(())))
        }
    }
}

/// A type to maintain the set of account listener clients and distribute events to them.
pub struct AccountEventEmitter {
    /// Collection of account listener clients.
    clients: Mutex<Vec<Client>>,

    /// Helper for outputting listener information via fuchsia_inspect.
    inspect: inspect::Listeners,
}

impl AccountEventEmitter {
    /// Create a new emitter with no clients.
    pub fn new(parent: &Node) -> Self {
        let inspect = inspect::Listeners::new(parent);
        Self { clients: Mutex::new(Vec::new()), inspect }
    }

    /// Send an event to all active listeners filtered by their respective options. Awaits until
    /// all messages have been confirmed by the servers.
    pub async fn publish<'a>(&'a self, event: &'a AccountEvent) {
        let mut clients_lock = self.clients.lock().await;
        clients_lock.retain(|client| !client.listener.is_closed());
        self.inspect.active.set(clients_lock.len() as u64);
        let futures = (*clients_lock)
            .iter()
            .map(|client| client.possibly_send(event))
            .map(|fut| Pin::<Box<_>>::from(Box::new(fut)));
        let all_futures = join_all(futures);
        std::mem::drop(clients_lock);
        all_futures.await;
        self.inspect.events.add(1);
    }

    /// Add a new listener to the collection.
    pub async fn add_listener<'a>(
        &'a self,
        listener: AccountListenerProxy,
        options: Options,
        initial_account_ids: &'a [AccountId],
    ) -> Result<(), fidl::Error> {
        let mut clients_lock = self.clients.lock().await;
        let future = if options.initial_state {
            let mut v = initial_account_ids
                .iter()
                .map(|id| AccountAuthState {
                    account_id: id.clone().into(),
                    auth_state: MINIMUM_AUTH_STATE,
                })
                .collect::<Vec<_>>();
            FutureObj::new(Box::pin(listener.on_initialize(&mut v.iter_mut())))
        } else {
            FutureObj::new(Box::pin(ok(())))
        };
        clients_lock.push(Client::new(listener, options));
        self.inspect.total_opened.add(1_u64);
        self.inspect.active.set(clients_lock.len() as u64);
        std::mem::drop(clients_lock);
        future.await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::*;
    use fidl_fuchsia_identity_account::{AccountListenerMarker, AccountListenerRequest};
    use fuchsia_inspect::{assert_data_tree, Inspector};
    use futures::prelude::*;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref ACCOUNT_ID_ADD: AccountId = AccountId::new(1);
        static ref ACCOUNT_ID_REMOVE: AccountId = AccountId::new(2);
        static ref TEST_ACCOUNT_ID: AccountId = AccountId::new(3);
        static ref EVENT_ADDED: AccountEvent = AccountEvent::AccountAdded(ACCOUNT_ID_ADD.clone());
        static ref EVENT_REMOVED: AccountEvent =
            AccountEvent::AccountRemoved(ACCOUNT_ID_REMOVE.clone());
        static ref TEST_ACCOUNT_IDS: Vec<AccountId> = vec![TEST_ACCOUNT_ID.clone()];
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_all() {
        let options = Options { initial_state: true, add_account: true, remove_account: true };
        let (listener, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(listener, options);
        assert!(client.should_send(&EVENT_ADDED));
        assert!(client.should_send(&EVENT_REMOVED));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_none() {
        let options = Options { initial_state: false, add_account: false, remove_account: false };
        let (proxy, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(proxy, options);
        assert!(!client.should_send(&EVENT_ADDED));
        assert!(!client.should_send(&EVENT_REMOVED));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_some() {
        let options = Options { initial_state: true, add_account: false, remove_account: true };
        let (proxy, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(proxy, options);
        assert!(!client.should_send(&EVENT_ADDED));
        assert!(client.should_send(&EVENT_REMOVED));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_single_listener() {
        let options = Options { initial_state: false, add_account: true, remove_account: false };
        let (client_end, mut stream) = create_request_stream::<AccountListenerMarker>().unwrap();
        let client = Client::new(client_end.into_proxy().unwrap(), options);

        // Expect only the AccountAdded event, the filter skips the AccountRemoved event
        let serve_fut = async move {
            let request = stream.try_next().await.unwrap();
            if let Some(AccountListenerRequest::OnAccountAdded { account_state, responder }) =
                request
            {
                assert_eq!(AccountId::from(account_state.account_id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if stream.try_next().await.unwrap().is_some() {
                panic!("Unexpected message, channel should be closed");
            }
        };
        let request_fut = async move {
            assert!(client.possibly_send(&EVENT_ADDED).await.is_ok());
            assert!(client.possibly_send(&EVENT_REMOVED).await.is_ok());
        };
        join(serve_fut, request_fut).await;
    }

    /// Given two independent clients with different options/filters, send events and check
    /// that the clients receive the expected messages.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_event_emitter() {
        let options_1 = Options { initial_state: false, add_account: true, remove_account: false };
        let options_2 = Options { initial_state: true, add_account: false, remove_account: true };
        let (client_end_1, mut stream_1) =
            create_request_stream::<AccountListenerMarker>().unwrap();
        let (client_end_2, mut stream_2) =
            create_request_stream::<AccountListenerMarker>().unwrap();
        let listener_1 = client_end_1.into_proxy().unwrap();
        let listener_2 = client_end_2.into_proxy().unwrap();
        let inspector = Inspector::new();
        let account_event_emitter = AccountEventEmitter::new(inspector.root());

        let serve_fut_1 = async move {
            let request = stream_1.try_next().await.unwrap();
            if let Some(AccountListenerRequest::OnAccountAdded { account_state, responder }) =
                request
            {
                assert_eq!(AccountId::from(account_state.account_id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if stream_1.try_next().await.unwrap().is_some() {
                panic!("Unexpected message, channel should be closed");
            }
        };

        let serve_fut_2 = async move {
            let request = stream_2.try_next().await.unwrap();
            if let Some(AccountListenerRequest::OnInitialize { account_states, responder }) =
                request
            {
                assert_eq!(
                    account_states,
                    vec![AccountAuthState {
                        account_id: TEST_ACCOUNT_ID.clone().into(),
                        auth_state: MINIMUM_AUTH_STATE
                    }]
                );
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            let request = stream_2.try_next().await.unwrap();
            if let Some(AccountListenerRequest::OnAccountRemoved { account_id, responder }) =
                request
            {
                assert_eq!(AccountId::from(account_id), ACCOUNT_ID_REMOVE.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if stream_2.try_next().await.unwrap().is_some() {
                panic!("Unexpected message, channel should be closed");
            }
        };

        let request_fut = async move {
            assert_data_tree!(inspector, root : { listeners: contains {
                active: 0_u64,
                total_opened: 0_u64,
            }});
            assert!(account_event_emitter
                .add_listener(listener_1, options_1, &TEST_ACCOUNT_IDS)
                .await
                .is_ok());
            assert_data_tree!(inspector, root : { listeners: contains {
                active: 1_u64,
                total_opened: 1_u64,
            }});
            assert!(account_event_emitter
                .add_listener(listener_2, options_2, &TEST_ACCOUNT_IDS)
                .await
                .is_ok());
            assert_data_tree!(inspector, root : { listeners: {
                active: 2_u64,
                total_opened: 2_u64,
                events: 0_u64,
            }});

            account_event_emitter.publish(&EVENT_ADDED).await;
            assert_data_tree!(inspector, root : { listeners: contains { events: 1_u64 }});
            account_event_emitter.publish(&EVENT_REMOVED).await;
            assert_data_tree!(inspector, root : { listeners: contains { events: 2_u64 }});
        };
        join3(serve_fut_1, serve_fut_2, request_fut).await;
    }

    /// Check that that stale clients are cleaned up, once the server is closed
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_cleanup_stale_clients() {
        let options = Options { initial_state: false, add_account: true, remove_account: true };
        let (client_end, mut stream) = create_request_stream::<AccountListenerMarker>().unwrap();
        let listener = client_end.into_proxy().unwrap();
        let inspector = Inspector::new();
        let account_event_emitter = AccountEventEmitter::new(inspector.root());
        assert!(account_event_emitter
            .add_listener(listener, options, &TEST_ACCOUNT_IDS)
            .await
            .is_ok());

        let serve_fut = async move {
            let request = stream.try_next().await.unwrap();
            if let Some(AccountListenerRequest::OnAccountAdded { account_state, responder }) =
                request
            {
                assert_eq!(AccountId::from(account_state.account_id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
        };

        let request_fut = async move {
            account_event_emitter.publish(&EVENT_ADDED).await; // Normal event
            assert_data_tree!(inspector, root : { listeners: {
                active: 1_u64, // Listener remains
                total_opened: 1_u64,
                events: 1_u64,
            }});
            (account_event_emitter, inspector)
        };
        let (_, (account_event_emitter, inspector)) = join(serve_fut, request_fut).await;

        // Now the server is dropped, so the new publish should trigger a drop of the client
        account_event_emitter.publish(&EVENT_REMOVED).await;
        assert_data_tree!(inspector, root : { listeners: {
            active: 0_u64, // Listener dropped
            total_opened: 1_u64,
            events: 2_u64,
        }});
    }
}
