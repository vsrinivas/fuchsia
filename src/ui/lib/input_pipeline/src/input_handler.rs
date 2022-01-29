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
/// produce multiple events as an outcome. If the [`InputHandler`] sends an [`InputEvent`] to a service
/// that consumes the event, then the [`InputHandler`] updates the [`InputEvent.handled`] accordingly.
///
/// [`handle_input_event()`] should not be invoked concurrently since sequences of events must be
/// preserved. The state created by event n may affect the interpretation of event n+1.
#[async_trait(?Send)]
pub trait InputHandler {
    /// Returns a vector of InputEvents to propagate to the next InputHandler.
    ///
    /// * The vector may be empty if, e.g., the handler chose to buffer the
    ///   event.
    /// * The vector may have multiple events if, e.g.,
    ///   * the handler chose to release previously buffered events, or
    ///   * the handler unpacked a single event into multiple events
    ///
    /// # Parameters
    /// `input_event`: The InputEvent to be handled.
    async fn handle_input_event(
        self: std::rc::Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent>;
}

/// An [`UnhandledInputHandler`] is like an [`InputHandler`], but only deals in unhandled events.
#[async_trait(?Send)]
pub trait UnhandledInputHandler {
    /// Returns a vector of InputEvents to propagate to the next InputHandler.
    ///
    /// * The vector may be empty if, e.g., the handler chose to buffer the
    ///   event.
    /// * The vector may have multiple events if, e.g.,
    ///   * the handler chose to release previously buffered events, or
    ///   * the handler unpacked a single event into multiple events
    ///
    /// # Parameters
    /// `input_event`: The InputEvent to be handled.
    async fn handle_unhandled_input_event(
        self: std::rc::Rc<Self>,
        unhandled_input_event: input_device::UnhandledInputEvent,
    ) -> Vec<input_device::InputEvent>;
}

#[async_trait(?Send)]
impl<T> InputHandler for T
where
    T: UnhandledInputHandler,
{
    async fn handle_input_event(
        self: std::rc::Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event.handled {
            input_device::Handled::Yes => return vec![input_event],
            input_device::Handled::No => {
                T::handle_unhandled_input_event(
                    self,
                    input_device::UnhandledInputEvent {
                        device_event: input_event.device_event,
                        device_descriptor: input_event.device_descriptor,
                        event_time: input_event.event_time,
                    },
                )
                .await
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{async_trait, InputHandler as _, UnhandledInputHandler},
        crate::input_device::{
            Handled, InputDeviceDescriptor, InputDeviceEvent, InputEvent, UnhandledInputEvent,
        },
        fuchsia_zircon as zx,
        futures::{channel::mpsc, StreamExt as _},
        pretty_assertions::assert_eq,
        test_case::test_case,
    };

    struct FakeUnhandledInputHandler {
        event_sender: mpsc::UnboundedSender<UnhandledInputEvent>,
        mark_events_handled: bool,
    }

    #[async_trait(?Send)]
    impl UnhandledInputHandler for FakeUnhandledInputHandler {
        async fn handle_unhandled_input_event(
            self: std::rc::Rc<Self>,
            unhandled_input_event: UnhandledInputEvent,
        ) -> Vec<InputEvent> {
            self.event_sender
                .unbounded_send(unhandled_input_event.clone())
                .expect("failed to send");
            vec![InputEvent::from(unhandled_input_event).into_handled_if(self.mark_events_handled)]
        }
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn blanket_impl_passes_unhandled_events_to_wrapped_handler() {
        let (event_sender, mut event_receiver) = mpsc::unbounded();
        let handler = std::rc::Rc::new(FakeUnhandledInputHandler {
            event_sender,
            mark_events_handled: false,
        });
        handler
            .clone()
            .handle_input_event(InputEvent {
                device_event: InputDeviceEvent::Fake,
                device_descriptor: InputDeviceDescriptor::Fake,
                event_time: zx::Time::from_nanos(1),
                handled: Handled::No,
            })
            .await;
        assert_eq!(
            event_receiver.next().await,
            Some(UnhandledInputEvent {
                device_event: InputDeviceEvent::Fake,
                device_descriptor: InputDeviceDescriptor::Fake,
                event_time: zx::Time::from_nanos(1),
            })
        )
    }

    #[test_case(false; "not marked by handler")]
    #[test_case(true; "marked by handler")]
    #[fuchsia::test(allow_stalls = false)]
    async fn blanket_impl_propagates_wrapped_handlers_return_value(mark_events_handled: bool) {
        let (event_sender, _event_receiver) = mpsc::unbounded();
        let handler =
            std::rc::Rc::new(FakeUnhandledInputHandler { event_sender, mark_events_handled });
        let input_event = InputEvent {
            device_event: InputDeviceEvent::Fake,
            device_descriptor: InputDeviceDescriptor::Fake,
            event_time: zx::Time::from_nanos(1),
            handled: Handled::No,
        };
        let expected_propagated_event = input_event.clone().into_handled_if(mark_events_handled);
        pretty_assertions::assert_eq!(
            handler.clone().handle_input_event(input_event).await.as_slice(),
            [expected_propagated_event]
        );
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn blanket_impl_filters_handled_events_from_wrapped_handler() {
        let (event_sender, mut event_receiver) = mpsc::unbounded();
        let handler = std::rc::Rc::new(FakeUnhandledInputHandler {
            event_sender,
            mark_events_handled: false,
        });
        handler
            .clone()
            .handle_input_event(InputEvent {
                device_event: InputDeviceEvent::Fake,
                device_descriptor: InputDeviceDescriptor::Fake,
                event_time: zx::Time::from_nanos(1),
                handled: Handled::Yes,
            })
            .await;

        // Drop `handler` to dispose of `event_sender`. This ensures
        // that `event_receiver.next()` does not block.
        std::mem::drop(handler);

        assert_eq!(event_receiver.next().await, None)
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn blanket_impl_propagates_handled_events_to_next_handler() {
        let (event_sender, _event_receiver) = mpsc::unbounded();
        let handler = std::rc::Rc::new(FakeUnhandledInputHandler {
            event_sender,
            mark_events_handled: false,
        });
        assert_eq!(
            handler
                .clone()
                .handle_input_event(InputEvent {
                    device_event: InputDeviceEvent::Fake,
                    device_descriptor: InputDeviceDescriptor::Fake,
                    event_time: zx::Time::from_nanos(1),
                    handled: Handled::Yes,
                })
                .await
                .as_slice(),
            [InputEvent {
                device_event: InputDeviceEvent::Fake,
                device_descriptor: InputDeviceDescriptor::Fake,
                event_time: zx::Time::from_nanos(1),
                handled: Handled::Yes,
            }]
        );
    }
}
