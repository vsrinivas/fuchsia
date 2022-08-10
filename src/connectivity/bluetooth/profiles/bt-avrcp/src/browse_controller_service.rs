// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth_avrcp::*,
    fidl_fuchsia_bluetooth_avrcp_test::*,
    fuchsia_async as fasync,
    futures::{
        self,
        future::{FutureExt, TryFutureExt},
        stream::TryStreamExt,
    },
    tracing::warn,
};

use crate::peer::Controller;

/// FIDL wrapper for a internal PeerController for browse-related tasks.
struct BrowseControllerService {
    /// Handle to internal controller client for the remote peer.
    controller: Controller,

    /// Incoming FIDL request stream from the FIDL client.
    fidl_stream: BrowseControllerRequestStream,
}

impl BrowseControllerService {
    fn new(controller: Controller, fidl_stream: BrowseControllerRequestStream) -> Self {
        Self { controller, fidl_stream }
    }

    async fn handle_fidl_request(&mut self, request: BrowseControllerRequest) -> Result<(), Error> {
        match request {
            BrowseControllerRequest::GetFileSystemItems {
                start_index,
                end_index,
                attribute_option,
                responder,
            } => responder.send(
                &mut self
                    .controller
                    .get_file_system_items(start_index, end_index, attribute_option)
                    .await,
            )?,
            BrowseControllerRequest::GetMediaPlayerItems { start_index, end_index, responder } => {
                responder.send(
                    &mut self.controller.get_media_player_items(start_index, end_index).await,
                )?
            }
            BrowseControllerRequest::GetNowPlayingItems {
                start_index,
                end_index,
                attribute_option,
                responder,
            } => responder.send(
                &mut self
                    .controller
                    .get_now_playing_items(start_index, end_index, attribute_option)
                    .await,
            )?,
            BrowseControllerRequest::SetBrowsedPlayer { player_id, responder } => responder
                .send(&mut self.controller.set_browsed_player(player_id).await.map(Into::into))?,
            BrowseControllerRequest::ChangePath { responder, .. } => {
                responder.send(&mut Err(BrowseControllerError::CommandNotImplemented))?
            }
            BrowseControllerRequest::PlayFileSystemItem { responder, .. } => {
                responder.send(&mut Err(BrowseControllerError::CommandNotImplemented))?
            }
            BrowseControllerRequest::PlayNowPlayingItem { responder, .. } => {
                responder.send(&mut Err(BrowseControllerError::CommandNotImplemented))?
            }
        };
        Ok(())
    }

    async fn run(&mut self) -> Result<(), Error> {
        while let Some(req) = self.fidl_stream.try_next().await? {
            self.handle_fidl_request(req).await?;
        }
        Ok(())
    }
}

/// FIDL wrapper for a internal PeerController for the test
/// (BrowseControllerExt) interface methods.
struct BrowseControllerExtService {
    controller: Controller,
    fidl_stream: BrowseControllerExtRequestStream,
}

impl BrowseControllerExtService {
    async fn handle_fidl_request(&self, request: BrowseControllerExtRequest) -> Result<(), Error> {
        match request {
            BrowseControllerExtRequest::IsConnected { responder } => {
                responder.send(self.controller.is_browse_connected())?;
            }
            BrowseControllerExtRequest::SendRawBrowseCommand { pdu_id, command, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .send_raw_browse_command(pdu_id, &command[..])
                        .map_err(|e| BrowseControllerError::from(e))
                        .await,
                )?;
            }
        };
        Ok(())
    }

    async fn run(&mut self) -> Result<(), Error> {
        while let Some(req) = self.fidl_stream.try_next().await? {
            self.handle_fidl_request(req).await?;
        }
        Ok(())
    }
}

/// Spawns a future that facilitates communication between a PeerController and a FIDL client.
pub fn spawn_service(
    controller: Controller,
    fidl_stream: BrowseControllerRequestStream,
) -> fasync::Task<()> {
    fasync::Task::spawn(
        async move {
            let mut acc = BrowseControllerService::new(controller, fidl_stream);
            acc.run().await?;
            Ok(())
        }
        .boxed()
        .unwrap_or_else(|e: anyhow::Error| {
            warn!("AVRCP client browse controller finished: {:?}", e)
        }),
    )
}

/// Spawns a future that facilitates communication between a PeerController and a test FIDL client.
pub fn spawn_ext_service(
    controller: Controller,
    fidl_stream: BrowseControllerExtRequestStream,
) -> fasync::Task<()> {
    fasync::Task::spawn(
        async move {
            let mut acc = BrowseControllerExtService { controller, fidl_stream };
            acc.run().await?;
            Ok(())
        }
        .boxed()
        .unwrap_or_else(|e: anyhow::Error| {
            warn!("AVRCP test client browse controller finished: {:?}", e)
        }),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::peer::RemotePeerHandle;
    use crate::peer_manager::TargetDelegate;
    use assert_matches::assert_matches;
    use async_test_helpers::run_while;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
    use fuchsia_bluetooth::types::PeerId;
    use pin_utils::pin_mut;
    use std::sync::Arc;

    fn setup() -> Controller {
        let (profile_proxy, mut _profile_requests) =
            create_proxy_and_stream::<ProfileMarker>().expect("should have initialized");
        let peer = RemotePeerHandle::spawn_peer(
            PeerId(0x1),
            Arc::new(TargetDelegate::new()),
            profile_proxy,
        );

        Controller::new(peer)
    }

    #[fuchsia::test]
    /// Tests that the client stream handler will spawn a controller when a controller request
    /// successfully sets up a controller.
    fn run_client() {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");

        // Set up for testing.
        let controller = setup();

        // Initialize client.
        let (bc_proxy, bc_server) =
            create_proxy_and_stream::<BrowseControllerMarker>().expect("Controller proxy creation");
        let mut client = BrowseControllerService::new(controller, bc_server);
        let run_fut = client.run();
        pin_mut!(run_fut);

        // Verify that the client can process a request.
        let request_fut = bc_proxy.set_browsed_player(0);
        let (_, mut run_fut) = run_while(&mut exec, run_fut, request_fut);

        // Verify that the client is still running.
        assert!(exec.run_until_stalled(&mut run_fut).is_pending());

        // The handler should end when the client is closed.
        drop(bc_proxy);
        assert_matches!(exec.run_until_stalled(&mut run_fut), futures::task::Poll::Ready(Ok(())));
    }

    /// Tests that the client stream handler will spawn a controller when a controller request
    /// successfully sets up a controller.
    #[fuchsia::test]
    fn run_ext_client() {
        let mut exec = fasync::TestExecutor::new().expect("TestExecutor should be created");

        // Set up testing.
        let controller = setup();

        // Initialize client.
        let (bc_proxy, bc_server) = create_proxy_and_stream::<BrowseControllerExtMarker>()
            .expect("Controller proxy creation");
        let mut client = BrowseControllerExtService { controller, fidl_stream: bc_server };

        let run_fut = client.run();
        pin_mut!(run_fut);

        // Verify that the client can process a request.
        let request_fut = bc_proxy.is_connected();
        let (_, mut run_fut) = run_while(&mut exec, run_fut, request_fut);

        // Verify that the client is still running.
        assert!(exec.run_until_stalled(&mut run_fut).is_pending());

        // The handler should end when the client is closed.
        drop(bc_proxy);
        assert_matches!(exec.run_until_stalled(&mut run_fut), futures::task::Poll::Ready(Ok(())));
    }
}
