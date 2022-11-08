// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use mockall::*;

// This component maps the current state with a new event to produce a
// new state.
// The only statue held by the state machine is the current state.

#[derive(Debug, Clone)]
pub enum State {
    Home,
}

impl PartialEq for State {
    fn eq(&self, other: &Self) -> bool {
        std::mem::discriminant(self) == std::mem::discriminant(other)
    }
}

#[derive(Debug, Clone)]
pub enum Event {
    Cancel,
}

// This tests only for enum entry not the value contained in the enum.
impl PartialEq for Event {
    fn eq(&self, other: &Self) -> bool {
        std::mem::discriminant(self) == std::mem::discriminant(other)
    }
}

pub trait EventHandler {
    fn handle_event(&mut self, event: Event);
}

#[cfg_attr(test, automock)]
pub trait EventProcessor {
    fn process_event(&mut self, event: Event) -> Option<State>;
}

pub struct StateMachine {
    current_state: State,
}

impl StateMachine {
    pub fn new(state: State) -> Self {
        Self { current_state: state }
    }

    pub fn event(&mut self, event: Event) -> Option<State> {
        #[cfg(feature = "debug_logging")]
        println!("====== SM: state {:?}, event: {:?}", self.current_state, event);
        let new_state = match (&self.current_state, event) {
            // Any cancel sends us back to the start.
            (_, Event::Cancel) => Some(State::Home),
        };
        if new_state.is_some() {
            #[cfg(feature = "debug_logging")]
            println!("====== New state is {:?}", new_state);
            self.current_state = new_state.as_ref().unwrap().clone();
        }
        new_state
    }
}

#[cfg_attr(test, automock)]
impl EventProcessor for StateMachine {
    fn process_event(&mut self, event: Event) -> Option<State> {
        self.event(event)
    }
}

#[cfg(test)]
mod test {
    // TODO(b/258049697): Tests to check the expected flow through more than one state.
    // c.f. https://cs.opensource.google/fuchsia/fuchsia/+/main:src/recovery/system/src/fdr.rs;l=183.

    use crate::ota::state_machine::{Event, State, StateMachine};
    use lazy_static::lazy_static;

    lazy_static! {
        static ref STATES: Vec<State> = vec![State::Home,];
        static ref EVENTS: Vec<Event> = vec![Event::Cancel,];
    }

    // TODO(b/258049617): Enable this when variant_count is in the allowed features list
    // This will enable a check to make sure all events and states are used
    // #[test]
    // fn check_test_validity() {
    //     assert_eq!(std::mem::variant_count::<State>(), STATES.len());
    //     assert_eq!(std::mem::variant_count::<Event>(), EVENTS.len());
    // }

    #[test]
    fn ensure_all_state_and_event_combos_can_not_crash_state_machine() {
        let mut sm = StateMachine::new(State::Home);
        // Generate all possible combinations of States and Events
        for state in STATES.iter() {
            for event in EVENTS.iter() {
                // Set the current state here because sm.event() can change it
                sm.current_state = state.clone();
                let _new_state = sm.event(event.clone());
                if let Some(new_state) = _new_state {
                    assert_eq!(new_state, sm.current_state);
                }
            }
        }
    }
}
