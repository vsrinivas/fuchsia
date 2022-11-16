// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_identity_authentication as fidl, fuchsia_zircon::Time, futures::channel::mpsc,
    futures::lock::Mutex, std::cell::RefCell, tracing::warn,
};

/// A state machine that tracks the current state of the interaction between a client attempting to
/// enroll or authenticate and the password authenticator. The state machine will publish the
/// current state using the supplied `mspc::Sender` each time state is updated.
pub(crate) struct StateMachine {
    /// The current state.
    current_state: Mutex<State>,
    /// A `Sender` to publish state updates.
    sender: RefCell<mpsc::Sender<State>>,
}

impl StateMachine {
    /// Constructs a new state machine beginning in the default state.
    pub fn new(sender: mpsc::Sender<State>) -> Self {
        StateMachine {
            current_state: Mutex::new(State::WaitingForPassword),
            sender: RefCell::new(sender),
        }
    }

    /// Returns the current state.
    pub async fn get(&self) -> State {
        *self.current_state.lock().await
    }

    /// Moves from any state to the `Error` state with the supplied `PasswordError`.
    pub async fn set_error(&self, error: PasswordError) {
        let new_state = State::Error { error_type: error };
        let mut current_state_lock = self.current_state.lock().await;
        *current_state_lock = new_state;
        if let Err(err) = self.sender.borrow_mut().try_send(*current_state_lock) {
            warn!("Error sending set_error state: {:?}", err);
        };
    }

    /// If the current state is `Error` then move to the next state indicated by the error.
    /// If the current state is not `Error` this function has no effect.
    pub async fn leave_error(&self) {
        let mut current_state_lock = self.current_state.lock().await;
        if let State::Error { error_type } = &*current_state_lock {
            if let PasswordError::MustWait(_) = error_type {
                // TODO(fxb/113473): When leaving a must MustWait error and entering the
                // WaitingForTime state, start an async task to automatically leave the
                // WaitingForTime state once the time expires. Until we are ready to support
                // MustWait errors just throw an exception if they occur.
                unimplemented!();
            } else {
                // All other errors only describe a problem with a previously supplied password.
                // Once the error is no longer relevant, move back to waiting for another password.
                *current_state_lock = State::WaitingForPassword;
                if let Err(err) = self.sender.borrow_mut().try_send(*current_state_lock) {
                    warn!("Error sending leave_error state: {:?}", err);
                };
            }
        }
    }
}

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
/// The set of states that interaction between a client attempting to enroll or authenticate and the
/// password authenticator may be in. These states reflects the state password authenticator returns
/// in fuchsia.identity.authentication.PasswordInteraction.WatchState,
/// see sdk/fidl/fuchsia.identity.authentication/mechanisms.fidl for further information.
pub(crate) enum State {
    /// Client must wait before another attempt.
    #[allow(dead_code)]
    WaitingForTime { time: Time },
    /// All preconditions are met.
    WaitingForPassword,
    /// Verification failed with the supplied error.
    Error { error_type: PasswordError },
}

impl State {
    /// Convenience function that returns true iff the state is an error.
    pub fn is_error(&self) -> bool {
        matches!(self, State::Error { .. })
    }
}

impl From<State> for fidl::PasswordInteractionWatchStateResponse {
    fn from(s: State) -> Self {
        match s {
            // TODO(fxb/113473): Properly implement WaitingForTime.
            State::WaitingForTime { time } => {
                fidl::PasswordInteractionWatchStateResponse::Waiting(vec![
                    fidl::PasswordCondition::WaitUntil(time.into_nanos()),
                ])
            }
            State::WaitingForPassword => {
                fidl::PasswordInteractionWatchStateResponse::Waiting(vec![
                    fidl::PasswordCondition::SetPassword(fidl::Empty),
                ])
            }
            State::Error { error_type: e } => {
                fidl::PasswordInteractionWatchStateResponse::Error(e.into())
            }
        }
    }
}

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
/// The set of errors that may be encountered during a password interaction.
pub enum PasswordError {
    TooShort(/* minumum_length */ u8),
    TooWeak,
    Incorrect,
    MustWait(/* wait until time */ Time),
    NotWaitingForPassword,
}

impl From<PasswordError> for fidl::PasswordError {
    fn from(error: PasswordError) -> Self {
        match error {
            PasswordError::TooShort(minimum_length) => {
                fidl::PasswordError::TooShort(minimum_length)
            }
            PasswordError::TooWeak => fidl::PasswordError::TooWeak(fidl::Empty),
            PasswordError::Incorrect => fidl::PasswordError::Incorrect(fidl::Empty),
            PasswordError::MustWait(_) => fidl::PasswordError::MustWait(fidl::Empty),
            PasswordError::NotWaitingForPassword => {
                fidl::PasswordError::NotWaitingForPassword(fidl::Empty)
            }
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, assert_matches::assert_matches, futures::StreamExt};
    const TEST_MINIMUM_LENGTH: u8 = 6;

    /// Constructs a `StateMachine` for testing returning a tuple of the state machine and
    /// a `Receiver` that receivers
    fn make_state_machine() -> (StateMachine, mpsc::Receiver<State>) {
        let (sender, receiver) = mpsc::channel(2);
        (StateMachine::new(sender), receiver)
    }

    #[fuchsia::test]
    async fn initialization() {
        let (state_machine, _) = make_state_machine();
        assert_matches!(state_machine.get().await, State::WaitingForPassword);
    }

    #[fuchsia::test]
    async fn try_set_error() {
        let (state_machine, mut receiver) = make_state_machine();
        for error in [
            PasswordError::Incorrect,
            PasswordError::TooShort(TEST_MINIMUM_LENGTH),
            PasswordError::TooWeak,
            PasswordError::MustWait(Time::INFINITE),
        ] {
            state_machine.set_error(error).await;
            assert_eq!(state_machine.get().await, State::Error { error_type: error });
            assert_eq!(receiver.next().await, Some(State::Error { error_type: error }));
        }
    }

    #[fuchsia::test]
    async fn try_leave_error_valid() {
        let (state_machine, mut receiver) = make_state_machine();
        for error in [
            PasswordError::Incorrect,
            PasswordError::TooShort(TEST_MINIMUM_LENGTH),
            PasswordError::TooWeak,
        ] {
            state_machine.set_error(error).await;
            state_machine.leave_error().await;
            assert_matches!(state_machine.get().await, State::WaitingForPassword);
            // The receiver should have received events to first enter then leave the error.
            assert_eq!(receiver.next().await, Some(State::Error { error_type: error }));
            assert_eq!(receiver.next().await, Some(State::WaitingForPassword));
        }
    }

    #[fuchsia::test]
    #[should_panic]
    async fn try_leave_error_invalid() {
        let (state_machine, _) = make_state_machine();

        // Attempting to leave a must wait error leads to a panic because we don't support
        // waiting for time yet.
        state_machine.set_error(PasswordError::MustWait(Time::INFINITE)).await;
        state_machine.leave_error().await;
    }

    #[fuchsia::test]
    async fn try_leave_error_noop() {
        let (state_machine, mut receiver) = make_state_machine();

        // Attempting to leave a waiting state has no effect.
        assert_eq!(state_machine.get().await, State::WaitingForPassword);
        state_machine.leave_error().await;
        assert_eq!(state_machine.get().await, State::WaitingForPassword);
        assert!(receiver.try_next().is_err());
    }
}
