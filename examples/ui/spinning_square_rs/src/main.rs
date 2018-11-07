// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error, ResultExt};
use fidl::endpoints::{create_endpoints, create_proxy, RequestStream, ServerEnd, ServiceMarker};
use fidl_fuchsia_ui_app as viewsv2;
use fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba};
use fidl_fuchsia_ui_scenic::{SessionListenerMarker, SessionListenerRequest};
use fidl_fuchsia_ui_viewsv1::ViewProviderRequest::CreateView;
use fidl_fuchsia_ui_viewsv1::{
    ViewListenerMarker, ViewListenerRequest, ViewManagerMarker, ViewManagerProxy,
    ViewProviderMarker, ViewProviderRequestStream, ViewProxy,
};
use fuchsia_app as component;
use fuchsia_app::{client::connect_to_service, server::ServiceFactory};
use fuchsia_async::{self as fasync, Interval};
use fuchsia_scenic::{ImportNode, Material, Rectangle, Session, SessionPtr, ShapeNode};
use fuchsia_zircon::{self as zx, ClockId, Duration, EventPair, Handle, Time};
use futures::future::ready as fready;
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use std::f32::consts::PI;
use std::sync::{Arc, Mutex};

struct SpinningSquareView {
    _view: ViewProxy,
    session: SessionPtr,
    import_node: ImportNode,
    background_node: ShapeNode,
    spinning_square_node: ShapeNode,
    width: f32,
    height: f32,
    start: Time,
}

type SpinningSquareViewPtr = Arc<Mutex<SpinningSquareView>>;

impl SpinningSquareView {
    pub fn new(
        view_listener_request: ServerEnd<ViewListenerMarker>, view: ViewProxy, mine: zx::EventPair,
        scenic: fidl_fuchsia_ui_scenic::ScenicProxy,
    ) -> Result<SpinningSquareViewPtr, Error> {
        let (session_listener, session_listener_request) = create_endpoints()?;

        let (session_proxy, session_request) = create_proxy()?;
        scenic.create_session(session_request, Some(session_listener))?;
        let session = Session::new(session_proxy);

        let view_controller = SpinningSquareView {
            _view: view,
            session: session.clone(),
            import_node: ImportNode::new(session.clone(), mine),
            background_node: ShapeNode::new(session.clone()),
            spinning_square_node: ShapeNode::new(session.clone()),
            width: 0.0,
            height: 0.0,
            start: Time::get(ClockId::Monotonic),
        };

        let view_controller = Arc::new(Mutex::new(view_controller));

        Self::setup_timer(&view_controller);
        Self::setup_session_listener(&view_controller, session_listener_request);
        Self::setup_view_listener(&view_controller, view_listener_request);

        if let Ok(vc) = view_controller.lock() {
            vc.setup_scene();
            vc.present();
        } else {
            bail!("Inexplicably couldn't lock mutex for view controller.");
        }

        Ok(view_controller)
    }

    fn setup_timer(view_controller: &SpinningSquareViewPtr) {
        let timer = Interval::new(Duration::from_millis(10));
        let view_controller = view_controller.clone();
        let f = timer
            .map(move |_| {
                view_controller.lock().unwrap().update();
            })
            .collect::<()>();
        fasync::spawn(f);
    }

    fn setup_session_listener(
        view_controller: &SpinningSquareViewPtr,
        session_listener_request: ServerEnd<SessionListenerMarker>,
    ) {
        let view_controller = view_controller.clone();
        fasync::spawn(
            session_listener_request
                .into_stream()
                .unwrap()
                .map_ok(move |request| match request {
                    SessionListenerRequest::OnScenicEvent { events, .. } => view_controller
                        .lock()
                        .unwrap()
                        .handle_session_events(events),
                    _ => (),
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
        );
    }

    fn setup_view_listener(
        view_controller: &SpinningSquareViewPtr,
        view_listener_request: ServerEnd<ViewListenerMarker>,
    ) {
        let view_controller = view_controller.clone();
        fasync::spawn(
            view_listener_request
                .into_stream()
                .unwrap()
                .try_for_each(
                    move |ViewListenerRequest::OnPropertiesChanged {
                              properties,
                              responder,
                          }| {
                        view_controller
                            .lock()
                            .unwrap()
                            .handle_properies_changed(&properties);
                        fready(responder.send())
                    },
                )
                .unwrap_or_else(|e| eprintln!("view listener error: {:?}", e)),
        );
    }

    fn setup_scene(&self) {
        self.import_node
            .resource()
            .set_event_mask(gfx::METRICS_EVENT_MASK);
        self.import_node.add_child(&self.background_node);
        let material = Material::new(self.session.clone());
        material.set_color(ColorRgba {
            red: 0xb7,
            green: 0x41,
            blue: 0x0e,
            alpha: 0xff,
        });
        self.background_node.set_material(&material);

        self.import_node.add_child(&self.spinning_square_node);
        let material = Material::new(self.session.clone());
        material.set_color(ColorRgba {
            red: 0xff,
            green: 0x00,
            blue: 0xff,
            alpha: 0xff,
        });
        self.spinning_square_node.set_material(&material);
    }

    fn update(&mut self) {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let center_x = self.width * 0.5;
        let center_y = self.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            self.session.clone(),
            self.width,
            self.height,
        ));
        self.background_node
            .set_translation(center_x, center_y, 0.0);
        let square_size = self.width.min(self.height) * 0.6;
        let t = ((Time::get(ClockId::Monotonic).nanos() - self.start.nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;
        let angle = t * PI * 2.0;
        self.spinning_square_node.set_shape(&Rectangle::new(
            self.session.clone(),
            square_size,
            square_size,
        ));
        self.spinning_square_node
            .set_translation(center_x, center_y, 8.0);
        self.spinning_square_node
            .set_rotation(0.0, 0.0, (angle * 0.5).sin(), (angle * 0.5).cos());
        self.present();
    }

    fn present(&self) {
        fasync::spawn(
            self.session
                .lock()
                .present(0)
                .map_ok(|_| ())
                .unwrap_or_else(|e| eprintln!("present error: {:?}", e)),
        );
    }

    fn handle_session_events(&mut self, events: Vec<fidl_fuchsia_ui_scenic::Event>) {
        events.iter().for_each(|event| match event {
            fidl_fuchsia_ui_scenic::Event::Gfx(gfx::Event::Metrics(_event)) => {
                self.update();
            }
            _ => (),
        });
    }

    fn handle_properies_changed(&mut self, properties: &fidl_fuchsia_ui_viewsv1::ViewProperties) {
        if let Some(ref view_properties) = properties.view_layout {
            self.width = view_properties.size.width;
            self.height = view_properties.size.height;
            self.update();
        }
    }
}

struct App {
    view_manager: ViewManagerProxy,
    views: Vec<SpinningSquareViewPtr>,
}

type AppPtr = Arc<Mutex<App>>;

impl App {
    pub fn new() -> AppPtr {
        let view_manager = connect_to_service::<ViewManagerMarker>().unwrap();
        Arc::new(Mutex::new(App {
            view_manager,
            views: vec![],
        }))
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
        let (view, view_server_end) = create_proxy()?;
        let (view_listener, view_listener_request) = create_endpoints()?;
        let (mine, theirs) = zx::EventPair::create().unwrap();
        self.view_manager.create_view2(
            view_server_end,
            view_token,
            view_listener,
            theirs,
            Some("spinning_square_rs"),
        )?;
        let (scenic, scenic_request) = create_proxy()?;
        self.view_manager.get_scenic(scenic_request).unwrap();
        self.views.push(SpinningSquareView::new(
            view_listener_request,
            view,
            mine,
            scenic,
        )?);
        Ok(())
    }
}

struct V1ViewProvider {
    app: AppPtr,
}

impl ServiceFactory for V1ViewProvider {
    fn service_name(&self) -> &str {
        ViewProviderMarker::NAME
    }

    fn spawn_service(&mut self, channel: fasync::Channel) {
        App::spawn_v1_view_provider_server(&self.app, channel);
    }
}

struct V2ViewProvider {
    app: AppPtr,
}

impl ServiceFactory for V2ViewProvider {
    fn service_name(&self) -> &str {
        viewsv2::ViewProviderMarker::NAME
    }

    fn spawn_service(&mut self, channel: fasync::Channel) {
        App::spawn_v2_view_provider_server(&self.app, channel);
    }
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let app = App::new();

    let fut = component::server::ServicesServer::new()
        .add_service(V1ViewProvider { app: app.clone() })
        .add_service(V2ViewProvider { app: app.clone() })
        .start()
        .context("Error starting view provider server")?;

    executor.run_singlethreaded(fut)?;
    Ok(())
}
