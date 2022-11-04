// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    async_utils::hanging_get::server::{HangingGet, Publisher},
    fidl_fuchsia_identity_authentication::{
        Empty, Error as ApiError, TestAuthenticatorCondition, TestInteractionRequest,
        TestInteractionRequestStream, TestInteractionWatchStateResponder,
        TestInteractionWatchStateResponse,
    },
    futures::TryStreamExt,
    std::cell::RefCell,
    tracing::warn,
};
type NotifyFn =
    Box<dyn Fn(&TestInteractionWatchStateResponse, TestInteractionWatchStateResponder) -> bool>;
type StateHangingGet =
    HangingGet<TestInteractionWatchStateResponse, TestInteractionWatchStateResponder, NotifyFn>;
#[allow(dead_code)] // Unused for now since we only have a single state which we don't update.
type StatePublisher =
    Publisher<TestInteractionWatchStateResponse, TestInteractionWatchStateResponder, NotifyFn>;

#[allow(dead_code)]
pub struct TestInteraction {
    /// Hanging get broker that requires mutability.
    state_hanging_get: RefCell<StateHangingGet>,
}

impl TestInteraction {
    #[allow(dead_code)]
    /// Creates a new TestInteraction handler.
    fn new() -> Self {
        let initial_state = TestInteractionWatchStateResponse::Waiting(vec![
            TestAuthenticatorCondition::SetSuccess(Empty),
        ]);
        let hanging_get = Self::create_hanging_get_broker(initial_state);
        Self { state_hanging_get: RefCell::new(hanging_get) }
    }

    fn create_hanging_get_broker(
        initial_state: TestInteractionWatchStateResponse,
    ) -> StateHangingGet {
        let notify_fn: NotifyFn = Box::new(|state, responder| {
            match responder.send(&mut state.to_owned()) {
                Ok(()) => true, // indicates that the client was successfully updated with the new state
                Err(err) => {
                    warn!(
                        "Failed to send \
                        fuchsia.identity.authentication.Interaction.WatchState \
                        response: {:?}",
                        err
                    );
                    false
                }
            }
        });
        StateHangingGet::new(initial_state, notify_fn)
    }

    #[allow(dead_code)]
    /// Asynchronously handles the supplied stream of `TestInteractionRequestStream` messages.
    /// Returns a `bool` which specifies whether authentication/enroll request
    /// made on the fuchsia.identity.authentication.StorageUnlockMechanism channel
    /// should succeed or not.
    async fn handle_requests_from_stream(
        &self,
        mut stream: TestInteractionRequestStream,
    ) -> Result<bool, ApiError> {
        while let Some(req) = stream.try_next().await.map_err(|e| {
            warn!("Unable to get next request from TestInteractionRequestStream {:?}", e);
            ApiError::Resource
        })? {
            match req {
                TestInteractionRequest::SetSuccess { .. } => {
                    return Ok(true);
                }
                TestInteractionRequest::WatchState { responder } => {
                    let subscriber = self.state_hanging_get.borrow_mut().new_subscriber();
                    subscriber.register(responder).map_err(|err| {
                        warn!(
                            "Failed to register new TestInteractionWatchState subscriber: {:?}",
                            err
                        );
                        ApiError::Internal
                    })?;
                }
            }
        }
        Ok(false)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_identity_authentication::{TestInteractionMarker, TestInteractionProxy},
        fuchsia_async::Task,
    };

    fn make_proxy() -> (TestInteractionProxy, Task<bool>) {
        let (proxy, server_end) =
            create_proxy::<TestInteractionMarker>().expect("Failed to create interaction proxy");
        let test_interaction_handler = TestInteraction::new();
        let task = Task::local(async move {
            return test_interaction_handler
                .handle_requests_from_stream(server_end.into_stream().expect(
                    "Failed to create \
                        fuchsia.identity.authentication.TestInteraction stream \
                        from server end",
                ))
                .await
                .expect(
                    "Error while handling \
                    fuchsia.identity.authentication.TestInteraction request stream",
                );
        });
        (proxy, task)
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_interaction_initial_state() {
        let (proxy, testinteraction_task) = make_proxy();
        let state = proxy.watch_state().await.expect("Failed to get test interaction state");
        assert_eq!(
            state,
            TestInteractionWatchStateResponse::Waiting(vec![
                TestAuthenticatorCondition::SetSuccess(Empty)
            ])
        );

        std::mem::drop(proxy); // close the channel.

        // The task should return false since we didn't call SetSuccess.
        assert_eq!(testinteraction_task.await, false);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_interaction_succeed() -> Result<(), fidl::Error> {
        let (proxy, testinteraction_task) = make_proxy();
        let state = proxy.watch_state().await.expect("Failed to get test interaction state");
        assert_eq!(
            state,
            TestInteractionWatchStateResponse::Waiting(vec![
                TestAuthenticatorCondition::SetSuccess(Empty)
            ])
        );

        proxy.set_success()?;

        // The task should return true after calling SetSuccess.
        assert_eq!(testinteraction_task.await, true);

        Ok(())
    }
}
