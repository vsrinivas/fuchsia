// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
use {
    anyhow::{anyhow, Error},
    async_utils::hanging_get::server::{HangingGet, Publisher},
    fidl::endpoints::ControlHandle,
    fidl_fuchsia_identity_authentication::{
        PasswordError, PasswordInteractionRequest, PasswordInteractionRequestStream,
        PasswordInteractionWatchStateResponder, PasswordInteractionWatchStateResponse,
    },
    fuchsia_zircon as zx,
    futures::StreamExt,
    std::{cell::RefCell, rc::Rc},
    tracing::warn,
};

/// A trait for types that can determine whether a supplied password is either
/// valid or potentially valid. A validator either supports enrollment or
/// authentication.
pub trait Validator<T: Sized> {
    fn validate(&self) -> Result<T, PasswordError>;
}

type NotifyFn = Box<
    dyn Fn(&PasswordInteractionWatchStateResponse, PasswordInteractionWatchStateResponder) -> bool,
>;

type PasswordInteractionStateHangingGet = HangingGet<
    PasswordInteractionWatchStateResponse,
    PasswordInteractionWatchStateResponder,
    NotifyFn,
>;

type PasswordInteractionStatePublisher = Publisher<
    PasswordInteractionWatchStateResponse,
    PasswordInteractionWatchStateResponder,
    NotifyFn,
>;

/// Tracks the state of PasswordInteraction events.
pub struct PasswordInteractionHandler<V, T>
where
    V: Validator<T>,
{
    hanging_get: RefCell<PasswordInteractionStateHangingGet>,
    stream: RefCell<PasswordInteractionRequestStream>,
    publisher: RefCell<Option<PasswordInteractionStatePublisher>>,
    validator: V,
    _phantom: std::marker::PhantomData<T>,
}

impl<V, T> PasswordInteractionHandler<V, T>
where
    V: Validator<T>,
{
    pub fn new(stream: PasswordInteractionRequestStream, validator: V) -> Self {
        let hanging_get = PasswordInteractionHandler::<V, T>::init_hanging_get(
            PasswordInteractionWatchStateResponse::Waiting(vec![]),
        );

        Self {
            hanging_get: RefCell::new(hanging_get),
            stream: RefCell::new(stream),
            publisher: RefCell::new(None),
            validator,
            _phantom: std::marker::PhantomData,
        }
    }

    fn init_hanging_get(
        initial_state: PasswordInteractionWatchStateResponse,
    ) -> PasswordInteractionStateHangingGet {
        let notify_fn: NotifyFn = Box::new(|state, responder| {
            if responder.send(&mut state.to_owned()).is_err() {
                tracing::warn!("Failed to send password interaction state");
            }

            true
        });

        PasswordInteractionStateHangingGet::new(initial_state, notify_fn)
    }

    fn handle_password_interaction_request_stream(
        self: Rc<Self>,
    ) -> impl futures::Future<Output = Result<T, Error>> {
        let subscriber = self.hanging_get.borrow_mut().new_subscriber();

        async move {
            while let Some(request) = self.stream.borrow_mut().next().await {
                match request {
                    Ok(PasswordInteractionRequest::SetPassword { password: _, control_handle }) => {
                        //  TODO(fxb/108842): Check that the state is waiting::set_password before calling validate.
                        match self.validator.validate() {
                            Ok(res) => {
                                control_handle.shutdown_with_epitaph(zx::Status::OK);
                                return Ok(res);
                            }
                            Err(e) => {
                                let state_publisher = self.hanging_get.borrow().new_publisher();
                                state_publisher
                                    .set(PasswordInteractionWatchStateResponse::Error(e));
                                *self.publisher.borrow_mut() = Some(state_publisher);
                            }
                        }
                    }
                    Ok(PasswordInteractionRequest::WatchState { responder }) => {
                        subscriber.register(responder)?;
                        if let Some(publisher) = &*self.publisher.borrow_mut() {
                            // TODO(fxb/108842): Actually implement the state machine instead of using
                            // Waiting(vec![]) for the initial state.
                            publisher.update(move |state| {
                                if *state == PasswordInteractionWatchStateResponse::Waiting(vec![])
                                {
                                    false
                                } else {
                                    *state = PasswordInteractionWatchStateResponse::Waiting(vec![]);
                                    true
                                }
                            });
                        }
                    }
                    Err(e) => {
                        warn!("Responded with fidl error: {}", e);
                        return Err(anyhow!("Error reading PasswordInteractionRequest: {}", e));
                    }
                }
            }
            return Err(anyhow!("Channel closed before successful validation."));
        }
    }
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_identity_authentication::{
            PasswordInteractionMarker, PasswordInteractionProxy,
        },
        fuchsia_async::Task,
    };

    struct TestValidateSuccess {}
    impl Validator<()> for TestValidateSuccess {
        fn validate(&self) -> Result<(), PasswordError> {
            Ok(())
        }
    }

    struct TestValidateError {}
    impl Validator<()> for TestValidateError {
        fn validate(&self) -> Result<(), PasswordError> {
            Err(PasswordError::TooShort(6))
        }
    }

    fn make_proxy<V: Validator<T> + 'static, T: Sized + 'static>(
        validate: V,
    ) -> PasswordInteractionProxy {
        let (proxy, stream) = create_proxy_and_stream::<PasswordInteractionMarker>()
            .expect("Failed to create password interaction proxy");
        let password_interaction_handler = PasswordInteractionHandler::new(stream, validate);

        Task::local(async move {
            if Rc::new(password_interaction_handler)
                .handle_password_interaction_request_stream()
                .await
                .is_err()
            {
                panic!("Failed to handle password interaction request stream");
            }
        })
        .detach();
        proxy
    }

    #[fuchsia::test]
    async fn interaction_handler_listener_gets_initial_state() {
        let validate = TestValidateSuccess {};
        let proxy = make_proxy(validate);

        let state = proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, PasswordInteractionWatchStateResponse::Waiting(vec![]));
    }

    #[fuchsia::test]
    async fn interaction_handler_listener_is_notified_on_state_change() {
        let validate = TestValidateSuccess {};
        let password_proxy = make_proxy(validate);
        // The initial state should be Waiting.
        let state = password_proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, PasswordInteractionWatchStateResponse::Waiting(vec![]));

        // Send a SetPassword event and verify that channel closes on success.
        let _ = password_proxy.set_password("password").unwrap();
        assert_matches!(
            password_proxy.take_event_stream().next().await.unwrap(),
            Err(fidl::Error::ClientChannelClosed { status: fidl::Status::OK, .. })
        );
    }

    #[fuchsia::test]
    async fn interaction_handler_listener_reports_error() {
        let validate = TestValidateError {};
        let password_proxy = make_proxy(validate);

        // The initial state should be Waiting.
        let state = password_proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, PasswordInteractionWatchStateResponse::Waiting(vec![]));

        // Send a SetPassword event and verify that state is Error.
        let _ = password_proxy.set_password("password").unwrap();
        let state = password_proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, PasswordInteractionWatchStateResponse::Error(PasswordError::TooShort(6)));
    }

    #[fuchsia::test]
    async fn interaction_handler_listener_reports_error_before_first_call_to_watch_state() {
        let validate = TestValidateError {};
        let password_proxy = make_proxy(validate);

        // Send a SetPassword event and verify that state is Error.
        let _ = password_proxy.set_password("password").unwrap();
        let state = password_proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, PasswordInteractionWatchStateResponse::Error(PasswordError::TooShort(6)));

        // Check that the state has been reset to Waiting on following call to WatchState.
        let state = password_proxy.watch_state().await.expect("Failed to get interaction state");
        assert_eq!(state, PasswordInteractionWatchStateResponse::Waiting(vec![]));
    }
}
