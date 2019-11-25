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
///
pub use wlan_statemachine_macro::statemachine;

/// Wrapper to safely replace states of state machine which don't consume their states.
/// Use this wrapper if state transitions are performed on mutable references rather than consumed
/// states.
/// Example:
/// ```
/// fn on_event(event: Event, statemachine: &mut StateMachine<Foo>) {
///     statemachine.replace_state(|state| match state {
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
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
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
/// `statemachine!`.
pub trait StateTransition<S> {
    #[doc(hidden)]
    fn __internal_transition_to(new_state: S) -> State<S>;
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

    // Note: must be public to be accessible through `statemachine!` macro.
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

    pub fn transition_to<T>(self, new_state: T) -> State<T>
    where
        State<S>: StateTransition<T>,
    {
        Self::__internal_transition_to(new_state)
    }

    pub fn apply<T, E>(self, transition: T) -> E
    where
        T: MultiTransition<E, S>,
    {
        transition.from(self)
    }
}

/// Convenience functions for unit testing.
/// Note: Do ONLY use in tests!
pub mod testing {
    use super::*;

    /// Creates a new State with the given data.
    pub fn new_state<S>(data: S) -> State<S> {
        State::<S>::__internal_new(data)
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
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
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

pub trait MultiTransition<E, S> {
    fn from(self, state: State<S>) -> E;
    fn via(self, transition: Transition<S>) -> E;
}

impl<S> Transition<S> {
    pub fn to<T>(self, new_state: T) -> State<T>
    where
        State<S>: StateTransition<T>,
    {
        State::<S>::__internal_transition_to(new_state)
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
    statemachine!(
        enum States,

        () => A,
        A => B,
        B => [C, A],
        C => [A],
        // Test duplicate transitions
        B => C,
        B => [A, C],
    );

    fn multi_transition(foo: u8) -> BTransition {
        match foo {
            0 => BTransition::ToA(A),
            _ => BTransition::ToC(C(SharedStateData::default())),
        }
    }

    #[derive(Debug)]
    pub struct A2;
    #[derive(Debug)]
    pub struct B2;
    statemachine!(
        // Test derive attribute.
        #[derive(Debug)]
        enum States2,
        () => A2,
        A2 => B2,
        A2 => B2, // Test duplicate transitions
    );

    #[test]
    fn state_transitions() {
        let state = State::new(A);
        // Regular state transition:
        let state = state.transition_to(B(SharedStateData::default()));

        // Modify and share state data with new state.
        let (transition, mut data) = state.release_data();
        data.0.foo = 5;
        let state = transition.to(C(data.0));
        assert_eq!(state.0.foo, 5);
    }

    #[test]
    fn state_transition_self_transition() {
        let state = State::new(A);

        let state = state.transition_to(B(SharedStateData { foo: 5 }));
        let (transition, data) = state.release_data();
        assert_eq!(data.0.foo, 5);

        let state = transition.to(B(SharedStateData { foo: 2 }));
        let (_, data) = state.release_data();
        assert_eq!(data.0.foo, 2);
    }

    #[test]
    fn statemachine() {
        let mut statemachine = StateMachine::new(States::A(State::new(A)));
        statemachine.replace_state(|state| match state {
            States::A(state) => state.transition_to(B(SharedStateData::default())).into(),
            _ => state,
        });

        match statemachine.into_state() {
            States::B(State { data: B(SharedStateData { foo: 0 }), .. }) => (),
            _ => panic!("unexpected state"),
        }
    }

    #[test]
    fn transition_enums() {
        let state = State::new(A).transition_to(B(SharedStateData::default()));
        let transition = multi_transition(0);
        match state.apply(transition) {
            States::A(_) => (),
            _ => panic!("expected transition into A"),
        };
    }

    #[test]
    fn transition_enums_release() {
        let state = State::new(A).transition_to(B(SharedStateData::default()));
        let (transition, _data) = state.release_data();

        let target = multi_transition(0);
        match target.via(transition) {
            States::A(_) => (),
            _ => panic!("expected transition into A"),
        };
    }

    #[test]
    fn transition_enums_branching() {
        let state = State::new(A).transition_to(B(SharedStateData::default()));
        let (transition, _data) = state.release_data();

        let target = multi_transition(1);
        match target.via(transition) {
            States::C(_) => (),
            _ => panic!("expected transition into C"),
        };
    }

    #[test]
    fn generated_enum() {
        let _state_machine: States2 = match States2::A2(State::new(A2)) {
            // Test generated From impls:
            States2::A2(state) => state.transition_to(B2).into(),
            other => panic!("expected state A to be active: {:?}", other),
        };

        // No assertion needed. This test verifies that the enum struct "States2" was generated
        // properly.
    }
}
