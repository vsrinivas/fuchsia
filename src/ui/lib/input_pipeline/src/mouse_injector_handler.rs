// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    crate::mouse_binding,
    crate::mouse_config,
    crate::utils::{CursorMessage, Position, Size},
    anyhow::{anyhow, Context, Error, Result},
    async_trait::async_trait,
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_input_interaction_observation as interaction_observation,
    fidl_fuchsia_input_report::Range,
    fidl_fuchsia_ui_pointerinjector as pointerinjector,
    fidl_fuchsia_ui_pointerinjector_configuration as pointerinjector_config,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::{channel::mpsc::Sender, stream::StreamExt, SinkExt},
    std::iter::FromIterator,
    std::{cell::RefCell, collections::HashMap, option::Option, rc::Rc},
};

/// A [`MouseInjectorHandler`] parses mouse events and forwards them to Scenic through the
/// fidl_fuchsia_pointerinjector protocols.
pub struct MouseInjectorHandler {
    /// The mutable fields of this handler.
    mutable_state: RefCell<MutableState>,

    /// The scope and coordinate system of injection.
    /// See [`fidl_fuchsia_pointerinjector::Context`] for more details.
    context_view_ref: fidl_fuchsia_ui_views::ViewRef,

    /// The region where dispatch is attempted for injected events.
    /// See [`fidl_fuchsia_pointerinjector::Target`] for more details.
    target_view_ref: fidl_fuchsia_ui_views::ViewRef,

    /// The maximum position sent to clients, used to bound relative movements
    /// and scale absolute positions from device coordinates.
    max_position: Position,

    /// The FIDL proxy to register new injectors.
    injector_registry_proxy: pointerinjector::RegistryProxy,

    /// The FIDL proxy used to get configuration details for pointer injection.
    configuration_proxy: pointerinjector_config::SetupProxy,

    /// The FIDL proxy used to report mouse activity to the activity service.
    aggregator_proxy: interaction_observation::AggregatorProxy,
}

struct MutableState {
    /// A rectangular region that directs injected events into a target.
    /// See fidl_fuchsia_pointerinjector::Viewport for more details.
    viewport: Option<pointerinjector::Viewport>,

    /// The injectors registered with Scenic, indexed by their device ids.
    injectors: HashMap<u32, pointerinjector::DeviceProxy>,

    /// The current position.
    current_position: Position,

    /// A [`Sender`] used to communicate the current cursor state.
    cursor_message_sender: Sender<CursorMessage>,

    /// Set to true when in immersive mode.
    immersive_mode: bool,

    /// The current visibility for the cursor.
    is_cursor_visible: bool,
}

#[async_trait(?Send)]
impl InputHandler for MouseInjectorHandler {
    async fn handle_input_event(
        self: Rc<Self>,
        mut input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Mouse(ref mouse_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Mouse(ref mouse_device_descriptor),
                event_time,
                handled: input_device::Handled::No,
                trace_id: _,
            } => {
                // TODO(fxbug.dev/90317): Investigate latency introduced by waiting for update_cursor_renderer
                if let Err(e) =
                    self.update_cursor_renderer(mouse_event, &mouse_device_descriptor).await
                {
                    fx_log_err!("update_cursor_renderer failed: {}", e);
                }
                let immersive_mode = self.mutable_state.borrow().immersive_mode;
                if let Err(e) = self.update_cursor_visibility(!immersive_mode).await {
                    fx_log_err!("update_cursor_visibility failed: {}", e);
                }

                // Create a new injector if this is the first time seeing device_id.
                if let Err(e) = self
                    .ensure_injector_registered(&mouse_event, &mouse_device_descriptor, event_time)
                    .await
                {
                    fx_log_err!("ensure_injector_registered failed: {}", e);
                }

                // Handle the event.
                if let Err(e) = self
                    .send_event_to_scenic(&mouse_event, &mouse_device_descriptor, event_time)
                    .await
                {
                    fx_log_err!("send_event_to_scenic failed: {}", e);
                }

                // Report the event to the Activity Service.
                if let Err(e) = self.report_mouse_activity(event_time).await {
                    fx_log_err!("report_mouse_activity failed: {}", e);
                }

                // Consume the input event.
                input_event.handled = input_device::Handled::Yes;
            }
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::TouchScreen(ref _touch_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::TouchScreen(ref _touch_device_descriptor),
                event_time: _,
                handled: _,
                trace_id: _,
            } => {
                // Hide the cursor on touch input.
                // TODO(fxbug.dev/90290): Remove this workaround when we have a
                // proper cursor API.
                let visible = false;
                if let Err(e) = self.update_cursor_visibility(visible).await {
                    fx_log_err!("update_cursor_visibility failed: {}", e);
                }
            }
            input_device::InputEvent {
                device_event:
                    input_device::InputDeviceEvent::MouseConfig(
                        mouse_config::MouseConfigEvent::ToggleImmersiveMode,
                    ),
                device_descriptor: input_device::InputDeviceDescriptor::MouseConfig,
                event_time: _,
                handled: _,
                trace_id: _,
            } => {
                // Immersive mode hides the cursor and is a temporary workaround
                // until we have a cursor API that makes it possible for UI
                // components to control the appearance of the cursor.
                //
                // TODO(fxbug.dev/90290): Remove this workaround when we have a
                // proper cursor API.
                if let Err(e) = self.toggle_immersive_mode().await {
                    fx_log_err!("update_cursor_visibility failed: {}", e);
                }
                input_event.handled = input_device::Handled::Yes
            }
            _ => {}
        }
        vec![input_event]
    }
}

impl MouseInjectorHandler {
    /// Creates a new mouse handler that holds mouse pointer injectors.
    /// The caller is expected to spawn a task to continually watch for updates to the viewport.
    /// Example:
    /// let handler = MouseInjectorHandler::new(display_size).await?;
    /// fasync::Task::local(handler.clone().watch_viewport()).detach();
    ///
    /// # Parameters
    /// - `display_size`: The size of the associated display.
    /// - `cursor_message_sender`: A [`Sender`] used to communicate the current cursor state.
    ///
    /// # Errors
    /// If unable to connect to pointerinjector protocols.
    pub async fn new(
        display_size: Size,
        cursor_message_sender: Sender<CursorMessage>,
    ) -> Result<Rc<Self>, Error> {
        let configuration_proxy = connect_to_protocol::<pointerinjector_config::SetupMarker>()?;
        let injector_registry_proxy = connect_to_protocol::<pointerinjector::RegistryMarker>()?;
        let aggregator_proxy = connect_to_protocol::<interaction_observation::AggregatorMarker>()?;

        Self::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            display_size,
            cursor_message_sender,
        )
        .await
    }

    /// Creates a new mouse handler that holds mouse pointer injectors.
    /// The caller is expected to spawn a task to continually watch for updates to the viewport.
    /// Example:
    /// let handler = MouseInjectorHandler::new_with_config_proxy(config_proxy, display_size).await?;
    /// fasync::Task::local(handler.clone().watch_viewport()).detach();
    ///
    /// # Parameters
    /// - `configuration_proxy`: A proxy used to get configuration details for pointer
    ///    injection.
    /// - `display_size`: The size of the associated display.
    /// - `cursor_message_sender`: A [`Sender`] used to communicate the current cursor state.
    ///
    /// # Errors
    /// If unable to get injection view refs from `configuration_proxy`.
    /// If unable to connect to pointerinjector Registry protocol.
    pub async fn new_with_config_proxy(
        configuration_proxy: pointerinjector_config::SetupProxy,
        display_size: Size,
        cursor_message_sender: Sender<CursorMessage>,
    ) -> Result<Rc<Self>, Error> {
        let aggregator_proxy = connect_to_protocol::<interaction_observation::AggregatorMarker>()?;
        let injector_registry_proxy = connect_to_protocol::<pointerinjector::RegistryMarker>()?;
        Self::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            display_size,
            cursor_message_sender,
        )
        .await
    }

    /// Creates a new mouse handler that holds mouse pointer injectors.
    /// The caller is expected to spawn a task to continually watch for updates to the viewport.
    /// Example:
    /// let handler = MouseInjectorHandler::new_handler(None, None, display_size).await?;
    /// fasync::Task::local(handler.clone().watch_viewport()).detach();
    ///
    /// # Parameters
    /// - `aggregator_proxy`: A proxy used to report to the activity service
    /// - `configuration_proxy`: A proxy used to get configuration details for pointer
    ///    injection.
    /// - `injector_registry_proxy`: A proxy used to register new pointer injectors.
    /// - `display_size`: The size of the associated display.
    /// - `cursor_message_sender`: A [`Sender`] used to communicate the current cursor state.
    ///
    /// # Errors
    /// If unable to get injection view refs from `configuration_proxy`.
    async fn new_handler(
        aggregator_proxy: interaction_observation::AggregatorProxy,
        configuration_proxy: pointerinjector_config::SetupProxy,
        injector_registry_proxy: pointerinjector::RegistryProxy,
        display_size: Size,
        cursor_message_sender: Sender<CursorMessage>,
    ) -> Result<Rc<Self>, Error> {
        // Get the context and target views to inject into.
        let (context_view_ref, target_view_ref) = configuration_proxy.get_view_refs().await?;
        let handler = Rc::new(Self {
            mutable_state: RefCell::new(MutableState {
                viewport: None,
                injectors: HashMap::new(),
                // Initially centered.
                current_position: Position {
                    x: display_size.width / 2.0,
                    y: display_size.height / 2.0,
                },
                cursor_message_sender,
                immersive_mode: false,
                is_cursor_visible: true,
            }),
            context_view_ref,
            target_view_ref,
            max_position: Position { x: display_size.width, y: display_size.height },
            injector_registry_proxy,
            configuration_proxy,
            aggregator_proxy,
        });

        Ok(handler)
    }

    /// Adds a new pointer injector and tracks it in `self.injectors` if one doesn't exist at
    /// `mouse_descriptor.device_id`.
    ///
    /// # Parameters
    /// - `mouse_event`: The mouse event to send to Scenic.
    /// - `mouse_descriptor`: The descriptor for the device that sent the mouse event.
    /// - `event_time`: The time in nanoseconds when the event was first recorded.
    async fn ensure_injector_registered(
        self: &Rc<Self>,
        mouse_event: &mouse_binding::MouseEvent,
        mouse_descriptor: &mouse_binding::MouseDeviceDescriptor,
        event_time: zx::Time,
    ) -> Result<(), anyhow::Error> {
        let mut inner = self.mutable_state.borrow_mut();
        if inner.injectors.contains_key(&mouse_descriptor.device_id) {
            return Ok(());
        }

        // Create a new injector.
        let (device_proxy, device_server) = create_proxy::<pointerinjector::DeviceMarker>()
            .context("Failed to create DeviceProxy.")?;
        let context = fuchsia_scenic::duplicate_view_ref(&self.context_view_ref)
            .context("Failed to duplicate context view ref.")?;
        let target = fuchsia_scenic::duplicate_view_ref(&self.target_view_ref)
            .context("Failed to duplicate target view ref.")?;

        let viewport = inner.viewport.clone();
        let config = pointerinjector::Config {
            device_id: Some(mouse_descriptor.device_id),
            device_type: Some(pointerinjector::DeviceType::Mouse),
            context: Some(pointerinjector::Context::View(context)),
            target: Some(pointerinjector::Target::View(target)),
            viewport,
            dispatch_policy: Some(pointerinjector::DispatchPolicy::MouseHoverAndLatchInTarget),
            scroll_v_range: mouse_descriptor.wheel_v_range.clone(),
            scroll_h_range: mouse_descriptor.wheel_h_range.clone(),
            buttons: mouse_descriptor.buttons.clone(),
            ..pointerinjector::Config::EMPTY
        };

        // Register the new injector.
        self.injector_registry_proxy
            .register(config, device_server)
            .await
            .context("Failed to register injector.")?;
        fx_log_info!("Registered injector with device id {:?}", mouse_descriptor.device_id);

        // Keep track of the injector.
        inner.injectors.insert(mouse_descriptor.device_id, device_proxy.clone());

        // Inject ADD event the first time a MouseDevice is seen.
        let events_to_send = vec![self.create_pointer_sample_event(
            mouse_event,
            event_time,
            pointerinjector::EventPhase::Add,
            inner.current_position,
            None,
        )];
        device_proxy
            .inject(&mut events_to_send.into_iter())
            .await
            .context("Failed to ADD new MouseDevice.")?;

        Ok(())
    }

    /// Updates the current cursor position according to the received mouse event.
    ///
    /// The updated cursor state is sent via `self.inner.cursor_message_sender` to a client
    /// that renders the cursor on-screen.
    ///
    /// If there is no movement, the location is not sent.
    ///
    /// # Parameters
    /// - `mouse_event`: The mouse event to use to update the cursor location.
    /// - `mouse_descriptor`: The descriptor for the input device generating the input reports.
    async fn update_cursor_renderer(
        &self,
        mouse_event: &mouse_binding::MouseEvent,
        mouse_descriptor: &mouse_binding::MouseDeviceDescriptor,
    ) -> Result<(), anyhow::Error> {
        let mut inner = self.mutable_state.borrow_mut();
        let new_position = match (mouse_event.location, mouse_descriptor) {
            (
                mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                    counts: _,
                    millimeters: offset,
                }),
                mouse_binding::MouseDeviceDescriptor { counts_per_mm, .. },
            ) => inner.current_position + self.position_in_counts(offset, *counts_per_mm as f32),
            (
                mouse_binding::MouseLocation::Absolute(position),
                mouse_binding::MouseDeviceDescriptor {
                    absolute_x_range: Some(x_range),
                    absolute_y_range: Some(y_range),
                    ..
                },
            ) => self.scale_absolute_position(&position, &x_range, &y_range),
            (mouse_binding::MouseLocation::Absolute(_), _) => {
                return Err(anyhow!(
                    "Received an Absolute mouse location without absolute device ranges."
                ))
            }
        };
        inner.current_position = new_position;
        Position::clamp(&mut inner.current_position, Position::zero(), self.max_position);

        // Only send a position-update message if the cursor is visible.  If it isn't, then when it
        // eventually becomes visible, the position will be updated first.
        if inner.is_cursor_visible {
            let msg = CursorMessage::SetPosition(inner.current_position);
            inner
                .cursor_message_sender
                .send(msg)
                .await
                .context("Failed to send current mouse position to cursor renderer")?;
        }
        Ok(())
    }

    /// Updates the current cursor's visibility.
    ///
    /// The updated visibility is sent to a client via `self.inner.cursor_message_sender`.
    ///
    /// If there is no change to visibility, the state is not sent.
    ///
    /// # Parameters
    /// - `visible`: The new visibility of the cursor.
    async fn update_cursor_visibility(&self, visible: bool) -> Result<(), anyhow::Error> {
        let mut inner = self.mutable_state.borrow_mut();

        // No change to visibility needed.
        if visible == inner.is_cursor_visible {
            return Ok(());
        }
        inner.is_cursor_visible = visible;

        // If we just became visible, update the cursor position.  We update the position before
        // making it visible, because doing it the other order can result in the cursor briefly
        // appearing in the wrong position before jumping to the correct position.
        if inner.is_cursor_visible {
            let pos = inner.current_position;

            inner
                .cursor_message_sender
                .send(CursorMessage::SetPosition(pos))
                .await
                .context("Failed to send current mouse position to cursor renderer")?;
        }

        inner
            .cursor_message_sender
            .send(CursorMessage::SetVisibility(visible))
            .await
            .context("Failed to send current visibility to cursor renderer")
    }

    /// Toggle "immersive mode", which means that the cursor is not shown.
    ///
    /// When leaving immersive mode, the cursor becomes visible.  Returns a bool that says whether
    /// the handler is now in immersive mode.
    async fn toggle_immersive_mode(&self) -> Result<bool, anyhow::Error> {
        let immersive_mode = {
            let mut inner = self.mutable_state.borrow_mut();
            inner.immersive_mode = !inner.immersive_mode;
            fx_log_info!("Toggled immersive mode: {}", inner.immersive_mode);
            inner.immersive_mode
        };

        if let Err(e) = self.update_cursor_visibility(!immersive_mode).await {
            return Err(e);
        }
        Ok(immersive_mode)
    }

    /// Returns an absolute cursor position scaled from device coordinates to the handler's
    /// max position.
    ///
    /// # Parameters
    /// - `position`: Absolute cursor position in device coordinates.
    /// - `x_range`: The range of possible x values of absolute mouse positions.
    /// - `y_range`: The range of possible y values of absolute mouse positions.
    fn scale_absolute_position(
        &self,
        position: &Position,
        x_range: &Range,
        y_range: &Range,
    ) -> Position {
        let range_min = Position { x: x_range.min as f32, y: y_range.min as f32 };
        let range_max = Position { x: x_range.max as f32, y: y_range.max as f32 };
        self.max_position * ((*position - range_min) / (range_max - range_min))
    }

    /// Sends the given event to Scenic.
    ///
    /// # Parameters
    /// - `mouse_event`: The mouse event to send to Scenic.
    /// - `mouse_descriptor`: The descriptor for the device that sent the mouse event.
    /// - `event_time`: The time in nanoseconds when the event was first recorded.
    async fn send_event_to_scenic(
        &self,
        mouse_event: &mouse_binding::MouseEvent,
        mouse_descriptor: &mouse_binding::MouseDeviceDescriptor,
        event_time: zx::Time,
    ) -> Result<(), anyhow::Error> {
        let inner = self.mutable_state.borrow();
        if let Some(injector) = inner.injectors.get(&mouse_descriptor.device_id) {
            let relative_motion = match mouse_event.location {
                mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                    counts: _,
                    millimeters: offset_mm,
                }) if mouse_event.phase == mouse_binding::MousePhase::Move => {
                    let offset =
                        self.position_in_counts(offset_mm, mouse_descriptor.counts_per_mm as f32);
                    Some([offset.x, offset.y])
                }
                _ => None,
            };
            let events_to_send = vec![self.create_pointer_sample_event(
                mouse_event,
                event_time,
                pointerinjector::EventPhase::Change,
                inner.current_position,
                relative_motion,
            )];
            let _ = injector.inject(&mut events_to_send.into_iter()).await;

            Ok(())
        } else {
            Err(anyhow::format_err!(
                "No injector found for mouse device {}.",
                mouse_descriptor.device_id
            ))
        }
    }

    /// Creates a [`fidl_fuchsia_ui_pointerinjector::Event`] representing the given MouseEvent.
    ///
    /// # Parameters
    /// - `mouse_event`: The mouse event to send to Scenic.
    /// - `event_time`: The time in nanoseconds when the event was first recorded.
    /// - `phase`: The EventPhase to send to Scenic.
    /// - `current_position`: The current cursor position.
    /// - `relative_motion`: The relative motion to send to Scenic.
    fn create_pointer_sample_event(
        &self,
        mouse_event: &mouse_binding::MouseEvent,
        event_time: zx::Time,
        phase: pointerinjector::EventPhase,
        current_position: Position,
        relative_motion: Option<[f32; 2]>,
    ) -> pointerinjector::Event {
        let pointer_sample = pointerinjector::PointerSample {
            pointer_id: Some(0),
            phase: Some(phase),
            position_in_viewport: Some([current_position.x, current_position.y]),
            scroll_v: match mouse_event.wheel_delta_v {
                Some(mouse_binding::WheelDelta {
                    raw_data: mouse_binding::RawWheelDelta::Ticks(tick),
                    ..
                }) => Some(tick),
                _ => None,
            },
            scroll_h: match mouse_event.wheel_delta_h {
                Some(mouse_binding::WheelDelta {
                    raw_data: mouse_binding::RawWheelDelta::Ticks(tick),
                    ..
                }) => Some(tick),
                _ => None,
            },
            scroll_v_physical_pixel: match mouse_event.wheel_delta_v {
                Some(mouse_binding::WheelDelta { physical_pixel: Some(pixel), .. }) => {
                    Some(pixel.into())
                }
                _ => None,
            },
            scroll_h_physical_pixel: match mouse_event.wheel_delta_h {
                Some(mouse_binding::WheelDelta { physical_pixel: Some(pixel), .. }) => {
                    Some(pixel.into())
                }
                _ => None,
            },
            is_precision_scroll: match mouse_event.phase {
                mouse_binding::MousePhase::Wheel => match mouse_event.is_precision_scroll {
                    Some(mouse_binding::PrecisionScroll::Yes) => Some(true),
                    Some(mouse_binding::PrecisionScroll::No) => Some(false),
                    None => {
                        fx_log_err!(
                            "mouse wheel event does not have value in is_precision_scroll."
                        );
                        None
                    }
                },
                _ => None,
            },
            pressed_buttons: Some(Vec::from_iter(mouse_event.pressed_buttons.iter().cloned())),
            relative_motion,
            ..pointerinjector::PointerSample::EMPTY
        };
        pointerinjector::Event {
            timestamp: Some(event_time.into_nanos()),
            data: Some(pointerinjector::Data::PointerSample(pointer_sample)),
            trace_flow_id: None,
            ..pointerinjector::Event::EMPTY
        }
    }

    /// Reports the given event_time to the activity service.
    async fn report_mouse_activity(&self, event_time: zx::Time) -> Result<(), fidl::Error> {
        self.aggregator_proxy.report_discrete_activity(event_time.into_nanos()).await
    }

    /// Watches for viewport updates from the scene manager.
    pub async fn watch_viewport(self: Rc<Self>) {
        let configuration_proxy = self.configuration_proxy.clone();
        let mut viewport_stream = HangingGetStream::new(
            configuration_proxy,
            pointerinjector_config::SetupProxy::watch_viewport,
        );
        loop {
            match viewport_stream.next().await {
                Some(Ok(new_viewport)) => {
                    // Update the viewport tracked by this handler.
                    let mut inner = self.mutable_state.borrow_mut();
                    inner.viewport = Some(new_viewport.clone());

                    // Update Scenic with the latest viewport.
                    for (_device_id, injector) in inner.injectors.iter() {
                        let events = &mut vec![pointerinjector::Event {
                            timestamp: Some(fuchsia_async::Time::now().into_nanos()),
                            data: Some(pointerinjector::Data::Viewport(new_viewport.clone())),
                            trace_flow_id: Some(fuchsia_trace::Id::new().into()),
                            ..pointerinjector::Event::EMPTY
                        }]
                        .into_iter();
                        injector.inject(events).await.expect("Failed to inject updated viewport.");
                    }
                }
                Some(Err(e)) => {
                    fx_log_err!("Error while reading viewport update: {}", e);
                    return;
                }
                None => {
                    fx_log_err!("Viewport update stream terminated unexpectedly");
                    return;
                }
            }
        }
    }

    /// Converts a Position given in millimeters to a Position given in counts.
    fn position_in_counts(&self, pos_mm: Position, counts_per_mm: f32) -> Position {
        Position { x: pos_mm.x * counts_per_mm, y: pos_mm.y * counts_per_mm }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::testing_utilities::{
            assert_handler_ignores_input_event_sequence, create_mouse_event,
            create_mouse_event_with_handled, create_mouse_pointer_sample_event,
            create_mouse_pointer_sample_event_with_wheel_physical_pixel, create_touch_contact,
            create_touch_screen_event_with_handled,
        },
        crate::touch_binding,
        assert_matches::assert_matches,
        fidl_fuchsia_input_report as fidl_input_report,
        fidl_fuchsia_ui_pointerinjector as pointerinjector, fuchsia_async as fasync,
        fuchsia_zircon as zx,
        futures::{channel::mpsc, StreamExt},
        maplit::hashmap,
        pretty_assertions::assert_eq,
        std::collections::HashSet,
        std::ops::Add,
        test_case::test_case,
    };

    // const MOUSE_ID: u32 = 1;
    const DISPLAY_WIDTH: f32 = 100.0;
    const DISPLAY_HEIGHT: f32 = 100.0;

    /// Returns an |input_device::InputDeviceDescriptor::MouseDescriptor|.
    const DESCRIPTOR: input_device::InputDeviceDescriptor =
        input_device::InputDeviceDescriptor::Mouse(mouse_binding::MouseDeviceDescriptor {
            device_id: 1,
            absolute_x_range: Some(fidl_input_report::Range { min: 0, max: 100 }),
            absolute_y_range: Some(fidl_input_report::Range { min: 0, max: 100 }),
            wheel_v_range: Some(fidl_input_report::Axis {
                range: fidl_input_report::Range { min: -1, max: 1 },
                unit: fidl_input_report::Unit {
                    type_: fidl_input_report::UnitType::Other,
                    exponent: 0,
                },
            }),
            wheel_h_range: Some(fidl_input_report::Axis {
                range: fidl_input_report::Range { min: -1, max: 1 },
                unit: fidl_input_report::Unit {
                    type_: fidl_input_report::UnitType::Other,
                    exponent: 0,
                },
            }),
            buttons: None,
            counts_per_mm: mouse_binding::DEFAULT_COUNTS_PER_MM,
        });

    /// Returns an TouchDescriptor.
    fn get_touch_device_descriptor() -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::TouchScreen(
            touch_binding::TouchScreenDeviceDescriptor {
                device_id: 1,
                contacts: vec![touch_binding::ContactDeviceDescriptor {
                    x_range: fidl_input_report::Range { min: 0, max: 100 },
                    y_range: fidl_input_report::Range { min: 0, max: 100 },
                    x_unit: fidl_input_report::Unit {
                        type_: fidl_input_report::UnitType::Meters,
                        exponent: -6,
                    },
                    y_unit: fidl_input_report::Unit {
                        type_: fidl_input_report::UnitType::Meters,
                        exponent: -6,
                    },
                    pressure_range: None,
                    width_range: None,
                    height_range: None,
                }],
            },
        )
    }

    /// Handles |fidl_fuchsia_pointerinjector_configuration::SetupRequest::GetViewRefs|.
    async fn handle_configuration_request_stream(
        stream: &mut pointerinjector_config::SetupRequestStream,
    ) {
        if let Some(Ok(request)) = stream.next().await {
            match request {
                pointerinjector_config::SetupRequest::GetViewRefs { responder, .. } => {
                    let mut context = fuchsia_scenic::ViewRefPair::new()
                        .expect("Failed to create viewrefpair.")
                        .view_ref;
                    let mut target = fuchsia_scenic::ViewRefPair::new()
                        .expect("Failed to create viewrefpair.")
                        .view_ref;
                    let _ = responder.send(&mut context, &mut target);
                }
                _ => {}
            };
        }
    }

    /// Handles |fidl_fuchsia_pointerinjector::RegistryRequest|s by forwarding the registered device
    /// over `injector_sender` to be handled by handle_device_request_stream().
    async fn handle_registry_request_stream(
        mut stream: pointerinjector::RegistryRequestStream,
        injector_sender: futures::channel::oneshot::Sender<pointerinjector::DeviceRequestStream>,
    ) {
        if let Some(request) = stream.next().await {
            match request {
                Ok(pointerinjector::RegistryRequest::Register {
                    config: _,
                    injector,
                    responder,
                    ..
                }) => {
                    let injector_stream =
                        injector.into_stream().expect("Failed to get stream from server end.");
                    let _ = injector_sender.send(injector_stream);
                    responder.send().expect("failed to respond");
                }
                _ => {}
            };
        } else {
            panic!("RegistryRequestStream failed.");
        }
    }

    // Handles |fidl_fuchsia_pointerinjector::RegistryRequest|s
    async fn handle_registry_request_stream2(
        mut stream: pointerinjector::RegistryRequestStream,
        injector_sender: mpsc::UnboundedSender<Vec<pointerinjector::Event>>,
    ) {
        let (injector, responder) = match stream.next().await {
            Some(Ok(pointerinjector::RegistryRequest::Register {
                config: _,
                injector,
                responder,
                ..
            })) => (injector, responder),
            other => panic!("expected register request, but got {:?}", other),
        };
        let injector_stream: pointerinjector::DeviceRequestStream =
            injector.into_stream().expect("Failed to get stream from server end.");
        responder.send().expect("failed to respond");
        injector_stream
            .for_each(|request| {
                futures::future::ready({
                    match request {
                        Ok(pointerinjector::DeviceRequest::Inject {
                            events,
                            responder: device_injector_responder,
                        }) => {
                            let _ = injector_sender.unbounded_send(events);
                            device_injector_responder.send().expect("failed to respond")
                        }
                        Err(e) => panic!("FIDL error {}", e),
                    }
                })
            })
            .await;
    }

    /// Handles |fidl_fuchsia_pointerinjector::DeviceRequest|s by asserting the injector stream
    /// received on `injector_stream_receiver` gets `expected_events`.
    async fn handle_device_request_stream(
        injector_stream_receiver: futures::channel::oneshot::Receiver<
            pointerinjector::DeviceRequestStream,
        >,
        expected_events: Vec<pointerinjector::Event>,
    ) {
        let mut injector_stream =
            injector_stream_receiver.await.expect("Failed to get DeviceRequestStream.");
        for expected_event in expected_events {
            match injector_stream.next().await {
                Some(Ok(pointerinjector::DeviceRequest::Inject { events, responder })) => {
                    assert_eq!(events, vec![expected_event]);
                    responder.send().expect("failed to respond");
                }
                Some(Err(e)) => panic!("FIDL error {}", e),
                None => panic!("Expected another event."),
            }
        }
    }

    /// Handles |fidl_fuchsia_interaction_observation::AggregatorRequest|s.
    async fn handle_aggregator_request_stream(
        mut stream: interaction_observation::AggregatorRequestStream,
        expected_times: Vec<i64>,
    ) {
        for expected_time in expected_times {
            if let Some(request) = stream.next().await {
                match request {
                    Ok(interaction_observation::AggregatorRequest::ReportDiscreteActivity {
                        event_time,
                        responder,
                    }) => {
                        assert_eq!(event_time, expected_time);
                        responder.send().expect("failed to respond");
                    }
                    other => panic!("expected aggregator report request, but got {:?}", other),
                };
            } else {
                panic!("AggregatorRequestStream failed.");
            }
        }
    }

    // Creates a |pointerinjector::Viewport|.
    fn create_viewport(min: f32, max: f32) -> pointerinjector::Viewport {
        pointerinjector::Viewport {
            extents: Some([[min, min], [max, max]]),
            viewport_to_context_transform: None,
            ..pointerinjector::Viewport::EMPTY
        }
    }

    // Synthesize a "hardware" input event to be processed by the tested MouseInjectorHandler.
    fn create_relative_mouse_move_event(
        position: Position,
        time: fuchsia_zircon::Time,
    ) -> input_device::InputEvent {
        create_mouse_event(
            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                // TODO(https://fxbug.dev/102570): Remove counts.
                counts: Position::zero(),
                millimeters: Position {
                    x: position.x / mouse_binding::DEFAULT_COUNTS_PER_MM as f32,
                    y: position.y / mouse_binding::DEFAULT_COUNTS_PER_MM as f32,
                },
            }),
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Move,
            HashSet::new(),
            HashSet::new(),
            time,
            &DESCRIPTOR,
        )
    }

    // Create a pointerinjector event which will be compared with one produced by the tested
    // MouseInjectorHandler.
    fn create_expected_pointerinjector_mouse_change_sample_event(
        position: Position,
        relative_motion: Option<[f32; 2]>,
        time: fuchsia_zircon::Time,
    ) -> pointerinjector::Event {
        create_mouse_pointer_sample_event(
            pointerinjector::EventPhase::Change,
            vec![],
            position,
            relative_motion,
            None, /*wheel_delta_v*/
            None, /*wheel_delta_h*/
            None, /*is_precision_scroll*/
            time,
        )
    }

    // Expect that |msg| is a SetPosition message with the expected position, otherwise panic.
    fn expect_cursor_position_message(msg: Option<CursorMessage>, expected_position: Position) {
        assert_eq!(Some(CursorMessage::SetPosition(expected_position)), msg);
    }

    // Expect that |msg| is a SetVisibility message with the expected boolean, otherwise panic.
    fn expect_cursor_visibility_message(msg: Option<CursorMessage>, expected_bool: bool) {
        assert_eq!(Some(CursorMessage::SetVisibility(expected_bool)), msg);
    }

    // Tests that MouseInjectorHandler::receives_viewport_updates() tracks viewport updates
    // and notifies injectors about said updates.
    #[fuchsia::test]
    fn receives_viewport_updates() {
        let mut exec = fasync::TestExecutor::new().expect("executor needed");

        // Set up fidl streams.
        let (aggregator_proxy, _) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, _) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let (sender, _) = futures::channel::mpsc::channel::<CursorMessage>(0);

        // Create mouse handler.
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);
        let (mouse_handler_res, _) = exec.run_singlethreaded(futures::future::join(
            mouse_handler_fut,
            config_request_stream_fut,
        ));
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        // Add an injector.
        let (injector_device_proxy, mut injector_device_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::DeviceMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        mouse_handler.mutable_state.borrow_mut().injectors.insert(1, injector_device_proxy);

        // This nested block is used to bound the lifetime of `watch_viewport_fut`.
        {
            // Request a viewport update.
            let watch_viewport_fut = mouse_handler.clone().watch_viewport();
            futures::pin_mut!(watch_viewport_fut);
            assert!(exec.run_until_stalled(&mut watch_viewport_fut).is_pending());

            // Send a viewport update.
            match exec.run_singlethreaded(&mut configuration_request_stream.next()) {
                Some(Ok(pointerinjector_config::SetupRequest::WatchViewport {
                    responder, ..
                })) => {
                    responder.send(create_viewport(0.0, 100.0)).expect("Failed to send viewport.");
                }
                other => panic!("Received unexpected value: {:?}", other),
            };
            assert!(exec.run_until_stalled(&mut watch_viewport_fut).is_pending());

            // Check that the injector received an updated viewport
            exec.run_singlethreaded(async {
                match injector_device_request_stream.next().await {
                    Some(Ok(pointerinjector::DeviceRequest::Inject { events, responder })) => {
                        assert_eq!(events.len(), 1);
                        assert!(events[0].data.is_some());
                        assert_eq!(
                            events[0].data,
                            Some(pointerinjector::Data::Viewport(create_viewport(0.0, 100.0)))
                        );
                        responder.send().expect("injector stream failed to respond.");
                    }
                    other => panic!("Received unexpected value: {:?}", other),
                }
            });

            // Request viewport update.
            assert!(exec.run_until_stalled(&mut watch_viewport_fut).is_pending());

            // Send viewport update.
            match exec.run_singlethreaded(&mut configuration_request_stream.next()) {
                Some(Ok(pointerinjector_config::SetupRequest::WatchViewport {
                    responder, ..
                })) => {
                    responder
                        .send(create_viewport(100.0, 200.0))
                        .expect("Failed to send viewport.");
                }
                other => panic!("Received unexpected value: {:?}", other),
            };

            // Process viewport update.
            assert!(exec.run_until_stalled(&mut watch_viewport_fut).is_pending());
        }

        // Check that the injector received an updated viewport
        exec.run_singlethreaded(async {
            match injector_device_request_stream.next().await {
                Some(Ok(pointerinjector::DeviceRequest::Inject { events, responder })) => {
                    assert_eq!(events.len(), 1);
                    assert!(events[0].data.is_some());
                    assert_eq!(
                        events[0].data,
                        Some(pointerinjector::Data::Viewport(create_viewport(100.0, 200.0)))
                    );
                    responder.send().expect("injector stream failed to respond.");
                }
                other => panic!("Received unexpected value: {:?}", other),
            }
        });

        // Check the viewport on the handler is accurate.
        let expected_viewport = create_viewport(100.0, 200.0);
        assert_eq!(mouse_handler.mutable_state.borrow().viewport, Some(expected_viewport));
    }

    fn wheel_delta_ticks(
        ticks: i64,
        physical_pixel: Option<f32>,
    ) -> Option<mouse_binding::WheelDelta> {
        Some(mouse_binding::WheelDelta {
            raw_data: mouse_binding::RawWheelDelta::Ticks(ticks),
            physical_pixel,
        })
    }

    fn wheel_delta_mm(mm: f32, physical_pixel: Option<f32>) -> Option<mouse_binding::WheelDelta> {
        Some(mouse_binding::WheelDelta {
            raw_data: mouse_binding::RawWheelDelta::Millimeters(mm),
            physical_pixel,
        })
    }

    // Tests that a mouse move event both sends an update to scenic and sends the current cursor
    // location via the cursor location sender.
    #[test_case(
        mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {counts:Position { x: 0.0, y: 0.0 }, millimeters: Position { x: 10.0 / mouse_binding::DEFAULT_COUNTS_PER_MM as f32, y: 15.0/mouse_binding::DEFAULT_COUNTS_PER_MM as f32 }}),
        Position { x: DISPLAY_WIDTH / 2.0 + 10.0, y: DISPLAY_HEIGHT / 2.0 + 15.0 },
        [10.0, 15.0]; "Valid move event."
    )]
    #[test_case(
        mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {counts:Position {
          x: 0.0,
          y: 0.0,
        }, millimeters: Position { x: (DISPLAY_WIDTH  + 2.0)/ mouse_binding::DEFAULT_COUNTS_PER_MM as f32, y: (DISPLAY_HEIGHT + 2.0)  / mouse_binding::DEFAULT_COUNTS_PER_MM as f32 }}),
        Position {
          x: DISPLAY_WIDTH,
          y: DISPLAY_HEIGHT,
        },
        [DISPLAY_WIDTH + 2.0, DISPLAY_HEIGHT + 2.0]; "Move event exceeds max bounds."
    )]
    #[test_case(
      mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {counts:Position { x: 0.0, y: 0.0 }, millimeters: Position { x: -(DISPLAY_WIDTH + 20.0) / mouse_binding::DEFAULT_COUNTS_PER_MM as f32, y: -(DISPLAY_HEIGHT + 15.0)  / mouse_binding::DEFAULT_COUNTS_PER_MM as f32}}),
      Position { x: 0.0, y: 0.0 },
      [-(DISPLAY_WIDTH + 20.0), -(DISPLAY_HEIGHT + 15.0)]; "Move event exceeds min bounds."
    )]
    #[fuchsia::test(allow_stalls = false)]
    async fn move_event(
        move_location: mouse_binding::MouseLocation,
        expected_position: Position,
        expected_relative_motion: [f32; 2],
    ) {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        let (sender, mut receiver) = futures::channel::mpsc::channel::<CursorMessage>(1);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        let event_time = zx::Time::get_monotonic();
        let input_event = create_mouse_event(
            move_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Move,
            HashSet::new(),
            HashSet::new(),
            event_time,
            &DESCRIPTOR,
        );

        // Handle event.
        let handle_event_fut = mouse_handler.handle_input_event(input_event);
        let expected_events = vec![
            create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Add,
                vec![],
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time,
            ),
            create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![],
                expected_position,
                Some(expected_relative_motion),
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time,
            ),
        ];

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            futures::channel::oneshot::channel::<pointerinjector::DeviceRequestStream>();
        let registry_fut = handle_registry_request_stream(
            injector_registry_request_stream,
            injector_stream_sender,
        );
        let device_fut = handle_device_request_stream(injector_stream_receiver, expected_events);
        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![event_time.into_nanos()],
        );

        // Await all futures concurrently. If this completes, then the mouse event was handled and
        // matches `expected_events`.
        let (handle_result, _, _, _) =
            futures::join!(handle_event_fut, registry_fut, device_fut, aggregator_fut);
        match receiver.next().await {
            Some(CursorMessage::SetPosition(position)) => {
                pretty_assertions::assert_eq!(position, expected_position);
            }
            Some(CursorMessage::SetVisibility(_)) => {
                panic!("Received unexpected cursor visibility update.")
            }
            None => panic!("Did not receive cursor update."),
        }

        // No unhandled events.
        assert_matches!(
            handle_result.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::Yes, .. }]
        );
    }

    // Tests that an absolute mouse move event scales the location from device coordinates to
    // between {0, 0} and the handler's maximum position.
    #[fuchsia::test(allow_stalls = false)]
    async fn move_absolute_event() {
        const DEVICE_ID: u32 = 1;

        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        let (sender, mut receiver) = futures::channel::mpsc::channel::<CursorMessage>(1);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        // The location is rescaled from the device coordinate system defined
        // by `absolute_x_range` and `absolute_y_range`, to the display coordinate
        // system defined by `max_position`.
        //
        //          -50 y              0 +------------------ w
        //            |                  |         .
        //            |                  |         .
        //            |                  |         .
        // -50 x -----o----- 50   ->     | . . . . . . . . .
        //            |                  |         .
        //         * { x: -25, y: 25 }   |    * { x: w * 0.25, y: h * 0.75 }
        //            |                  |         .
        //           50                h |         .
        //
        // Where w = DISPLAY_WIDTH, h = DISPLAY_HEIGHT
        let cursor_location =
            mouse_binding::MouseLocation::Absolute(Position { x: -25.0, y: 25.0 });
        let event_time = zx::Time::get_monotonic();
        let descriptor =
            input_device::InputDeviceDescriptor::Mouse(mouse_binding::MouseDeviceDescriptor {
                device_id: DEVICE_ID,
                absolute_x_range: Some(fidl_input_report::Range { min: -50, max: 50 }),
                absolute_y_range: Some(fidl_input_report::Range { min: -50, max: 50 }),
                wheel_v_range: None,
                wheel_h_range: None,
                buttons: None,
                counts_per_mm: mouse_binding::DEFAULT_COUNTS_PER_MM,
            });
        let input_event = create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Move,
            HashSet::new(),
            HashSet::new(),
            event_time,
            &descriptor,
        );

        // Handle event.
        let handle_event_fut = mouse_handler.handle_input_event(input_event);
        let expected_position = Position { x: DISPLAY_WIDTH * 0.25, y: DISPLAY_HEIGHT * 0.75 };
        let expected_events = vec![
            create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Add,
                vec![],
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time,
            ),
            create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![],
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time,
            ),
        ];

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            futures::channel::oneshot::channel::<pointerinjector::DeviceRequestStream>();
        let registry_fut = handle_registry_request_stream(
            injector_registry_request_stream,
            injector_stream_sender,
        );
        let device_fut = handle_device_request_stream(injector_stream_receiver, expected_events);
        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![event_time.into_nanos()],
        );

        // Await all futures concurrently. If this completes, then the mouse event was handled and
        // matches `expected_events`.
        let (handle_result, _, _, _) =
            futures::join!(handle_event_fut, registry_fut, device_fut, aggregator_fut);
        match receiver.next().await {
            Some(CursorMessage::SetPosition(position)) => {
                assert_eq!(position, expected_position);
            }
            Some(CursorMessage::SetVisibility(_)) => {
                panic!("Received unexpected cursor visibility update.")
            }
            None => panic!("Did not receive cursor update."),
        }

        // No unhandled events.
        assert_matches!(
            handle_result.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::Yes, .. }]
        );
    }

    // Tests that mouse down and up events inject button press state.
    #[test_case(
      mouse_binding::MousePhase::Down,
      vec![1], vec![1]; "Down event injects button press state."
    )]
    #[test_case(
      mouse_binding::MousePhase::Up,
      vec![1], vec![]; "Up event injects button press state."
    )]
    #[fuchsia::test(allow_stalls = false)]
    async fn button_state_event(
        phase: mouse_binding::MousePhase,
        affected_buttons: Vec<mouse_binding::MouseButton>,
        pressed_buttons: Vec<mouse_binding::MouseButton>,
    ) {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        let (sender, mut receiver) = futures::channel::mpsc::channel::<CursorMessage>(1);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        let cursor_location = mouse_binding::MouseLocation::Absolute(Position { x: 0.0, y: 0.0 });
        let event_time = zx::Time::get_monotonic();

        let input_event = create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            phase,
            HashSet::from_iter(affected_buttons.clone()),
            HashSet::from_iter(pressed_buttons.clone()),
            event_time,
            &DESCRIPTOR,
        );

        // Handle event.
        let handle_event_fut = mouse_handler.handle_input_event(input_event);
        let expected_position = Position { x: 0.0, y: 0.0 };
        let expected_events = vec![
            create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Add,
                pressed_buttons.clone(),
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time,
            ),
            create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                pressed_buttons.clone(),
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time,
            ),
        ];

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            futures::channel::oneshot::channel::<pointerinjector::DeviceRequestStream>();
        let registry_fut = handle_registry_request_stream(
            injector_registry_request_stream,
            injector_stream_sender,
        );
        let device_fut = handle_device_request_stream(injector_stream_receiver, expected_events);
        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![event_time.into_nanos()],
        );

        // Await all futures concurrently. If this completes, then the mouse event was handled and
        // matches `expected_events`.
        let (handle_result, _, _, _) =
            futures::join!(handle_event_fut, registry_fut, device_fut, aggregator_fut);
        match receiver.next().await {
            Some(CursorMessage::SetPosition(position)) => {
                pretty_assertions::assert_eq!(position, expected_position);
            }
            Some(CursorMessage::SetVisibility(_)) => {
                panic!("Received unexpected cursor visibility update.")
            }
            None => panic!("Did not receive cursor update."),
        }

        // No unhandled events.
        assert_matches!(
            handle_result.as_slice(),
            [input_device::InputEvent { handled: input_device::Handled::Yes, .. }]
        );
    }

    // Tests that mouse down followed by mouse up events inject button press state.
    #[fuchsia::test(allow_stalls = false)]
    async fn down_up_event() {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        // Note: The size of the CursorMessage channel's buffer is 2 to allow for one cursor
        // update for every input event being sent.
        let (sender, mut receiver) = futures::channel::mpsc::channel::<CursorMessage>(2);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        let cursor_location = mouse_binding::MouseLocation::Absolute(Position { x: 0.0, y: 0.0 });
        let event_time1 = zx::Time::get_monotonic();
        let event_time2 = event_time1.add(fuchsia_zircon::Duration::from_micros(1));

        let event1 = create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Down,
            HashSet::from_iter(vec![1]),
            HashSet::from_iter(vec![1]),
            event_time1,
            &DESCRIPTOR,
        );

        let event2 = create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Up,
            HashSet::from_iter(vec![1]),
            HashSet::new(),
            event_time2,
            &DESCRIPTOR,
        );

        let expected_position = Position { x: 0.0, y: 0.0 };

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            mpsc::unbounded::<Vec<pointerinjector::Event>>();
        // Up to 2 events per handle_input_event() call.
        let mut injector_stream_receiver = injector_stream_receiver.ready_chunks(2);
        let registry_fut = handle_registry_request_stream2(
            injector_registry_request_stream,
            injector_stream_sender,
        );
        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![event_time1.into_nanos(), event_time2.into_nanos()],
        );

        // Run all futures until the handler future completes.
        let _registry_task = fasync::Task::local(registry_fut);
        let _aggregator_task = fasync::Task::local(aggregator_fut);

        mouse_handler.clone().handle_input_event(event1).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Add,
                    vec![1],
                    expected_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                ),
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Change,
                    vec![1],
                    expected_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                )
            ])
        );

        // Send another input event.
        mouse_handler.clone().handle_input_event(event2).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![],
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time2,
            )])
        );

        // Wait until validation is complete.
        match receiver.next().await {
            Some(CursorMessage::SetPosition(position)) => {
                assert_eq!(position, expected_position);
            }
            Some(CursorMessage::SetVisibility(_)) => {
                panic!("Received unexpected cursor visibility update.")
            }
            None => panic!("Did not receive cursor update."),
        }
    }

    /// Tests that two staggered button presses followed by stagged releases generate four mouse
    /// events with distinct `affected_button` and `pressed_button`.
    /// Specifically, we test and expect the following in order:
    /// | Action           | MousePhase | Injected Phase | `pressed_buttons` |
    /// | ---------------- | ---------- | -------------- | ----------------- |
    /// | Press button 1   | Down       | Change         | [1]               |
    /// | Press button 2   | Down       | Change         | [1, 2]            |
    /// | Release button 1 | Up         | Change         | [2]               |
    /// | Release button 2 | Up         | Change         | []                |
    #[fuchsia::test(allow_stalls = false)]
    async fn down_down_up_up_event() {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        // Note: The size of the CursorMessage channel's buffer is 4 to allow for one cursor
        // update for every input event being sent.
        let (sender, mut receiver) = futures::channel::mpsc::channel::<CursorMessage>(4);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        let cursor_location = mouse_binding::MouseLocation::Absolute(Position { x: 0.0, y: 0.0 });
        let event_time1 = zx::Time::get_monotonic();
        let event_time2 = event_time1.add(fuchsia_zircon::Duration::from_micros(1));
        let event_time3 = event_time2.add(fuchsia_zircon::Duration::from_micros(1));
        let event_time4 = event_time3.add(fuchsia_zircon::Duration::from_micros(1));

        let event1 = create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Down,
            HashSet::from_iter(vec![1]),
            HashSet::from_iter(vec![1]),
            event_time1,
            &DESCRIPTOR,
        );
        let event2 = create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Down,
            HashSet::from_iter(vec![2]),
            HashSet::from_iter(vec![1, 2]),
            event_time2,
            &DESCRIPTOR,
        );
        let event3 = create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Up,
            HashSet::from_iter(vec![1]),
            HashSet::from_iter(vec![2]),
            event_time3,
            &DESCRIPTOR,
        );
        let event4 = create_mouse_event(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Up,
            HashSet::from_iter(vec![2]),
            HashSet::new(),
            event_time4,
            &DESCRIPTOR,
        );

        let expected_position = Position { x: 0.0, y: 0.0 };

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            mpsc::unbounded::<Vec<pointerinjector::Event>>();
        // Up to 2 events per handle_input_event() call.
        let mut injector_stream_receiver = injector_stream_receiver.ready_chunks(2);
        let registry_fut = handle_registry_request_stream2(
            injector_registry_request_stream,
            injector_stream_sender,
        );
        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![
                event_time1.into_nanos(),
                event_time2.into_nanos(),
                event_time3.into_nanos(),
                event_time4.into_nanos(),
            ],
        );

        // Run all futures until the handler future completes.
        let _registry_task = fasync::Task::local(registry_fut);
        let _aggregator_task = fasync::Task::local(aggregator_fut);
        mouse_handler.clone().handle_input_event(event1).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Add,
                    vec![1],
                    expected_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                ),
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Change,
                    vec![1],
                    expected_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                )
            ])
        );

        // Send another down event.
        mouse_handler.clone().handle_input_event(event2).await;
        let pointer_sample_event2 = injector_stream_receiver
            .next()
            .await
            .map(|events| events.concat())
            .expect("Failed to receive pointer sample event.");
        let expected_event_time: i64 = event_time2.into_nanos();
        assert_eq!(pointer_sample_event2.len(), 1);

        // We must break this event result apart for assertions since the
        // `pressed_buttons` can be given with elements in any order.
        match &pointer_sample_event2[0] {
            pointerinjector::Event {
                timestamp: Some(actual_event_time),
                data:
                    Some(pointerinjector::Data::PointerSample(pointerinjector::PointerSample {
                        pointer_id: Some(0),
                        phase: Some(pointerinjector::EventPhase::Change),
                        position_in_viewport: Some(actual_position),
                        scroll_v: None,
                        scroll_h: None,
                        pressed_buttons: Some(actual_buttons),
                        relative_motion: None,
                        ..
                    })),
                ..
            } => {
                assert_eq!(actual_event_time, &expected_event_time);
                assert_eq!(actual_position[0], expected_position.x);
                assert_eq!(actual_position[1], expected_position.y);
                assert_eq!(
                    HashSet::<mouse_binding::MouseButton>::from_iter(actual_buttons.clone()),
                    HashSet::from_iter(vec![1, 2])
                );
            }
            _ => panic!("Unexpected pointer sample event: {:?}", pointer_sample_event2[0]),
        }

        // Send another up event.
        mouse_handler.clone().handle_input_event(event3).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![2],
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time3,
            )])
        );

        // Send another up event.
        mouse_handler.clone().handle_input_event(event4).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![],
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time4,
            )])
        );

        // Wait until validation is complete.
        match receiver.next().await {
            Some(CursorMessage::SetPosition(position)) => {
                assert_eq!(position, expected_position);
            }
            Some(CursorMessage::SetVisibility(_)) => {
                panic!("Received unexpected cursor visibility update.")
            }
            None => panic!("Did not receive cursor update."),
        }
    }

    /// Tests that button press, mouse move, and button release inject changes accordingly.
    #[fuchsia::test(allow_stalls = false)]
    async fn down_move_up_event() {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        // Note: The size of the CursorMessage channel's buffer is 3 to allow for one cursor
        // update for every input event being sent.
        let (sender, mut receiver) = futures::channel::mpsc::channel::<CursorMessage>(3);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        let event_time1 = zx::Time::get_monotonic();
        let event_time2 = event_time1.add(fuchsia_zircon::Duration::from_micros(1));
        let event_time3 = event_time2.add(fuchsia_zircon::Duration::from_micros(1));
        let zero_position = Position { x: 0.0, y: 0.0 };
        let expected_position = Position { x: 10.0, y: 15.0 };
        let expected_relative_motion = [10.0, 15.0];
        let event1 = create_mouse_event(
            mouse_binding::MouseLocation::Absolute(Position { x: 0.0, y: 0.0 }),
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Down,
            HashSet::from_iter(vec![1]),
            HashSet::from_iter(vec![1]),
            event_time1,
            &DESCRIPTOR,
        );
        let event2 = create_mouse_event(
            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 0.0, y: 0.0 },
                millimeters: Position {
                    x: 10.0 / mouse_binding::DEFAULT_COUNTS_PER_MM as f32,
                    y: 15.0 / mouse_binding::DEFAULT_COUNTS_PER_MM as f32,
                },
            }),
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Move,
            HashSet::from_iter(vec![1]),
            HashSet::from_iter(vec![1]),
            event_time2,
            &DESCRIPTOR,
        );
        let event3 = create_mouse_event(
            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 0.0, y: 0.0 },
                millimeters: Position { x: 0.0, y: 0.0 },
            }),
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Up,
            HashSet::from_iter(vec![1]),
            HashSet::from_iter(vec![]),
            event_time3,
            &DESCRIPTOR,
        );

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            mpsc::unbounded::<Vec<pointerinjector::Event>>();
        // Up to 2 events per handle_input_event() call.
        let mut injector_stream_receiver = injector_stream_receiver.ready_chunks(2);
        let registry_fut = handle_registry_request_stream2(
            injector_registry_request_stream,
            injector_stream_sender,
        );
        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![event_time1.into_nanos(), event_time2.into_nanos(), event_time3.into_nanos()],
        );

        // Run all futures until the handler future completes.
        let _registry_task = fasync::Task::local(registry_fut);
        let _aggregator_task = fasync::Task::local(aggregator_fut);
        mouse_handler.clone().handle_input_event(event1).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Add,
                    vec![1],
                    zero_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                ),
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Change,
                    vec![1],
                    zero_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                )
            ])
        );

        // Wait until cursor position validation is complete.
        match receiver.next().await {
            Some(CursorMessage::SetPosition(position)) => {
                assert_eq!(position, zero_position);
            }
            Some(CursorMessage::SetVisibility(_)) => {
                panic!("Received unexpected cursor visibility update.")
            }
            None => panic!("Did not receive cursor update."),
        }

        // Send a move event.
        mouse_handler.clone().handle_input_event(event2).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![1],
                expected_position,
                Some(expected_relative_motion),
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time2,
            )])
        );

        // Wait until cursor position validation is complete.
        match receiver.next().await {
            Some(CursorMessage::SetPosition(position)) => {
                assert_eq!(position, expected_position);
            }
            Some(CursorMessage::SetVisibility(_)) => {
                panic!("Received unexpected cursor visibility update.")
            }
            None => panic!("Did not receive cursor update."),
        }

        // Send an up event.
        mouse_handler.clone().handle_input_event(event3).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![],
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time3,
            )])
        );

        // Wait until cursor position validation is complete.
        match receiver.next().await {
            Some(CursorMessage::SetPosition(position)) => {
                assert_eq!(position, expected_position);
            }
            Some(CursorMessage::SetVisibility(_)) => {
                panic!("Received unexpected cursor visibility update.")
            }
            None => panic!("Did not receive cursor update."),
        }
    }

    // Tests that a mouse move event that has already been handled is not forwarded to scenic.
    #[fuchsia::test(allow_stalls = false)]
    async fn handler_ignores_handled_events() {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        let (sender, mut receiver) = futures::channel::mpsc::channel::<CursorMessage>(1);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        let cursor_relative_position = Position { x: 50.0, y: 75.0 };
        let cursor_location =
            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 0.0, y: 0.0 },
                millimeters: Position {
                    x: cursor_relative_position.x / mouse_binding::DEFAULT_COUNTS_PER_MM as f32,
                    y: cursor_relative_position.y / mouse_binding::DEFAULT_COUNTS_PER_MM as f32,
                },
            });
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_mouse_event_with_handled(
            cursor_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Move,
            HashSet::new(),
            HashSet::new(),
            event_time,
            &DESCRIPTOR,
            input_device::Handled::Yes,
        )];

        assert_handler_ignores_input_event_sequence(
            mouse_handler,
            input_events,
            injector_registry_request_stream,
            aggregator_request_stream,
        )
        .await;

        // The cursor location stream should not receive any position.
        assert!(receiver.next().await.is_none());
    }

    fn zero_relative_location() -> mouse_binding::MouseLocation {
        mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
            counts: Position { x: 0.0, y: 0.0 },
            millimeters: Position { x: 0.0, y: 0.0 },
        })
    }

    #[test_case(
        create_mouse_event(
            zero_relative_location(),
            wheel_delta_ticks(1, None),               /*wheel_delta_v*/
            None,                                     /*wheel_delta_h*/
            Some(mouse_binding::PrecisionScroll::No), /*is_precision_scroll*/
            mouse_binding::MousePhase::Wheel,
            HashSet::new(),
            HashSet::new(),
            zx::Time::ZERO,
            &DESCRIPTOR,
        ),
        create_mouse_pointer_sample_event(
            pointerinjector::EventPhase::Change,
            vec![],
            Position { x: 50.0, y: 50.0 },
            None,    /*relative_motion*/
            Some(1), /*wheel_delta_v*/
            None,    /*wheel_delta_h*/
            Some(false), /*is_precision_scroll*/
            zx::Time::ZERO,
        ); "v tick scroll"
    )]
    #[test_case(
        create_mouse_event(
            zero_relative_location(),
            None,                                     /*wheel_delta_v*/
            wheel_delta_ticks(1, None),               /*wheel_delta_h*/
            Some(mouse_binding::PrecisionScroll::No), /*is_precision_scroll*/
            mouse_binding::MousePhase::Wheel,
            HashSet::new(),
            HashSet::new(),
            zx::Time::ZERO,
            &DESCRIPTOR,
        ),
        create_mouse_pointer_sample_event(
            pointerinjector::EventPhase::Change,
            vec![],
            Position { x: 50.0, y: 50.0 },
            None,    /*relative_motion*/
            None,    /*wheel_delta_v*/
            Some(1), /*wheel_delta_h*/
            Some(false), /*is_precision_scroll*/
            zx::Time::ZERO,
        ); "h tick scroll"
    )]
    #[test_case(
        create_mouse_event(
            zero_relative_location(),
            wheel_delta_ticks(1, Some(120.0)),        /*wheel_delta_v*/
            None,                                     /*wheel_delta_h*/
            Some(mouse_binding::PrecisionScroll::No), /*is_precision_scroll*/
            mouse_binding::MousePhase::Wheel,
            HashSet::new(),
            HashSet::new(),
            zx::Time::ZERO,
            &DESCRIPTOR,
        ),
        create_mouse_pointer_sample_event_with_wheel_physical_pixel(
            pointerinjector::EventPhase::Change,
            vec![],
            Position { x: 50.0, y: 50.0 },
            None,        /*relative_motion*/
            Some(1),     /*wheel_delta_v*/
            None,        /*wheel_delta_h*/
            Some(120.0), /*wheel_delta_v_physical_pixel*/
            None,        /*wheel_delta_h_physical_pixel*/
            Some(false), /*is_precision_scroll*/
            zx::Time::ZERO,
        ); "v tick scroll with physical pixel"
    )]
    #[test_case(
        create_mouse_event(
            zero_relative_location(),
            None,                                     /*wheel_delta_v*/
            wheel_delta_ticks(1, Some(120.0)),        /*wheel_delta_h*/
            Some(mouse_binding::PrecisionScroll::No), /*is_precision_scroll*/
            mouse_binding::MousePhase::Wheel,
            HashSet::new(),
            HashSet::new(),
            zx::Time::ZERO,
            &DESCRIPTOR,
        ),
        create_mouse_pointer_sample_event_with_wheel_physical_pixel(
            pointerinjector::EventPhase::Change,
            vec![],
            Position { x: 50.0, y: 50.0 },
            None,        /*relative_motion*/
            None,        /*wheel_delta_v*/
            Some(1),     /*wheel_delta_h*/
            None,        /*wheel_delta_v_physical_pixel*/
            Some(120.0), /*wheel_delta_h_physical_pixel*/
            Some(false), /*is_precision_scroll*/
            zx::Time::ZERO,
        ); "h tick scroll with physical pixel"
    )]
    #[test_case(
        create_mouse_event(
            zero_relative_location(),
            wheel_delta_mm(1.0, Some(120.0)),          /*wheel_delta_v*/
            None,                                      /*wheel_delta_h*/
            Some(mouse_binding::PrecisionScroll::Yes), /*is_precision_scroll*/
            mouse_binding::MousePhase::Wheel,
            HashSet::new(),
            HashSet::new(),
            zx::Time::ZERO,
            &DESCRIPTOR,
        ),
        create_mouse_pointer_sample_event_with_wheel_physical_pixel(
            pointerinjector::EventPhase::Change,
            vec![],
            Position { x: 50.0, y: 50.0 },
            None,        /*relative_motion*/
            None,        /*wheel_delta_v*/
            None,        /*wheel_delta_h*/
            Some(120.0), /*wheel_delta_v_physical_pixel*/
            None,        /*wheel_delta_h_physical_pixel*/
            Some(true),  /*is_precision_scroll*/
            zx::Time::ZERO,
        ); "v mm scroll with physical pixel"
    )]
    #[test_case(
        create_mouse_event(
            zero_relative_location(),
            None,                                      /*wheel_delta_v*/
            wheel_delta_mm(1.0, Some(120.0)),          /*wheel_delta_h*/
            Some(mouse_binding::PrecisionScroll::Yes), /*is_precision_scroll*/
            mouse_binding::MousePhase::Wheel,
            HashSet::new(),
            HashSet::new(),
            zx::Time::ZERO,
            &DESCRIPTOR,
        ),
        create_mouse_pointer_sample_event_with_wheel_physical_pixel(
            pointerinjector::EventPhase::Change,
            vec![],
            Position { x: 50.0, y: 50.0 },
            None,        /*relative_motion*/
            None,        /*wheel_delta_v*/
            None,        /*wheel_delta_h*/
            None,        /*wheel_delta_v_physical_pixel*/
            Some(120.0), /*wheel_delta_h_physical_pixel*/
            Some(true),  /*is_precision_scroll*/
            zx::Time::ZERO,
        ); "h mm scroll with physical pixel"
    )]
    /// Test simple scroll in vertical and horizontal.
    #[fuchsia::test(allow_stalls = false)]
    async fn scroll(event: input_device::InputEvent, want_event: pointerinjector::Event) {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        let (sender, _) = futures::channel::mpsc::channel::<CursorMessage>(1);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            mpsc::unbounded::<Vec<pointerinjector::Event>>();
        // Up to 2 events per handle_input_event() call.
        let mut injector_stream_receiver = injector_stream_receiver.ready_chunks(2);
        let registry_fut = handle_registry_request_stream2(
            injector_registry_request_stream,
            injector_stream_sender,
        );

        let event_time = zx::Time::get_monotonic();

        let event = input_device::InputEvent { event_time, ..event };

        let want_event =
            pointerinjector::Event { timestamp: Some(event_time.into_nanos()), ..want_event };

        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![event.event_time.into_nanos()],
        );

        // Run all futures until the handler future completes.
        let _registry_task = fasync::Task::local(registry_fut);
        let _aggregator_task = fasync::Task::local(aggregator_fut);

        mouse_handler.clone().handle_input_event(event).await;
        let got_events =
            injector_stream_receiver.next().await.map(|events| events.concat()).unwrap();
        pretty_assertions::assert_eq!(got_events.len(), 2);
        assert_matches!(
            got_events[0],
            pointerinjector::Event {
                data: Some(pointerinjector::Data::PointerSample(pointerinjector::PointerSample {
                    phase: Some(pointerinjector::EventPhase::Add),
                    ..
                })),
                ..
            }
        );

        pretty_assertions::assert_eq!(got_events[1], want_event);
    }

    /// Test button down -> scroll -> button up -> continue scroll.
    #[fuchsia::test(allow_stalls = false)]
    async fn down_scroll_up_scroll() {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        let (sender, _) = futures::channel::mpsc::channel::<CursorMessage>(1);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            mpsc::unbounded::<Vec<pointerinjector::Event>>();
        // Up to 2 events per handle_input_event() call.
        let mut injector_stream_receiver = injector_stream_receiver.ready_chunks(2);
        let registry_fut = handle_registry_request_stream2(
            injector_registry_request_stream,
            injector_stream_sender,
        );

        let event_time1 = zx::Time::get_monotonic();
        let event_time2 = event_time1.add(fuchsia_zircon::Duration::from_micros(1));
        let event_time3 = event_time2.add(fuchsia_zircon::Duration::from_micros(1));
        let event_time4 = event_time3.add(fuchsia_zircon::Duration::from_micros(1));

        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![
                event_time1.into_nanos(),
                event_time2.into_nanos(),
                event_time3.into_nanos(),
                event_time4.into_nanos(),
            ],
        );

        // Run all futures until the handler future completes.
        let _registry_task = fasync::Task::local(registry_fut);
        let _aggregator_task = fasync::Task::local(aggregator_fut);

        let zero_location =
            mouse_binding::MouseLocation::Relative(mouse_binding::RelativeLocation {
                counts: Position { x: 0.0, y: 0.0 },
                millimeters: Position { x: 0.0, y: 0.0 },
            });
        let expected_position = Position { x: 50.0, y: 50.0 };

        let down_event = create_mouse_event(
            zero_location,
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Down,
            HashSet::from_iter(vec![1]),
            HashSet::from_iter(vec![1]),
            event_time1,
            &DESCRIPTOR,
        );

        let wheel_event = create_mouse_event(
            zero_location,
            wheel_delta_ticks(1, None),               /* wheel_delta_v */
            None,                                     /* wheel_delta_h */
            Some(mouse_binding::PrecisionScroll::No), /* is_precision_scroll */
            mouse_binding::MousePhase::Wheel,
            HashSet::from_iter(vec![1]),
            HashSet::from_iter(vec![1]),
            event_time2,
            &DESCRIPTOR,
        );

        let up_event = create_mouse_event(
            zero_location,
            None,
            None,
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Up,
            HashSet::from_iter(vec![1]),
            HashSet::new(),
            event_time3,
            &DESCRIPTOR,
        );

        let continue_wheel_event = create_mouse_event(
            zero_location,
            wheel_delta_ticks(1, None),               /* wheel_delta_v */
            None,                                     /* wheel_delta_h */
            Some(mouse_binding::PrecisionScroll::No), /* is_precision_scroll */
            mouse_binding::MousePhase::Wheel,
            HashSet::new(),
            HashSet::new(),
            event_time4,
            &DESCRIPTOR,
        );

        // Handle button down event.
        mouse_handler.clone().handle_input_event(down_event).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Add,
                    vec![1],
                    expected_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                ),
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Change,
                    vec![1],
                    expected_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                ),
            ])
        );

        // Handle wheel event with button pressing.
        mouse_handler.clone().handle_input_event(wheel_event).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![1],
                expected_position,
                None,        /*relative_motion*/
                Some(1),     /*wheel_delta_v*/
                None,        /*wheel_delta_h*/
                Some(false), /*is_precision_scroll*/
                event_time2,
            )])
        );

        // Handle button up event.
        mouse_handler.clone().handle_input_event(up_event).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![],
                expected_position,
                None, /*relative_motion*/
                None, /*wheel_delta_v*/
                None, /*wheel_delta_h*/
                None, /*is_precision_scroll*/
                event_time3,
            )])
        );

        // Handle wheel event after button released.
        mouse_handler.clone().handle_input_event(continue_wheel_event).await;
        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![create_mouse_pointer_sample_event(
                pointerinjector::EventPhase::Change,
                vec![],
                expected_position,
                None,        /*relative_motion*/
                Some(1),     /*wheel_delta_v*/
                None,        /*wheel_delta_h*/
                Some(false), /*is_precision_scroll*/
                event_time4,
            )])
        );
    }

    // Tests that a mouse move event that has already been handled is not forwarded to scenic.
    #[fuchsia::test(allow_stalls = false)]
    async fn touch_hides_cursor() {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        let (sender, mut receiver) = futures::channel::mpsc::channel::<CursorMessage>(1);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        const TOUCH_ID: u32 = 1;
        let touch_descriptor = get_touch_device_descriptor();
        let event_time = zx::Time::get_monotonic();
        let input_events = vec![create_touch_screen_event_with_handled(
            hashmap! {
                fidl_fuchsia_ui_input::PointerEventPhase::Add
                    => vec![create_touch_contact(TOUCH_ID, Position{ x: 20.0, y: 40.0 })],
                fidl_fuchsia_ui_input::PointerEventPhase::Down
                    => vec![create_touch_contact(TOUCH_ID, Position{ x: 20.0, y: 40.0 })],
            },
            event_time,
            &touch_descriptor,
            input_device::Handled::Yes,
        )];

        assert_handler_ignores_input_event_sequence(
            mouse_handler,
            input_events,
            injector_registry_request_stream,
            aggregator_request_stream,
        )
        .await;

        // Touch event should hide the cursor.
        match receiver.next().await {
            Some(CursorMessage::SetVisibility(visible)) => assert_eq!(visible, false),
            _ => panic!("Touch event did not hide the cursor."),
        };
    }

    // Tests that mouse cursor position events are suppressed when the mouse cursor is invisible.
    #[fuchsia::test(allow_stalls = false)]
    async fn invisibility_suppresses_cursor_positions_updates() {
        // Set up fidl streams.
        let (aggregator_proxy, aggregator_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<interaction_observation::AggregatorMarker>()
                .expect("Failed to create interaction observation Aggregator proxy and stream.");
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create MouseInjectorHandler.
        let (cursor_message_sender, mut cursor_message_receiver) =
            futures::channel::mpsc::channel::<CursorMessage>(20);
        let mouse_handler_fut = MouseInjectorHandler::new_handler(
            aggregator_proxy,
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
            cursor_message_sender,
        );
        let (mouse_handler_res, _) = futures::join!(mouse_handler_fut, config_request_stream_fut);
        let mouse_handler = mouse_handler_res.expect("Failed to create mouse handler");

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            mpsc::unbounded::<Vec<pointerinjector::Event>>();
        // Up to 2 events per handle_input_event() call.
        let mut injector_stream_receiver = injector_stream_receiver.ready_chunks(2);
        let registry_fut = handle_registry_request_stream2(
            injector_registry_request_stream,
            injector_stream_sender,
        );

        let event_time1 = zx::Time::get_monotonic();
        let event_time2 = event_time1.add(fuchsia_zircon::Duration::from_micros(1));
        let event_time3 = event_time2.add(fuchsia_zircon::Duration::from_micros(1));
        let event_time4 = event_time3.add(fuchsia_zircon::Duration::from_micros(1));
        let event_time5 = event_time4.add(fuchsia_zircon::Duration::from_micros(1));

        let aggregator_fut = handle_aggregator_request_stream(
            aggregator_request_stream,
            vec![
                event_time1.into_nanos(),
                event_time2.into_nanos(),
                event_time3.into_nanos(),
                event_time4.into_nanos(),
                event_time5.into_nanos(),
            ],
        );

        // Run all futures until the handler future completes.
        let _registry_task = fasync::Task::local(registry_fut);
        let _aggregator_task = fasync::Task::local(aggregator_fut);

        // The first event is to initially add the pointer so that we can more directly compare
        // event2/event3 with event4/event5.
        let zero_position = Position { x: 0.0, y: 0.0 };
        let event1 = create_mouse_event(
            mouse_binding::MouseLocation::Absolute(zero_position),
            None, /* wheel_delta_v */
            None, /* wheel_delta_h */
            None, /* is_precision_scroll */
            mouse_binding::MousePhase::Down,
            HashSet::new(),
            HashSet::new(),
            event_time1,
            &DESCRIPTOR,
        );

        mouse_handler.clone().handle_input_event(event1).await;
        expect_cursor_position_message(cursor_message_receiver.next().await, zero_position);

        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![
                create_mouse_pointer_sample_event(
                    pointerinjector::EventPhase::Add,
                    vec![],
                    zero_position,
                    None, /*relative_motion*/
                    None, /*wheel_delta_v*/
                    None, /*wheel_delta_h*/
                    None, /*is_precision_scroll*/
                    event_time1,
                ),
                create_expected_pointerinjector_mouse_change_sample_event(
                    zero_position,
                    None,
                    event_time1
                ),
            ]),
            "after event1"
        );

        // event2/event3 cause pointerinjector events to be sent, and since the cursor is visible,
        // also causes cursor position messages to be sent.
        let event2 = create_relative_mouse_move_event(Position { x: 3.0, y: 5.0 }, event_time2);
        let event3 = create_relative_mouse_move_event(Position { x: 4.0, y: 6.0 }, event_time3);

        mouse_handler.clone().handle_input_event(event2).await;
        mouse_handler.clone().handle_input_event(event3).await;

        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![
                create_expected_pointerinjector_mouse_change_sample_event(
                    Position { x: 3.0, y: 5.0 },
                    Some([3.0, 5.0]),
                    event_time2
                ),
                create_expected_pointerinjector_mouse_change_sample_event(
                    Position { x: 7.0, y: 11.0 },
                    Some([4.0, 6.0]),
                    event_time3
                ),
            ]),
            "after event2 and event3"
        );

        expect_cursor_position_message(
            cursor_message_receiver.next().await,
            Position { x: 3.0, y: 5.0 },
        );
        expect_cursor_position_message(
            cursor_message_receiver.next().await,
            Position { x: 7.0, y: 11.0 },
        );

        // Make the cursor invisible.  The MouseInjectorHandler will continue to generate
        // pointerinjector events, but it will not generate any SetPosition cursor messages until
        // the cursor becomes visible again.
        assert_eq!(true, mouse_handler.toggle_immersive_mode().await.unwrap());
        expect_cursor_visibility_message(cursor_message_receiver.next().await, false);

        // event4/event5 cause pointerinjector events to be sent.  However, now the handler is in
        // immersive mode; since the cursor is NOT visible, no cursor position messages are sent.
        let event4 = create_relative_mouse_move_event(Position { x: -2.0, y: -2.0 }, event_time4);
        let event5 = create_relative_mouse_move_event(Position { x: -1.0, y: -3.0 }, event_time5);

        mouse_handler.clone().handle_input_event(event4).await;
        mouse_handler.clone().handle_input_event(event5).await;

        assert_eq!(
            injector_stream_receiver.next().await.map(|events| events.concat()),
            Some(vec![
                create_expected_pointerinjector_mouse_change_sample_event(
                    Position { x: 5.0, y: 9.0 },
                    Some([-2.0, -2.0]),
                    event_time4
                ),
                create_expected_pointerinjector_mouse_change_sample_event(
                    Position { x: 4.0, y: 6.0 },
                    Some([-1.0, -3.0]),
                    event_time5
                ),
            ]),
            "after event4 and event5"
        );

        // Make the cursor visible again.  This will result in a cursor position update, followed
        // by a cursor visibility update.
        //
        // NOTE: above, we didn't verify directly that no cursor position messages were generated.
        // However, if they were, there would have been 2: one for each of event4/event5, similar to
        // how there was one for each of event2/event3 while the cursor was visible.  Instead, by
        // toggling off immersive mode, we make the cursor visible, which sends:
        //   - a cursor position message with the latest position, then:
        //   - a cursor visibility message
        assert_eq!(false, mouse_handler.toggle_immersive_mode().await.unwrap());
        expect_cursor_position_message(
            cursor_message_receiver.next().await,
            Position { x: 4.0, y: 6.0 },
        );
        expect_cursor_visibility_message(cursor_message_receiver.next().await, true);
    }
}
