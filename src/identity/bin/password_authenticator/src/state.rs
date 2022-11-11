// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_identity_authentication as fidl, futures::lock::Mutex, std::cell::RefCell};

type ChangeFn = Box<dyn Fn(&State)>;

#[allow(dead_code)]
/// A state machine that tracks the current state of the interaction between a client attempting to
/// enroll or authenticate and the password authenticator. The state machine will optionally call a
/// function with the current state each time that state changes.
pub(crate) struct StateMachine {
    /// The current state.
    current_state: Mutex<State>,
    /// The function to call on state change.
    on_change: RefCell<Option<ChangeFn>>,
}

impl StateMachine {
    /// Constructs a new state machine beginning in the default state.
    #[allow(dead_code)]
    pub fn new() -> Self {
        StateMachine {
            current_state: Mutex::new(State::WaitingForPassword),
            on_change: RefCell::new(None),
        }
    }

    /// Registers a function to be called every time the state is changed.
    #[allow(dead_code)]
    pub fn register_change_function(&self, on_change: Box<dyn Fn(&State)>) {
        self.on_change.replace(Some(on_change));
    }

    /// Returns the current state.
    #[allow(dead_code)]
    pub async fn get(&self) -> State {
        *self.current_state.lock().await
    }

    /// Moves from any state to the `Error` state with the supplied `PasswordError`, calling the
    /// `on_change` function if one exists.
    #[allow(dead_code)]
    pub async fn set_error(&self, error: PasswordError) {
        let new_state = State::Error { error_type: error };
        let mut current_state_lock = self.current_state.lock().await;
        *current_state_lock = new_state;
        if let Some(on_change) = &*self.on_change.borrow() {
            (on_change)(&current_state_lock);
        }
    }

    /// If the current state is `Error` then move to the next state indicated by the error, calling
    /// the `on_change` function if one exists. If the current state is not `Error` this function
    /// has no effect.
    #[allow(dead_code)]
    pub async fn leave_error(&self) {
        let mut current_state_lock = self.current_state.lock().await;
        if let State::Error { error_type } = &*current_state_lock {
            if let PasswordError::MustWait = error_type {
                // TODO(fxb/113473): When leaving a must MustWait error and entering the
                // WaitingForTime state, start an async task to automatically leave the
                // WaitingForTime state once the time expires. Until we are ready to support
                // MustWait errors just throw an exception if they occur.
                unimplemented!();
            } else {
                // All other errors only describe a problem with a previously supplied password.
                // Once the error is no longer relevant, move back to waiting for another password.
                *current_state_lock = State::WaitingForPassword;
                if let Some(on_change) = &*self.on_change.borrow() {
                    (on_change)(&current_state_lock);
                }
            }
        }
    }
}

#[allow(dead_code)]
#[derive(Debug, PartialEq, Eq, Copy, Clone)]
/// The set of states that interaction between a client attempting to enroll or authenticate and the
/// password authenticator may be in. These states reflects the state password authenticator returns
/// in fuchsia.identity.authentication.PasswordInteraction.WatchState,
/// see sdk/fidl/fuchsia.identity.authentication/mechanisms.fidl for further information.
pub(crate) enum State {
    /// Client must wait before another attempt.
    WaitingForTime,
    /// All preconditions are met.
    WaitingForPassword,
    /// Verification failed with the supplied error.
    Error { error_type: PasswordError },
}

#[allow(dead_code)]
#[derive(Debug, PartialEq, Eq, Copy, Clone)]
/// The set of errors that may be encountered during a password interaction.
pub enum PasswordError {
    TooShort(/* minumum_length : */ u8),
    TooWeak,
    Incorrect,
    MustWait,
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
            PasswordError::MustWait => fidl::PasswordError::MustWait(fidl::Empty),
            PasswordError::NotWaitingForPassword => {
                fidl::PasswordError::NotWaitingForPassword(fidl::Empty)
            }
        }
    }
}

// Mirror of State for error-logging purposes--contains the name of the state,
// but not the internal contents.
#[derive(Debug)]
pub enum StateName {
    WaitingForTime,
    WaitingForPassword,
    Error,
}

impl From<&State> for StateName {
    fn from(state: &State) -> Self {
        match state {
            State::WaitingForPassword => StateName::WaitingForPassword,
            State::WaitingForTime => StateName::WaitingForTime,
            State::Error { .. } => StateName::Error,
        }
    }
}

impl State {
    /// Convenience function that returns true iff the state is an error.
    #[allow(dead_code)]
    pub fn is_error(&self) -> bool {
        matches!(self, State::Error { .. })
    }
}

#[cfg(test)]
mod test {
    use {super::*, assert_matches::assert_matches, std::rc::Rc};
    const TEST_MINIMUM_LENGTH: u8 = 6;

    #[fuchsia::test]
    async fn initialization() {
        let state_machine = StateMachine::new();
        assert_matches!(state_machine.get().await, State::WaitingForPassword);
    }

    #[fuchsia::test]
    async fn try_set_error_without_change_function() {
        let state_machine = StateMachine::new();
        for error in [
            PasswordError::Incorrect,
            PasswordError::TooShort(TEST_MINIMUM_LENGTH),
            PasswordError::TooWeak,
            PasswordError::MustWait,
            // Note: repeat the same error to verify that is allowed
            PasswordError::MustWait,
        ] {
            state_machine.set_error(error).await;
            assert_eq!(state_machine.get().await, State::Error { error_type: error });
        }
    }

    #[fuchsia::test]
    async fn try_set_error_with_change_function() {
        let state_machine = StateMachine::new();
        let reported_state = Rc::new(RefCell::new(State::WaitingForTime));
        let reported_state_clone = Rc::clone(&reported_state);
        state_machine.register_change_function(Box::new(move |state| {
            *reported_state_clone.borrow_mut() = *state;
        }));

        for error in [
            PasswordError::Incorrect,
            PasswordError::TooShort(TEST_MINIMUM_LENGTH),
            PasswordError::TooWeak,
            PasswordError::MustWait,
            // Note: repeat the same error to verify that is allowed
            PasswordError::MustWait,
        ] {
            state_machine.set_error(error).await;
            assert_eq!(*reported_state.borrow(), State::Error { error_type: error });
            assert_eq!(state_machine.get().await, State::Error { error_type: error });
        }
    }

    #[fuchsia::test]
    async fn try_leave_error_valid() {
        let state_machine = StateMachine::new();
        let reported_state = Rc::new(RefCell::new(State::WaitingForTime));
        let reported_state_clone = Rc::clone(&reported_state);
        state_machine.register_change_function(Box::new(move |state| {
            *reported_state_clone.borrow_mut() = *state;
        }));

        for error in [
            PasswordError::Incorrect,
            PasswordError::TooShort(TEST_MINIMUM_LENGTH),
            PasswordError::TooWeak,
        ] {
            state_machine.set_error(error).await;
            state_machine.leave_error().await;
            assert_matches!(*reported_state.borrow(), State::WaitingForPassword);
            assert_matches!(state_machine.get().await, State::WaitingForPassword);
        }
    }

    #[fuchsia::test]
    #[should_panic]
    async fn try_leave_error_invalid() {
        let state_machine = StateMachine::new();

        // Attempting to leave a must wait error leads to a panic because we don't support
        // waiting for time yet.
        state_machine.set_error(PasswordError::MustWait).await;
        state_machine.leave_error().await;
    }

    #[fuchsia::test]
    async fn try_leave_error_noop() {
        let state_machine = StateMachine::new();
        let was_called = Rc::new(RefCell::new(false));
        let was_called_clone = Rc::clone(&was_called);
        state_machine.register_change_function(Box::new(move |_| {
            *was_called_clone.borrow_mut() = true;
        }));

        // Attempting to leave a waiting state has no effect.
        assert_eq!(state_machine.get().await, State::WaitingForPassword);
        state_machine.leave_error().await;
        assert_eq!(state_machine.get().await, State::WaitingForPassword);
        assert!(!*was_called.borrow());
    }
}
