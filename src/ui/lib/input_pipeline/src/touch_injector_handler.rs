// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler::InputHandler,
    crate::touch,
    crate::utils::{Position, Size},
    anyhow::Error,
    async_trait::async_trait,
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_ui_pointerinjector as pointerinjector,
    fidl_fuchsia_ui_pointerinjector_configuration as pointerinjector_config,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::lock::Mutex,
    futures::stream::StreamExt,
    std::collections::HashMap,
    std::convert::TryInto,
    std::rc::Rc,
};

/// An input handler that parses touch events and forwards them to Scenic through the
/// fidl_fuchsia_pointerinjector protocols.
#[derive(Debug)]
pub struct TouchInjectorHandler {
    /// The mutable fields of this handler.
    inner: Mutex<TouchInjectorHandlerInner>,

    /// The scope and coordinate system of injection.
    /// See fidl_fuchsia_pointerinjector::Context for more details.
    context_view_ref: fidl_fuchsia_ui_views::ViewRef,

    /// The region where dispatch is attempted for injected events.
    /// See fidl_fuchsia_pointerinjector::Target for more details.
    target_view_ref: fidl_fuchsia_ui_views::ViewRef,

    /// The size of the display associated with the touch device, used to convert
    /// coordinates from the touch input report to device coordinates (which is what
    /// Scenic expects).
    display_size: Size,

    /// The FIDL proxy to register new injectors.
    injector_registry_proxy: pointerinjector::RegistryProxy,

    /// The FIDL proxy used to get configuration details for pointer injection.
    configuration_proxy: pointerinjector_config::SetupProxy,
}

#[derive(Debug)]
struct TouchInjectorHandlerInner {
    /// A rectangular region that directs injected events into a target.
    /// See fidl_fuchsia_pointerinjector::Viewport for more details.
    viewport: Option<pointerinjector::Viewport>,

    /// The injectors registered with Scenic, indexed by their device ids.
    injectors: HashMap<u32, pointerinjector::DeviceProxy>,
}

#[async_trait(?Send)]
impl InputHandler for TouchInjectorHandler {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::Touch(touch_event),
                device_descriptor:
                    input_device::InputDeviceDescriptor::Touch(touch_device_descriptor),
                event_time,
            } => {
                // Create a new injector if this is the first time seeing device_id.
                self.ensure_injector_registered(&touch_device_descriptor).await;

                // Handle the event.
                self.handle_touch_event(&touch_event, &touch_device_descriptor, event_time).await;

                // Consume the event (i.e., don't forward it to the next handler).
                vec![]
            }
            // Forward event to the next handler.
            input_event => vec![input_event],
        }
    }
}

impl TouchInjectorHandler {
    /// Creates a new touch handler that holds touch pointer injectors.
    /// The caller is expected to spawn a task to continually watch for updates to the viewport.
    /// Example:
    /// let handler = TouchInjectorHandler::new(display_size).await.expect("Error");
    /// fasync::Task::local(handler.clone().watch_viewport()).detach();
    ///
    /// # Parameters
    /// - `display_size`: The size of the associated touch display.
    ///
    /// # Errors
    /// If unable to connect to pointerinjector protocols.
    pub async fn new(display_size: Size) -> Result<Rc<Self>, Error> {
        let configuration_proxy = connect_to_protocol::<pointerinjector_config::SetupMarker>()?;
        let injector_registry_proxy = connect_to_protocol::<pointerinjector::RegistryMarker>()?;

        Self::new_handler(configuration_proxy, injector_registry_proxy, display_size).await
    }

    /// Creates a new touch handler that holds touch pointer injectors.
    /// The caller is expected to spawn a task to continually watch for updates to the viewport.
    /// Example:
    /// let handler = TouchInjectorHandler::new(display_size).await.expect("Error");
    /// fasync::Task::local(handler.clone().watch_viewport()).detach();
    ///
    /// # Parameters
    /// - `configuration_proxy`: A proxy used to get configuration details for pointer injection.
    /// - `injector_registry_proxy`: A proxy used to register new pointer injectors.
    /// - `display_size`: The size of the associated touch display.
    ///
    /// # Errors
    /// If unable to get injection view refs from `configuration_proxy`.
    pub async fn new_handler(
        configuration_proxy: pointerinjector_config::SetupProxy,
        injector_registry_proxy: pointerinjector::RegistryProxy,
        display_size: Size,
    ) -> Result<Rc<Self>, Error> {
        // Get the context and target views to inject into.
        let (context, target) = configuration_proxy.get_view_refs().await?;

        // Continuously watch for viewport updates.
        let handler = Rc::new(TouchInjectorHandler {
            inner: Mutex::new(TouchInjectorHandlerInner {
                viewport: None,
                injectors: HashMap::new(),
            }),
            context_view_ref: context,
            target_view_ref: target,
            display_size,
            injector_registry_proxy,
            configuration_proxy,
        });

        Ok(handler)
    }

    /// Adds a new pointer injector and tracks it in `self.injectors` if one doesn't exist at
    /// `touch_descriptor.device_id`.
    ///
    /// # Parameters
    /// - `touch_descriptor`: The descriptor of the new touch device.
    async fn ensure_injector_registered(
        self: &Rc<Self>,
        touch_descriptor: &touch::TouchDeviceDescriptor,
    ) {
        let mut inner = self.inner.lock().await;
        if inner.injectors.contains_key(&touch_descriptor.device_id) {
            return;
        }

        // Create a new injector.
        let (device_proxy, device_server) =
            create_proxy::<pointerinjector::DeviceMarker>().expect("Failed to create DeviceProxy.");
        let context = fuchsia_scenic::duplicate_view_ref(&self.context_view_ref)
            .expect("Failed to duplicate context view ref.");
        let target = fuchsia_scenic::duplicate_view_ref(&self.target_view_ref)
            .expect("Failed to duplicate target view ref.");
        let viewport = inner.viewport.clone();
        let config = pointerinjector::Config {
            device_id: Some(touch_descriptor.device_id),
            device_type: Some(pointerinjector::DeviceType::Touch),
            context: Some(pointerinjector::Context::View(context)),
            target: Some(pointerinjector::Target::View(target)),
            viewport: viewport,
            dispatch_policy: Some(pointerinjector::DispatchPolicy::TopHitAndAncestorsInTarget),
            scroll_v_range: None,
            scroll_h_range: None,
            buttons: None,
            ..pointerinjector::Config::EMPTY
        };

        // Register the new injector.
        self.injector_registry_proxy
            .register(config, device_server)
            .await
            .expect("Failed to register injector.");
        fx_log_info!("Registered injector with device id {:?}", touch_descriptor.device_id);

        // Keep track of the injector.
        inner.injectors.insert(touch_descriptor.device_id, device_proxy);
    }

    /// Handles the given event and sends it to Scenic.
    ///
    /// # Parameters
    /// - `touch_event`: The touch event to send to Scenic.
    /// - `touch_descriptor`: The descriptor for the device that sent the touch event.
    /// - `event_time`: The time in nanoseconds when the event was first recorded.
    async fn handle_touch_event(
        &self,
        touch_event: &touch::TouchEvent,
        touch_descriptor: &touch::TouchDeviceDescriptor,
        event_time: input_device::EventTime,
    ) {
        // The order in which events are sent to clients.
        let ordered_phases = vec![
            pointerinjector::EventPhase::Add,
            pointerinjector::EventPhase::Change,
            pointerinjector::EventPhase::Remove,
        ];

        let mut events: Vec<pointerinjector::Event> = vec![];
        for phase in ordered_phases {
            let contacts: Vec<touch::TouchContact> = touch_event
                .injector_contacts
                .get(&phase)
                .map_or(vec![], |contacts| contacts.to_owned());
            let new_events = contacts.into_iter().map(|contact| {
                Self::create_pointer_sample_event(
                    phase,
                    &contact,
                    touch_descriptor,
                    &self.display_size,
                    event_time,
                )
            });
            events.extend(new_events);
        }

        let inner = self.inner.lock().await;
        if let Some(injector) = inner.injectors.get(&touch_descriptor.device_id) {
            let events_to_send = &mut events.into_iter();
            let fut = injector.inject(events_to_send);
            let _ = fut.await;
        } else {
            fx_log_err!("No injector found for touch device {}.", touch_descriptor.device_id);
        }
    }

    /// Creates a [`fidl_fuchsia_ui_pointerinjector::Event`] representing the given touch contact.
    ///
    /// # Parameters
    /// - `phase`: The phase of the touch contact.
    /// - `contact`: The touch contact to create the event for.
    /// - `touch_descriptor`: The device descriptor for the device that generated the event.
    /// - `display_size`: The size of the associated touch display.
    /// - `event_time`: The time in nanoseconds when the event was first recorded.
    fn create_pointer_sample_event(
        phase: pointerinjector::EventPhase,
        contact: &touch::TouchContact,
        touch_descriptor: &touch::TouchDeviceDescriptor,
        display_size: &Size,
        event_time: input_device::EventTime,
    ) -> pointerinjector::Event {
        let position =
            Self::display_coordinate_from_contact(&contact, &touch_descriptor, display_size);
        let pointer_sample = pointerinjector::PointerSample {
            pointer_id: Some(contact.id),
            phase: Some(phase),
            position_in_viewport: Some([position.x, position.y]),
            scroll_v: None,
            scroll_h: None,
            pressed_buttons: None,
            ..pointerinjector::PointerSample::EMPTY
        };
        let data = pointerinjector::Data::PointerSample(pointer_sample);

        pointerinjector::Event {
            timestamp: Some(event_time.try_into().unwrap()),
            data: Some(data),
            trace_flow_id: None,
            ..pointerinjector::Event::EMPTY
        }
    }

    /// Converts an input event touch to a display coordinate, which is the coordinate space in
    /// which Scenic handles events.
    ///
    /// The display coordinate is calculated by normalizing the contact position to the display
    /// size. It does not account for the viewport position, which Scenic handles directly.
    ///
    /// # Parameters
    /// - `contact`: The contact to get the display coordinate from.
    /// - `touch_descriptor`: The device descriptor for the device that generated the event.
    ///                       This is used to compute the device coordinate.
    ///
    /// # Returns
    /// (x, y) coordinates.
    fn display_coordinate_from_contact(
        contact: &touch::TouchContact,
        touch_descriptor: &touch::TouchDeviceDescriptor,
        display_size: &Size,
    ) -> Position {
        if let Some(contact_descriptor) = touch_descriptor.contacts.first() {
            // Scale the x position.
            let x_range: f32 =
                contact_descriptor.x_range.max as f32 - contact_descriptor.x_range.min as f32;
            let x_wrt_range: f32 = contact.position.x - contact_descriptor.x_range.min as f32;
            let x: f32 = (display_size.width * x_wrt_range) / x_range;

            // Scale the y position.
            let y_range: f32 =
                contact_descriptor.y_range.max as f32 - contact_descriptor.y_range.min as f32;
            let y_wrt_range: f32 = contact.position.y - contact_descriptor.y_range.min as f32;
            let y: f32 = (display_size.height * y_wrt_range) / y_range;

            Position { x, y }
        } else {
            return contact.position;
        }
    }

    /// Watches for viewport updates from the scene manager.
    pub async fn watch_viewport(self: Rc<Self>) {
        let configuration_proxy = self.configuration_proxy.clone();
        let mut viewport_stream =
            HangingGetStream::new(Box::new(move || Some(configuration_proxy.watch_viewport())));
        loop {
            match viewport_stream.next().await {
                Some(Ok(new_viewport)) => {
                    // Update the viewport tracked by this handler.
                    let mut inner = self.inner.lock().await;
                    inner.viewport = Some(new_viewport.clone());

                    // Update Scenic with the latest viewport.
                    for (_device_id, injector) in inner.injectors.iter() {
                        let events = &mut vec![pointerinjector::Event {
                            timestamp: Some(fuchsia_async::Time::now().into_nanos()),
                            data: Some(pointerinjector::Data::Viewport(new_viewport.clone())),
                            trace_flow_id: Some(fuchsia_trace::generate_nonce()),
                            ..pointerinjector::Event::EMPTY
                        }]
                        .into_iter();
                        injector.inject(events).await.expect("Failed to inject updated viewport.");
                    }
                }
                _ => break,
            }
        }

        fx_log_err!("Stopped tracking viewport changes.");
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::testing_utilities::{
            create_touch_contact, create_touch_event, create_touch_pointer_sample_event,
        },
        fidl_fuchsia_input_report as fidl_input_report, fidl_fuchsia_ui_input as fidl_ui_input,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::StreamExt,
        maplit::hashmap,
    };

    const TOUCH_ID: u32 = 1;
    const DISPLAY_WIDTH: f32 = 100.0;
    const DISPLAY_HEIGHT: f32 = 100.0;

    /// Returns an |input_device::InputDeviceDescriptor::TouchDescriptor|.
    fn get_touch_device_descriptor() -> input_device::InputDeviceDescriptor {
        input_device::InputDeviceDescriptor::Touch(touch::TouchDeviceDescriptor {
            device_id: 1,
            contacts: vec![touch::ContactDeviceDescriptor {
                x_range: fidl_input_report::Range { min: 0, max: 100 },
                y_range: fidl_input_report::Range { min: 0, max: 100 },
                pressure_range: None,
                width_range: None,
                height_range: None,
            }],
        })
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
            assert!(false, "RegistryRequestStream failed.");
        }
    }

    /// Handles |fidl_fuchsia_pointerinjector::DeviceRequest|s by asserting the injector stream
    /// received on `injector_stream_receiver` gets `expected_event`.
    async fn handle_device_request_stream(
        injector_stream_receiver: futures::channel::oneshot::Receiver<
            pointerinjector::DeviceRequestStream,
        >,
        expected_event: pointerinjector::Event,
    ) {
        let mut injector_stream =
            injector_stream_receiver.await.expect("Failed to get DeviceRequestStream.");
        match injector_stream.next().await {
            Some(request) => match request {
                Ok(pointerinjector::DeviceRequest::Inject { events, responder }) => {
                    assert_eq!(events.len(), 1);
                    assert_eq!(events[0].timestamp, expected_event.timestamp);
                    assert_eq!(events[0].data, expected_event.data);
                    responder.send().expect("failed to respond");
                }
                _ => {
                    assert!(false, "Unexpected DeviceRequest.");
                }
            },
            None => assert!(false, "Expected another event."),
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

    // Tests that TouchInjectorHandler::watch_viewport() tracks viewport updates and notifies
    // injectors about said updates.
    #[test]
    fn watch_viewport() {
        let mut exec = fasync::TestExecutor::new().expect("executor needed");

        // Create touch handler.
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, _injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let touch_handler_fut = TouchInjectorHandler::new_handler(
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
        );
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);
        let (touch_handler_res, _) = exec.run_singlethreaded(futures::future::join(
            touch_handler_fut,
            config_request_stream_fut,
        ));
        if touch_handler_res.is_err() {
            panic!("Failed to create touch handler.")
        }
        let touch_handler = touch_handler_res.unwrap();

        // Add an injector.
        let (injector_device_proxy, mut injector_device_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::DeviceMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        exec.run_singlethreaded(async {
            let mut inner = touch_handler.inner.lock().await;
            inner.injectors.insert(1, injector_device_proxy);
        });

        {
            // Watch for viewport changes.
            let watch_viewport_fut = touch_handler.clone().watch_viewport();
            futures::pin_mut!(watch_viewport_fut);
            let _ = exec.run_until_stalled(&mut watch_viewport_fut);

            // Send a viewport update.
            match exec.run_singlethreaded(&mut configuration_request_stream.next()) {
                Some(Ok(pointerinjector_config::SetupRequest::WatchViewport {
                    responder, ..
                })) => {
                    responder.send(create_viewport(0.0, 100.0)).expect("Failed to send viewport.");
                }
                other => panic!("Received unexpected value: {:?}", other),
            };
            let _ = exec.run_until_stalled(&mut watch_viewport_fut);

            // Check that the injector received an updated viewport
            let expected_data = pointerinjector::Data::Viewport(create_viewport(0.0, 100.0));
            exec.run_singlethreaded(async {
                match injector_device_request_stream.next().await {
                    Some(Ok(pointerinjector::DeviceRequest::Inject { events, responder })) => {
                        assert_eq!(events.len(), 1);
                        assert!(events[0].data.is_some());
                        assert_eq!(events[0].data, Some(expected_data));
                        responder.send().expect("injector stream failed to respond.");
                    }
                    other => panic!("Received unexpected value: {:?}", other),
                }
            });

            // Send another viewport update.
            let _ = exec.run_until_stalled(&mut watch_viewport_fut);
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
            let _ = exec.run_until_stalled(&mut watch_viewport_fut);

            // Check that the injector received an updated viewport
            let expected_data = pointerinjector::Data::Viewport(create_viewport(100.0, 200.0));
            exec.run_singlethreaded(async {
                match injector_device_request_stream.next().await {
                    Some(Ok(pointerinjector::DeviceRequest::Inject { events, responder })) => {
                        assert_eq!(events.len(), 1);
                        assert!(events[0].data.is_some());
                        assert_eq!(events[0].data, Some(expected_data));
                        responder.send().expect("injector stream failed to respond.");
                    }
                    other => panic!("Received unexpected value: {:?}", other),
                }
            });
        }

        // Check the viewport on the handler is accurate.
        let expected_viewport = create_viewport(100.0, 200.0);
        exec.run_singlethreaded(async {
            assert_eq!(touch_handler.inner.lock().await.viewport, Some(expected_viewport));
        });
    }

    // Tests that an add contact event is handled correctly.
    #[fasync::run_singlethreaded(test)]
    async fn add_contact() {
        // Set up fidl streams.
        let (configuration_proxy, mut configuration_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector_config::SetupMarker>()
                .expect("Failed to create pointerinjector Setup proxy and stream.");
        let (injector_registry_proxy, injector_registry_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<pointerinjector::RegistryMarker>()
                .expect("Failed to create pointerinjector Registry proxy and stream.");
        let config_request_stream_fut =
            handle_configuration_request_stream(&mut configuration_request_stream);

        // Create TouchInjectorHandler.
        let touch_handler_fut = TouchInjectorHandler::new_handler(
            configuration_proxy,
            injector_registry_proxy,
            Size { width: DISPLAY_WIDTH, height: DISPLAY_HEIGHT },
        );
        let (touch_handler_res, _) = futures::join!(touch_handler_fut, config_request_stream_fut);
        if touch_handler_res.is_err() {
            panic!("Failed to create touch handler.")
        }
        let touch_handler = touch_handler_res.unwrap();

        // Create touch event.
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let contact = create_touch_contact(TOUCH_ID, Position { x: 20.0, y: 40.0 });
        let descriptor = get_touch_device_descriptor();
        let input_event = create_touch_event(
            hashmap! {
                fidl_ui_input::PointerEventPhase::Add
                    => vec![contact.clone()],
            },
            event_time,
            &descriptor,
        );
        // Handle event.
        let handle_event_fut = touch_handler.handle_input_event(input_event);

        // Declare expected event.
        let expected_event = create_touch_pointer_sample_event(
            pointerinjector::EventPhase::Add,
            &contact,
            Position { x: 20.0, y: 40.0 },
            event_time,
        );

        // Create a channel for the the registered device's handle to be forwarded to the
        // DeviceRequestStream handler. This allows the registry_fut to complete and allows
        // handle_input_event() to continue.
        let (injector_stream_sender, injector_stream_receiver) =
            futures::channel::oneshot::channel::<pointerinjector::DeviceRequestStream>();
        let registry_fut = handle_registry_request_stream(
            injector_registry_request_stream,
            injector_stream_sender,
        );
        let device_fut = handle_device_request_stream(injector_stream_receiver, expected_event);

        // Await all futures concurrently. If this completes, then the touch event was handled and
        // matches `expected_event`.
        let _ = futures::join!(handle_event_fut, registry_fut, device_fut);
    }
}
