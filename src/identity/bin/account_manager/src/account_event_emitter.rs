// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation for the event emitting client end of the AccountListener FIDL interface,
//! sending events to listeners, optionally configured with filters, about changes in the accounts
//! presence and states during their lifetime.

use account_common::LocalAccountId;
use fidl::endpoints::Proxy;
use fidl_fuchsia_identity_account::{
    AccountListenerOptions, AccountListenerProxy, InitialAccountState,
};
use fuchsia_inspect::{Node, NumericProperty, Property};
use futures::future::*;
use futures::lock::Mutex;
use std::pin::Pin;

use crate::inspect;

// TODO(dnordstrom): Add a mechanism to publicize auth state changes.

/// Events emitted on account listeners
pub enum AccountEvent {
    /// AccountAdded is emitted after an account has been added.
    AccountAdded(LocalAccountId),

    /// AccountRemoved is emitted after an account has been removed.
    AccountRemoved(LocalAccountId),
}

/// The client end of an account listener.
struct Client {
    listener: AccountListenerProxy,
    options: AccountListenerOptions,
}

impl Client {
    /// Create a new client, given the listener's client end and options used for filtering.
    fn new(listener: AccountListenerProxy, options: AccountListenerOptions) -> Self {
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
                let mut account_state =
                    InitialAccountState { account_id: id.clone().into(), auth_state: None };
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
        if self.should_send(&event) {
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
        let futures = (&*clients_lock)
            .into_iter()
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
        options: AccountListenerOptions,
        initial_account_ids: &'a Vec<LocalAccountId>,
    ) -> Result<(), fidl::Error> {
        let mut clients_lock = self.clients.lock().await;
        let future = if options.initial_state {
            let mut v = initial_account_ids
                .iter()
                .map(|id| InitialAccountState { account_id: id.clone().into(), auth_state: None })
                .collect::<Vec<_>>();
            FutureObj::new(Box::pin(listener.on_initialize(&mut v.iter_mut())))
        } else {
            FutureObj::new(Box::pin(ok(())))
        };
        clients_lock.push(Client::new(listener, options));
        self.inspect.total_opened.add(1 as u64);
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
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use futures::prelude::*;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref ACCOUNT_ID_ADD: LocalAccountId = LocalAccountId::new(1);
        static ref ACCOUNT_ID_REMOVE: LocalAccountId = LocalAccountId::new(2);
        static ref TEST_ACCOUNT_ID: LocalAccountId = LocalAccountId::new(3);
        static ref EVENT_ADDED: AccountEvent = AccountEvent::AccountAdded(ACCOUNT_ID_ADD.clone());
        static ref EVENT_REMOVED: AccountEvent =
            AccountEvent::AccountRemoved(ACCOUNT_ID_REMOVE.clone());
        static ref TEST_ACCOUNT_IDS: Vec<LocalAccountId> = vec![TEST_ACCOUNT_ID.clone()];
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_all() {
        let options = AccountListenerOptions {
            initial_state: true,
            add_account: true,
            remove_account: true,
            scenario: None,
            granularity: None,
        };
        let (listener, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(listener, options);
        assert_eq!(client.should_send(&EVENT_ADDED), true);
        assert_eq!(client.should_send(&EVENT_REMOVED), true);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_none() {
        let options = AccountListenerOptions {
            initial_state: false,
            add_account: false,
            remove_account: false,
            scenario: None,
            granularity: None,
        };
        let (proxy, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(proxy, options);
        assert_eq!(client.should_send(&EVENT_ADDED), false);
        assert_eq!(client.should_send(&EVENT_REMOVED), false);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_some() {
        let options = AccountListenerOptions {
            initial_state: true,
            add_account: false,
            remove_account: true,
            scenario: None,
            granularity: None,
        };
        let (proxy, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(proxy, options);
        assert_eq!(client.should_send(&EVENT_ADDED), false);
        assert_eq!(client.should_send(&EVENT_REMOVED), true);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_single_listener() {
        let options = AccountListenerOptions {
            initial_state: false,
            add_account: true,
            remove_account: false,
            scenario: None,
            granularity: None,
        };
        let (client_end, mut stream) = create_request_stream::<AccountListenerMarker>().unwrap();
        let client = Client::new(client_end.into_proxy().unwrap(), options);

        // Expect only the AccountAdded event, the filter skips the AccountRemoved event
        let serve_fut = async move {
            let request = stream.try_next().await.unwrap();
            if let Some(AccountListenerRequest::OnAccountAdded { account_state, responder }) =
                request
            {
                assert_eq!(LocalAccountId::from(account_state.account_id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if let Some(_) = stream.try_next().await.unwrap() {
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
        let options_1 = AccountListenerOptions {
            initial_state: false,
            add_account: true,
            remove_account: false,
            scenario: None,
            granularity: None,
        };
        let options_2 = AccountListenerOptions {
            initial_state: true,
            add_account: false,
            remove_account: true,
            scenario: None,
            granularity: None,
        };
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
                assert_eq!(LocalAccountId::from(account_state.account_id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if let Some(_) = stream_1.try_next().await.unwrap() {
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
                    vec![InitialAccountState {
                        account_id: TEST_ACCOUNT_ID.clone().into(),
                        auth_state: None
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
                assert_eq!(LocalAccountId::from(account_id), ACCOUNT_ID_REMOVE.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if let Some(_) = stream_2.try_next().await.unwrap() {
                panic!("Unexpected message, channel should be closed");
            }
        };

        let request_fut = async move {
            assert_inspect_tree!(inspector, root : { listeners: contains {
                active: 0 as u64,
                total_opened: 0 as u64,
            }});
            assert!(account_event_emitter
                .add_listener(listener_1, options_1, &TEST_ACCOUNT_IDS)
                .await
                .is_ok());
            assert_inspect_tree!(inspector, root : { listeners: contains {
                active: 1 as u64,
                total_opened: 1 as u64,
            }});
            assert!(account_event_emitter
                .add_listener(listener_2, options_2, &TEST_ACCOUNT_IDS)
                .await
                .is_ok());
            assert_inspect_tree!(inspector, root : { listeners: {
                active: 2 as u64,
                total_opened: 2 as u64,
                events: 0 as u64,
            }});

            account_event_emitter.publish(&EVENT_ADDED).await;
            assert_inspect_tree!(inspector, root : { listeners: contains { events: 1 as u64 }});
            account_event_emitter.publish(&EVENT_REMOVED).await;
            assert_inspect_tree!(inspector, root : { listeners: contains { events: 2 as u64 }});
        };
        join3(serve_fut_1, serve_fut_2, request_fut).await;
    }

    /// Check that that stale clients are cleaned up, once the server is closed
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_cleanup_stale_clients() {
        let options = AccountListenerOptions {
            initial_state: false,
            add_account: true,
            remove_account: true,
            scenario: None,
            granularity: None,
        };
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
                assert_eq!(LocalAccountId::from(account_state.account_id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
        };

        let request_fut = async move {
            account_event_emitter.publish(&EVENT_ADDED).await; // Normal event
            assert_inspect_tree!(inspector, root : { listeners: {
                active: 1 as u64, // Listener remains
                total_opened: 1 as u64,
                events: 1 as u64,
            }});
            (account_event_emitter, inspector)
        };
        let (_, (account_event_emitter, inspector)) = join(serve_fut, request_fut).await;

        // Now the server is dropped, so the new publish should trigger a drop of the client
        account_event_emitter.publish(&EVENT_REMOVED).await;
        assert_inspect_tree!(inspector, root : { listeners: {
            active: 0 as u64, // Listener dropped
            total_opened: 1 as u64,
            events: 2 as u64,
        }});
    }
}
