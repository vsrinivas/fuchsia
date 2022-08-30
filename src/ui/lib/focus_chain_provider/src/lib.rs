// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides a hanging-get implementation of the `fuchsia.ui.focus.FocusChainProvider` protocol.
//!
//! * Create a new publisher and request stream handler using [`make_publisher_and_stream_handler()`].
//! * Handle a new client's stream of watch requests using
//!   [`FocusChainProviderRequestStreamHandler::handle_request_stream`].
//! * Update the focus chain using [`FocusChainProviderPublisher::set_state_and_notify_if_changed`]
//!   or [`FocusChainProviderPublisher::set_state_and_notify_always`].

mod instance_counter;

use {
    crate::instance_counter::InstanceCounter,
    async_utils::hanging_get::server as hanging_get,
    fidl_fuchsia_ui_focus::{
        self as focus, FocusChainProviderWatchFocusKoidChainResponder, FocusKoidChain,
    },
    fidl_fuchsia_ui_focus_ext::FocusChainExt,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{lock::Mutex, stream::TryStreamExt, TryFutureExt},
    std::sync::Arc,
    tracing::error,
};

// Local shorthand type aliases
type HangingGetNotifyFn =
    Box<dyn Fn(&FocusKoidChain, FocusChainProviderWatchFocusKoidChainResponder) -> bool + Send>;
type HangingGetBroker = hanging_get::HangingGet<
    FocusKoidChain,
    FocusChainProviderWatchFocusKoidChainResponder,
    HangingGetNotifyFn,
>;
type HangingGetPublisher = hanging_get::Publisher<
    FocusKoidChain,
    FocusChainProviderWatchFocusKoidChainResponder,
    HangingGetNotifyFn,
>;

/// Creates a new publisher and stream handler pair. Their initial focus chain value is always
/// `FocusKoidChain::EMPTY`.
pub fn make_publisher_and_stream_handler(
) -> (FocusChainProviderPublisher, FocusChainProviderRequestStreamHandler) {
    let notify_fn: HangingGetNotifyFn =
        Box::new(|focus_koid_chain, responder| match responder.send(focus_koid_chain.clone()) {
            Ok(()) => true,
            Err(e) => {
                error!("Failed to send focus chain to client: {e:?}");
                false
            }
        });

    let broker = hanging_get::HangingGet::new(FocusKoidChain::EMPTY, notify_fn);
    let publisher = broker.new_publisher();
    let subscriber_counter = InstanceCounter::new();

    (
        FocusChainProviderPublisher { publisher },
        FocusChainProviderRequestStreamHandler {
            broker: Arc::new(Mutex::new(broker)),
            subscriber_counter,
        },
    )
}

/// Allows new focus chain values to be stored for transmission to watcher clients (through the
/// corresponding [`FocusChainProviderRequestStreamHandler`]).
///
/// Instantiate using [`make_publisher_and_stream_handler()`].
#[derive(Clone)]
pub struct FocusChainProviderPublisher {
    publisher: HangingGetPublisher,
}

impl FocusChainProviderPublisher {
    /// Updates the focus chain. If the new value is different from the previous value, sends an
    /// update to all listeners.
    ///
    /// Returns an error if there are any problems with duplicating the `FocusChain`.
    pub fn set_state_and_notify_if_changed<C: FocusChainExt>(
        &self,
        new_state: &C,
    ) -> Result<(), zx::Status> {
        let new_state = new_state.to_focus_koid_chain()?;
        let publisher = self.publisher.clone();
        publisher.update(|old_state| match old_state.equivalent(&new_state) {
            Ok(true) => false,
            Ok(false) => {
                *old_state = new_state;
                true
            }
            Err(e) => unreachable!("Unexpected state {e:?}"),
        });
        Ok(())
    }

    /// Updates the focus chain. Sends an update to all listeners even if the value hasn't changed.
    ///
    /// Returns an error if there are any problems with duplicating the `FocusChain`.
    pub fn set_state_and_notify_always<C: FocusChainExt>(
        &self,
        new_state: &C,
    ) -> Result<(), zx::Status> {
        let publisher = self.publisher.clone();
        publisher.set(new_state.to_focus_koid_chain()?);
        Ok(())
    }
}

/// Handles streams of requests from `FocusChainProvider` clients, responding to them with the
/// latest value from the corresponding [`FocusChainProviderPublisher`].
///
/// Instantiate using [`make_publisher_and_stream_handler()`].
#[derive(Clone)]
pub struct FocusChainProviderRequestStreamHandler {
    broker: Arc<Mutex<HangingGetBroker>>,
    subscriber_counter: InstanceCounter,
}

impl FocusChainProviderRequestStreamHandler {
    /// Handles a [`fidl_fuchsia_ui_focus::FocusChainProviderRequestStream`] for a single client,
    /// spawning a new local `Task`.
    #[must_use = "The Task must be retained or `.detach()`ed."]
    pub fn handle_request_stream(
        &self,
        mut stream: focus::FocusChainProviderRequestStream,
    ) -> fasync::Task<()> {
        let broker = self.broker.clone();
        let counter = self.subscriber_counter.clone();
        fasync::Task::local(
            async move {
                let subscriber = broker.lock().await.new_subscriber();
                // Will be dropped when the task is being dropped.
                let _count_token = counter.make_token();
                while let Some(req) = stream.try_next().await? {
                    match req {
                        focus::FocusChainProviderRequest::WatchFocusKoidChain {
                            payload: _payload,
                            responder,
                        } => {
                            subscriber.register(responder)?;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| error!("{e:#?}")),
        )
    }

    /// Returns the number of active subscribers. Mostly useful for tests.
    pub fn subscriber_count(&self) -> usize {
        self.subscriber_counter.count()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_ui_focus_test_helpers::make_focus_chain};

    // Most of the testing happens in `async_utils::hanging_get`.
    #[fuchsia::test]
    async fn smoke_test() {
        let (publisher, stream_handler) = super::make_publisher_and_stream_handler();
        let (client, stream) =
            fidl::endpoints::create_proxy_and_stream::<focus::FocusChainProviderMarker>().unwrap();
        stream_handler.handle_request_stream(stream).detach();
        assert_eq!(stream_handler.subscriber_count(), 0);

        let received_focus_koid_chain = client
            .watch_focus_koid_chain(focus::FocusChainProviderWatchFocusKoidChainRequest::EMPTY)
            .await
            .expect("watch_focus_koid_chain");
        assert!(received_focus_koid_chain.equivalent(&FocusKoidChain::EMPTY).unwrap());
        assert_eq!(stream_handler.subscriber_count(), 1);

        let (served_focus_chain, _view_ref_controls) = make_focus_chain(2);
        publisher.set_state_and_notify_if_changed(&served_focus_chain).expect("set_state");
        let received_focus_koid_chain = client
            .watch_focus_koid_chain(focus::FocusChainProviderWatchFocusKoidChainRequest::EMPTY)
            .await
            .expect("watch_focus_chain");
        assert!(received_focus_koid_chain.equivalent(&served_focus_chain).unwrap());
        assert_eq!(stream_handler.subscriber_count(), 1);
    }

    #[fuchsia::test]
    async fn only_newest_value_is_sent() {
        let (publisher, stream_handler) = super::make_publisher_and_stream_handler();
        let (client, stream) =
            fidl::endpoints::create_proxy_and_stream::<focus::FocusChainProviderMarker>().unwrap();
        stream_handler.handle_request_stream(stream).detach();

        let received_focus_koid_chain = client
            .watch_focus_koid_chain(focus::FocusChainProviderWatchFocusKoidChainRequest::EMPTY)
            .await
            .expect("watch_focus_koid_chain");
        assert!(received_focus_koid_chain.equivalent(&FocusKoidChain::EMPTY).unwrap());

        let (served_focus_chain, _view_ref_controls) = make_focus_chain(2);
        publisher.set_state_and_notify_if_changed(&served_focus_chain).expect("set_state");

        let (served_focus_chain, _view_ref_controls) = make_focus_chain(3);
        publisher.set_state_and_notify_if_changed(&served_focus_chain).expect("set_state");

        let received_focus_koid_chain = client
            .watch_focus_koid_chain(focus::FocusChainProviderWatchFocusKoidChainRequest::EMPTY)
            .await
            .expect("watch_focus_chain");
        assert_eq!(received_focus_koid_chain.len(), 3);
        assert!(received_focus_koid_chain.equivalent(&served_focus_chain).unwrap());
    }
}
