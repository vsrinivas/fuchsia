// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_device,
    crate::input_handler,
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_input_injection,
    fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    futures::channel::mpsc::{Receiver, Sender},
    futures::lock::Mutex,
    futures::{StreamExt, TryStreamExt},
    io_util::open_directory_in_namespace,
    std::collections::HashMap,
    std::path::PathBuf,
    std::sync::Arc,
};

type BoxedInputDeviceBinding = Box<dyn input_device::InputDeviceBinding>;

/// An [`InputDeviceBindingHashMap`] maps an input device to one or more InputDeviceBindings.
/// It expects filenames of the input devices seen in /dev/class/input-report (ex. "001") or
/// "injected_device" as keys.
pub type InputDeviceBindingHashMap = Arc<Mutex<HashMap<String, Vec<BoxedInputDeviceBinding>>>>;

/// An [`InputPipeline`] manages input devices and propagates input events through input handlers.
///
/// On creation, clients declare what types of input devices an [`InputPipeline`] manages. The
/// [`InputPipeline`] will continuously detect new input devices of supported type(s).
///
/// # Example
/// ```
/// let ime_handler =
///     ImeHandler::new(scene_manager.session.clone(), scene_manager.compositor_id).await?;
/// let touch_handler = TouchHandler::new(
///     scene_manager.session.clone(),
///     scene_manager.compositor_id,
///     scene_manager.display_size
/// ).await?;
///
/// let input_pipeline = InputPipeline::new(
///     vec![
///         input_device::InputDeviceType::Touch,
///         input_device::InputDeviceType::Keyboard,
///     ],
///     vec![Box::new(ime_handler), Box::new(touch_handler)],
/// );
/// input_pipeline.handle_input_events().await;
/// ```
pub struct InputPipeline {
    /// The input handlers that will dispatch InputEvents from the `device_bindings`.
    /// The order of handlers in `input_handlers` is the order
    input_handlers: Vec<Box<dyn input_handler::InputHandler>>,

    /// A clone of this sender is given to every InputDeviceBinding that this pipeline owns.
    /// Each InputDeviceBinding will send InputEvents to the pipeline through this channel.
    pub input_event_sender: Sender<input_device::InputEvent>,

    /// Receives InputEvents from all InputDeviceBindings that this pipeline owns.
    input_event_receiver: Receiver<input_device::InputEvent>,

    /// The types of devices this pipeline supports.
    pub input_device_types: Vec<input_device::InputDeviceType>,

    /// The InputDeviceBindings bound to this pipeline.
    pub input_device_bindings: InputDeviceBindingHashMap,
}

impl InputPipeline {
    /// Creates a new [`InputPipeline`].
    ///
    /// # Parameters
    /// - `device_types`: The types of devices the new [`InputPipeline`] will support.
    /// - `input_handlers`: The input handlers that the [`InputPipeline`] sends InputEvents to.
    ///                     Handlers process InputEvents in the order that they appear in
    ///                     `input_handlers`.
    pub async fn new(
        device_types: Vec<input_device::InputDeviceType>,
        input_handlers: Vec<Box<dyn input_handler::InputHandler>>,
    ) -> Result<Self, Error> {
        let (input_event_sender, input_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);

        let input_pipeline = InputPipeline {
            input_handlers,
            input_event_sender,
            input_event_receiver,
            input_device_types: device_types,
            input_device_bindings: Arc::new(Mutex::new(HashMap::new())),
        };

        // Watches the input device directory for new input devices. Creates new InputDeviceBindings
        // that send InputEvents to `input_event_receiver`.
        let device_watcher = Self::get_device_watcher().await?;
        let dir_proxy =
            open_directory_in_namespace(input_device::INPUT_REPORT_PATH, OPEN_RIGHT_READABLE)?;

        let bindings: InputDeviceBindingHashMap = Arc::new(Mutex::new(HashMap::new()));

        let device_types = input_pipeline.input_device_types.clone();
        let input_event_sender = input_pipeline.input_event_sender.clone();
        let device_bindings = bindings.clone();
        fasync::Task::spawn(async move {
            let _ = Self::watch_for_devices(
                device_watcher,
                dir_proxy,
                device_types,
                input_event_sender,
                device_bindings,
                false, /* break_on_idle */
            )
            .await;
        })
        .detach();

        Ok(input_pipeline)
    }

    /// Sends all InputEvents from `input_event_receiver` to all `input_handlers`.
    pub async fn handle_input_events(mut self) {
        while let Some(input_event) = self.input_event_receiver.next().await {
            let mut result_events: Vec<input_device::InputEvent> = vec![input_event];
            // Pass the InputEvent through all InputHandlers
            for input_handler in &mut self.input_handlers {
                // The outputted events from one InputHandler serves as the input
                // events for the next InputHandler.
                let mut next_result_events: Vec<input_device::InputEvent> = vec![];
                for event in result_events {
                    next_result_events.append(&mut input_handler.handle_input_event(event).await);
                }
                result_events = next_result_events;
            }
        }

        fx_log_err!("Input pipeline stopped handling input events.");
    }

    /// Returns a [`fuchsia_vfs_watcher::Watcher`] to the input report directory.
    ///
    /// # Errors
    /// If the input report directory cannot be read.
    async fn get_device_watcher() -> Result<Watcher, Error> {
        let input_report_dir_proxy = open_directory_in_namespace(
            input_device::INPUT_REPORT_PATH,
            io_util::OPEN_RIGHT_READABLE,
        )?;
        Watcher::new(input_report_dir_proxy).await
    }

    /// Watches the input report directory for new input devices. Creates InputDeviceBindings
    /// if new devices match a type in `device_types`.
    ///
    /// # Parameters
    /// - `device_watcher`: Watches the input report directory for new devices.
    /// - `dir_proxy`: The directory containing InputDevice connections.
    /// - `device_types`: The types of devices to watch for.
    /// - `input_event_sender`: The channel new InputDeviceBindings will send InputEvents to.
    /// - `bindings`: Holds all the InputDeviceBindings
    /// - `break_on_idle`: If true, stops watching for devices once all existing devices are handled.
    ///
    /// # Errors
    /// If the input report directory or a file within it cannot be read.
    async fn watch_for_devices(
        mut device_watcher: Watcher,
        dir_proxy: fidl_fuchsia_io::DirectoryProxy,
        device_types: Vec<input_device::InputDeviceType>,
        input_event_sender: Sender<input_device::InputEvent>,
        bindings: InputDeviceBindingHashMap,
        break_on_idle: bool,
    ) -> Result<(), Error> {
        while let Some(msg) = device_watcher.try_next().await? {
            if let Ok(filename) = msg.filename.into_os_string().into_string() {
                if filename == "." {
                    continue;
                }

                let pathbuf = PathBuf::from(filename.clone());
                match msg.event {
                    WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                        let device_proxy =
                            input_device::get_device_from_dir_entry_path(&dir_proxy, &pathbuf)?;
                        add_device_bindings(
                            &device_types,
                            device_proxy,
                            &input_event_sender,
                            &bindings,
                            filename,
                        )
                        .await;
                    }
                    WatchEvent::IDLE => {
                        if break_on_idle {
                            break;
                        }
                    }
                    _ => (),
                }
            }
        }

        Err(format_err!("Input pipeline stopped watching for new input devices."))
    }

    /// Handles the incoming InputDeviceRegistryRequestStream.
    ///
    /// This method will end when the request stream is closed. If the stream closes with an
    /// error the error will be returned in the Result.
    ///
    /// # Parameters
    /// - `stream`: The stream of InputDeviceRegistryRequests.
    /// - `device_types`: The types of devices to watch for.
    /// - `input_event_sender`: The channel new InputDeviceBindings will send InputEvents to.
    /// - `bindings`: Holds all the InputDeviceBindings associated with the InputPipeline.
    pub async fn handle_input_device_registry_request_stream(
        mut stream: fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
        device_types: &Vec<input_device::InputDeviceType>,
        input_event_sender: &Sender<input_device::InputEvent>,
        bindings: &InputDeviceBindingHashMap,
    ) -> Result<(), Error> {
        while let Some(request) = stream
            .try_next()
            .await
            .context("Error handling input device registry request stream")?
        {
            match request {
                fidl_fuchsia_input_injection::InputDeviceRegistryRequest::Register {
                    device,
                    ..
                } => {
                    // Add a binding if the device is a type being tracked
                    let device_proxy = device.into_proxy().expect("Error getting device proxy.");

                    add_device_bindings(
                        device_types,
                        device_proxy,
                        input_event_sender,
                        bindings,
                        "injected_device".to_string(),
                    )
                    .await;
                }
            }
        }

        Ok(())
    }
}

/// Adds InputDeviceBindings for devices of tracked `device_types` to `bindings`.
///
/// # Parameters
/// - `device_types`: The types of devices to watch for.
/// - `device_proxy`: A proxy to the input device.
/// - `input_event_sender`: The channel new InputDeviceBindings will send InputEvents to.
/// - `bindings`: Holds all the InputDeviceBindings associated with the InputPipeline.
/// - `device_name`: The device name of the associated bindings.
async fn add_device_bindings(
    device_types: &Vec<input_device::InputDeviceType>,
    device_proxy: fidl_fuchsia_input_report::InputDeviceProxy,
    input_event_sender: &Sender<input_device::InputEvent>,
    bindings: &InputDeviceBindingHashMap,
    device_name: String,
) {
    let mut new_bindings: Vec<BoxedInputDeviceBinding> = vec![];

    for device_type in device_types {
        let proxy = device_proxy.clone();
        if input_device::is_device_type(&proxy, *device_type).await {
            if let Ok(binding) =
                input_device::get_device_binding(*device_type, proxy, input_event_sender.clone())
                    .await
            {
                new_bindings.push(binding);
            }
        }
    }

    if !new_bindings.is_empty() {
        let mut bindings = bindings.lock().await;
        bindings.entry(device_name).or_insert(Vec::new()).extend(new_bindings);
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
        crate::utils::Position,
        fidl::endpoints::{create_proxy, create_proxy_and_stream, create_request_stream},
        fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
        fidl_fuchsia_ui_input as fidl_ui_input, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::channel::mpsc::Sender,
        futures::FutureExt,
        rand::Rng,
        std::collections::HashSet,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
            pseudo_directory, service as pseudo_fs_service,
        },
    };

    /// Returns the InputEvent sent over `sender`.
    ///
    /// # Parameters
    /// - `sender`: The channel to send the InputEvent over.
    fn send_input_event(mut sender: Sender<input_device::InputEvent>) -> input_device::InputEvent {
        let mut rng = rand::thread_rng();
        let offset = Position { x: rng.gen_range(0, 10) as f32, y: rng.gen_range(0, 10) as f32 };
        let input_event = input_device::InputEvent {
            device_event: input_device::InputDeviceEvent::Mouse(mouse::MouseEvent::new(
                mouse::MouseLocation::Relative(offset),
                fidl_ui_input::PointerEventPhase::Move,
                HashSet::new(),
            )),
            device_descriptor: input_device::InputDeviceDescriptor::Mouse(
                mouse::MouseDeviceDescriptor {
                    device_id: 1,
                    absolute_x_range: None,
                    absolute_y_range: None,
                },
            ),
            event_time: zx::Time::get_monotonic().into_nanos() as input_device::EventTime,
        };
        match sender.try_send(input_event.clone()) {
            Err(_) => assert!(false),
            _ => {}
        }

        input_event
    }

    /// Returns a KeyboardDescriptor on an InputDeviceRequest.
    ///
    /// # Parameters
    /// - `input_device_request`: The request to handle.
    fn handle_input_device_reqeust(
        input_device_request: fidl_fuchsia_input_report::InputDeviceRequest,
    ) {
        match input_device_request {
            fidl_fuchsia_input_report::InputDeviceRequest::GetDescriptor { responder } => {
                let _ = responder.send(fidl_fuchsia_input_report::DeviceDescriptor {
                    device_info: None,
                    mouse: None,
                    sensor: None,
                    touch: None,
                    keyboard: Some(fidl_fuchsia_input_report::KeyboardDescriptor {
                        input: Some(fidl_fuchsia_input_report::KeyboardInputDescriptor {
                            keys: None,
                            keys3: None,
                        }),
                        output: None,
                    }),
                    consumer_control: None,
                });
            }
            _ => {}
        }
    }

    /// Tests that an input pipeline handles events from multiple devices.
    #[fasync::run_singlethreaded(test)]
    async fn multiple_devices_single_handler() {
        // Create two fake device bindings.
        let (input_event_sender, input_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let first_device_binding =
            fake_input_device_binding::FakeInputDeviceBinding::new(input_event_sender.clone());
        let second_device_binding =
            fake_input_device_binding::FakeInputDeviceBinding::new(input_event_sender.clone());

        // Create a fake input handler.
        let (handler_event_sender, mut handler_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let input_handler = fake_input_handler::FakeInputHandler::new(handler_event_sender);

        // Build the input pipeline.
        let input_pipeline = InputPipeline {
            input_handlers: vec![Box::new(input_handler)],
            input_event_sender,
            input_event_receiver,
            input_device_types: vec![],
            input_device_bindings: Arc::new(Mutex::new(HashMap::new())),
        };

        // Send an input event from each device.
        let first_device_event = send_input_event(first_device_binding.input_event_sender());
        let second_device_event = send_input_event(second_device_binding.input_event_sender());

        // Run the pipeline.
        fasync::Task::spawn(async {
            input_pipeline.handle_input_events().await;
        })
        .detach();

        // Assert the handler receives the events.
        let first_handled_event = handler_event_receiver.next().await;
        assert_eq!(first_handled_event, Some(first_device_event));

        let second_handled_event = handler_event_receiver.next().await;
        assert_eq!(second_handled_event, Some(second_device_event));
    }

    /// Tests that an input pipeline handles events through multiple input handlers.
    #[fasync::run_singlethreaded(test)]
    async fn single_device_multiple_handlers() {
        // Create two fake device bindings.
        let (input_event_sender, input_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let input_device_binding =
            fake_input_device_binding::FakeInputDeviceBinding::new(input_event_sender.clone());

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
        let input_pipeline = InputPipeline {
            input_handlers: vec![Box::new(first_input_handler), Box::new(second_input_handler)],
            input_event_sender,
            input_event_receiver,
            input_device_types: vec![],
            input_device_bindings: Arc::new(Mutex::new(HashMap::new())),
        };

        // Send an input event.
        let input_event = send_input_event(input_device_binding.input_event_sender());

        // Run the pipeline.
        fasync::Task::spawn(async {
            input_pipeline.handle_input_events().await;
        })
        .detach();

        // Assert both handlers receive the event.
        let first_handler_event = first_handler_event_receiver.next().await;
        assert_eq!(first_handler_event, Some(input_event.clone()));
        let second_handler_event = second_handler_event_receiver.next().await;
        assert_eq!(second_handler_event, Some(input_event));
    }

    /// Tests that a single keyboard device binding is created for the one input device in the
    /// input report directory.
    #[fasync::run_singlethreaded(test)]
    async fn watch_devices_one_match_exists() {
        // Create a file in a pseudo directory that represents an input device.
        let mut count: i8 = 0;
        let dir = pseudo_directory! {
            "001" => pseudo_fs_service::host(
                move |mut request_stream: fidl_fuchsia_input_report::InputDeviceRequestStream| {
                    async move {
                        while count < 3 {
                            if let Some(input_device_request) =
                                request_stream.try_next().await.unwrap()
                            {
                                handle_input_device_reqeust(input_device_request);
                                count += 1;
                            }
                        }

                    }.boxed()
                },
            )
        };

        // Create a Watcher on the pseudo directory.
        let pseudo_dir_clone = dir.clone();
        let (dir_proxy_for_watcher, dir_server_for_watcher) =
            create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
        let server_end_for_watcher = dir_server_for_watcher.into_channel().into();
        let scope_for_watcher = ExecutionScope::new();
        dir.open(
            scope_for_watcher,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            Path::empty(),
            server_end_for_watcher,
        );
        let device_watcher = Watcher::new(dir_proxy_for_watcher).await.unwrap();

        // Get a proxy to the pseudo directory for the input pipeline. The input pipeline uses this
        // proxy to get connections to input devices.
        let (dir_proxy_for_pipeline, dir_server_for_pipeline) =
            create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
        let server_end_for_pipeline = dir_server_for_pipeline.into_channel().into();
        let scope_for_pipeline = ExecutionScope::new();
        pseudo_dir_clone.open(
            scope_for_pipeline,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            Path::empty(),
            server_end_for_pipeline,
        );

        let (input_event_sender, _input_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let bindings: InputDeviceBindingHashMap = Arc::new(Mutex::new(HashMap::new()));

        let _ = InputPipeline::watch_for_devices(
            device_watcher,
            dir_proxy_for_pipeline,
            vec![input_device::InputDeviceType::Keyboard],
            input_event_sender,
            bindings.clone(),
            true, /* break_on_idle */
        )
        .await;

        // Assert that one device was found.
        let bindings = bindings.lock().await;
        assert_eq!(bindings.len(), 1);
    }

    /// Tests that no device bindings are created because the input pipeline looks for mouse devices
    /// but only a keyboard exists.
    #[fasync::run_singlethreaded(test)]
    async fn watch_devices_no_matches_exist() {
        // Create a file in a pseudo directory that represents an input device.
        let mut count: i8 = 0;
        let dir = pseudo_directory! {
            "001" => pseudo_fs_service::host(
                move |mut request_stream: fidl_fuchsia_input_report::InputDeviceRequestStream| {
                    async move {
                        while count < 1 {
                            if let Some(input_device_request) =
                                request_stream.try_next().await.unwrap()
                            {
                                handle_input_device_reqeust(input_device_request);
                                count += 1;
                            }
                        }

                    }.boxed()
                },
            )
        };

        // Create a Watcher on the pseudo directory.
        let pseudo_dir_clone = dir.clone();
        let (dir_proxy_for_watcher, dir_server_for_watcher) =
            create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
        let server_end_for_watcher = dir_server_for_watcher.into_channel().into();
        let scope_for_watcher = ExecutionScope::new();
        dir.open(
            scope_for_watcher,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            Path::empty(),
            server_end_for_watcher,
        );
        let device_watcher = Watcher::new(dir_proxy_for_watcher).await.unwrap();

        // Get a proxy to the pseudo directory for the input pipeline. The input pipeline uses this
        // proxy to get connections to input devices.
        let (dir_proxy_for_pipeline, dir_server_for_pipeline) =
            create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
        let server_end_for_pipeline = dir_server_for_pipeline.into_channel().into();
        let scope_for_pipeline = ExecutionScope::new();
        pseudo_dir_clone.open(
            scope_for_pipeline,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            Path::empty(),
            server_end_for_pipeline,
        );

        let (input_event_sender, _input_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let bindings: InputDeviceBindingHashMap = Arc::new(Mutex::new(HashMap::new()));

        let _ = InputPipeline::watch_for_devices(
            device_watcher,
            dir_proxy_for_pipeline,
            vec![input_device::InputDeviceType::Mouse],
            input_event_sender,
            bindings.clone(),
            true, /* break_on_idle */
        )
        .await;

        // Assert that no devices were found.
        let bindings = bindings.lock().await;
        assert_eq!(bindings.len(), 0);
    }

    /// Tests that a single keyboard device binding is created for the input device registered
    /// through InputDeviceRegistry.
    #[fasync::run_singlethreaded(test)]
    async fn handle_input_device_registry_request_stream() {
        let (input_device_registry_proxy, input_device_registry_request_stream) =
            create_proxy_and_stream::<fidl_fuchsia_input_injection::InputDeviceRegistryMarker>()
                .unwrap();
        let (input_device_client_end, mut input_device_request_stream) =
            create_request_stream::<fidl_fuchsia_input_report::InputDeviceMarker>().unwrap();

        let device_types = vec![input_device::InputDeviceType::Keyboard];
        let (input_event_sender, _input_event_receiver) =
            futures::channel::mpsc::channel(input_device::INPUT_EVENT_BUFFER_SIZE);
        let bindings: InputDeviceBindingHashMap = Arc::new(Mutex::new(HashMap::new()));

        // Handle input device requests.
        let mut count: i8 = 0;
        fasync::Task::spawn(async move {
            // Register a device.
            let _ = input_device_registry_proxy.register(input_device_client_end);

            while count < 3 {
                if let Some(input_device_request) =
                    input_device_request_stream.try_next().await.unwrap()
                {
                    handle_input_device_reqeust(input_device_request);
                    count += 1;
                }
            }

            // End handle_input_device_registry_request_stream() by taking the event stream.
            input_device_registry_proxy.take_event_stream();
        })
        .detach();

        // Start listening for InputDeviceRegistryRequests.
        let bindings_clone = bindings.clone();
        let _ = InputPipeline::handle_input_device_registry_request_stream(
            input_device_registry_request_stream,
            &device_types,
            &input_event_sender,
            &bindings_clone,
        )
        .await;

        // Assert that a device was registered.
        let bindings = bindings.lock().await;
        assert_eq!(bindings.len(), 1);
    }
}
