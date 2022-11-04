// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    account_common::AccountManagerError,
    async_utils::hanging_get::server::{HangingGet, Publisher},
    fidl::endpoints::{ControlHandle, ServerEnd},
    fidl_fuchsia_identity_account::Error as ApiError,
    fidl_fuchsia_identity_authentication::{
        AttemptedEvent, InteractionMarker, InteractionRequest, InteractionRequestStream,
        InteractionWatchStateResponder, InteractionWatchStateResponse, Mechanism, Mode,
    },
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    std::cell::RefCell,
    tracing::warn,
};

type NotifyFn = Box<dyn Fn(&InteractionWatchStateResponse, InteractionWatchStateResponder) -> bool>;

type StateHangingGet =
    HangingGet<InteractionWatchStateResponse, InteractionWatchStateResponder, NotifyFn>;

type StatePublisher =
    Publisher<InteractionWatchStateResponse, InteractionWatchStateResponder, NotifyFn>;

/// Data associated with authentication enrollment.
/// TODO(fxb/114073): Remove this and start using a common type across all modules.
pub struct EnrollmentData(Vec<u8>);

/// Data associated with an enrollment of an authentication mechanism
/// capable of storage unlock.
/// TODO(fxb/114074): Remove this and start using a common type across all modules.
pub struct PrekeyMaterial(Vec<u8>);

#[allow(dead_code)]
// TODO(fxb/104337): Use the following type when we start returning the data
// from handle_requests_from_stream.
enum AuthenticatorResponse {
    Authenticate(AttemptedEvent),
    Enrollment(EnrollmentData, PrekeyMaterial),
}

/// Generate an InteractionWatchStateResponse when the `Mode` and `Mechanism`
/// are specified.
fn create_interaction_watch_state(
    mechanism: Mechanism,
    mode: &Mode,
) -> InteractionWatchStateResponse {
    match mode {
        Mode::Authenticate => InteractionWatchStateResponse::Authenticate(vec![mechanism]),
        Mode::Enroll => InteractionWatchStateResponse::Enrollment(vec![mechanism]),
    }
}

#[allow(dead_code)]
/// Implements the fuchsia.identity.authentication.Interaction protocol. It
/// handles all InteractionRequests from the client and connects it to the
/// appropriate authenticator for enrollment and authentication.
pub struct Interaction {
    /// Hanging get broker that requires mutability.
    state_hanging_get: RefCell<StateHangingGet>,

    /// Mechanism to be used for authentication operations.
    mechanism: Mechanism,

    /// The mode which Authentication protocol will operate in.
    mode: Mode,

    /// State publisher to update all the subscribers.
    state_publisher: StatePublisher,
}

impl Interaction {
    /// Creates a new Interaction handler which starts in the specified `Mode`
    /// and handles the InteractionRequests coming over from the client.
    fn new(mechanism: Mechanism, mode: Mode) -> Self {
        let initial_state = create_interaction_watch_state(mechanism, &mode);
        let hanging_get = Self::create_hanging_get_broker(initial_state);
        let state_publisher = hanging_get.new_publisher();
        Self { state_hanging_get: RefCell::new(hanging_get), mechanism, mode, state_publisher }
    }

    #[allow(dead_code)]
    /// Handles request stream over the provided `server_end` and connects to an
    /// authenticator supporting the provided `mechanism`. Performs authenticate
    /// operation and returns the result. It will always run in `Authenticate` mode.
    pub async fn authenticate(
        server_end: ServerEnd<InteractionMarker>,
        mechanism: Mechanism,
    ) -> Result<AttemptedEvent, AccountManagerError> {
        let interaction = Self::new(mechanism, Mode::Authenticate);
        let stream = server_end.into_stream()?;
        interaction.handle_requests_from_stream(stream).await?;
        // TODO(104337): Return the actual result from the authentication
        // once it is fully functional.
        Ok(AttemptedEvent::EMPTY)
    }

    #[allow(dead_code)]
    /// Handles request stream over the provided `server_end` and connects to an
    /// authenticator supporting the provided `mechanism`. Performs enroll
    /// operation and returns the result. It will always run in `Enroll` mode.
    pub async fn enroll(
        server_end: ServerEnd<InteractionMarker>,
        mechanism: Mechanism,
    ) -> Result<(EnrollmentData, PrekeyMaterial), AccountManagerError> {
        let interaction = Self::new(mechanism, Mode::Enroll);
        let stream = server_end.into_stream()?;
        interaction.handle_requests_from_stream(stream).await?;
        // TODO(104337): Return the actual result from the enrollment
        // once it is fully functional.
        Ok((EnrollmentData(vec![]), PrekeyMaterial(vec![])))
    }

    /// Generate an InteractionWatchStateResponse based on the current
    /// `Mode` and `Mechanism`.
    fn create_state(&self) -> InteractionWatchStateResponse {
        create_interaction_watch_state(self.mechanism, &self.mode)
    }

    /// Create a HangingGet broker with the provided `initial_state`.
    fn create_hanging_get_broker(initial_state: InteractionWatchStateResponse) -> StateHangingGet {
        let notify_fn: NotifyFn = Box::new(|state, responder| {
            if responder.send(&mut state.to_owned()).is_err() {
                warn!(
                    "Failed to send \
                    fuchsia.identity.authentication.Interaction.WatchState response."
                );
            }

            true
        });

        StateHangingGet::new(initial_state, notify_fn)
    }

    /// Asynchronously handles the supplied stream of `InteractionRequestStream` messages.
    async fn handle_requests_from_stream(
        &self,
        mut stream: InteractionRequestStream,
    ) -> Result<(), AccountManagerError> {
        while let Some(req) = stream.try_next().await? {
            match req {
                InteractionRequest::StartPassword { mode, control_handle, ui } => {
                    match self.validate_prerequisites(Mechanism::Password, mode) {
                        Err(_) => {
                            // Close the PasswordInteraction channel and set the new state.
                            if let Err(e) = ui.close_with_epitaph(zx::Status::INVALID_ARGS) {
                                warn!("Couldn't close PasswordInteraction ServerEnd: {:?}", e);
                            }
                            self.state_publisher.set(self.create_state());
                        }
                        Ok(()) => {
                            // TODO(114056): Plug the password authenticator here once both ends are ready.
                            warn!(
                                "Unsupported operation: \
                                fuchsia.identity.authentication.Interaction.StartPassword"
                            );
                            control_handle.shutdown_with_epitaph(zx::Status::NOT_SUPPORTED);
                            return Ok(());
                        }
                    }
                }
                InteractionRequest::StartTest { mode, control_handle, ui } => {
                    match self.validate_prerequisites(Mechanism::Test, mode) {
                        Err(_) => {
                            // Close the TestInteraction channel and set the new state.
                            if let Err(e) = ui.close_with_epitaph(zx::Status::INVALID_ARGS) {
                                warn!("Couldn't close TestInteraction ServerEnd: {:?}", e);
                            }
                            self.state_publisher.set(self.create_state());
                        }
                        Ok(()) => {
                            // TODO(104337): Get enrollment/authentication result
                            // from the authenticator and return.
                            control_handle.shutdown_with_epitaph(zx::Status::OK);
                            return Ok(());
                        }
                    }
                }
                InteractionRequest::WatchState { responder } => {
                    let subscriber = self.state_hanging_get.borrow_mut().new_subscriber();
                    subscriber.register(responder).map_err(|err| {
                        warn!("Failed to register new InteractionWatchState subscriber: {:?}", err);
                        ApiError::Internal
                    })?;
                }
            }
        }

        warn!("Client closed channel without finishing all Interaction steps");
        Err(AccountManagerError::from(ApiError::FailedPrecondition))
    }

    /// Check that the request is sent for the correct mode and mechanism.
    fn validate_prerequisites(
        &self,
        mechanism: Mechanism,
        mode: Mode,
    ) -> Result<(), AccountManagerError> {
        if mode != self.mode {
            warn!(
                "InteractionRequest: Expected mode: {:?}, Requested mode: {:?}. Exiting early",
                self.mode, mode
            );

            return Err(AccountManagerError::from(ApiError::InvalidRequest));
        }
        if self.mechanism != mechanism {
            warn!(
                "Interaction server does not support request with mechanism {:?}\
                     since it was initiated with mechanism: {:?}",
                mechanism, self.mechanism
            );

            return Err(AccountManagerError::from(ApiError::InvalidRequest));
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl::endpoints::{create_endpoints, create_proxy},
        fidl_fuchsia_identity_authentication::InteractionProxy,
        fuchsia_async::Task,
        futures::StreamExt,
    };

    fn make_proxy(mechanism: Mechanism, mode: Mode) -> InteractionProxy {
        let (proxy, server_end) =
            create_proxy::<InteractionMarker>().expect("Failed to create interaction proxy");
        let interaction_handler = Interaction::new(mechanism, mode);

        Task::local(async move {
            interaction_handler
                .handle_requests_from_stream(server_end.into_stream().expect(
                    "Failed to create \
                            fuchsia.identity.authentication.Interaction stream \
                            from server end",
                ))
                .await
                .unwrap_or_else(|err| {
                    warn!(
                        "Error while handling \
                        fuchsia.identity.authentication.Interaction request stream: {:?}",
                        err
                    );
                });
        })
        .detach();
        proxy
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_initial_state() {
        let proxy = make_proxy(Mechanism::Test, Mode::Enroll);
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Enrollment(vec![Mechanism::Test]));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_unsupported_password_mechanism() {
        let proxy = make_proxy(Mechanism::Password, Mode::Enroll);
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Enrollment(vec![Mechanism::Password]));

        let mut state_fut = proxy.watch_state();
        assert!(futures::poll!(&mut state_fut).is_pending());

        let (_password_proxy, test_password_interaction_server_end) = create_endpoints().unwrap();
        // Send a StartPassword event and verify that channel closes with unsupported error.
        let _ = proxy.start_password(test_password_interaction_server_end, Mode::Enroll).unwrap();
        assert_matches!(
            proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::NOT_SUPPORTED, .. })
        );

        // Check that the state remains valid after the above operation.
        assert_eq!(
            state_fut.await.expect("Failed to get interaction state"),
            InteractionWatchStateResponse::Enrollment(vec![Mechanism::Password])
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_test_succeed() {
        let proxy = make_proxy(Mechanism::Test, Mode::Enroll);
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Enrollment(vec![Mechanism::Test]));

        let (_test_proxy, test_interaction_server_end) = create_endpoints().unwrap();
        // Send a StartTest event and verify that channel closes with Ok.
        let _ = proxy.start_test(test_interaction_server_end, Mode::Enroll).unwrap();
        assert_matches!(
            proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::OK, .. })
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_test_invalid_mode_delayed_watch_state() {
        let proxy = make_proxy(Mechanism::Test, Mode::Enroll);

        let (test_proxy, test_interaction_server_end) = create_proxy().unwrap();
        // Send a StartTest event with a different mode and verify that the
        // TestInteraction channel is closed.
        let _ = proxy.start_test(test_interaction_server_end, Mode::Authenticate).unwrap();
        assert_matches!(
            test_proxy.watch_state().await,
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::INVALID_ARGS, .. })
        );

        // Check that the state remains valid after the above operation.
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Enrollment(vec![Mechanism::Test]));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_test_invalid_mechanism_delayed_watch_state() {
        let proxy = make_proxy(Mechanism::Test, Mode::Enroll);

        let (password_proxy, test_password_interaction_server_end) = create_proxy().unwrap();
        // Send a StartPassword event which is a different mode and verify that
        // the TestInteraction channel is closed.
        let _ = proxy.start_password(test_password_interaction_server_end, Mode::Enroll).unwrap();
        assert_matches!(
            password_proxy.watch_state().await,
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::INVALID_ARGS, .. })
        );

        // Check that the state remains valid after the above operation.
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Enrollment(vec![Mechanism::Test]));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_test_invalid_mode_multiple_watch_calls() {
        let proxy = make_proxy(Mechanism::Test, Mode::Enroll);
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Enrollment(vec![Mechanism::Test]));

        let mut state_fut = proxy.watch_state();
        assert!(futures::poll!(&mut state_fut).is_pending());

        let (test_proxy, test_interaction_server_end) = create_proxy().unwrap();
        // Send a StartTest event with a different mode and verify that the
        // TestInteraction channel is closed.
        let _ = proxy.start_test(test_interaction_server_end, Mode::Authenticate).unwrap();
        assert_matches!(
            test_proxy.watch_state().await,
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::INVALID_ARGS, .. })
        );

        // Check that the state remains valid after the above operation.
        assert_eq!(
            state_fut.await.expect("Failed to get interaction state"),
            InteractionWatchStateResponse::Enrollment(vec![Mechanism::Test])
        );
    }
}
