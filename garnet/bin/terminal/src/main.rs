// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl::endpoints::{create_endpoints, create_proxy, RequestStream, ServerEnd, ServiceMarker};
use fidl_fuchsia_images as images;
use fidl_fuchsia_math::SizeF;
use fidl_fuchsia_ui_app as viewsv2;
use fidl_fuchsia_ui_gfx as gfx;
use fidl_fuchsia_ui_input::{
    ImeServiceMarker, InputEvent, InputMethodAction, InputMethodEditorClientMarker,
    InputMethodEditorClientRequest, InputMethodEditorMarker, KeyboardEventPhase, KeyboardType,
    TextAffinity, TextInputState, TextRange, TextSelection,
};
use fidl_fuchsia_ui_scenic::{self as scenic, ScenicProxy, SessionListenerRequest};
use fidl_fuchsia_ui_viewsv1::ViewProviderRequest::CreateView;
use fidl_fuchsia_ui_viewsv1::{
    ViewListenerMarker, ViewListenerRequest, ViewManagerMarker, ViewManagerProxy, ViewProperties,
    ViewProviderMarker, ViewProviderRequestStream, ViewProxy,
};
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_scenic::{HostImageCycler, ImportNode, Session, SessionPtr};
use fuchsia_ui::{
    Canvas, Color, FontDescription, FontFace, Paint, Point, SharedBufferPixelSink, Size,
};
use fuchsia_zircon::{EventPair, Handle};
use futures::{FutureExt, TryFutureExt, TryStreamExt};
use std::env;
use std::sync::{Arc, Mutex};
use term_model::ansi::Handler;
use term_model::config::Config;
use term_model::term::{SizeInfo, Term};

static FONT_DATA: &'static [u8] =
    include_bytes!("../../fonts/third_party/robotomono/RobotoMono-Regular.ttf");

type FontFacePtr = Arc<Mutex<FontFace<'static>>>;

struct ViewController {
    face: FontFacePtr,
    _view: ViewProxy,
    session: SessionPtr,
    import_node: ImportNode,
    image_cycler: HostImageCycler,
    metrics: Option<gfx::Metrics>,
    logical_size: Option<SizeF>,
    term: Option<Term>,
}

type ViewControllerPtr = Arc<Mutex<ViewController>>;

impl ViewController {
    pub fn new(
        face: FontFacePtr, view_listener_request: ServerEnd<ViewListenerMarker>, view: ViewProxy,
        mine: EventPair, scenic: ScenicProxy,
    ) -> Result<ViewControllerPtr, Error> {
        let ime_service = connect_to_service::<ImeServiceMarker>()?;
        let (ime_listener, ime_listener_request) = create_endpoints::<InputMethodEditorMarker>()?;
        let (ime_client_listener, ime_client_listener_request) =
            create_endpoints::<InputMethodEditorClientMarker>()?;
        let mut text_input_state = TextInputState {
            revision: 0,
            text: "".to_string(),
            selection: TextSelection { base: 0, extent: 0, affinity: TextAffinity::Upstream },
            composing: TextRange { start: 0, end: 0 },
        };
        ime_service.get_input_method_editor(
            KeyboardType::Text,
            InputMethodAction::None,
            &mut text_input_state,
            ime_client_listener,
            ime_listener_request,
        )?;

        let (session_listener, session_listener_request) = create_endpoints()?;
        let (session_proxy, session_request) = create_proxy()?;
        scenic.create_session(session_request, Some(session_listener))?;
        let session = Session::new(session_proxy);

        let view_controller = ViewController {
            face,
            _view: view,
            session: session.clone(),
            import_node: ImportNode::new(session.clone(), mine),
            image_cycler: HostImageCycler::new(session.clone()),
            metrics: None,
            logical_size: None,
            term: None,
        };
        view_controller.setup_scene();
        view_controller.present();

        let view_controller = Arc::new(Mutex::new(view_controller));
        {
            let view_controller = view_controller.clone();
            fasync::spawn(
                async move {
                    // In order to keep the channel alive, we need to move ime_listener into this block.
                    // Otherwise it's unused, which closes the channel immediately.
                    let _dummy = ime_listener;
                    let mut stream = ime_client_listener_request.into_stream()?;
                    while let Some(request) = await!(stream.try_next())? {
                        match request {
                            InputMethodEditorClientRequest::DidUpdateState {
                                event: Some(event),
                                ..
                            } => view_controller.lock().unwrap().handle_input_event(*event),
                            _ => (),
                        }
                    }
                    Ok(())
                }
                    .unwrap_or_else(|e: failure::Error| eprintln!("input listener error: {:?}", e)),
            );
        }
        {
            let view_controller = view_controller.clone();
            fasync::spawn(
                async move {
                    let mut stream = session_listener_request.into_stream()?;
                    while let Some(request) = await!(stream.try_next())? {
                        match request {
                            SessionListenerRequest::OnScenicEvent { events, control_handle: _ } => {
                                view_controller.lock().unwrap().handle_session_events(events)
                            }
                            _ => (),
                        }
                    }
                    Ok(())
                }
                    .unwrap_or_else(|e: failure::Error| eprintln!("view listener error: {:?}", e)),
            );
        }
        {
            let view_controller = view_controller.clone();
            fasync::spawn(
                async move {
                    let mut stream = view_listener_request.into_stream()?;
                    while let Some(req) = await!(stream.try_next())? {
                        let ViewListenerRequest::OnPropertiesChanged { properties, responder } =
                            req;
                        view_controller.lock().unwrap().handle_properties_changed(properties);
                        responder
                            .send()
                            .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e))
                    }
                    Ok(())
                }
                    .unwrap_or_else(|e: failure::Error| eprintln!("view listener error: {:?}", e)),
            );
        }
        Ok(view_controller)
    }

    fn setup_scene(&self) {
        self.import_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);
        self.import_node.add_child(self.image_cycler.node());
    }

    fn invalidate(&mut self) {
        self.begin_frame();
    }

    fn begin_frame(&mut self) {
        let (metrics, logical_size) = match (self.metrics.as_ref(), self.logical_size.as_ref()) {
            (Some(metrics), Some(logical_size)) => (metrics, logical_size),
            _ => return,
        };
        let physical_width = (logical_size.width * metrics.scale_x) as u32;
        let physical_height = (logical_size.height * metrics.scale_y) as u32;
        let stride = physical_width * 4;
        let info = images::ImageInfo {
            transform: images::Transform::Normal,
            width: physical_width,
            height: physical_height,
            stride,
            pixel_format: images::PixelFormat::Bgra8,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::Opaque,
        };
        {
            let guard = self.image_cycler.acquire(info).expect("failed to allocate buffer");
            let mut face = self.face.lock().unwrap();
            let mut canvas = Canvas::<SharedBufferPixelSink>::new(guard.image().buffer(), stride);
            let size = Size { width: 14, height: 22 };
            let mut font = FontDescription { face: &mut face, size: 20, baseline: 18 };
            let term = self.term.get_or_insert_with(|| {
                let mut term = Term::new(
                    &Config::default(),
                    SizeInfo {
                        width: physical_width as f32,
                        height: physical_height as f32,
                        cell_width: size.width as f32,
                        cell_height: size.height as f32,
                        padding_x: 0.,
                        padding_y: 0.,
                    },
                );
                for c in "$ echo \"hello, world!\"".chars() {
                    term.input(c);
                }
                term
            });
            for cell in term.renderable_cells(&Config::default(), None, true) {
                let mut buffer: [u8; 4] = [0, 0, 0, 0];
                canvas.fill_text_cells(
                    cell.c.encode_utf8(&mut buffer),
                    Point {
                        x: size.width * cell.column.0 as u32,
                        y: size.height * cell.line.0 as u32,
                    },
                    size,
                    &mut font,
                    &Paint {
                        fg: Color { r: cell.fg.r, g: cell.fg.g, b: cell.fg.b, a: 0xFF },
                        bg: Color { r: cell.bg.r, g: cell.bg.g, b: cell.bg.b, a: 0xFF },
                    },
                )
            }
        }

        let node = self.image_cycler.node();
        node.set_scale(1.0 / metrics.scale_x, 1.0 / metrics.scale_y, 1.0);
        node.set_translation(logical_size.width / 2.0, logical_size.height / 2.0, 0.0);
        self.present();
    }

    fn present(&self) {
        fasync::spawn(self.session.lock().present(0).map(|_| ()));
    }

    fn handle_session_events(&mut self, events: Vec<scenic::Event>) {
        events.iter().for_each(|event| match event {
            scenic::Event::Gfx(gfx::Event::Metrics(event)) => {
                self.metrics = Some(gfx::Metrics { ..event.metrics });
                self.invalidate();
            }
            _ => (),
        });
    }

    fn handle_properties_changed(&mut self, properties: ViewProperties) {
        if let Some(view_properties) = properties.view_layout {
            self.logical_size = Some(view_properties.size);
            self.invalidate();
        }
    }

    fn handle_input_event(&mut self, event: InputEvent) {
        if let (Some(term), InputEvent::Keyboard(event)) = (&mut self.term, &event) {
            if event.phase == KeyboardEventPhase::Pressed
                || event.phase == KeyboardEventPhase::Repeat
            {
                if let Some(c) = std::char::from_u32(event.code_point) {
                    if c != '\0' {
                        term.input(c);
                        self.invalidate();
                    }
                }
            }
        }
    }
}

struct App {
    face: FontFacePtr,
    view_manager: ViewManagerProxy,
    view_controllers: Vec<ViewControllerPtr>,
}

type AppPtr = Arc<Mutex<App>>;

impl App {
    pub fn new() -> Result<AppPtr, Error> {
        let view_manager = connect_to_service::<ViewManagerMarker>()?;
        Ok(Arc::new(Mutex::new(App {
            face: Arc::new(Mutex::new(FontFace::new(FONT_DATA).unwrap())),
            view_manager,
            view_controllers: vec![],
        })))
    }

    pub fn spawn_v1_view_provider_server(app: &AppPtr, chan: fasync::Channel) {
        let app = app.clone();
        fasync::spawn(
            ViewProviderRequestStream::from_channel(chan)
                .try_for_each(move |req| {
                    let CreateView { view_owner, .. } = req;
                    let token: EventPair = EventPair::from(Handle::from(view_owner.into_channel()));
                    App::app_create_view(app.clone(), token).unwrap();
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running V1 view_provider server: {:?}", e)),
        )
    }

    pub fn spawn_v2_view_provider_server(app: &AppPtr, chan: fasync::Channel) {
        let app = app.clone();
        fasync::spawn(
            viewsv2::ViewProviderRequestStream::from_channel(chan)
                .try_for_each(move |req| {
                    let viewsv2::ViewProviderRequest::CreateView { token, .. } = req;
                    App::app_create_view(app.clone(), token).unwrap();
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running V2 view_provider server: {:?}", e)),
        )
    }

    pub fn app_create_view(app: AppPtr, view_token: EventPair) -> Result<(), Error> {
        app.lock().unwrap().create_view(view_token)
    }

    pub fn create_view(&mut self, view_token: EventPair) -> Result<(), Error> {
        let (view, view_request) = create_proxy()?;
        let (view_listener, view_listener_request) = create_endpoints()?;
        let (mine, theirs) = EventPair::create()?;
        self.view_manager.create_view2(
            view_request,
            view_token,
            view_listener,
            theirs,
            Some("Terminal"),
        )?;
        let (scenic, scenic_request) = create_proxy()?;
        self.view_manager.get_scenic(scenic_request)?;
        self.view_controllers.push(ViewController::new(
            self.face.clone(),
            view_listener_request,
            view,
            mine,
            scenic,
        )?);
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let app_for_v1 = App::new()?;
    let app_for_v2 = app_for_v1.clone();

    let fut = fuchsia_app::server::ServicesServer::new()
        .add_service((ViewProviderMarker::NAME, move |chan| {
            App::spawn_v1_view_provider_server(&app_for_v1, chan)
        }))
        .add_service((viewsv2::ViewProviderMarker::NAME, move |chan| {
            App::spawn_v2_view_provider_server(&app_for_v2, chan)
        }))
        .start()
        .context("Error starting view provider server")?;

    executor.run_singlethreaded(fut)?;
    Ok(())
}
