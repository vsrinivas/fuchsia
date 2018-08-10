// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_images as images;
extern crate fidl_fuchsia_math;
extern crate fidl_fuchsia_ui_gfx as gfx;
extern crate fidl_fuchsia_ui_scenic as scenic;
extern crate fidl_fuchsia_ui_viewsv1;
extern crate fidl_fuchsia_ui_viewsv1token;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_scenic;
extern crate fuchsia_zircon;
extern crate futures;
extern crate parking_lot;

mod canvas;

use canvas::{Canvas, Color, FontDescription, FontFace, Paint, Point, Size};
use component::client::connect_to_service;
use component::server::ServiceFactory;
use failure::{Error, ResultExt};
use fidl::endpoints2::{create_endpoints, ClientEnd, RequestStream, ServerEnd, ServiceMarker};
use fidl_fuchsia_math::SizeF;
use fidl_fuchsia_ui_viewsv1::ViewProviderRequest::CreateView;
use fidl_fuchsia_ui_viewsv1::{ViewListenerMarker, ViewListenerRequest, ViewManagerMarker,
                              ViewManagerProxy, ViewMarker, ViewProperties, ViewProviderMarker,
                              ViewProviderRequestStream, ViewProxy};
use fidl_fuchsia_ui_viewsv1token::ViewOwnerMarker;
use fuchsia_scenic::{HostImageCycler, ImportNode, Session, SessionPtr};
use fuchsia_zircon::{Channel, EventPair};
use futures::{FutureExt, TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use scenic::{ScenicMarker, ScenicProxy, SessionListenerMarker, SessionListenerRequest,
             SessionMarker};
use std::env;
use std::sync::Arc;

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
}

type ViewControllerPtr = Arc<Mutex<ViewController>>;

impl ViewController {
    pub fn new(
        face: FontFacePtr, view_listener_request: ServerEnd<ViewListenerMarker>, view: ViewProxy,
        mine: EventPair, scenic: ScenicProxy,
    ) -> Result<ViewControllerPtr, Error> {
        let (session_listener_client, session_listener_server) = Channel::create()?;
        let session_listener = ClientEnd::new(session_listener_client);
        let session_listener_request =
            ServerEnd::<SessionListenerMarker>::new(session_listener_server);

        let (session_proxy, session_request) = create_endpoints::<SessionMarker>()?;
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
        };
        view_controller.setup_scene();
        view_controller.present();

        let view_controller = Arc::new(Mutex::new(view_controller));
        {
            let view_controller = view_controller.clone();
            async::spawn(
                session_listener_request
                    .into_stream()
                    .unwrap()
                    .map_ok(move |request| {
                        match request {
                            SessionListenerRequest::OnEvent {
                                events,
                                control_handle: _,
                            } => view_controller.lock().handle_session_events(events),
                            _ => (),
                        }
                    })
                    .try_collect::<()>()
                    .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
            );
        }
        {
            let view_controller = view_controller.clone();
            async::spawn(
                view_listener_request
                    .into_stream()
                    .unwrap()
                    .map_ok(
                        move |ViewListenerRequest::OnPropertiesChanged {
                                  properties,
                                  responder,
                              }| {
                            view_controller.lock().handle_properies_changed(properties);
                            responder.send()
                                .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e))
                        },
                    )
                    .try_collect::<()>()
                    .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e))
            );
        }
        Ok(view_controller)
    }

    fn setup_scene(&self) {
        self.import_node
            .resource()
            .set_event_mask(gfx::METRICS_EVENT_MASK);
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
            let guard = self
                .image_cycler
                .acquire(info)
                .expect("failed to allocate buffer");
            let mut face = self.face.lock();
            let mut canvas = Canvas::new(guard.image().buffer(), stride);
            let paint = Paint {
                fg: Color {
                    r: 0xFF,
                    g: 0xDE,
                    b: 0xAD,
                    a: 0xFF,
                },
                bg: Color {
                    r: 0x00,
                    g: 0x00,
                    b: 0x8b,
                    a: 0xFF,
                },
            };
            let point = Point { x: 0, y: 0 };
            let size = Size { width: 14, height: 22 };
            let mut font = FontDescription {
                face: &mut face,
                size: 20,
                baseline: 18,
            };
            canvas.fill_text("$ echo \"hello, world!\"", point, size, &mut font, &paint);
        }

        let node = self.image_cycler.node();
        node.set_scale(1.0 / metrics.scale_x, 1.0 / metrics.scale_y, 1.0);
        node.set_translation(logical_size.width / 2.0, logical_size.height / 2.0, 0.0);
        self.present();
    }

    fn present(&self) {
        async::spawn(
            self.session
                .lock()
                .present(0)
                .map(|_| ())
        );
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

    fn handle_properies_changed(&mut self, properties: ViewProperties) {
        if let Some(view_properties) = properties.view_layout {
            self.logical_size = Some(view_properties.size);
            self.invalidate();
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

    pub fn spawn_view_provider_server(app: &AppPtr, channel: async::Channel) {
        let app = app.clone();
        async::spawn(
            ViewProviderRequestStream::from_channel(channel)
                .map_ok(move |request| {
                    let CreateView { view_owner, .. } = request;
                    app.lock()
                        .create_view(view_owner)
                        .expect("failed to create view");
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("error running view_provider server: {:?}", e)),
        )
    }

    pub fn create_view(
        &mut self, view_owner_request: ServerEnd<ViewOwnerMarker>,
    ) -> Result<(), Error> {
        let (view, view_request) = create_endpoints::<ViewMarker>()?;
        let (view_listener, view_listener_server) = Channel::create()?;
        let view_listener_request = ServerEnd::new(view_listener_server);
        let (mine, theirs) = EventPair::create()?;
        self.view_manager.create_view(
            view_request,
            view_owner_request,
            ClientEnd::new(view_listener),
            theirs,
            Some("Terminal"),
        )?;
        let (scenic, scenic_request) = create_endpoints::<ScenicMarker>()?;
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

struct ViewProvider {
    app: AppPtr,
}

impl ServiceFactory for ViewProvider {
    fn service_name(&self) -> &str {
        ViewProviderMarker::NAME
    }

    fn spawn_service(&mut self, channel: async::Channel) {
        App::spawn_view_provider_server(&self.app, channel);
    }
}

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "1");

    let mut executor = async::Executor::new().context("Error creating executor")?;
    let app = App::new()?;

    let fut = component::server::ServicesServer::new()
        .add_service(ViewProvider { app: app.clone() })
        .start()
        .context("Error starting view provider server")?;

    executor.run_singlethreaded(fut)?;
    Ok(())
}
