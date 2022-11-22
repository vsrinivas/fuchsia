// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    account_common::AccountManagerError,
    anyhow::format_err,
    async_utils::hanging_get::server::{HangingGet, Publisher},
    fidl::endpoints::{ControlHandle, ServerEnd},
    fidl_fuchsia_identity_account::Error as ApiError,
    fidl_fuchsia_identity_authentication::{
        AttemptedEvent, Enrollment, InteractionMarker, InteractionProtocolServerEnd,
        InteractionRequest, InteractionRequestStream, InteractionWatchStateResponder,
        InteractionWatchStateResponse, Mechanism, Mode, StorageUnlockMechanismMarker,
        StorageUnlockMechanismProxy,
    },
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::{Future, TryStreamExt},
    identity_common::{EnrollmentData, PrekeyMaterial},
    std::{cell::RefCell, rc::Rc},
    tracing::warn,
};

type NotifyFn = Box<dyn Fn(&InteractionWatchStateResponse, InteractionWatchStateResponder) -> bool>;

type StateHangingGet =
    HangingGet<InteractionWatchStateResponse, InteractionWatchStateResponder, NotifyFn>;

type StatePublisher =
    Publisher<InteractionWatchStateResponse, InteractionWatchStateResponder, NotifyFn>;

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
    /// authenticator supporting the provided `mechanism`. `enrollments` specify
    /// the list of enrollments to be accepted by the authenticator. Performs authenticate
    /// operation and returns the result. It will always run in `Authenticate` mode.
    pub async fn authenticate(
        server_end: ServerEnd<InteractionMarker>,
        mechanism: Mechanism,
        enrollments: Vec<Enrollment>,
    ) -> Result<AttemptedEvent, AccountManagerError> {
        let interaction = Self::new(mechanism, Mode::Authenticate);
        let stream = server_end.into_stream()?;
        let enrollments = Rc::new(RefCell::new(enrollments));
        interaction
            .handle_requests_from_stream(
                stream,
                Box::new(move |storage_unlock_proxy, ipse| {
                    Self::start_authentication(Rc::clone(&enrollments), storage_unlock_proxy, ipse)
                }),
            )
            .await
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
        interaction.handle_requests_from_stream(stream, Box::new(Self::start_enrollment)).await
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
    ///
    /// * `stream` - Stream of InteractionRequests to be processed.
    /// * `authenticator_fn` - A function pointer which will talk to the StorageUnlockMechanism
    /// server to process the Authentication and Enrollment requests and return
    /// the result of the operation.
    async fn handle_requests_from_stream<T, Fut>(
        &self,
        mut stream: InteractionRequestStream,
        authenticator_fn: Box<
            dyn Fn(StorageUnlockMechanismProxy, InteractionProtocolServerEnd) -> Fut + 'static,
        >,
    ) -> Result<T, AccountManagerError>
    where
        Fut: Future<Output = Result<T, AccountManagerError>>,
    {
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
                            return Err(AccountManagerError::from(ApiError::InvalidRequest));
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
                            let storage_unlock_proxy = self.get_storage_unlock_proxy()?;
                            match authenticator_fn(
                                storage_unlock_proxy,
                                InteractionProtocolServerEnd::Test(ui),
                            )
                            .await
                            {
                                Ok(result) => {
                                    control_handle.shutdown_with_epitaph(zx::Status::OK);
                                    return Ok(result);
                                }
                                Err(err) => {
                                    warn!("Authenticator operation failed with error: {:?}", err);
                                }
                            }
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

    /// Get the proxy to the appropriate authenticator based on the Mechanism.
    fn get_storage_unlock_proxy(&self) -> Result<StorageUnlockMechanismProxy, ApiError> {
        // TODO(fxb/104199): Return the correct proxy based on the current
        // operation mechanism.
        client::connect_to_protocol::<StorageUnlockMechanismMarker>().map_err(|err| {
            warn!("Failed to connect to authenticator {:?}", err);
            ApiError::Resource
        })
    }

    async fn start_authentication(
        enrollments: Rc<RefCell<Vec<Enrollment>>>,
        storage_unlock_proxy: StorageUnlockMechanismProxy,
        mut ipse: InteractionProtocolServerEnd,
    ) -> Result<AttemptedEvent, AccountManagerError> {
        let auth_attempt = storage_unlock_proxy
            .authenticate(&mut ipse, &mut enrollments.borrow_mut().iter_mut())
            .await
            .map_err(|err| {
                AccountManagerError::new(ApiError::Unknown)
                    .with_cause(format_err!("Error connecting to authenticator: {:?}", err))
            })?
            .map_err(|authenticator_err| AccountManagerError::from(authenticator_err).api_error)?;

        Ok(auth_attempt)
    }

    async fn start_enrollment(
        storage_unlock_proxy: StorageUnlockMechanismProxy,
        mut ipse: InteractionProtocolServerEnd,
    ) -> Result<(EnrollmentData, PrekeyMaterial), AccountManagerError> {
        let (data, prekey_material) = storage_unlock_proxy
            .enroll(&mut ipse)
            .await
            .map_err(|err| {
                AccountManagerError::new(ApiError::Unknown)
                    .with_cause(format_err!("Error connecting to authenticator: {:?}", err))
            })?
            .map_err(|authenticator_err| AccountManagerError::from(authenticator_err).api_error)?;
        Ok((EnrollmentData(data), PrekeyMaterial(prekey_material)))
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

    /// Returns an InteractionProxy and a task which reports the result of handling
    /// all the requests on the Interaction channel.
    ///
    /// * `mechanism` - Current operating mechanism for authentication.
    /// * `mode` - Operation to be performed.
    /// * `storage_unlock` - A function pointer which will perform the actual operation
    /// and return the result.
    fn make_proxy<T, Fut>(
        mechanism: Mechanism,
        mode: Mode,
        storage_unlock: Box<
            dyn Fn(StorageUnlockMechanismProxy, InteractionProtocolServerEnd) -> Fut + 'static,
        >,
    ) -> (InteractionProxy, Task<Result<T, AccountManagerError>>)
    where
        Fut: Future<Output = Result<T, AccountManagerError>> + 'static,
    {
        let (proxy, server_end) =
            create_proxy::<InteractionMarker>().expect("Failed to create interaction proxy");
        let interaction_handler = Interaction::new(mechanism, mode);

        let task = Task::local(async move {
            interaction_handler
                .handle_requests_from_stream(
                    server_end.into_stream().expect(
                        "Failed to create \
                            fuchsia.identity.authentication.Interaction stream \
                            from server end",
                    ),
                    storage_unlock,
                )
                .await
        });
        (proxy, task)
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_initial_state() {
        let (proxy, _task) =
            make_proxy(Mechanism::Test, Mode::Enroll, Box::new(move |_, _| async { Ok(()) }));
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Enrollment(vec![Mechanism::Test]));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_unsupported_password_mechanism() {
        let (proxy, task) =
            make_proxy(Mechanism::Password, Mode::Enroll, Box::new(move |_, _| async { Ok(()) }));
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

        // The result of the task should report an InvalidRequest error.
        assert_matches!(
            task.await,
            Err(AccountManagerError { api_error: ApiError::InvalidRequest, .. })
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_test_succeed_enrollment() {
        let (proxy, task) = make_proxy(
            Mechanism::Test,
            Mode::Enroll,
            Box::new(|_, ipse| async move {
                assert_matches!(ipse, InteractionProtocolServerEnd::Test(_));
                Ok(()) // Final result after operation completion.
            }),
        );
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Enrollment(vec![Mechanism::Test]));

        let (_test_proxy, test_interaction_server_end) = create_endpoints().unwrap();
        // Send a StartTest event and verify that channel closes with Ok.
        let _ = proxy.start_test(test_interaction_server_end, Mode::Enroll).unwrap();
        assert_matches!(
            proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::OK, .. })
        );

        // Check that the enrollment operation succeeds.
        assert_matches!(task.await, Ok(()));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_test_succeed_authentication() {
        let (proxy, task) = make_proxy(
            Mechanism::Test,
            Mode::Authenticate,
            Box::new(|_, _| async {
                Ok(()) // Final result after operation completion.
            }),
        );
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Authenticate(vec![Mechanism::Test]));

        let (_test_proxy, test_interaction_server_end) = create_endpoints().unwrap();
        // Send a StartTest event and verify that channel closes with Ok.
        let _ = proxy.start_test(test_interaction_server_end, Mode::Authenticate).unwrap();
        assert_matches!(
            proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::OK, .. })
        );

        // Check that the authentication operation succeeds.
        assert_matches!(task.await, Ok(()));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_test_fail_authentication() {
        let (proxy, _task) = make_proxy(
            Mechanism::Test,
            Mode::Authenticate,
            Box::new(|_, _| async {
                Err(AccountManagerError::new(ApiError::Internal)) as Result<(), AccountManagerError>
                // Final result after operation completion.
            }),
        );
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Authenticate(vec![Mechanism::Test]));

        let (_test_proxy, test_interaction_server_end) = create_endpoints().unwrap();
        // Send a StartTest event and verify that channel closes with Ok.
        let _ = proxy.start_test(test_interaction_server_end, Mode::Authenticate).unwrap();

        // Since authentication failed, it should return the same response as before.
        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, InteractionWatchStateResponse::Authenticate(vec![Mechanism::Test]));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn interaction_test_invalid_mode_delayed_watch_state() {
        let (proxy, _task) = make_proxy(
            Mechanism::Test,
            Mode::Enroll,
            Box::new(|_, _| async {
                Ok(()) // Final result after operation completion.
            }),
        );

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
        let (proxy, _task) = make_proxy(
            Mechanism::Test,
            Mode::Enroll,
            Box::new(|_, _| async {
                Ok(()) // Final result after operation completion.
            }),
        );

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
        let (proxy, _task) = make_proxy(
            Mechanism::Test,
            Mode::Enroll,
            Box::new(|_, _| async {
                Ok(()) // Final result after operation completion.
            }),
        );
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
