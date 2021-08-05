// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::input_device, async_trait::async_trait};

/// An [`InputHandler`] dispatches InputEvents to an external service. It maintains
/// service connections necessary to handle the events.
///
/// For example, an [`ImeInputHandler`] holds a proxy to IME and keyboard services.
///
/// [`InputHandler`]s process individual input events through [`handle_input_event()`], which can
/// produce multiple events as an outcome. If the [`InputHandler`] sends an InputEvent to a service
/// that consumes the event, then the [`InputHandler`] doesn't return any events.
/// [`handle_input_event()`] should not be invoked concurrently since sequences of events must be
/// preserved. The state created by event n may affect the interpretation of event n+1.
#[async_trait(?Send)]
pub trait InputHandler {
    /// Returns a vector of InputEvents after handling `input_event`.
    ///
    /// # Parameters
    /// `input_event`: The InputEvent to be handled.
    async fn handle_input_event(
        self: std::rc::Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent>;
}
