// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::local_mirror_manager::LocalMirrorManager,
    anyhow::{Context as _, Error},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_pkg::{LocalMirrorRequest, LocalMirrorRequestStream},
    fidl_fuchsia_pkg_ext::RepositoryUrl,
    futures::stream::TryStreamExt as _,
    std::convert::TryFrom,
};

mod local_mirror_manager;

/// An implementation of fuchsia.pkg/LocalMirror.
pub struct PkgLocalMirror {
    manager: LocalMirrorManager,
}

impl PkgLocalMirror {
    pub async fn new(usb_dir: &DirectoryProxy) -> Result<Self, Error> {
        Ok(Self { manager: LocalMirrorManager::new(usb_dir).await? })
    }

    /// Handle a fuchsia.pkg/LocalMirror request stream.
    pub async fn handle_request_stream(
        &self,
        mut stream: LocalMirrorRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) =
            stream.try_next().await.context("receiving LocalMirror request")?
        {
            self.handle_request(request).await?;
        }
        Ok(())
    }

    async fn handle_request(&self, request: LocalMirrorRequest) -> Result<(), Error> {
        match request {
            LocalMirrorRequest::GetMetadata { repo_url, path, metadata, responder } => {
                let url = RepositoryUrl::try_from(&repo_url)
                    .with_context(|| format!("parsing repo url: {:?}", repo_url.url))?;
                let mut response = self.manager.get_metadata(url, &path, metadata).await;
                let () = responder.send(&mut response).context("sending GetMetadata response")?;
            }
            LocalMirrorRequest::GetBlob { blob_id, blob, responder } => {
                let mut response = self.manager.get_blob(blob_id.into(), blob).await;
                let () = responder.send(&mut response).context("sending GetBlob response")?;
            }
        }
        Ok(())
    }
}
