// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::model::error::ModelError, std::fmt::Debug};

/// The payload of a walk state.
pub trait WalkStateUnit<Rhs = Self> {
    type Error: Into<ModelError>;

    /// Validates whether the next state in a walk state is valid or not when advancing or
    /// finalizing.
    fn validate_next(&self, next_state: &Rhs) -> Result<(), Self::Error>;

    /// The error that is returned by the walk state when attempting to finalize with an invalid
    /// state.
    fn finalize_error() -> Self::Error;
}

/// WalkState contains all information required to traverse offer and expose chains in a tree
/// tracing routes from any point back to their originating source. This includes in the most
/// complex case traversing from a use to offer chain, back through to an expose chain.
#[derive(Debug, Clone)]
pub enum WalkState<T: WalkStateUnit + Debug + Clone> {
    Begin,
    Intermediate(T),
    Finished(T),
}

impl<T: WalkStateUnit + Debug + Clone> WalkState<T> {
    /// Constructs a new WalkState representing the start of a walk.
    pub fn new() -> Self {
        WalkState::Begin
    }

    /// Constructs a new WalkState representing the start of a walk after a
    /// hard coded initial node. Used to represent framework state with static state definitions
    /// such as rights in directories or filters in events.
    pub fn at(state: T) -> Self {
        WalkState::Intermediate(state)
    }

    /// Advances the WalkState if and only if the state passed satisfies the validation.
    pub fn advance(&self, next_state: Option<T>) -> Result<Self, ModelError> {
        match (&self, &next_state) {
            (WalkState::Finished(_), _) => {
                panic!("Attempting to advance a finished WalkState");
            }
            (WalkState::Begin, Some(proposed_state)) => {
                Ok(WalkState::Intermediate(proposed_state.clone()))
            }
            (WalkState::Intermediate(last_seen_state), Some(proposed_state)) => {
                last_seen_state.validate_next(proposed_state).map_err(|e| e.into())?;
                Ok(WalkState::Intermediate(proposed_state.clone()))
            }
            (_, None) => Ok(self.clone()),
        }
    }

    /// Whether or not the state is Finished.
    pub fn is_finished(&self) -> bool {
        match self {
            WalkState::Finished(_) => true,
            _ => false,
        }
    }

    /// Finalizes the state preventing future modification, this is called when the walker arrives
    /// at a node with a source of Framework, Builtin, Namespace or Self. The provided |state|
    /// should always be the state at the CapabilitySource.
    pub fn finalize(&self, state: Option<T>) -> Result<Self, ModelError> {
        if self.is_finished() {
            panic!("Attempted to finalized a finished walk state.");
        }
        if state.is_none() {
            return Err(T::finalize_error().into());
        }
        let final_state = state.unwrap();
        match self {
            WalkState::Begin => Ok(WalkState::Finished(final_state)),
            WalkState::Intermediate(last_seen_state) => {
                last_seen_state.validate_next(&final_state).map_err(|e| e.into())?;
                Ok(WalkState::Finished(final_state))
            }
            WalkState::Finished(_) => {
                unreachable!("Captured earlier");
            }
        }
    }
}
