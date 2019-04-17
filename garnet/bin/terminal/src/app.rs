use crate::view_controller::{FontFacePtr, ViewController, ViewControllerPtr};
use carnelian::FontFace;
use failure::Error;
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_ui_app::{ViewProviderRequest, ViewProviderRequestStream};
use fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_scenic::Session;
use futures::{TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::sync::Arc;

static FONT_DATA: &'static [u8] =
    include_bytes!("../../fonts/third_party/robotomono/RobotoMono-Regular.ttf");

pub struct App {
    face: FontFacePtr,
    scenic: ScenicProxy,
    view_controllers: Vec<ViewControllerPtr>,
}

type AppPtr = Arc<Mutex<App>>;

impl App {
    pub fn new() -> Result<AppPtr, Error> {
        let scenic = connect_to_service::<ScenicMarker>()?;
        Ok(Arc::new(Mutex::new(App {
            face: Arc::new(Mutex::new(FontFace::new(FONT_DATA)?)),
            scenic,
            view_controllers: vec![],
        })))
    }

    pub fn spawn_view_provider_server(app: &AppPtr, stream: ViewProviderRequestStream) {
        let app = app.clone();
        fasync::spawn(
            stream
                .try_for_each(move |req| {
                    let ViewProviderRequest::CreateView { token, .. } = req;
                    let view_token = ViewToken { value: token };

                    app.lock()
                        .create_view(view_token)
                        .expect("error creating view from ViewProvider");
                    futures::future::ready(Ok(()))
                })
                .unwrap_or_else(|e| eprintln!("error running ViewProvider server: {:?}", e)),
        )
    }

    pub fn create_view(&mut self, view_token: ViewToken) -> Result<(), Error> {
        let (session_listener, session_listener_request) = create_endpoints()?;
        let (session_proxy, session_request) = create_proxy()?;
        self.scenic.create_session(session_request, Some(session_listener))?;
        let session = Session::new(session_proxy);

        self.view_controllers.push(ViewController::new(
            self.face.clone(),
            view_token,
            session,
            session_listener_request,
        )?);
        Ok(())
    }
}
