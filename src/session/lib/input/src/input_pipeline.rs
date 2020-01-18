// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::input_device, crate::input_handler, futures::StreamExt};

/// An [`InputPipeline`] manages input events from input devices through input handlers.
///
/// # Example
/// ```
/// let keyboard_binding: KeyboardBinding = InputDeviceBinding::any_device().await?;
/// let touch_binding: touch::TouchBinding = InputDeviceBinding::any_device().await?;
///
/// let ime_handler =
///     ImeHandler::new(scene_manager.session.clone(), scene_manager.compositor_id).await?;
/// let touch_handler = TouchHandler::new(
///     scene_manager.session.clone(),
///     scene_manager.compositor_id,
///     scene_manager.display_width as i64,
///     scene_manager.display_height as i64,
/// ).await?;
///
/// let input_pipeline = InputPipeline::new(
///     vec![Box::new(keyboard_binding)],
///     vec![Box::new(ime_handler), Box::new(touch_handler)],
/// );
/// input_pipeline.handle_input_events().await;
/// ```
pub struct InputPipeline {
    /// The bindings to input devices that this [`InputPipeline`] manages.
    device_bindings: Vec<Box<dyn input_device::InputDeviceBinding>>,

    /// The input handlers that will dispatch InputEvents from the `device_bindings`.
    /// The order of handlers in `input_handlers` is the order
    input_handlers: Vec<Box<dyn input_handler::InputHandler>>,
}

impl InputPipeline {
    /// Creates a new [`InputPipeline`].
    ///
    /// # Parameters
    /// - `device_bindings`: The bindings to input devices that the [`InputPipeline`] manages.
    /// - `input_handlers`: The input handlers that the [`InputPipeline`] sends InputEvents to.
    ///                     Handlers process InputEvents in the order that they appear in
    ///                     `input_handlers`.
    pub fn new(
        device_bindings: Vec<Box<dyn input_device::InputDeviceBinding>>,
        input_handlers: Vec<Box<dyn input_handler::InputHandler>>,
    ) -> Self {
        InputPipeline { device_bindings: device_bindings, input_handlers: input_handlers }
    }

    /// Sends all InputEvents received by the `device_bindings` through all `input_handlers`.
    pub async fn handle_input_events(mut self) {
        let mut event_streams = vec![];
        for device in &mut self.device_bindings {
            event_streams.push(device.input_event_stream());
        }
        let mut event_stream = futures::stream::select_all(event_streams);

        while let Some(input_event) = event_stream.next().await {
            let mut result_events: Vec<input_device::InputEvent> = vec![input_event];
            for input_handler in &mut self.input_handlers {
                let mut next_result_events: Vec<input_device::InputEvent> = vec![];
                for event in result_events {
                    next_result_events.append(&mut input_handler.handle_input_event(event).await);
                }
                result_events = next_result_events;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::fake_input_device_binding,
        crate::fake_input_handler,
        crate::input_device::{self, InputDeviceBinding},
        crate::mouse,
        fidl_fuchsia_ui_input as fidl_ui_input, fuchsia_async as fasync,
        futures::channel::mpsc::Sender,
        rand::Rng,
        std::collections::HashSet,
    };

    /// Returns the InputEvent sent over `sender`.
    ///
    /// # Parameters
    /// - `sender`: The channel to send the InputEvent over.
    fn send_input_event(mut sender: Sender<input_device::InputEvent>) -> input_device::InputEvent {
        let mut rng = rand::thread_rng();
        let input_event = input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse::MouseEvent {
                movement_x: rng.gen_range(0, 10),
                movement_y: rng.gen_range(0, 10),
                phase: fidl_ui_input::PointerEventPhase::Move,
                buttons: HashSet::new(),
            }),
            device_descriptor: input_device::InputDeviceDescriptor::Mouse(
                mouse::MouseDeviceDescriptor { device_id: 1 },
            ),
        };
        match sender.try_send(input_event.clone()) {
            Err(_) => assert!(false),
            _ => {}
        }

        input_event
    }

    /// Tests that an input pipeline handles events from multiple devices.
    #[fasync::run_singlethreaded(test)]
    async fn multiple_devices_single_handler() {
        // Create two fake device bindings.
        let first_device_binding = fake_input_device_binding::FakeInputDeviceBinding::new();
        let first_device_binding_sender = first_device_binding.input_event_sender();
        let second_device_binding = fake_input_device_binding::FakeInputDeviceBinding::new();
        let second_device_binding_sender = second_device_binding.input_event_sender();

        // Create two fake input handlers.
        let (handler_event_sender, mut handler_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let input_handler = fake_input_handler::FakeInputHandler::new(handler_event_sender);

        // Build the input pipeline.
        let input_pipeline = InputPipeline::new(
            vec![Box::new(first_device_binding), Box::new(second_device_binding)],
            vec![Box::new(input_handler)],
        );

        // Send an input event from each device.
        let first_device_event = send_input_event(first_device_binding_sender);
        let second_device_event = send_input_event(second_device_binding_sender);

        // Run the pipeline.
        fasync::spawn(async {
            input_pipeline.handle_input_events().await;
        });

        // Assert the handler receives the events.
        let first_handled_event = handler_event_receiver.next().await;
        assert_eq!(first_handled_event, Some(first_device_event));

        let second_handled_event = handler_event_receiver.next().await;
        assert_eq!(second_handled_event, Some(second_device_event));
    }

    /// Tests that an input pipeline handles events through multiple input handlers.
    #[fasync::run_singlethreaded(test)]
    async fn single_device_multiple_handlers() {
        // Create a fake device binding.
        let device_binding = fake_input_device_binding::FakeInputDeviceBinding::new();
        let device_binding_sender = device_binding.input_event_sender();

        // Create two fake input handlers.
        let (first_handler_event_sender, mut first_handler_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let first_input_handler =
            fake_input_handler::FakeInputHandler::new(first_handler_event_sender);
        let (second_handler_event_sender, mut second_handler_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let second_input_handler =
            fake_input_handler::FakeInputHandler::new(second_handler_event_sender);

        // Build the input pipeline.
        let input_pipeline = InputPipeline::new(
            vec![Box::new(device_binding)],
            vec![Box::new(first_input_handler), Box::new(second_input_handler)],
        );

        // Send an input event.
        let input_event = send_input_event(device_binding_sender);

        // Run the pipeline.
        fasync::spawn(async {
            input_pipeline.handle_input_events().await;
        });

        // Assert both handlers receive the event.
        let first_handler_event = first_handler_event_receiver.next().await;
        assert_eq!(first_handler_event, Some(input_event.clone()));
        let second_handler_event = second_handler_event_receiver.next().await;
        assert_eq!(second_handler_event, Some(input_event));
    }
}
