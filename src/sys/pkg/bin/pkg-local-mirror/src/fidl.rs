// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::local_mirror_manager::LocalMirrorManager,
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_pkg::{LocalMirrorRequest, LocalMirrorRequestStream},
    fidl_fuchsia_pkg_ext::RepositoryUrl,
    fuchsia_component::server::{ServiceFs, ServiceObjLocal},
    fuchsia_syslog::fx_log_err,
    futures::prelude::*,
    std::convert::TryFrom,
};

pub enum IncomingService {
    LocalMirror(LocalMirrorRequestStream),
}

pub struct FidlServer {
    manager: LocalMirrorManager,
}

impl FidlServer {
    pub fn new(manager: LocalMirrorManager) -> Self {
        Self { manager }
    }

    /// Runs the FIDL Server.
    pub async fn run(self, mut fs: ServiceFs<ServiceObjLocal<'_, IncomingService>>) {
        fs.dir("svc").add_fidl_service(IncomingService::LocalMirror);

        fs.for_each_concurrent(None, |incoming_service| {
            self.handle_client(incoming_service)
                .unwrap_or_else(|e| fx_log_err!("while handling client: {:#}", anyhow!(e)))
        })
        .await
    }

    /// Handles an incoming FIDL connection from a client.
    async fn handle_client(&self, incoming_service: IncomingService) -> Result<(), Error> {
        match incoming_service {
            IncomingService::LocalMirror(mut stream) => {
                while let Some(request) =
                    stream.try_next().await.context("while receiving LocalMirror request")?
                {
                    self.handle_local_mirror_request(request).await?;
                }
            }
        }
        Ok(())
    }

    /// Handles fuchsia.pkg/LocalMirror requests.
    async fn handle_local_mirror_request(&self, request: LocalMirrorRequest) -> Result<(), Error> {
        match request {
            LocalMirrorRequest::GetMetadata { repo_url, path, metadata, responder } => {
                let url = RepositoryUrl::try_from(&repo_url)
                    .with_context(|| format!("while parsing repo url: {:?}", repo_url.url))?;
                let mut response = self.manager.get_metadata(url, &path, metadata).await;
                let () =
                    responder.send(&mut response).context("while sending GetMetadata response")?;
            }
            LocalMirrorRequest::GetBlob { .. } => {
                todo!("fxbug.dev/59192: Implement me!");
            }
        }
        Ok(())
    }
}
