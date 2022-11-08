// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ota::state_machine::{Event, EventHandler, EventProcessor};

pub struct Controller {
    state_machine: Box<dyn EventProcessor>,
}

impl Controller {
    pub fn new(state_machine: Box<dyn EventProcessor>) -> Self {
        Self { state_machine }
    }
}

impl EventHandler for Controller {
    fn handle_event(&mut self, event: Event) {
        let event_clone = event.clone();
        let state = self.state_machine.process_event(event);
        // Future CLs will pass the new state to actions and screens
        println!("Controller: {:?} -> {:?}", event_clone, state);
    }
}

#[cfg(test)]
mod test {
    use mockall::predicate::*;

    use crate::ota::controller::{Controller, EventHandler};
    use crate::ota::state_machine::{Event, MockEventProcessor, State};

    #[test]
    fn send_states() {
        let mut state_machine = MockEventProcessor::new();
        state_machine
            .expect_process_event()
            .with(eq(Event::Cancel))
            .return_const(Some(State::Home));
        let mut controller = Controller::new(Box::new(state_machine));
        let controller = &mut controller as &mut dyn EventHandler;
        controller.handle_event(Event::Cancel);
    }
}
