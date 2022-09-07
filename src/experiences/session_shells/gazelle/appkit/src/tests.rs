// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_proxy_and_stream, create_request_stream},
    fidl_fuchsia_element as felement, fidl_fuchsia_ui_app as ui_app,
    fidl_fuchsia_ui_test_scene as ui_test_scene, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_scenic::flatland::ViewCreationTokenPair,
    futures::future::{AbortHandle, Abortable},
    futures::{StreamExt, TryStreamExt},
    std::collections::HashMap,
    tracing::*,
};

use crate::{
    child_view::{ChildView, ChildViewId},
    event::{ChildViewEvent, Event, SystemEvent, ViewSpecHolder, WindowEvent},
    utils::EventSender,
    window::{Window, WindowBuilder, WindowId},
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
    let (event_sender, mut receiver) = EventSender::<TestEvent>::new();

    let (graphical_presenter_proxy, graphical_presenter_request_stream) =
        create_proxy_and_stream::<felement::GraphicalPresenterMarker>()?;

    let (services_abort, services_registration) = AbortHandle::new_pair();
    let services_fut = Abortable::new(
        start_services(event_sender.clone(), graphical_presenter_request_stream),
        services_registration,
    );

    let mut app = TestApp::new();
    // Declare an event handler that does not allow blocking async calls within it.
    let mut event_handler = |event| {
        info!("------ParentView {:?}", event);
        match event {
            Event::Init => {}
            Event::WindowEvent(id, window_event) => match window_event {
                WindowEvent::Resized(width, height) => {
                    app.width = width;
                    app.height = height;
                    if app.active_window.is_none() {
                        app.active_window = Some(id);

                        let cloned_graphical_presenter = graphical_presenter_proxy.clone();
                        fasync::Task::spawn(async move {
                            create_child_view_spec(cloned_graphical_presenter)
                                .await
                                .expect("Failed to create_child_view");
                        })
                        .detach();
                    }
                }
                WindowEvent::NeedsRedraw(_) => {
                    assert!(
                        app.width > 0 && app.height > 0,
                        "Redraw event received before window was resized"
                    );
                }
                _ => {}
            },
            Event::SystemEvent(system_event) => match system_event {
                SystemEvent::ViewCreationToken(view_creation_token) => {
                    let mut window = WindowBuilder::new()
                        .with_view_creation_token(view_creation_token)
                        .build(event_sender.clone())
                        .unwrap();
                    window.create_view().expect("Failed to create view for window");
                    app.windows.insert(window.id(), window);
                }
                SystemEvent::PresentViewSpec(view_spec_holder) => {
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
            Event::ChildViewEvent(child_view_id, window_id, child_view_event) => {
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
                    ChildViewEvent::Attached(view_ref) => {
                        window.request_focus(view_ref);
                        event_sender.send(Event::Exit).expect("Failed to send Event::Exit event");
                    }
                    ChildViewEvent::Detached => {}
                }
            }
            Event::Exit => {
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
    Ok(())
}

async fn start_services(
    event_sender: EventSender<TestEvent>,
    graphical_presenter_request_stream: felement::GraphicalPresenterRequestStream,
) {
    let view_provider_fut = start_view_provider(event_sender.clone());
    let graphical_presenter_fut =
        start_graphical_presenter(event_sender.clone(), graphical_presenter_request_stream);

    futures::join!(view_provider_fut, graphical_presenter_fut);
}

async fn start_view_provider(event_sender: EventSender<TestEvent>) {
    let (view_provider, mut view_provider_request_stream) =
        create_request_stream::<ui_app::ViewProviderMarker>()
            .expect("failed to create ViewProvider request stream");

    let scene_provider_fut = async move {
        let scene_provider = connect_to_protocol::<ui_test_scene::ControllerMarker>()
            .expect("failed to connect to fuchsia.ui.test.scene.Controller");
        let _view_ref_koid = scene_provider
            .attach_client_view(ui_test_scene::ControllerAttachClientViewRequest {
                view_provider: Some(view_provider),
                ..ui_test_scene::ControllerAttachClientViewRequest::EMPTY
            })
            .await
            .expect("failed to attach root client view");
    };

    let view_provider_fut = async move {
        match view_provider_request_stream.next().await.unwrap() {
            Ok(ui_app::ViewProviderRequest::CreateView2 { args, .. }) => {
                event_sender
                    .send(Event::SystemEvent(SystemEvent::ViewCreationToken(
                        args.view_creation_token.unwrap(),
                    )))
                    .expect("Failed to send SystemEvent::ViewCreationToken event");
            }
            _ => panic!("ViewProvider impl only handles CreateView2()"),
        }
    };

    futures::join!(scene_provider_fut, view_provider_fut);
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
                    .send(Event::SystemEvent(SystemEvent::PresentViewSpec(ViewSpecHolder {
                        view_spec,
                        annotation_controller,
                        view_controller_request,
                        responder,
                    })))
                    .expect("Failed to send SystemEvent::PresentViewSpec event");
            }
        }
    }
}

async fn create_child_view_spec(
    graphical_presenter: felement::GraphicalPresenterProxy,
) -> Result<(), Error> {
    let ViewCreationTokenPair { view_creation_token, viewport_creation_token } =
        ViewCreationTokenPair::new()?;
    let view_spec = felement::ViewSpec {
        viewport_creation_token: Some(viewport_creation_token),
        ..felement::ViewSpec::EMPTY
    };
    let _ = graphical_presenter.present_view(view_spec, None, None).await;

    fasync::Task::local(async move {
        let (sender, mut receiver) = futures::channel::mpsc::unbounded::<Event<TestEvent>>();
        let event_sender = EventSender::<TestEvent>(sender);
        event_sender.send(Event::Init).expect("Failed to send Event::Init event");

        let mut _window_holder: Option<Window<TestEvent>> = None;
        let mut view_creation_token = Some(view_creation_token);
        while let Some(event) = receiver.next().await {
            info!("------ChildView  {:?}", event);
            match event {
                Event::Init => {
                    let mut window = WindowBuilder::new()
                        .with_view_creation_token(view_creation_token.take().unwrap())
                        .build(event_sender.clone())
                        .unwrap();
                    window.create_view().expect("Failed to create window for child view");
                    _window_holder = Some(window);
                }
                _ => {}
            }
        }
    })
    .detach();

    Ok(())
}
