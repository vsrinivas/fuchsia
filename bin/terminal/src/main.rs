// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_ui_gfx;
extern crate fidl_fuchsia_ui_scenic;
extern crate fidl_fuchsia_ui_viewsv1;
extern crate fidl_fuchsia_ui_viewsv1token;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_scenic as scenic;
extern crate fuchsia_zircon;
extern crate futures;
extern crate parking_lot;

use component::client::connect_to_service;
use component::server::ServiceFactory;
use failure::{Error, ResultExt};
use fidl::endpoints2::{create_endpoints, ClientEnd, RequestStream, ServerEnd, ServiceMarker};
use fidl_fuchsia_ui_gfx::ColorRgba;
use fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy, SessionMarker};
use fidl_fuchsia_ui_viewsv1::ViewProviderRequest::CreateView;
use fidl_fuchsia_ui_viewsv1::{ViewListenerMarker, ViewListenerRequest, ViewManagerMarker,
                              ViewManagerProxy, ViewMarker, ViewProperties, ViewProviderMarker,
                              ViewProviderRequestStream, ViewProxy};
use fidl_fuchsia_ui_viewsv1token::ViewOwnerMarker;
use fuchsia_zircon::{Channel, EventPair};
use futures::future::ok as fok;
use futures::{FutureExt, StreamExt};
use parking_lot::Mutex;
use scenic::{ImportNode, Material, Rectangle, Session, SessionPtr, ShapeNode};
use std::sync::Arc;

struct ViewController {
    _view: ViewProxy,
    session: SessionPtr,
    import_node: ImportNode,
    content_node: ShapeNode,
    content_material: Material,
    content_shape: Option<Rectangle>,
    width: f32,
    height: f32,
}

type ViewControllerPtr = Arc<Mutex<ViewController>>;

impl ViewController {
    pub fn new(
        view_listener_request: ServerEnd<ViewListenerMarker>, view: ViewProxy, mine: EventPair,
        scenic: ScenicProxy,
    ) -> Result<ViewControllerPtr, Error> {
        let (session_proxy, session_request) = create_endpoints::<SessionMarker>()?;
        scenic.create_session(session_request, None)?;
        let session = Session::new(session_proxy);
        let view_controller = ViewController {
            _view: view,
            session: session.clone(),
            import_node: ImportNode::new(session.clone(), mine),
            content_node: ShapeNode::new(session.clone()),
            content_material: Material::new(session.clone()),
            content_shape: None,
            width: 0.0,
            height: 0.0,
        };
        view_controller.setup_scene();
        view_controller.present();

        let view_controller = Arc::new(Mutex::new(view_controller));
        {
            let view_controller = view_controller.clone();
            async::spawn(
                view_listener_request
                    .into_stream()
                    .unwrap()
                    .for_each(
                        move |ViewListenerRequest::OnPropertiesChanged {
                                  properties,
                                  responder,
                              }| {
                            view_controller.lock().handle_properies_changed(&properties);
                            responder.send()
                        },
                    )
                    .map(|_| ())
                    .recover(|e| eprintln!("view listener error: {:?}", e)),
            );
        }
        Ok(view_controller)
    }

    fn setup_scene(&self) {
        self.import_node.add_child(&self.content_node);
        self.content_node.set_material(&self.content_material);
        self.content_material.set_color(ColorRgba {
            red: 0xb7,
            green: 0x41,
            blue: 0x0e,
            alpha: 0xff,
        });
    }

    fn present(&self) {
        async::spawn(
            self.session
                .lock()
                .present(0)
                .map(|_| ())
                .recover(|e| eprintln!("present error: {:?}", e)),
        );
    }

    fn update(&mut self) {
        let rect = Rectangle::new(self.session.clone(), self.width, self.height);
        self.content_node.set_shape(&rect);
        self.content_node
            .set_translation(self.width / 2.0, self.height / 2.0, 0.0);
        self.content_shape = Some(rect);
        self.present();
    }

    fn handle_properies_changed(&mut self, properties: &ViewProperties) {
        if let Some(ref view_properties) = properties.view_layout {
            self.width = view_properties.size.width;
            self.height = view_properties.size.height;
            self.update();
        }
    }
}

struct App {
    view_manager: ViewManagerProxy,
    view_controllers: Vec<ViewControllerPtr>,
}

type AppPtr = Arc<Mutex<App>>;

impl App {
    pub fn new() -> Result<AppPtr, Error> {
        let view_manager = connect_to_service::<ViewManagerMarker>()?;
        Ok(Arc::new(Mutex::new(App {
            view_manager,
            view_controllers: vec![],
        })))
    }

    pub fn spawn_view_provider_server(app: &AppPtr, channel: async::Channel) {
        let app = app.clone();
        async::spawn(
            ViewProviderRequestStream::from_channel(channel)
                .for_each(move |request| {
                    let CreateView { view_owner, .. } = request;
                    app.lock()
                        .create_view(view_owner)
                        .expect("failed to create view");
                    fok(())
                })
                .map(|_| ())
                .recover(|e| eprintln!("error running view_provider server: {:?}", e)),
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
    let mut executor = async::Executor::new().context("Error creating executor")?;
    let app = App::new()?;

    let fut = component::server::ServicesServer::new()
        .add_service(ViewProvider { app: app.clone() })
        .start()
        .context("Error starting view provider server")?;

    executor.run_singlethreaded(fut)?;
    Ok(())
}
