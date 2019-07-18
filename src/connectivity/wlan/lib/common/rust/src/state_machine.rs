// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    convert::{AsMut, AsRef},
    fmt::Debug,
    marker::PhantomData,
    ops::{Deref, DerefMut},
};

///! Generic state machine implementation with compile time checked state transitions.

/// Wrapper to safely replace states of state machine which don't consume their states.
/// Use this wrapper if state transitions are performed on mutable references rather than consumed
/// states.
/// Example:
/// ```
/// fn on_event(event: Event, state_machine: &mut StateMachine<Foo>) {
///     state_machine.replace_state(|state| match state {
///         State::A(_) => match event {
///             Event::A => State::B,
///             _ => state,
///         }
///         State::B => {
///             warn!("cannot receive events in State::B");
///             state
///         }
///     })
/// }
/// ```
pub struct StateMachine<S> {
    state: Option<S>,
}
impl<S: Debug> Debug for StateMachine<S> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "State: {:?}", self.state)
    }
}
impl<S: PartialEq> PartialEq for StateMachine<S> {
    fn eq(&self, other: &Self) -> bool {
        self.state == other.state
    }
}

impl<S> StateMachine<S> {
    /// Constructs a new `StateMachine`.
    pub fn new(state: S) -> Self {
        StateMachine { state: Some(state) }
    }

    /// Replaces the current state with one lazily constructed by `map`.
    pub fn replace_state<F>(&mut self, map: F) -> &mut Self
    where
        F: FnOnce(S) -> S,
    {
        // Safe to unwrap: `state` can never be None.
        self.state = Some(map(self.state.take().unwrap()));
        self
    }

    /// Replaces the current state with `new_state`.
    pub fn replace_state_with(&mut self, new_state: S) -> &mut Self {
        self.state = Some(new_state);
        self
    }

    /// Consumes the state machine and returns its current state.
    pub fn into_state(self) -> S {
        // Safe to unwrap: `state` can never be None.
        self.state.unwrap()
    }
}
impl<S> AsRef<S> for StateMachine<S> {
    fn as_ref(&self) -> &S {
        // Safe to unwrap: `state` can never be None.
        &self.state.as_ref().unwrap()
    }
}
impl<S> AsMut<S> for StateMachine<S> {
    fn as_mut(&mut self) -> &mut S {
        // Safe to unwrap: `state` can never be None.
        self.state.as_mut().unwrap()
    }
}
impl<S> Deref for StateMachine<S> {
    type Target = S;

    fn deref(&self) -> &Self::Target {
        self.as_ref()
    }
}
impl<S> DerefMut for StateMachine<S> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.as_mut()
    }
}

/// A `StateTransition` defines valid transitions from one state into another.
/// Implement `StateTransition` on the given `State` struct to define a new
/// state transition. Alternatively, use the convenience macro
/// `state_machine!`.
pub trait StateTransition<S> {
    #[doc(hidden)]
    fn __internal_into_state(new_state: S) -> State<S>;
}

/// Defines a state machine's initial state and its allowed transitions.
/// Example:
/// ```
/// state_machine!(
///     () => A,
///     A => B,
///     B => [C, A],
///     C => A
/// );
/// ```
#[macro_export]
macro_rules! state_machine {
    (@internal () => $initial:ty) => {
        impl InitialState for $initial {}
    };
    // Initial state followed by multiple lines of 1:N mappings.
    (() => $initial:ty, $($rest:tt)*) => {
        state_machine!(@internal () => $initial);
        state_machine!(@internal $($rest)*);
    };
    // 1:N mapping
    (@internal $from:ty => [$($to:ty),+]) => {
        $( state_machine!(@internal $from => $to); )*
    };
    (@internal $from:ty => [$($to:ty),+], $($rest:tt)*) => {
        $( state_machine!(@internal $from => $to); )*
        state_machine!(@internal $($rest)*);
    };
    // 1:1 mapping
    (@internal $from:ty => $to:ty) => {
        impl StateTransition<$to> for State<$from> {
            fn __internal_into_state(new_state: $to) -> State<$to> {
                State::<$to>::__internal_new(new_state)
            }
        }
    };
    (@internal $from:ty => $to:ty, $($rest:tt)*) => {
        state_machine!(@internal $from => $to);
        state_machine!(@internal $($rest)*);
    };
    (@internal) => {};
}

/// Marker for creating a new initial state.
/// This trait enforces that only the initial state can be created manually while all others must
/// be created through a proper state transition.
pub trait InitialState {}

/// Wrapper struct for a state S. Use in combination with `StateTransition`.
pub struct State<S> {
    pub data: S,
    // Prevent public from constructing a State while granting access to `data` for partial
    // matching multiple states.
    __internal_phantom: PhantomData<S>,
}
impl<S> State<S> {
    /// Construct the initial state of a state machine.
    pub fn new(data: S) -> State<S>
    where
        S: InitialState,
    {
        Self::__internal_new(data)
    }

    // Note: must be public to be accessible through `state_machine!` macro.
    #[doc(hidden)]
    pub fn __internal_new(data: S) -> State<S> {
        Self { data, __internal_phantom: PhantomData }
    }

    /// Releases the captured state data `S` and provides a transition instance
    /// to perform a compile time checked state transition.
    /// Use this function when the state data `S` is shared between multiple
    /// states.
    pub fn release_data(self) -> (Transition<S>, S) {
        (Transition { _phantom: PhantomData }, self.data)
    }

    pub fn into_state<T>(self, new_state: T) -> State<T>
    where
        State<S>: StateTransition<T>,
    {
        Self::__internal_into_state(new_state)
    }
}
impl<S> Deref for State<S> {
    type Target = S;

    fn deref(&self) -> &Self::Target {
        &self.data
    }
}
impl<S> DerefMut for State<S> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.data
    }
}

impl<S: Debug> Debug for State<S> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "State data: {:?}", self.data)
    }
}
impl<S: PartialEq> PartialEq for State<S> {
    fn eq(&self, other: &Self) -> bool {
        self.data == other.data
    }
}

/// Wrapper struct to enforce compile time checked state transitions of one state into another.
pub struct Transition<S> {
    _phantom: PhantomData<S>,
}

impl<S> Transition<S> {
    pub fn into_state<T>(self, new_state: T) -> State<T>
    where
        State<S>: StateTransition<T>,
    {
        State::<S>::__internal_into_state(new_state)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Default, Debug)]
    struct SharedStateData {
        foo: u8,
    }

    pub struct A;
    pub struct B(SharedStateData);
    pub struct C(SharedStateData);
    state_machine!(
        () => A,
        A => B,
        B => [C, A],
        C => [A]
    );

    enum States {
        A(State<A>),
        B(State<B>),
    }

    #[test]
    fn state_transitions() {
        let state = State::new(A);
        // Regular state transition:
        let state = state.into_state(B(SharedStateData::default()));

        // Modify and share state data with new state.
        let (transition, mut data) = state.release_data();
        data.0.foo = 5;
        let state = transition.into_state(C(data.0));
        assert_eq!(state.0.foo, 5);
    }

    #[test]
    fn state_machine() {
        let mut state_machine = StateMachine::new(States::A(State::new(A)));
        state_machine.replace_state(|state| match state {
            States::A(state) => States::B(state.into_state(B(SharedStateData::default()))),
            _ => state,
        });
        match state_machine.into_state() {
            States::B(State{ data: B(SharedStateData { foo: 0 }), ..}) => (),
            _ => panic!("unexpected state"),
        }
    }
}
