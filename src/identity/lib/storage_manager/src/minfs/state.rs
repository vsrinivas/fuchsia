// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[allow(dead_code)]
#[derive(Debug, PartialEq, Eq)]
/// A state machine representing one of three states for a directory protected
/// by a key. The three states are uninitialized, locked, and available. It is
/// only possible to get a handle to the directory when in the available state.
/// For valid states and state transitions, see the following diagram, copied
/// from src/identity/lib/storage_manager/src/lib.rs.
///
//       unlock
//  ┌───────────────┐
//  │               ▼
//  │             ┌─────────────────────────────────────────┐
//  │    ┌──────▶ │                AVAILABLE                │
//  │    │        └─────────────────────────────────────────┘
//  │    │          │                │           ▲
//  │    │ unlock   │ lock           │           │ provision
//  │    │          ▼                │           │
//  │    │        ┌───────────────┐  │           │
//  │    └─────── │    LOCKED     │  │           │
//  │             └───────────────┘  │           │
//  │               │                │ destroy   │
//  │               │ destroy        │           │
//  │               ▼                │           │
//  │             ┌───────────────┐  │           │
//  └──────────── │ UNINITIALIZED │ ◀┘           │
//                └───────────────┘              │
//                  │                            │
//                  └────────────────────────────┘
pub(crate) enum State<T> {
    /// The volume is uninitialized. There may be a valid partition, but it has
    /// not yet been unlocked once to confirm.
    Uninitialized,

    /// The volume is locked/encrypted.
    Locked,

    /// The volume is unlocked/unencrypted.
    Available { internals: T },
}

// Mirror of State for error-logging purposes -- contains the name of the state,
// but not the internal contents.
#[derive(Debug)]
pub enum StateName {
    Uninitialized,
    Locked,
    Available,
}

impl<T> From<&State<T>> for StateName {
    fn from(state: &State<T>) -> Self {
        match state {
            State::Uninitialized => StateName::Uninitialized,
            State::Locked => StateName::Locked,
            State::Available { .. } => StateName::Available,
        }
    }
}

#[derive(Debug, Error)]
pub enum StateTransitionError {
    #[allow(dead_code)]
    #[error("Wrong precondition for this action; found state '{:?}'", _0)]
    WrongPrecondition(StateName),
}

impl<T> State<T> {
    /// Returns a reference to the held internals, or None if we're in a
    /// non-AVAILABLE state.
    pub fn get_internals(&self) -> Option<&T> {
        match self {
            Self::Available { internals } => Some(&internals),
            Self::Locked | Self::Uninitialized => None,
        }
    }

    /// Attempts a transition from AVAILABLE to LOCKED.
    ///
    /// Upon successful transition, returns Ok(internals) with the contents of
    /// the AVAILABLE enum.
    ///
    /// If the state is not currently AVAILABLE, returns
    /// Err(StateTransitionError).
    #[allow(dead_code)]
    pub fn try_lock(&mut self) -> Result<T, StateTransitionError> {
        match std::mem::replace(self, State::Locked) {
            State::Available { internals } => Ok(internals),
            s @ (State::Locked | State::Uninitialized) => {
                // Oops, put it back.
                let res = Err(StateTransitionError::WrongPrecondition((&s).into()));
                *self = s;
                res
            }
        }
    }

    /// Attempts a transition from UNINITIALIZED to AVAILABLE.
    ///
    /// Upon successful transition, returns Ok(()).
    ///
    /// If the state is not currently UNINITIALIZED, returns
    /// Err(StateTransitionError).
    #[allow(dead_code)]
    pub fn try_provision(&mut self, internals: T) -> Result<(), StateTransitionError> {
        // from UNINITIALIZED to AVAILABLE.
        match *self {
            State::Uninitialized => {
                *self = State::Available { internals };
                Ok(())
            }
            ref s @ (State::Locked | State::Available { .. }) => {
                Err(StateTransitionError::WrongPrecondition(s.into()))
            }
        }
    }

    /// Attempts a transition from either LOCKED or UNINITIALIZED to AVAILABLE.
    ///
    /// Upon successful transition, returns Ok(()).
    ///
    /// If the state is not already one of LOCKED or UNINITIALIZED, returns
    /// Err(StateTransitionError).
    #[allow(dead_code)]
    pub fn try_unlock(&mut self, internals: T) -> Result<(), StateTransitionError> {
        // from LOCKED or UNINITIALIZED to AVAILABLE
        match *self {
            State::Available { .. } => {
                Err(StateTransitionError::WrongPrecondition(StateName::Available))
            }
            State::Locked | State::Uninitialized => {
                *self = State::Available { internals };
                Ok(())
            }
        }
    }

    /// Attempts a transition from either AVAILABLE or LOCKED to UNINITIALIZED.
    ///
    /// Upon successful transition from AVAILABLE, returns Ok(Some(internals))
    /// with the contents of the AVAILABLE enum.
    ///
    /// Upon successful transition from LOCKED, returns Ok(None).
    ///
    /// If the state is already UNINITIALIZED, returns
    /// Err(StateTransitionError).
    #[allow(dead_code)]
    pub fn try_destroy(&mut self) -> Result<Option<T>, StateTransitionError> {
        // from AVAILABLE or LOCKED to UNINITIALIZED.
        match std::mem::replace(self, State::Uninitialized) {
            State::Available { internals } => Ok(Some(internals)),
            State::Locked => Ok(None),
            // no need to put it back, but...
            State::Uninitialized => {
                Err(StateTransitionError::WrongPrecondition(StateName::Uninitialized))
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn test_get_internals_from_available() {
        let state = State::Available { internals: 123 };
        assert_matches!(state.get_internals(), Some(123));
    }

    #[test]
    fn test_get_internals_from_locked() {
        let state: State<()> = State::Locked {};
        assert_matches!(state.get_internals(), None);
    }

    #[test]
    fn test_get_internals_from_uninitialized() {
        let state: State<()> = State::Uninitialized {};
        assert_matches!(state.get_internals(), None);
    }

    #[test]
    fn test_try_lock_from_available() {
        // AVAILABLE --lock--> LOCKED
        let mut state = State::Available { internals: 123 };
        assert_matches!(state.try_lock(), Ok(123));
        // state has changed
        assert_eq!(state, State::Locked);
    }

    #[test]
    fn test_try_lock_from_locked() {
        // LOCKED --lock--> <noop>
        let mut state = State::<()>::Locked;
        assert_matches!(
            state.try_lock(),
            Err(StateTransitionError::WrongPrecondition(StateName::Locked))
        );
        // state has not been changed
        assert_eq!(state, State::Locked);
    }

    #[test]
    fn test_try_lock_from_uninitialized() {
        // UNINITIALIZED --lock--> <noop>
        let mut state = State::<()>::Uninitialized;
        assert_matches!(
            state.try_lock(),
            Err(StateTransitionError::WrongPrecondition(StateName::Uninitialized))
        );
        // state has not been changed
        assert_eq!(state, State::Uninitialized);
    }

    #[test]
    fn test_try_provision_from_uninitialized() {
        // UNINITIALIZED --provision--> AVAILABLE
        let mut state = State::<i32>::Uninitialized;
        assert_matches!(state.try_provision(123), Ok(()));
        // state has changed
        assert_eq!(state, State::Available { internals: 123 });
    }

    #[test]
    fn test_try_provision_from_locked() {
        // LOCKED --provision--> <noop>
        let mut state = State::<i32>::Locked;
        assert_matches!(
            state.try_provision(123),
            Err(StateTransitionError::WrongPrecondition(StateName::Locked))
        );
        // state has not been changed
        assert_eq!(state, State::Locked);
    }

    #[test]
    fn test_try_provision_from_available() {
        // AVAILABLE --provision--> <noop>
        let mut state = State::<i32>::Available { internals: 123 };
        assert_matches!(
            state.try_provision(777),
            Err(StateTransitionError::WrongPrecondition(StateName::Available))
        );
        // state has not been changed, internals have not been changed.
        assert_eq!(state, State::Available { internals: 123 });
    }

    #[test]
    fn test_try_unlock_from_uninitialized() {
        // UNINITIALIZED --unlock--> AVAILABLE
        let mut state = State::<i32>::Uninitialized;
        assert_matches!(state.try_unlock(123), Ok(()));
        // state has been changed
        assert_eq!(state, State::Available { internals: 123 });
    }

    #[test]
    fn test_try_unlock_from_locked() {
        // LOCKED --unlock--> AVAILABLE
        let mut state = State::<i32>::Locked;
        assert_matches!(state.try_unlock(123), Ok(()));
        // state has been changed
        assert_eq!(state, State::Available { internals: 123 });
    }

    #[test]
    fn test_try_unlock_from_available() {
        // AVAILABLE --unlock--> <noop>
        let mut state = State::<i32>::Available { internals: 123 };
        assert_matches!(
            state.try_unlock(777),
            Err(StateTransitionError::WrongPrecondition(StateName::Available))
        );
        // state has not been changed, internals have not been changed.
        assert_eq!(state, State::Available { internals: 123 });
    }

    #[test]
    fn test_try_destroy_from_uninitialized() {
        // UNINITIALIZED --destroy--> <noop>
        let mut state = State::<()>::Uninitialized;
        assert_matches!(
            state.try_destroy(),
            Err(StateTransitionError::WrongPrecondition(StateName::Uninitialized))
        );
        // state has not been changed,
        assert_eq!(state, State::Uninitialized);
    }

    #[test]
    fn test_try_destroy_from_locked() {
        // LOCKED --destroy--> UNINITIALIZED
        let mut state = State::<()>::Locked;
        assert_matches!(state.try_destroy(), Ok(None));
        // state has been changed
        assert_eq!(state, State::Uninitialized);
    }

    #[test]
    fn test_try_destroy_from_available() {
        // LOCKED --destroy--> UNINITIALIZED
        let mut state = State::<i32>::Available { internals: 123 };
        assert_matches!(state.try_destroy(), Ok(Some(123)));
        // state has been changed
        assert_eq!(state, State::Uninitialized);
    }
}
