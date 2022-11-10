// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ota::action::EventHandlerHolder;
use crate::ota::state_machine::Event;

/// This task is used whenever an unrecognised state is given to the action controller
/// It simply generates a Cancel event which will take the OTA state machine back to its start.
pub struct ErrorAction {}

impl ErrorAction {
    pub fn run(event_handler: EventHandlerHolder, message: String) {
        let mut event_handler = event_handler.lock().unwrap();
        event_handler.handle_event(Event::Error(message));
    }
}

#[cfg(test)]
mod test {
    use super::ErrorAction;
    use crate::ota::state_machine::{Event, EventHandler, MockEventHandler};
    use fuchsia_async::{self as fasync};
    use mockall::predicate::eq;
    use std::sync::{Arc, Mutex};

    #[fasync::run_singlethreaded(test)]
    async fn ensure_error_emits_fail_event() {
        let mut event_handler = MockEventHandler::new();
        event_handler
            .expect_handle_event()
            .with(eq(Event::Error("".to_string())))
            .times(1)
            .return_const(());
        let event_handler: Box<dyn EventHandler> = Box::new(event_handler);
        let event_handler = Arc::new(Mutex::new(event_handler));
        ErrorAction::run(event_handler, "Error".to_string());
    }
}
