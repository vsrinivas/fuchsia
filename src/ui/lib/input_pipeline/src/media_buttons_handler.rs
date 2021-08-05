// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_handler::InputHandler,
    crate::{consumer_controls, input_device},
    anyhow::{Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_input_report as fidl_input_report, fidl_fuchsia_ui_input as fidl_ui_input,
    fidl_fuchsia_ui_policy as fidl_ui_policy,
    fuchsia_syslog::fx_log_err,
    futures::lock::Mutex,
    futures::TryStreamExt,
    std::rc::Rc,
};

/// A [`MediaButtonsHandler`] tracks MediaButtonListeners and sends media button events to them.
#[derive(Debug)]
pub struct MediaButtonsHandler {
    /// The mutable fields of this handler.
    inner: Mutex<MediaButtonsHandlerInner>,
}

#[derive(Debug)]
struct MediaButtonsHandlerInner {
    /// The media button listeners.
    pub listeners: Vec<fidl_ui_policy::MediaButtonsListenerProxy>,

    /// The last MediaButtonsEvent sent to all listeners.
    /// This is used to send new listeners the state of the media buttons.
    pub last_event: Option<fidl_ui_input::MediaButtonsEvent>,
}

#[async_trait(?Send)]
impl InputHandler for MediaButtonsHandler {
    async fn handle_input_event(
        self: Rc<Self>,
        input_event: input_device::InputEvent,
    ) -> Vec<input_device::InputEvent> {
        match input_event {
            input_device::InputEvent {
                device_event: input_device::InputDeviceEvent::ConsumerControls(media_buttons_event),
                device_descriptor: input_device::InputDeviceDescriptor::ConsumerControls(_),
                event_time: _,
            } => {
                let media_buttons_event = Self::create_media_buttons_event(media_buttons_event);

                // Send the event if the media buttons are supported.
                self.send_event_to_listeners(&media_buttons_event).await;

                // Store the sent event.
                let mut inner = self.inner.lock().await;
                inner.last_event = Some(media_buttons_event);

                vec![]
            }
            _ => vec![input_event],
        }
    }
}

impl MediaButtonsHandler {
    /// Creates a new [`MediaButtonsHandler`] that sends media button events to listeners.
    pub fn new() -> Rc<Self> {
        let media_buttons_handler = Self {
            inner: Mutex::new(MediaButtonsHandlerInner { listeners: Vec::new(), last_event: None }),
        };

        Rc::new(media_buttons_handler)
    }

    /// Handles the incoming DeviceListenerRegistryRequestStream.
    ///
    /// This method will end when the request stream is closed. If the stream closes with an
    /// error the error will be returned in the Result.
    ///
    /// # Parameters
    /// - `stream`: The stream of DeviceListenerRegistryRequestStream.
    pub async fn handle_device_listener_registry_request_stream(
        self: &Rc<Self>,
        mut stream: fidl_ui_policy::DeviceListenerRegistryRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream
            .try_next()
            .await
            .context("Error handling device listener registry request stream")?
        {
            match request {
                fidl_ui_policy::DeviceListenerRegistryRequest::RegisterListener {
                    listener,
                    responder,
                } => {
                    if let Ok(proxy) = listener.into_proxy() {
                        // Add the listener to the registry.
                        let mut inner = self.inner.lock().await;
                        inner.listeners.push(proxy.clone());

                        // Send the listener the last media button event.
                        if let Some(event) = &inner.last_event {
                            proxy
                                .on_event(event.clone())
                                .await
                                .context("Failed to send media buttons event to listener")?;
                        }
                    }
                    let _ = responder.send();
                }
                _ => {}
            }
        }

        Ok(())
    }

    /// Creates a fidl_ui_input::MediaButtonsEvent from a media_buttons::MediaButtonEvent.
    ///
    /// # Parameters
    /// -  `event`: The MediaButtonEvent to create a MediaButtonsEvent from.
    fn create_media_buttons_event(
        event: consumer_controls::ConsumerControlsEvent,
    ) -> fidl_ui_input::MediaButtonsEvent {
        let mut new_event = fidl_ui_input::MediaButtonsEvent {
            volume: Some(0),
            mic_mute: Some(false),
            pause: Some(false),
            camera_disable: Some(false),
            ..fidl_ui_input::MediaButtonsEvent::EMPTY
        };
        for button in event.pressed_buttons {
            match button {
                fidl_input_report::ConsumerControlButton::VolumeUp => {
                    new_event.volume = Some(new_event.volume.unwrap().saturating_add(1));
                }
                fidl_input_report::ConsumerControlButton::VolumeDown => {
                    new_event.volume = Some(new_event.volume.unwrap().saturating_sub(1));
                }
                fidl_input_report::ConsumerControlButton::MicMute => {
                    new_event.mic_mute = Some(true);
                }
                fidl_input_report::ConsumerControlButton::Pause => {
                    new_event.pause = Some(true);
                }
                fidl_input_report::ConsumerControlButton::CameraDisable => {
                    new_event.camera_disable = Some(true);
                }
                _ => {}
            }
        }

        new_event
    }

    /// Sends media button events to media button listeners.
    ///
    /// # Parameters
    /// - `event`: The event to send to the listeners.
    async fn send_event_to_listeners(self: &Rc<Self>, event: &fidl_ui_input::MediaButtonsEvent) {
        let inner = self.inner.lock().await;
        for listener in inner.listeners.iter() {
            if let Err(e) = listener.on_event(event.clone()).await {
                fx_log_err!("Error sending MediaButtonsEvent to listener: {:?}", e);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::testing_utilities, fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_input_report as fidl_input_report, fuchsia_async as fasync,
        fuchsia_zircon as zx, futures::StreamExt,
    };

    fn spawn_device_listener_registry_server(
        handler: Rc<MediaButtonsHandler>,
    ) -> fidl_ui_policy::DeviceListenerRegistryProxy {
        let (device_listener_proxy, device_listener_stream) =
            create_proxy_and_stream::<fidl_ui_policy::DeviceListenerRegistryMarker>()
                .expect("Failed to create DeviceListenerRegistry proxy and stream.");

        fasync::Task::local(async move {
            let _ = handler
                .handle_device_listener_registry_request_stream(device_listener_stream)
                .await;
        })
        .detach();

        device_listener_proxy
    }

    fn create_ui_input_media_buttons_event(
        volume: Option<i8>,
        mic_mute: Option<bool>,
        pause: Option<bool>,
        camera_disable: Option<bool>,
    ) -> fidl_ui_input::MediaButtonsEvent {
        fidl_ui_input::MediaButtonsEvent {
            volume,
            mic_mute,
            pause,
            camera_disable,
            ..fidl_ui_input::MediaButtonsEvent::EMPTY
        }
    }

    /// Tests that a media button listener can be registered and is sent the latest event upon
    /// registration.
    #[fasync::run_singlethreaded(test)]
    async fn register_media_buttons_listener() {
        // Set up DeviceListenerRegistry.
        let media_buttons_handler = Rc::new(MediaButtonsHandler {
            inner: Mutex::new(MediaButtonsHandlerInner {
                listeners: vec![],
                last_event: Some(create_ui_input_media_buttons_event(Some(1), None, None, None)),
            }),
        });
        let device_listener_proxy =
            spawn_device_listener_registry_server(media_buttons_handler.clone());

        // Register a listener.
        let (listener, mut listener_stream) =
            fidl::endpoints::create_request_stream::<fidl_ui_policy::MediaButtonsListenerMarker>()
                .unwrap();
        let register_listener_fut = async {
            let res = device_listener_proxy.register_listener(listener).await;
            assert!(res.is_ok());
        };

        // Assert listener was registered and received last event.
        let expected_event = create_ui_input_media_buttons_event(Some(1), None, None, None);
        let assert_fut = async {
            match listener_stream.next().await {
                Some(Ok(fidl_ui_policy::MediaButtonsListenerRequest::OnEvent {
                    event,
                    responder,
                })) => {
                    assert_eq!(event, expected_event);
                    responder.send().expect("responder failed.");
                }
                _ => assert!(false),
            }
        };
        futures::join!(register_listener_fut, assert_fut);
        assert_eq!(media_buttons_handler.inner.lock().await.listeners.len(), 1);
    }

    /// Tests that all supported buttons are sent.
    #[fasync::run_singlethreaded(test)]
    async fn listener_receives_all_buttons() {
        let media_buttons_handler = MediaButtonsHandler::new();
        let device_listener_proxy =
            spawn_device_listener_registry_server(media_buttons_handler.clone());

        // Register a listener.
        let (listener, listener_stream) =
            fidl::endpoints::create_request_stream::<fidl_ui_policy::MediaButtonsListenerMarker>()
                .unwrap();
        let _ = device_listener_proxy.register_listener(listener).await;

        // Setup events and expectations.
        let descriptor = testing_utilities::consumer_controls_device_descriptor();
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events = vec![testing_utilities::create_consumer_controls_event(
            vec![
                fidl_input_report::ConsumerControlButton::VolumeUp,
                fidl_input_report::ConsumerControlButton::VolumeDown,
                fidl_input_report::ConsumerControlButton::Pause,
                fidl_input_report::ConsumerControlButton::MicMute,
                fidl_input_report::ConsumerControlButton::CameraDisable,
            ],
            event_time,
            &descriptor,
        )];
        let expected_events =
            vec![create_ui_input_media_buttons_event(Some(0), Some(true), Some(true), Some(true))];

        // Assert registered listener receives event.
        assert_input_event_sequence_generates_media_buttons_events!(
            input_handler: media_buttons_handler,
            input_events: input_events,
            expected_events: expected_events,
            media_buttons_listener_request_stream: vec![listener_stream],
        );
    }

    /// Tests that multiple listeners are supported.
    #[fasync::run_singlethreaded(test)]
    async fn multiple_listeners_receive_event() {
        let media_buttons_handler = MediaButtonsHandler::new();
        let device_listener_proxy =
            spawn_device_listener_registry_server(media_buttons_handler.clone());

        // Register two listeners.
        let (first_listener, first_listener_stream) =
            fidl::endpoints::create_request_stream::<fidl_ui_policy::MediaButtonsListenerMarker>()
                .unwrap();
        let (second_listener, second_listener_stream) =
            fidl::endpoints::create_request_stream::<fidl_ui_policy::MediaButtonsListenerMarker>()
                .unwrap();
        let _ = device_listener_proxy.register_listener(first_listener).await;
        let _ = device_listener_proxy.register_listener(second_listener).await;

        // Setup events and expectations.
        let descriptor = testing_utilities::consumer_controls_device_descriptor();
        let event_time = zx::Time::get_monotonic().into_nanos() as input_device::EventTime;
        let input_events = vec![testing_utilities::create_consumer_controls_event(
            vec![fidl_input_report::ConsumerControlButton::VolumeUp],
            event_time,
            &descriptor,
        )];
        let expected_events = vec![create_ui_input_media_buttons_event(
            Some(1),
            Some(false),
            Some(false),
            Some(false),
        )];

        // Assert registered listeners receives event.
        assert_input_event_sequence_generates_media_buttons_events!(
            input_handler: media_buttons_handler,
            input_events: input_events,
            expected_events: expected_events,
            media_buttons_listener_request_stream:
                vec![first_listener_stream, second_listener_stream],
        );
    }
}
