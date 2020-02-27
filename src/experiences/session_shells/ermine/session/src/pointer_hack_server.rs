use {
    crate::element_repository::ElementManagerServer,
    element_management::ElementManager,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_session::{ElementManagerMarker, ElementManagerRequestStream},
    fidl_fuchsia_ui_policy::PointerCaptureListenerHackProxy,
    fidl_fuchsia_ui_policy::{PresentationMarker, PresentationRequest, PresentationRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::StreamExt,
    std::sync::Arc,
};

enum ExposedServices {
    ElementManager(ElementManagerRequestStream),
    Presentation(PresentationRequestStream),
}

/// [`PointerHackServer`] serves the [`fidl_fuchsia_ui_policy::PresentationRequestStream`] and
/// maintains a list of [`PointerCaptureListenerHackProxy`]s.
pub struct PointerHackServer {
    pub pointer_listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
}

impl PointerHackServer {
    pub fn new<T: ElementManager + 'static>(
        server_chan: zx::Channel,
        element_server: ElementManagerServer<T>,
    ) -> PointerHackServer {
        let server = PointerHackServer { pointer_listeners: Arc::new(Mutex::new(vec![])) };
        let pointer_listeners = server.pointer_listeners.clone();
        fasync::spawn_local(async move {
            PointerHackServer::serve(server_chan, pointer_listeners, element_server).await;
        });
        server
    }

    async fn serve<T: ElementManager + 'static>(
        server_chan: zx::Channel,
        pointer_listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
        element_server: ElementManagerServer<T>,
    ) {
        let mut fs = ServiceFs::new();
        fs.add_fidl_service_at(PresentationMarker::NAME, ExposedServices::Presentation)
            .add_fidl_service_at(ElementManagerMarker::NAME, ExposedServices::ElementManager);

        fs.serve_connection(server_chan).unwrap();
        const MAX_CONCURRENT: usize = 10_000;

        let element_server_ref = &element_server;
        let pointer_listeners_ref = &pointer_listeners;
        fs.for_each_concurrent(MAX_CONCURRENT, |service_request: ExposedServices| async move {
            match service_request {
                ExposedServices::Presentation(request_stream) => {
                    let pointer_listeners = pointer_listeners_ref.clone();
                    PointerHackServer::handle_presentation_request_stream(
                        request_stream,
                        pointer_listeners,
                    )
                    .await;
                }
                ExposedServices::ElementManager(request_stream) => {
                    fx_log_info!("received incoming element manager request from ermine");
                    //TODO(47079): Error handling
                    let _ = element_server_ref.handle_request(request_stream).await;
                }
            }
        })
        .await;
    }

    async fn handle_presentation_request_stream(
        mut stream: PresentationRequestStream,
        pointer_listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
    ) {
        while let Some(Ok(request)) = stream.next().await {
            match request {
                PresentationRequest::CapturePointerEventsHack {
                    listener,
                    control_handle: _control_handle,
                } => {
                    let mut pointer_listeners = pointer_listeners.lock().await;
                    if let Ok(proxy) = listener.into_proxy() {
                        pointer_listeners.push(proxy);
                    }
                }
                invalid_request => {
                    println!("Received invalid PresentationRequest {:?}", invalid_request);
                }
            }
        }
    }
}
