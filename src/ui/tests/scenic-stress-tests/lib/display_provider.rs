// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    fidl::{
        endpoints::{RequestStream, ServerEnd},
        AsHandleRef, Event,
    },
    fidl_fuchsia_hardware_display as fdisp, fuchsia_async as fasync,
    fuchsia_async::futures::TryStreamExt,
    fuchsia_zircon as sys,
    log::debug,
    std::{
        collections::HashMap,
        convert::TryInto,
        sync::{Arc, Mutex},
    },
    test_utils_lib::injectors::ProtocolInjector,
};

/// Stores the state of the mock display, including
/// layer <-> (image, event) mappings, event objects
/// and monotonically increasing counters for image
/// and layer IDs.
struct DisplayStateInner {
    num_updates: u64,
    image_id: u64,
    layer_id: u64,
    active_images_layers: HashMap<u64, (u64, u64)>,
    events_to_signal: Vec<u64>,
    events: HashMap<u64, Event>,
}

/// A thin thread-safe wrapper over the display state
/// object. Provides thread-safe access to the internal
/// state of the display.
#[derive(Clone)]
pub struct DisplayState {
    inner: Arc<Mutex<DisplayStateInner>>,
}

impl DisplayState {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(DisplayStateInner {
                num_updates: 0,
                image_id: 0,
                layer_id: 0,
                active_images_layers: HashMap::new(),
                events_to_signal: vec![],
                events: HashMap::new(),
            })),
        }
    }

    // Returns the number of updates applied to the display
    pub fn get_num_updates(&self) -> u64 {
        let state = self.inner.lock().unwrap();
        state.num_updates
    }

    // Returns the current image ID and increments it by one.
    fn increment_image_id(&self) -> u64 {
        let mut state = self.inner.lock().unwrap();
        let image_id = state.image_id;
        state.image_id += 1;
        image_id
    }

    // Returns the current layer ID and increments it by one.
    fn increment_layer_id(&self) -> u64 {
        let mut state = self.inner.lock().unwrap();
        let layer_id = state.layer_id;
        state.layer_id += 1;
        layer_id
    }

    // Stores an event object with a given event ID.
    fn add_event(&self, event_id: u64, event: Event) {
        let mut state = self.inner.lock().unwrap();
        state.events.insert(event_id, event);
    }

    // For a particular layer, sets the active image ID and event ID on removal
    fn set_layer_image(&self, layer_id: u64, image_id: u64, event_on_remove: u64) {
        let mut state = self.inner.lock().unwrap();

        // Increment the number of updates applied to to the display
        state.num_updates += 1;

        let previous = state.active_images_layers.insert(layer_id, (image_id, event_on_remove));

        // On next vsync, signal that the previous image has been removed
        if let Some((prev_image_id, prev_on_remove_event)) = previous {
            // Why are we being asked to re-present the same image onto the same layer?
            assert_ne!(prev_image_id, image_id);

            state.events_to_signal.push(prev_on_remove_event);
        }
    }

    // Send signals for removed images and get list of active images
    fn prepare_vsync(&self) -> Vec<u64> {
        let mut state = self.inner.lock().unwrap();

        // Get all the events that need signaling and signal them.
        // This informs scenic that we are no longer displaying images
        // that were replaced.
        let events_to_signal: Vec<u64> = state.events_to_signal.drain(..).collect();
        for event_id in events_to_signal {
            let event = state.events.get(&event_id).unwrap();
            event.as_handle_ref().signal(sys::Signals::NONE, sys::Signals::EVENT_SIGNALED).unwrap();
        }

        state.active_images_layers.values().map(|x| x.0.clone()).collect()
    }
}

/// Mock implementation of a display controller that is accessible
/// via the fuchsia.hardware.display.Provider FIDL API.
///
/// This controller emulates a 640x480 display that operates at
/// 60hz in the RGB-x888 pixel format.
///
/// This controller implements only the basic functions
/// (importing/applying images, creating layers, enabling vsync)
/// needed to drive the mock display.
///
/// All other functions are unreliable and should not be used.
///
/// TODO(xbhatnag): Replace this mock with a fake display provider
/// that connects to a well-implemented fake display over Banjo.
pub struct DisplayController {
    state: DisplayState,
}

impl DisplayController {
    pub fn new(state: DisplayState) -> Self {
        Self { state }
    }

    pub fn serve(self, controller: ServerEnd<fdisp::ControllerMarker>) {
        let mut stream = controller.into_stream().unwrap();

        self.create_display(stream.control_handle());

        // Start handling controller requests from scenic
        fasync::Task::spawn(async move {
            loop {
                let request = stream.try_next().await.unwrap().unwrap();

                // This happens too often. Logging it is not worthwhile
                if let fdisp::ControllerRequest::AcknowledgeVsync { .. } = request {
                    continue;
                }

                self.process_controller_request(request);
                debug!("################################");
            }
        })
        .detach();
    }

    // Creates a new thread upon which vsync events are sent indefinitely.
    // Because this is a 60hz display, the vsync events are sent every 16ms.
    fn start_vsync_thread(&self, control_handle: fdisp::ControllerControlHandle) {
        let state = self.state.clone();
        fasync::Task::blocking(async move {
            loop {
                let mut images = state.prepare_vsync();
                let timestamp: u64 = fasync::Time::now().into_nanos().try_into().unwrap();
                control_handle.send_on_vsync(0, timestamp, &mut images, 0).unwrap();
                std::thread::sleep(std::time::Duration::from_millis(16));
            }
        })
        .detach();
    }

    fn create_display(&self, handle: fdisp::ControllerControlHandle) {
        // Create a fake display and inform scenic about it
        let mode = fdisp::Mode {
            horizontal_resolution: 640,
            vertical_resolution: 480,
            refresh_rate_e2: 60 * 100,
            flags: 0,
        };

        let info = fdisp::Info {
            id: 0,
            modes: vec![mode],
            pixel_format: vec![0x00040005], // RGB-x888
            cursor_configs: vec![],
            manufacturer_name: "TEST".to_string(),
            monitor_name: "TEST_DISPLAY".to_string(),
            monitor_serial: "1234".to_string(),
            horizontal_size_mm: 100,
            vertical_size_mm: 75,
            using_fallback_size: false,
        };

        let mut added_displays = vec![info];
        let removed_displays = [];

        handle.send_on_displays_changed(&mut added_displays.iter_mut(), &removed_displays).unwrap();
        handle.send_on_client_ownership_change(true).unwrap();
    }

    fn process_controller_request(&self, request: fdisp::ControllerRequest) {
        match request {
            fdisp::ControllerRequest::ImportImage {
                image_config,
                collection_id,
                index,
                responder,
            } => {
                debug!("ImportImage");
                debug!("image_config -> {:?}", image_config);
                debug!("collection_id -> {:?}", collection_id);
                debug!("index -> {:?}", index);
                let image_id = self.state.increment_image_id();
                responder.send(0, image_id).unwrap();
            }
            fdisp::ControllerRequest::ImportEvent { event, id, control_handle: _ } => {
                debug!("ImportEvent");
                debug!("event -> {:?}", event);
                debug!("id -> {:?}", id);
                self.state.add_event(id, event);
            }
            fdisp::ControllerRequest::CreateLayer { responder } => {
                debug!("CreateLayer");
                let layer_id = self.state.increment_layer_id();
                responder.send(0, layer_id).unwrap();
            }
            fdisp::ControllerRequest::SetDisplayLayers {
                display_id,
                layer_ids,
                control_handle: _,
            } => {
                debug!("SetDisplayLayers");
                debug!("display_id -> {:?}", display_id);
                debug!("layer_ids -> {:?}", layer_ids);
            }
            fdisp::ControllerRequest::SetLayerPrimaryConfig {
                layer_id,
                image_config,
                control_handle: _,
            } => {
                debug!("SetLayerPrimaryConfig");
                debug!("layer_id -> {:?}", layer_id);
                debug!("image_config -> {:?}", image_config);
            }
            fdisp::ControllerRequest::SetLayerImage {
                layer_id,
                image_id,
                wait_event_id,
                signal_event_id,
                control_handle: _,
            } => {
                debug!("SetLayerImage");
                debug!("layer_id -> {:?}", layer_id);
                debug!("image_id -> {:?}", image_id);
                debug!("wait_event_id -> {:?}", wait_event_id);
                debug!("signal_event_id -> {:?}", signal_event_id);
                self.state.set_layer_image(layer_id, image_id, signal_event_id);
            }
            fdisp::ControllerRequest::ApplyConfig { control_handle: _ } => {
                debug!("ApplyConfig");
            }
            fdisp::ControllerRequest::EnableVsync { enable, control_handle } => {
                debug!("EnableVsync");
                debug!("enable -> {:?}", enable);
                self.start_vsync_thread(control_handle);
            }
            fdisp::ControllerRequest::AcknowledgeVsync { cookie, control_handle: _ } => {
                debug!("AcknowledgeVsync");
                debug!("cookie -> {:?}", cookie);
            }
            fdisp::ControllerRequest::ImportBufferCollection {
                collection_id,
                collection_token,
                responder,
            } => {
                debug!("ImportBufferCollection");
                debug!("collection_id -> {:?}", collection_id);

                // Close the buffer collection token immediately
                let proxy = collection_token.into_proxy().unwrap();
                proxy.close().unwrap();

                responder.send(0).unwrap();
            }
            fdisp::ControllerRequest::ReleaseBufferCollection {
                collection_id,
                control_handle: _,
            } => {
                debug!("ReleaseBufferCollection");
                debug!("collection_id -> {:?}", collection_id);
            }
            fdisp::ControllerRequest::SetBufferCollectionConstraints {
                collection_id,
                config,
                responder,
            } => {
                debug!("SetBufferCollectionConstraints");
                debug!("collection_id -> {:?}", collection_id);
                debug!("config -> {:?}", config);
                responder.send(0).unwrap();
            }
            _ => {
                panic!("Unexpected ControllerRequest received");
            }
        }
    }
}

pub struct DisplayControllerProviderInjector {
    display_state: DisplayState,
}

impl DisplayControllerProviderInjector {
    pub fn new(display_state: DisplayState) -> Self {
        Self { display_state }
    }
}

#[async_trait]
impl ProtocolInjector for DisplayControllerProviderInjector {
    type Marker = fdisp::ProviderMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: fdisp::ProviderRequestStream,
    ) -> Result<(), Error> {
        if let Ok(Some(fdisp::ProviderRequest::OpenController {
            device: _,
            controller,
            responder,
        })) = request_stream.try_next().await
        {
            debug!("Received request to open controller");
            responder.send(0).unwrap();
            DisplayController::new(self.display_state.clone()).serve(controller);
        }
        Ok(())
    }
}
