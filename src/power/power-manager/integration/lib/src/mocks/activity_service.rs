// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_ui_activity as factivity, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::LocalComponentHandles,
    futures::{channel::mpsc, lock::Mutex, StreamExt, TryStreamExt},
    std::sync::Arc,
    tracing::*,
};

/// Mocks the fuchsia.ui.activity.Provider service to be used in integration tests.
pub struct MockActivityService {
    /// Sends a new activity state to the mock server. The expected usage is that the test holds the
    /// sender end to communicate new activity state values to the server on the receiver end.
    state_sender: Mutex<mpsc::Sender<factivity::State>>,

    /// Receiver end for activity state changes. When the server reads the new state from the
    /// receiver end, it will send that new state out to any listener clients that have previously
    /// called `WatchState`.
    state_receiver: Mutex<mpsc::Receiver<factivity::State>>,
}

impl MockActivityService {
    pub fn new() -> Arc<MockActivityService> {
        let (state_sender, state_receiver) = mpsc::channel(1);
        Arc::new(Self {
            state_sender: Mutex::new(state_sender),
            state_receiver: Mutex::new(state_receiver),
        })
    }

    /// Runs the mock using the provided `LocalComponentHandles`.
    ///
    /// Expected usage is to call this function from a closure for the
    /// `local_component_implementation` parameter to `RealmBuilder.add_local_child`.
    ///
    /// For example:
    ///     let mock_activity_service = MockActivityService::new();
    ///     let activity_service_child = realm_builder
    ///         .add_local_child(
    ///             "activity_service",
    ///             move |handles| Box::pin(mock_activity_service.clone().run(handles)),
    ///             ChildOptions::new(),
    ///         )
    ///         .await
    ///         .unwrap();
    ///
    pub async fn run(self: Arc<Self>, handles: LocalComponentHandles) -> Result<(), anyhow::Error> {
        self.run_inner(handles.outgoing_dir).await
    }

    async fn run_inner(
        self: Arc<Self>,
        outgoing_dir: ServerEnd<DirectoryMarker>,
    ) -> Result<(), anyhow::Error> {
        let mut fs = ServiceFs::new();
        fs.dir("svc").add_fidl_service(move |mut stream: factivity::ProviderRequestStream| {
            let this = self.clone();
            fasync::Task::local(async move {
                info!("MockActivityService: new connection");
                let factivity::ProviderRequest::WatchState { listener, .. } =
                    stream.try_next().await.unwrap().unwrap();
                info!("MockActivityService: received WatchState request");
                let listener = listener.into_proxy().unwrap();
                while let Some(state) = this.state_receiver.lock().await.next().await {
                    info!("MockActivityService: sending activity state: {:?}", state);
                    let _ = listener.on_state_changed(state, 0).await;
                }
                info!("MockActivityService: closing connection")
            })
            .detach();
        });

        fs.serve_connection(outgoing_dir).unwrap();
        fs.collect::<()>().await;

        Ok(())
    }

    pub async fn set_activity_state(&self, state: factivity::State) {
        info!("MockActivityService: set activity state: {:?}", state);
        self.state_sender.lock().await.try_send(state).expect("try_send() failed");
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, assert_matches::assert_matches,
        fuchsia_component::client::connect_to_protocol_at_dir_svc,
    };

    #[fuchsia::test]
    async fn test_set_activity_state() {
        // Create and serve the mock service
        let (dir, outgoing_dir) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let mock = MockActivityService::new();
        let _task = fasync::Task::local(mock.clone().run_inner(outgoing_dir));

        // Connect to the mock server
        let provider_client =
            connect_to_protocol_at_dir_svc::<factivity::ProviderMarker>(&dir).unwrap();

        // Call the server's `watch_state` method, providing a Listener client end
        let (listener_client, mut listener_stream) =
            fidl::endpoints::create_request_stream::<factivity::ListenerMarker>().unwrap();
        provider_client.watch_state(listener_client).unwrap();

        // Set `Active` state on the mock and verify the listener sees the correct state
        mock.set_activity_state(factivity::State::Active).await;
        let factivity::ListenerRequest::OnStateChanged { state, responder, .. } =
            listener_stream.next().await.unwrap().unwrap();
        assert_matches!(responder.send(), Ok(()));
        assert_eq!(state, factivity::State::Active);

        // Set `Idle` state on the mock and verify the listener sees the correct state
        mock.set_activity_state(factivity::State::Idle).await;
        let factivity::ListenerRequest::OnStateChanged { state, responder, .. } =
            listener_stream.next().await.unwrap().unwrap();
        assert_matches!(responder.send(), Ok(()));
        assert_eq!(state, factivity::State::Idle);
    }
}
