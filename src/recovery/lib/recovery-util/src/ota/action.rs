// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ota::actions::error::ErrorAction;
use crate::ota::actions::factory_reset::FactoryResetAction;
use crate::ota::state_machine::{EventHandler, State, StateHandler};
use std::sync::{Arc, Mutex};

pub type EventHandlerHolder = Arc<Mutex<Box<dyn EventHandler>>>;

/// This file initiates all the non-ui, background actions that are required to satisfy
/// the OTA UX UI slides. In general some states cause a background task to be run which
/// may  produce one or more state machine events. Actions may reboot the device.
pub struct Action {
    event_handler: EventHandlerHolder,
}

impl Action {
    pub fn new(event_handler: EventHandlerHolder) -> Self {
        Self { event_handler }
    }
}

impl StateHandler for Action {
    fn handle_state(&mut self, state: State) {
        let event_handler = self.event_handler.clone();
        // There are six states that will need background actions
        // They will be added in future CLs
        match state {
            State::FactoryReset => FactoryResetAction::run(event_handler),
            _ => ErrorAction::run(
                event_handler,
                format!("Error: Action called with unhandled state: {:?}", state),
            ),
        };
    }
}

#[cfg(test)]
mod test {
    use super::{Action, StateHandler};
    use crate::ota::state_machine::{Event, MockEventHandler, State};
    use fuchsia_async::{self as fasync};
    use mockall::predicate::eq;
    use std::sync::{Arc, Mutex};

    #[fasync::run_singlethreaded(test)]
    async fn unhandled_state_calls_error_task() {
        let mut event_handler = MockEventHandler::new();
        event_handler
            .expect_handle_event()
            .with(eq(Event::Error(String::new())))
            .times(1)
            .return_const(());
        let mut action = Action::new(Arc::new(Mutex::new(Box::new(event_handler))));
        action.handle_state(State::Home);
    }
}
