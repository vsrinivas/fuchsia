// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::LocalComponentHandles,
    futures::{channel::mpsc, lock::Mutex, StreamExt, TryStreamExt},
    std::sync::Arc,
    tracing::*,
};

/// Mocks the fuchsia.sys2.SystemController service to be used in integration tests.
pub struct MockSystemControllerService {
    shutdown_received_sender: Mutex<mpsc::Sender<()>>,
    shutdown_received_receiver: Mutex<mpsc::Receiver<()>>,
}

impl MockSystemControllerService {
    pub fn new() -> Arc<MockSystemControllerService> {
        let (sender, receiver) = mpsc::channel(1);
        Arc::new(Self {
            shutdown_received_sender: Mutex::new(sender),
            shutdown_received_receiver: Mutex::new(receiver),
        })
    }

    /// Runs the mock using the provided `LocalComponentHandles`.
    ///
    /// The mock intentionally does not complete shutdown requests in order to more closely mimic
    /// the behavior that Power Manager would normally see.
    ///
    /// Expected usage is to call this function from a closure for the
    /// `local_component_implementation` parameter to `RealmBuilder.add_local_child`.
    ///
    /// For example:
    ///     let mock_system_controller_service = MockInputSettingsService::new();
    ///     let system_controller_service_child = realm_builder
    ///         .add_local_child(
    ///             "system_controller_service",
    ///             move |handles| {
    ///                 Box::pin(system_controller_service.clone().run(handles))
    ///             },
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
        fs.dir("svc").add_fidl_service(move |mut stream: fsys::SystemControllerRequestStream| {
            let this = self.clone();
            fasync::Task::local(async move {
                info!("MockSystemControllerService: new connection");
                while let Some(fsys::SystemControllerRequest::Shutdown { .. }) =
                    stream.try_next().await.unwrap()
                {
                    info!("MockSystemControllerService: received shutdown request");
                    this.shutdown_received_sender
                        .lock()
                        .await
                        .try_send(())
                        .expect("Failed to notify shutdown");
                }
            })
            .detach();
        });

        fs.serve_connection(outgoing_dir).unwrap();
        fs.collect::<()>().await;

        Ok(())
    }

    /// Waits for the mock to receive a fidl.fuchsia.SystemController/Shutdown request.
    pub async fn wait_for_shutdown_request(&self) {
        self.shutdown_received_receiver
            .lock()
            .await
            .next()
            .await
            .expect("Failed to wait for shutdown request")
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_component::client::connect_to_protocol_at_dir_svc};

    #[fuchsia::test]
    async fn test_shutdown() {
        // Create and serve the mock service
        let (dir, outgoing_dir) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let mock = MockSystemControllerService::new();
        let _task = fasync::Task::local(mock.clone().run_inner(outgoing_dir));

        // Connect to the mock server
        let controller_client =
            connect_to_protocol_at_dir_svc::<fsys::SystemControllerMarker>(&dir).unwrap();

        // Call the server's `shutdown` method and verify the mock sees the request
        let _task = fuchsia_async::Task::local(controller_client.shutdown());
        mock.wait_for_shutdown_request().await;
    }
}
