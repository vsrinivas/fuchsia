// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_update::{CommitStatusProviderRequest, CommitStatusProviderRequestStream},
    fuchsia_component::server::{ServiceFs, ServiceObjLocal},
    fuchsia_zircon::{self as zx, EventPair, HandleBased},
    futures::{channel::oneshot, future, prelude::*},
    std::sync::Arc,
    tracing::warn,
};

pub struct FidlServer {
    p_external: EventPair,
    // The blocker is shared to support multiple IsCurrentSystemCommitted calls while blocked.
    blocker: future::Shared<oneshot::Receiver<()>>,
}

impl FidlServer {
    pub fn new(p_external: EventPair, blocker: oneshot::Receiver<()>) -> Self {
        Self { p_external, blocker: blocker.shared() }
    }

    pub async fn run(server: Arc<Self>, mut fs: ServiceFs<ServiceObjLocal<'_, IncomingService>>) {
        fs.dir("svc").add_fidl_service(IncomingService::CommitStatusProvider);
        fs.for_each_concurrent(None, |incoming_service| match incoming_service {
            IncomingService::CommitStatusProvider(stream) => {
                Self::handle_commit_status_provider_request_stream(Arc::clone(&server), stream)
                    .unwrap_or_else(|e| {
                        warn!(
                        "error handling fuchsia.update/CommitStatusProvider request stream:  {:#}",
                        anyhow!(e)
                    )
                    })
            }
        })
        .await;
    }

    /// Handle a fuchsia.update/CommitStatusProvider request stream.
    async fn handle_commit_status_provider_request_stream(
        server: Arc<Self>,
        mut stream: CommitStatusProviderRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            stream.try_next().await.context("while receiving CommitStatusProvider request")?
        {
            let () =
                Self::handle_commit_status_provider_request(Arc::clone(&server), request).await?;
        }
        Ok(())
    }

    async fn handle_commit_status_provider_request(
        server: Arc<Self>,
        req: CommitStatusProviderRequest,
    ) -> Result<(), Error> {
        // The server should only unblock when either of these conditions are met:
        // * The system is committed on boot and p_external already has `USER_0` asserted.
        // * The system is pending commit and p_external does not have `USER_0` asserted.
        //
        // This ensures that consumers (e.g. the GC service) will always observe the `USER_0` signal
        // on p_external if the system is committed. Otherwise, there would be an edge case where
        // the system is committed and the consumer received the EventPair, but we haven't yet
        // asserted the signal on the EventPair.
        //
        // If there is an error with `put_metadata_in_happy_state`, the FIDL server will hang here
        // indefinitely. This is acceptable because we'll Soon™ reboot on error.
        let () = server.blocker.clone().await.context("while unblocking fidl server")?;

        let CommitStatusProviderRequest::IsCurrentSystemCommitted { responder } = req;

        responder
            .send(
                server
                    .p_external
                    .duplicate_handle(zx::Rights::BASIC)
                    .context("while duplicating p_external")?,
            )
            .context("while sending IsCurrentSystemCommitted response")
    }
}
pub enum IncomingService {
    CommitStatusProvider(CommitStatusProviderRequestStream),
}
