use crate::view_controller::{FontFacePtr, ViewController, ViewControllerPtr};
use failure::Error;
use fidl::endpoints::{create_endpoints, create_proxy, RequestStream};
use fidl_fuchsia_ui_app as viewsv2;
use fidl_fuchsia_ui_viewsv1::ViewProviderRequest::CreateView;
use fidl_fuchsia_ui_viewsv1::{ViewManagerMarker, ViewManagerProxy, ViewProviderRequestStream};
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_ui::FontFace;
use fuchsia_zircon::{EventPair, Handle};
use futures::{TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::sync::Arc;

static FONT_DATA: &'static [u8] =
    include_bytes!("../../fonts/third_party/robotomono/RobotoMono-Regular.ttf");

pub struct App {
    face: FontFacePtr,
    view_manager: ViewManagerProxy,
    view_controllers: Vec<ViewControllerPtr>,
}

type AppPtr = Arc<Mutex<App>>;

impl App {
    pub fn new() -> Result<AppPtr, Error> {
        let view_manager = connect_to_service::<ViewManagerMarker>()?;
        Ok(Arc::new(Mutex::new(App {
            face: Arc::new(Mutex::new(FontFace::new(FONT_DATA)?)),
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
                    app.lock()
                        .create_view(token)
                        .expect("error creating view from V1 view_provider");
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
                    app.lock()
                        .create_view(token)
                        .expect("error creating view from V2 view_provider");
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running V2 view_provider server: {:?}", e)),
        )
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
