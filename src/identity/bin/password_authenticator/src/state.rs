// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[allow(dead_code)]
#[derive(Debug, PartialEq, Eq)]
/// A state machine representing one of three states for a password interaction.
/// The states are waiting_for_time, waiting_for_password, and error.
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
    TooShort,
    TooWeak,
    Incorrect,
    MustWait,
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

#[derive(Debug, Error)]
pub enum StateTransitionError {
    #[allow(dead_code)]
    #[error("Wrong precondition for this action; found state '{:?}'", _0)]
    WrongPrecondition(StateName),
    #[allow(dead_code)]
    #[error("Must keep waiting")]
    WaitTimeNotExpired,
}

impl State {
    /// Attempts to transition from Error to one of the WaitingFor* states.
    ///
    /// Upon successful transiition, returns Ok(()).
    ///
    /// If the state is not currently ERROR, returns Err(StateTransitionError).
    #[allow(dead_code)]
    fn try_leave_error(&mut self) -> Result<(), StateTransitionError> {
        match *self {
            State::Error { error_type: e } => {
                if e == PasswordError::MustWait {
                    *self = State::WaitingForTime;
                } else {
                    *self = State::WaitingForPassword;
                }

                Ok(())
            }
            ref s @ (State::WaitingForPassword | State::WaitingForTime) => {
                Err(StateTransitionError::WrongPrecondition(s.into()))
            }
        }
    }

    /// Attempts to transition from WaitingForTime to WaitingForPassword.
    ///
    /// Upon successful transition, returns Ok(()).
    ///
    /// If the state is not currently WaitingForTime or precondition is not
    /// true, returns Err(StateTransitionError).
    // TODO: precondition is intentionally a simple bool here in order to land
    //       the state machine's skeleton.
    #[allow(dead_code)]
    fn try_password(&mut self, precondition: bool) -> Result<(), StateTransitionError> {
        match *self {
            State::WaitingForTime => {
                if precondition {
                    *self = State::WaitingForPassword;
                    return Ok(());
                }
                *self = State::Error { error_type: PasswordError::MustWait };
                return Err(StateTransitionError::WaitTimeNotExpired);
            }
            State::WaitingForPassword => Ok(()),
            ref s @ State::Error { .. } => Err(StateTransitionError::WrongPrecondition(s.into())),
        }
    }

    // TODO: Consider adding an enter_error transition.
}

#[cfg(test)]
mod test {
    use {super::*, assert_matches::assert_matches};

    #[test]
    fn try_password_from_error() {
        for error in [
            PasswordError::Incorrect,
            PasswordError::TooShort,
            PasswordError::TooWeak,
            PasswordError::MustWait,
        ] {
            let mut state = State::Error { error_type: error };
            assert_matches!(
                state.try_password(true),
                Err(StateTransitionError::WrongPrecondition(StateName::Error))
            );
            assert_eq!(state, State::Error { error_type: error });
        }
    }

    #[test]
    fn try_password_from_waiting_for_password() {
        let mut state = State::WaitingForPassword;
        assert_matches!(state.try_password(true), Ok(()));
        assert_eq!(state, State::WaitingForPassword);
    }

    #[test]
    fn try_password_from_waiting_for_time_success() {
        let mut state = State::WaitingForTime;
        assert_matches!(state.try_password(true), Ok(()));
        assert_eq!(state, State::WaitingForPassword);
    }

    #[test]
    fn try_password_from_waiting_for_time_failure() {
        let mut state = State::WaitingForTime;
        assert_matches!(state.try_password(false), Err(StateTransitionError::WaitTimeNotExpired));
        assert_eq!(state, State::Error { error_type: PasswordError::MustWait });
    }

    #[test]
    fn try_leave_error_from_waiting_for_time() {
        let mut state = State::WaitingForTime;
        assert_matches!(
            state.try_leave_error(),
            Err(StateTransitionError::WrongPrecondition(StateName::WaitingForTime))
        );
        assert_eq!(state, State::WaitingForTime);
    }

    #[test]
    fn try_leave_error_from_waiting_for_password() {
        let mut state = State::WaitingForPassword;
        assert_matches!(
            state.try_leave_error(),
            Err(StateTransitionError::WrongPrecondition(StateName::WaitingForPassword))
        );
        assert_eq!(state, State::WaitingForPassword);
    }

    #[test]
    fn try_leave_error_to_waiting_for_password() {
        for error in [PasswordError::Incorrect, PasswordError::TooShort, PasswordError::TooWeak] {
            let mut state = State::Error { error_type: error };
            assert_matches!(state.try_leave_error(), Ok(()));
            assert_eq!(state, State::WaitingForPassword);
        }
    }

    #[test]
    fn try_leave_error_to_waiting_for_time() {
        let mut state = State::Error { error_type: PasswordError::MustWait };
        assert_matches!(state.try_leave_error(), Ok(()));
        assert_eq!(state, State::WaitingForTime);
    }
}
