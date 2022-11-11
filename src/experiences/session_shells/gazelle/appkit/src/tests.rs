// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    collections::HashMap,
    fs,
    io::{BufWriter, Cursor},
    sync::{Arc, Mutex},
};

use anyhow::Error;
use fidl::endpoints::{
    create_proxy, create_proxy_and_stream, create_request_stream, DiscoverableProtocolMarker,
};
use fidl_fuchsia_element as felement;
use fidl_fuchsia_input::Key;
use fidl_fuchsia_sysmem as sysmem;
use fidl_fuchsia_ui_app as ui_app;
use fidl_fuchsia_ui_composition as ui_comp;
use fidl_fuchsia_ui_input3 as ui_input3;
use fidl_fuchsia_ui_input3::{KeyEvent, KeyEventStatus, KeyEventType, KeyMeaning, NonPrintableKey};
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use fidl_fuchsia_ui_test_input as ui_test_input;
use fidl_fuchsia_ui_test_scene as ui_test_scene;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route};
use fuchsia_scenic::flatland::ViewCreationTokenPair;
use fuchsia_zircon as zx;
use futures::{
    future::{AbortHandle, Abortable},
    StreamExt, TryStreamExt,
};
use mapped_vmo::Mapping;
use png::HasParameters;
use pointer_fusion::*;
use tracing::*;

use crate::{
    child_view::{ChildView, ChildViewId},
    event::{ChildViewEvent, Event, EventSender, SystemEvent, ViewSpecHolder, WindowEvent},
    image::{load_image_from_bytes_using_allocators, load_png},
    utils::ProtocolConnector,
    window::{Window, WindowId},
};

#[derive(Debug)]
enum TestEvent {}

struct TestApp<T> {
    width: u32,
    height: u32,
    active_window: Option<WindowId>,
    windows: HashMap<WindowId, Window<T>>,
    child_views: HashMap<ChildViewId, ChildView<T>>,
}

impl<T> TestApp<T> {
    pub fn new() -> Self {
        TestApp {
            width: 0,
            height: 0,
            active_window: None,
            windows: HashMap::new(),
            child_views: HashMap::new(),
        }
    }
}

#[fuchsia::test]
async fn test_appkit() -> Result<(), Error> {
    let realm = build_realm().await?;
    let test_protocol_connector = TestProtocolConnector::new(realm);

    let (event_sender, mut receiver) = EventSender::<TestEvent>::new();

    let (graphical_presenter_proxy, graphical_presenter_request_stream) =
        create_proxy_and_stream::<felement::GraphicalPresenterMarker>()?;
    let scene_controller = test_protocol_connector.connect_to_test_scene_controller()?;

    let (services_abort, services_registration) = AbortHandle::new_pair();
    let services_fut = Abortable::new(
        start_services(event_sender.clone(), scene_controller, graphical_presenter_request_stream),
        services_registration,
    );

    let mut app = TestApp::new();
    // Declare an event handler that does not allow blocking async calls within it.
    let mut event_handler = |event| {
        let test_protocol_connector = test_protocol_connector.clone();
        info!("------ParentView {:?}", event);
        match event {
            Event::Init => {}
            Event::WindowEvent { window_id: id, event: window_event } => match window_event {
                WindowEvent::Resized { width, height, .. } => {
                    app.width = width;
                    app.height = height;
                    if app.active_window.is_none() {
                        app.active_window = Some(id);

                        let cloned_graphical_presenter = graphical_presenter_proxy.clone();
                        fasync::Task::spawn(async move {
                            create_child_view_spec(
                                cloned_graphical_presenter,
                                test_protocol_connector,
                            )
                            .await
                            .expect("Failed to create_child_view");
                        })
                        .detach();
                    }
                }
                WindowEvent::NeedsRedraw { .. } => {
                    assert_eq!(
                        app.width > 0 && app.height > 0,
                        true,
                        "Redraw event received before window was resized"
                    );
                }
                _ => {}
            },
            Event::SystemEvent { event: system_event } => match system_event {
                SystemEvent::ViewCreationToken { token: view_creation_token } => {
                    let mut window = Window::new(event_sender.clone())
                        .with_view_creation_token(view_creation_token)
                        .with_protocol_connector(test_protocol_connector.box_clone());
                    window.create_view().expect("Failed to create view for window");
                    app.windows.insert(window.id(), window);
                }
                SystemEvent::PresentViewSpec { view_spec_holder } => {
                    let window = app.windows.get_mut(&app.active_window.unwrap()).unwrap();
                    let child_view = window
                        .create_child_view(
                            view_spec_holder,
                            app.width,
                            app.height,
                            event_sender.clone(),
                        )
                        .expect("Failed to create child view");
                    app.child_views.insert(child_view.id(), child_view);
                }
            },
            Event::ChildViewEvent { child_view_id, window_id, event: child_view_event } => {
                let window = app.windows.get_mut(&window_id).unwrap();
                let child_view = app.child_views.get_mut(&child_view_id).unwrap();

                match child_view_event {
                    ChildViewEvent::Available => {
                        window.set_content(
                            window.get_root_transform_id(),
                            child_view.get_content_id(),
                        );
                        window.redraw();
                    }
                    ChildViewEvent::Attached { view_ref } => {
                        // Set focus to child view.
                        window.request_focus(view_ref);
                    }
                    ChildViewEvent::Detached | ChildViewEvent::Dismissed => {
                        window.set_content(
                            window.get_root_transform_id(),
                            ui_comp::ContentId { value: 0 },
                        );
                        window.redraw();
                        event_sender.send(Event::Exit).expect("Failed to send Event::Exit event");
                    }
                }
            }
            Event::Exit => {
                app.child_views.clear();
                app.windows.clear();
                services_abort.abort();
            }
            _ => {}
        }
    };
    let loop_fut = async move {
        while let Some(event) = receiver.next().await {
            let should_exit = matches!(event, Event::Exit);
            event_handler(event);
            if should_exit {
                break;
            }
        }
    };
    let _ = futures::join!(loop_fut, services_fut);
    test_protocol_connector.release().await?;
    Ok(())
}

async fn start_services(
    event_sender: EventSender<TestEvent>,
    scene_controller: ui_test_scene::ControllerProxy,
    graphical_presenter_request_stream: felement::GraphicalPresenterRequestStream,
) {
    let view_provider_fut = start_view_provider(event_sender.clone(), scene_controller);
    let graphical_presenter_fut =
        start_graphical_presenter(event_sender.clone(), graphical_presenter_request_stream);

    futures::join!(view_provider_fut, graphical_presenter_fut);
}

async fn start_view_provider(
    event_sender: EventSender<TestEvent>,
    scene_controller: ui_test_scene::ControllerProxy,
) {
    let (view_provider, mut view_provider_request_stream) =
        create_request_stream::<ui_app::ViewProviderMarker>()
            .expect("failed to create ViewProvider request stream");

    let scene_controller_fut = async move {
        let _view_ref_koid = scene_controller
            .attach_client_view(ui_test_scene::ControllerAttachClientViewRequest {
                view_provider: Some(view_provider),
                ..ui_test_scene::ControllerAttachClientViewRequest::EMPTY
            })
            .await
            .expect("failed to attach root client view");
    };

    let view_provider_fut = async move {
        match view_provider_request_stream
            .next()
            .await
            .expect("Failed to read ViewProvider request stream")
        {
            Ok(ui_app::ViewProviderRequest::CreateView2 { args, .. }) => {
                event_sender
                    .send(Event::SystemEvent {
                        event: SystemEvent::ViewCreationToken {
                            token: args.view_creation_token.unwrap(),
                        },
                    })
                    .expect("Failed to send SystemEvent::ViewCreationToken event");
            }
            // Panic for all other CreateView requests and errors to fail the test.
            _ => panic!("ViewProvider impl only handles CreateView2()"),
        }
    };

    futures::join!(scene_controller_fut, view_provider_fut);
}

async fn start_graphical_presenter(
    event_sender: EventSender<TestEvent>,
    mut request_stream: felement::GraphicalPresenterRequestStream,
) {
    while let Some(request) = request_stream
        .try_next()
        .await
        .expect("Failed to obtain next GraphicalPresenter request from stream")
    {
        match request {
            felement::GraphicalPresenterRequest::PresentView {
                view_spec,
                annotation_controller,
                view_controller_request,
                responder,
            } => {
                event_sender
                    .send(Event::SystemEvent {
                        event: SystemEvent::PresentViewSpec {
                            view_spec_holder: ViewSpecHolder {
                                view_spec,
                                annotation_controller,
                                view_controller_request,
                                responder: Some(responder),
                            },
                        },
                    })
                    .expect("Failed to send SystemEvent::PresentViewSpec event");
            }
        }
    }
}

async fn create_child_view_spec(
    graphical_presenter: felement::GraphicalPresenterProxy,
    protocol_connector: TestProtocolConnector,
) -> Result<(), Error> {
    let ViewCreationTokenPair { view_creation_token, viewport_creation_token } =
        ViewCreationTokenPair::new()?;
    let view_spec = felement::ViewSpec {
        viewport_creation_token: Some(viewport_creation_token),
        ..felement::ViewSpec::EMPTY
    };
    let (view_controller_proxy, view_controller_request) =
        create_proxy::<felement::ViewControllerMarker>()?;
    let _ = graphical_presenter.present_view(view_spec, None, Some(view_controller_request)).await;

    let (keyboard, keyboard_server) = create_proxy::<ui_test_input::KeyboardMarker>()?;
    let input_registry = protocol_connector.connect_to_test_input_registry()?;
    let screenshot = protocol_connector.connect_to_flatland_screenshot()?;

    input_registry
        .register_keyboard(ui_test_input::RegistryRegisterKeyboardRequest {
            device: Some(keyboard_server),
            ..ui_test_input::RegistryRegisterKeyboardRequest::EMPTY
        })
        .await?;

    let (mouse, mouse_server) = create_proxy::<ui_test_input::MouseMarker>()?;
    input_registry
        .register_mouse(ui_test_input::RegistryRegisterMouseRequest {
            device: Some(mouse_server),
            ..ui_test_input::RegistryRegisterMouseRequest::EMPTY
        })
        .await?;

    // Load an image as content for the child view.
    static IMAGE_DATA: &'static [u8] = include_bytes!("../test_data/checkerboard_100.png");
    let (bytes, width, height) = load_png(Cursor::new(IMAGE_DATA))?;

    // The checkerboard image should only have two colors (black and white) in equal amount.
    let histogram = build_histogram(&bytes);
    let mut iter = histogram.values();
    assert_eq!(iter.len(), 2);
    assert_eq!(iter.next(), iter.next());

    // Now load the image into shared memory buffer.
    let sysmem_allocator = protocol_connector.connect_to_sysmem_allocator()?;
    let flatland_allocator = protocol_connector.connect_to_flatland_allocator()?;
    let mut image_data = load_image_from_bytes_using_allocators(
        &bytes,
        width,
        height,
        sysmem_allocator,
        flatland_allocator,
    )
    .await?;

    fasync::Task::local(async move {
        let (sender, mut receiver) = futures::channel::mpsc::unbounded::<Event<TestEvent>>();
        let event_sender = EventSender::<TestEvent>(sender);
        event_sender.send(Event::Init).expect("Failed to send Event::Init event");

        let mut window_holder: Option<Window<TestEvent>> = None;
        let mut view_creation_token = Some(view_creation_token);
        while let Some(event) = receiver.next().await {
            info!("------ChildView  {:?}", event);
            match event {
                Event::Init => {
                    let mut window = Window::new(event_sender.clone())
                        .with_view_creation_token(view_creation_token.take().unwrap())
                        .with_protocol_connector(protocol_connector.box_clone());
                    window.create_view().expect("Failed to create window for child view");
                    window.register_shortcuts(vec![create_shortcut(
                        1,
                        vec![KeyMeaning::NonPrintableKey(NonPrintableKey::Tab)],
                    )]);
                    window_holder = Some(window);
                }
                Event::WindowEvent { event: window_event, .. } => match window_event {
                    WindowEvent::Resized { width, height, .. } => {
                        if let Some(window) = window_holder.as_mut() {
                            let image = window
                                .create_image(&mut image_data)
                                .expect("Failed to create image content");
                            image.set_size(width, height).expect("Failed to set image size");
                            window.set_content(
                                window.get_root_transform_id(),
                                image.get_content_id(),
                            );
                            window.redraw();
                        }
                    }
                    WindowEvent::Focused { focused } => {
                        if focused {
                            tap(mouse.clone());
                        }
                    }
                    WindowEvent::Pointer { event: PointerEvent { phase: Phase::Up, .. } } => {
                        // Inject tab key to invoke keyboard shortcut.
                        inject_text("\t".to_string(), keyboard.clone());
                    }
                    WindowEvent::Shortcut { id, responder } => {
                        assert!(id == 1);
                        responder
                            .send(ui_shortcut2::Handled::Handled)
                            .expect("Failed to respond to keyboard shortcut");

                        // Inject 'q' to quit.
                        inject_text("q".to_string(), keyboard.clone());
                    }
                    WindowEvent::Keyboard { event, responder } => {
                        // Take a screenshot before quitting to verify child view image content.
                        let histogram = take_screenshot(screenshot.clone())
                            .await
                            .expect("Failed to take screenshot");
                        info!("-----------histogram: {:?}", histogram);

                        // The child_view should render the checkerboard fullscreen and should
                        // only have at least two colors (black and white) in equal amounts.
                        assert!(histogram.values().len() >= 2);

                        if let KeyEvent {
                            key: Some(Key::Q),
                            type_: Some(KeyEventType::Released),
                            ..
                        } = event
                        {
                            // Dismiss the view, allowing the parent to drop it.
                            view_controller_proxy.dismiss().expect("Failed to dismiss child view");
                            responder
                                .send(KeyEventStatus::Handled)
                                .expect("Failed to respond to keyboard event");
                        } else {
                            responder
                                .send(KeyEventStatus::NotHandled)
                                .expect("Failed to respond to keyboard event");
                        }
                    }
                    _ => {}
                },
                _ => {}
            }
        }
    })
    .detach();

    Ok(())
}

fn inject_text(text: String, keyboard: ui_test_input::KeyboardProxy) {
    fasync::Task::local(async move {
        keyboard
            .simulate_us_ascii_text_entry(ui_test_input::KeyboardSimulateUsAsciiTextEntryRequest {
                text: Some(text),
                ..ui_test_input::KeyboardSimulateUsAsciiTextEntryRequest::EMPTY
            })
            .await
            .expect("Failed to inject text using fuchsia.ui.test.input.Keyboard");
    })
    .detach();
}

fn tap(mouse: ui_test_input::MouseProxy) {
    fasync::Task::local(async move {
        mouse
            .simulate_mouse_event(ui_test_input::MouseSimulateMouseEventRequest {
                pressed_buttons: Some(vec![ui_test_input::MouseButton::First]),
                ..ui_test_input::MouseSimulateMouseEventRequest::EMPTY
            })
            .await
            .expect("Failed to tap using fuchsia.ui.test.input.Mouse");
        mouse
            .simulate_mouse_event(ui_test_input::MouseSimulateMouseEventRequest {
                ..ui_test_input::MouseSimulateMouseEventRequest::EMPTY
            })
            .await
            .expect("Failed to tap using fuchsia.ui.test.input.Mouse");
    })
    .detach();
}

fn create_shortcut(id: u32, keys: Vec<KeyMeaning>) -> ui_shortcut2::Shortcut {
    ui_shortcut2::Shortcut {
        id,
        key_meanings: keys,
        options: ui_shortcut2::Options { ..ui_shortcut2::Options::EMPTY },
    }
}

type Histogram = HashMap<u32, u32>;

const SCREENSHOT_FILE: &'static str = "/custom_artifacts/screenshot.png";

async fn take_screenshot(screenshot: ui_comp::ScreenshotProxy) -> Result<Histogram, Error> {
    let data = screenshot
        .take(ui_comp::ScreenshotTakeRequest {
            format: Some(ui_comp::ScreenshotFormat::BgraRaw),
            ..ui_comp::ScreenshotTakeRequest::EMPTY
        })
        .await?;
    let vmo = data.vmo.unwrap();
    let size = vmo.get_size().unwrap() as usize;
    let mapping = Mapping::create_from_vmo(&vmo, size, zx::VmarFlags::PERM_READ)?;
    let mut image_data = vec![0u8; size];
    assert_eq!(mapping.read(&mut image_data), size);

    // Write the screenshot to file.
    let screenshot_file = fs::File::create(SCREENSHOT_FILE)
        .expect(&format!("cannot create file {}", SCREENSHOT_FILE));
    let ref mut w = BufWriter::new(screenshot_file);

    let image_size = data.size.expect("no data size returned from screenshot");
    let mut encoder = png::Encoder::new(w, image_size.width, image_size.height);
    encoder.set(png::BitDepth::Eight);
    encoder.set(png::ColorType::RGBA);
    let mut writer = encoder.write_header().unwrap();

    writer.write_image_data(&image_data).expect("failed to write image data as PNG");

    Ok(build_histogram(&image_data))
}

fn build_histogram(data: &[u8]) -> Histogram {
    use std::convert::TryInto;
    let mut histogram = HashMap::<u32, u32>::new();

    for chunk in data.chunks_exact(std::mem::size_of::<u32>()) {
        let pixel = u32::from_be_bytes(chunk.try_into().unwrap());
        let count = histogram.get(&pixel).unwrap_or(&0);
        histogram.insert(pixel, count + 1);
    }

    histogram
}

#[derive(Clone)]
pub struct TestProtocolConnector(Arc<Mutex<Option<RealmInstance>>>);

impl ProtocolConnector for TestProtocolConnector {
    fn connect_to_flatland(&self) -> Result<ui_comp::FlatlandProxy, Error> {
        self.connect_to_protocol::<ui_comp::FlatlandMarker>()
    }

    fn connect_to_graphical_presenter(&self) -> Result<felement::GraphicalPresenterProxy, Error> {
        self.connect_to_protocol::<felement::GraphicalPresenterMarker>()
    }

    fn connect_to_shortcuts_registry(&self) -> Result<ui_shortcut2::RegistryProxy, Error> {
        self.connect_to_protocol::<ui_shortcut2::RegistryMarker>()
    }

    fn connect_to_keyboard(&self) -> Result<ui_input3::KeyboardProxy, Error> {
        self.connect_to_protocol::<ui_input3::KeyboardMarker>()
    }

    fn connect_to_sysmem_allocator(&self) -> Result<sysmem::AllocatorProxy, Error> {
        connect_to_protocol::<sysmem::AllocatorMarker>()
    }

    fn connect_to_flatland_allocator(&self) -> Result<ui_comp::AllocatorProxy, Error> {
        self.connect_to_protocol::<ui_comp::AllocatorMarker>()
    }

    fn box_clone(&self) -> Box<dyn ProtocolConnector> {
        Box::new(TestProtocolConnector(self.0.clone()))
    }
}

impl TestProtocolConnector {
    fn new(realm: RealmInstance) -> Self {
        Self(Arc::new(Mutex::new(Some(realm))))
    }

    async fn release(self) -> Result<(), Error> {
        let realm = self.0.lock().unwrap().take().unwrap();
        realm.destroy().await?;
        Ok(())
    }

    fn connect_to_protocol<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error>
    where
        P: DiscoverableProtocolMarker,
    {
        self.0
            .lock()
            .expect("Failed ot lock test realm instance")
            .as_ref()
            .map(|realm| realm.root.connect_to_protocol_at_exposed_dir::<P>())
            .expect("Failed to connect to protocol in test realm")
    }

    fn connect_to_test_scene_controller(&self) -> Result<ui_test_scene::ControllerProxy, Error> {
        self.connect_to_protocol::<ui_test_scene::ControllerMarker>()
    }

    fn connect_to_test_input_registry(&self) -> Result<ui_test_input::RegistryProxy, Error> {
        self.connect_to_protocol::<ui_test_input::RegistryMarker>()
    }

    fn connect_to_flatland_screenshot(&self) -> Result<ui_comp::ScreenshotProxy, Error> {
        self.connect_to_protocol::<ui_comp::ScreenshotMarker>()
    }
}

async fn build_realm() -> anyhow::Result<RealmInstance> {
    let builder = RealmBuilder::new().await?;

    let test_ui_stack =
        builder.add_child("test-ui-stack", "#meta/test-ui-stack.cm", ChildOptions::new()).await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.ui.composition.Allocator"))
                .capability(Capability::protocol_by_name("fuchsia.ui.composition.Flatland"))
                .capability(Capability::protocol_by_name("fuchsia.ui.composition.Screenshot"))
                .capability(Capability::protocol_by_name("fuchsia.ui.input3.Keyboard"))
                .capability(Capability::protocol_by_name("fuchsia.ui.shortcut2.Registry"))
                .capability(Capability::protocol_by_name("fuchsia.ui.test.input.Registry"))
                .capability(Capability::protocol_by_name("fuchsia.ui.test.scene.Controller"))
                .from(&test_ui_stack)
                .to(Ref::parent()),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.scheduler.ProfileProvider"))
                .capability(Capability::protocol_by_name("fuchsia.sysmem.Allocator"))
                .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                .capability(Capability::protocol_by_name("fuchsia.vulkan.loader.Loader"))
                .from(Ref::parent())
                .to(&test_ui_stack),
        )
        .await?;

    let realm = builder.build().await?;
    Ok(realm)
}
