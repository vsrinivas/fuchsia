// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation for the event emitting client end of the AccountListener FIDL interface,
//! sending events to listeners, optionally configured with filters, about changes in the accounts
//! presence and states during their lifetime.

use account_common::{AccountAuthState, FidlAccountAuthState, LocalAccountId};
use fidl_fuchsia_auth_account::{AccountListenerOptions, AccountListenerProxy};
use futures::future::*;
use futures::lock::Mutex;
use std::pin::Pin;

/// Events emitted on account listeners
pub enum AccountEvent {
    /// AccountAdded is emitted after an account has been added.
    AccountAdded(LocalAccountId),

    /// AccountRemoved is emitted after an account has been removed.
    AccountRemoved(LocalAccountId),

    /// AuthStateChanged is emitted after an account's auth state has changed.
    #[allow(dead_code)]
    AuthStateChanged(AccountAuthState),
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
            AccountEvent::AuthStateChanged(_) => false, // TODO: Implement
        }
    }

    fn send<'a>(
        &'a self,
        event: &'a AccountEvent,
    ) -> impl Future<Output = Result<(), fidl::Error>> {
        match event {
            AccountEvent::AccountAdded(id) => {
                self.listener.on_account_added(&mut id.clone().into())
            }
            AccountEvent::AccountRemoved(id) => {
                self.listener.on_account_removed(&mut id.clone().into())
            }
            AccountEvent::AuthStateChanged(account_auth_state) => {
                self.listener.on_auth_state_changed(&mut (&account_auth_state.clone()).into())
            }
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

/// Collection of account listener emitters.
pub struct AccountEventEmitter {
    clients: Mutex<Vec<Client>>,
}

impl AccountEventEmitter {
    /// Create a new emitter with no clients.
    pub fn new() -> AccountEventEmitter {
        Self { clients: Mutex::new(Vec::new()) }
    }

    /// Send an event to all active listeners filtered by their respective options. Awaits until
    /// all messages have been confirmed by the servers.
    pub async fn publish<'a>(&'a self, event: &'a AccountEvent) {
        let mut clients_lock = await!(self.clients.lock());
        clients_lock.retain(|client| !client.listener.is_closed());
        let futures = (&*clients_lock)
            .into_iter()
            .map(|client| client.possibly_send(event))
            .map(|fut| Pin::<Box<_>>::from(Box::new(fut)));
        let all_futures = join_all(futures);
        std::mem::drop(clients_lock);
        await!(all_futures);
    }

    /// Add a new listener to the collection.
    pub async fn add_listener<'a>(
        &'a self,
        listener: AccountListenerProxy,
        options: AccountListenerOptions,
        initial_auth_states: &'a Vec<AccountAuthState>,
    ) -> Result<(), fidl::Error> {
        let mut clients_lock = await!(self.clients.lock());
        let future = if options.initial_state {
            let mut v: Vec<FidlAccountAuthState> = initial_auth_states
                .into_iter()
                .map(|auth_state| FidlAccountAuthState::from(auth_state))
                .collect();
            FutureObj::new(Box::pin(listener.on_initialize(&mut v.iter_mut())))
        } else {
            FutureObj::new(Box::pin(ok(())))
        };
        clients_lock.push(Client::new(listener, options));
        std::mem::drop(clients_lock);
        await!(future)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::*;
    use fidl_fuchsia_auth::AuthChangeGranularity;
    use fidl_fuchsia_auth_account::{AccountListenerMarker, AccountListenerRequest};
    use futures::prelude::*;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref ACCOUNT_ID_ADD: LocalAccountId = LocalAccountId::new(1);
        static ref ACCOUNT_ID_REMOVE: LocalAccountId = LocalAccountId::new(2);
        static ref EVENT_ADDED: AccountEvent = AccountEvent::AccountAdded(ACCOUNT_ID_ADD.clone());
        static ref EVENT_REMOVED: AccountEvent =
            AccountEvent::AccountRemoved(ACCOUNT_ID_REMOVE.clone());
        static ref EVENT_STATE_CHANGED: AccountEvent =
            AccountEvent::AuthStateChanged(AccountAuthState { account_id: LocalAccountId::new(4) });
        static ref AUTH_STATE: AccountAuthState =
            AccountAuthState { account_id: LocalAccountId::new(6) };
        static ref AUTH_STATES: Vec<AccountAuthState> = vec![AUTH_STATE.clone()];
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_all() {
        let options = AccountListenerOptions {
            initial_state: true,
            add_account: true,
            remove_account: true,
            granularity: AuthChangeGranularity { summary_changes: true },
        };
        let (listener, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(listener, options);
        assert_eq!(client.should_send(&EVENT_ADDED), true);
        assert_eq!(client.should_send(&EVENT_REMOVED), true);
        assert_eq!(client.should_send(&EVENT_STATE_CHANGED), false);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_none() {
        let options = AccountListenerOptions {
            initial_state: false,
            add_account: false,
            remove_account: false,
            granularity: AuthChangeGranularity { summary_changes: false },
        };
        let (proxy, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(proxy, options);
        assert_eq!(client.should_send(&EVENT_ADDED), false);
        assert_eq!(client.should_send(&EVENT_REMOVED), false);
        assert_eq!(client.should_send(&EVENT_STATE_CHANGED), false);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_should_send_some() {
        let options = AccountListenerOptions {
            initial_state: true,
            add_account: false,
            remove_account: true,
            granularity: AuthChangeGranularity { summary_changes: false },
        };
        let (proxy, _) = create_proxy::<AccountListenerMarker>().unwrap();
        let client = Client::new(proxy, options);
        assert_eq!(client.should_send(&EVENT_ADDED), false);
        assert_eq!(client.should_send(&EVENT_REMOVED), true);
        assert_eq!(client.should_send(&EVENT_STATE_CHANGED), false);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_single_listener() {
        let options = AccountListenerOptions {
            initial_state: false,
            add_account: true,
            remove_account: false,
            granularity: AuthChangeGranularity { summary_changes: false },
        };
        let (client_end, mut stream) = create_request_stream::<AccountListenerMarker>().unwrap();
        let client = Client::new(client_end.into_proxy().unwrap(), options);

        // Expect only the AccountAdded event, the filter skips the AccountRemoved event
        let serve_fut = async move {
            let request = await!(stream.try_next()).unwrap();
            if let Some(AccountListenerRequest::OnAccountAdded { id, responder }) = request {
                assert_eq!(LocalAccountId::from(id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if let Some(_) = await!(stream.try_next()).unwrap() {
                panic!("Unexpected message, channel should be closed");
            }
        };
        let request_fut = async move {
            assert!(await!(client.possibly_send(&EVENT_ADDED)).is_ok());
            assert!(await!(client.possibly_send(&EVENT_REMOVED)).is_ok());
        };
        await!(join(serve_fut, request_fut));
    }

    /// Given two independent clients with different options/filters, send events and check
    /// that the clients receive the expected messages.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_event_emitter() {
        let options_1 = AccountListenerOptions {
            initial_state: false,
            add_account: true,
            remove_account: false,
            granularity: AuthChangeGranularity { summary_changes: false },
        };
        let options_2 = AccountListenerOptions {
            initial_state: true,
            add_account: false,
            remove_account: true,
            granularity: AuthChangeGranularity { summary_changes: false },
        };
        let (client_end_1, mut stream_1) =
            create_request_stream::<AccountListenerMarker>().unwrap();
        let (client_end_2, mut stream_2) =
            create_request_stream::<AccountListenerMarker>().unwrap();
        let listener_1 = client_end_1.into_proxy().unwrap();
        let listener_2 = client_end_2.into_proxy().unwrap();
        let account_event_emitter = AccountEventEmitter::new();

        let serve_fut_1 = async move {
            let request = await!(stream_1.try_next()).unwrap();
            if let Some(AccountListenerRequest::OnAccountAdded { id, responder }) = request {
                assert_eq!(LocalAccountId::from(id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if let Some(_) = await!(stream_1.try_next()).unwrap() {
                panic!("Unexpected message, channel should be closed");
            }
        };

        let serve_fut_2 = async move {
            let request = await!(stream_2.try_next()).unwrap();
            if let Some(AccountListenerRequest::OnInitialize { account_auth_states, responder }) =
                request
            {
                assert_eq!(
                    account_auth_states,
                    vec![FidlAccountAuthState::from(&AUTH_STATE.clone())]
                );
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            let request = await!(stream_2.try_next()).unwrap();
            if let Some(AccountListenerRequest::OnAccountRemoved { id, responder }) = request {
                assert_eq!(LocalAccountId::from(id), ACCOUNT_ID_REMOVE.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            if let Some(_) = await!(stream_2.try_next()).unwrap() {
                panic!("Unexpected message, channel should be closed");
            }
        };

        let request_fut = async move {
            assert!(await!(account_event_emitter.add_listener(
                listener_1,
                options_1,
                &AUTH_STATES
            ))
            .is_ok());
            assert!(await!(account_event_emitter.add_listener(
                listener_2,
                options_2,
                &AUTH_STATES
            ))
            .is_ok());
            await!(account_event_emitter.publish(&EVENT_ADDED));
            await!(account_event_emitter.publish(&EVENT_REMOVED));
        };
        await!(join3(serve_fut_1, serve_fut_2, request_fut));
    }

    /// Check that that stale clients are cleaned up, once the server is closed
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_cleanup_stale_clients() {
        let options = AccountListenerOptions {
            initial_state: false,
            add_account: true,
            remove_account: true,
            granularity: AuthChangeGranularity { summary_changes: false },
        };
        let (client_end, mut stream) = create_request_stream::<AccountListenerMarker>().unwrap();
        let listener = client_end.into_proxy().unwrap();
        let account_event_emitter = AccountEventEmitter::new();
        assert!(await!(account_event_emitter.add_listener(listener, options, &AUTH_STATES)).is_ok());

        let serve_fut = async move {
            let request = await!(stream.try_next()).unwrap();
            if let Some(AccountListenerRequest::OnAccountAdded { id, responder }) = request {
                assert_eq!(LocalAccountId::from(id), ACCOUNT_ID_ADD.clone());
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
        };
        let request_fut = async move {
            await!(account_event_emitter.publish(&EVENT_ADDED)); // Normal event
            {
                let clients_lock = await!(account_event_emitter.clients.lock());
                assert_eq!(clients_lock.len(), 1); // Listener remains
            }
            account_event_emitter
        };
        let (_, account_event_emitter) = await!(join(serve_fut, request_fut));

        // Now the server is dropped, so the new publish should trigger a drop of the client
        await!(account_event_emitter.publish(&EVENT_REMOVED));
        let clients_lock = await!(account_event_emitter.clients.lock());
        assert!(clients_lock.is_empty());
    }
}
