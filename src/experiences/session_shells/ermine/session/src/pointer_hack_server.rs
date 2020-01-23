use {
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_ui_policy::PointerCaptureListenerHackProxy,
    fidl_fuchsia_ui_policy::{PresentationMarker, PresentationRequest, PresentationRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::StreamExt,
    std::sync::Arc,
};

/// [`PointerHackServer`] serves the [`fidl_fuchsia_ui_policy::PresentationRequestStream`] and
/// maintains a list of [`PointerCaptureListenerHackProxy`]s.
pub struct PointerHackServer {
    pub pointer_listeners: Arc<Mutex<Vec<PointerCaptureListenerHackProxy>>>,
}

impl PointerHackServer {
    pub fn new(server_chan: zx::Channel) -> PointerHackServer {
        let server = PointerHackServer { pointer_listeners: Arc::new(Mutex::new(vec![])) };
        server.serve(server_chan);
        server
    }

    fn serve(&self, server_chan: zx::Channel) {
        let mut fs = ServiceFs::new();
        let pointer_listeners = self.pointer_listeners.clone();
        fs.add_fidl_service_at(
            PresentationMarker::NAME,
            move |mut stream: PresentationRequestStream| {
                let pointer_listeners = pointer_listeners.clone();
                fasync::spawn(async move {
                    loop {
                        match stream.next().await {
                            Some(Ok(PresentationRequest::CapturePointerEventsHack {
                                listener,
                                control_handle: _control_handle,
                            })) => {
                                let mut pointer_listeners = pointer_listeners.lock().await;
                                pointer_listeners.push(
                                    listener
                                        .into_proxy()
                                        .expect("Could not convert channel to proxy."),
                                );
                            }
                            _ => {}
                        };
                    }
                });
            },
        );
        fs.serve_connection(server_chan).unwrap();
        fasync::spawn(fs.collect::<()>());
    }
}
