// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fdio, DirectoryProxy, FileMarker},
    fidl_fuchsia_pkg::GetMetadataError,
    fidl_fuchsia_pkg_ext::RepositoryUrl,
    fuchsia_syslog::fx_log_info,
};

pub struct LocalMirrorManager {
    metadata_dir: DirectoryProxy,
}

impl LocalMirrorManager {
    pub fn new(metadata_dir: DirectoryProxy) -> Self {
        Self { metadata_dir }
    }

    /// Connects the file in the USB metadata directory to the passed-in handle.
    /// Note we don't need to validate the path because the Open handler will
    /// reject invalid path segments, which will surface in OnOpen info. For context:
    /// https://fuchsia.dev/fuchsia-src/concepts/framework/namespaces?hl=en#client_interpreted_path_expressions.
    pub async fn get_metadata(
        &self,
        repo_url: RepositoryUrl,
        path: &str,
        metadata: ServerEnd<FileMarker>,
    ) -> Result<(), GetMetadataError> {
        let path = format!("{}/{}", repo_url.url().host(), path);
        let () = self
            .metadata_dir
            .open(
                fdio::OPEN_RIGHT_READABLE | fdio::OPEN_FLAG_DESCRIBE,
                fdio::MODE_TYPE_FILE,
                &path,
                ServerEnd::new(metadata.into_channel()),
            )
            .map_err(|e| {
                fx_log_info!("while opening {}: {:#}", path, anyhow!(e));
                GetMetadataError::ErrorOpeningMetadata
            })?;

        Ok(())
    }
}
