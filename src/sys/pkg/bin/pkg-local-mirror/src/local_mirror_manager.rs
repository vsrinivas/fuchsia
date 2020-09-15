// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fdio, DirectoryProxy, FileMarker},
    fidl_fuchsia_pkg::{GetBlobError, GetMetadataError},
    fidl_fuchsia_pkg_ext::{BlobId, RepositoryUrl},
    fuchsia_syslog::fx_log_info,
};

pub struct LocalMirrorManager {
    blobs_dir: DirectoryProxy,
    metadata_dir: DirectoryProxy,
}

impl LocalMirrorManager {
    pub async fn new(usb_dir: &DirectoryProxy) -> Result<Self, Error> {
        let blobs_dir = io_util::directory::open_directory(
            usb_dir,
            "blobs",
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        )
        .await
        .context("while opening blobs dir")?;

        let metadata_dir = io_util::directory::open_directory(
            usb_dir,
            "repository_metadata",
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        )
        .await
        .context("while opening metadata dir")?;

        Ok(Self { blobs_dir, metadata_dir })
    }

    /// Connects the file in the USB metadata directory to the passed-in handle.
    /// Note we don't need to validate the path because the open calls are forwarded
    /// to a directory that only contains metadata, and Fuchsia filesystems prevent
    /// accessing data that is outside the directory.
    /// https://fuchsia.dev/fuchsia-src/concepts/filesystems/dotdot?hl=en
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
                fx_log_info!("while opening metadata {}: {:#}", path, anyhow!(e));
                GetMetadataError::ErrorOpeningMetadata
            })?;

        Ok(())
    }

    /// Connects the file in the USB blobs directory to the passed-in handle.
    pub async fn get_blob(
        &self,
        blob_id: BlobId,
        blob: ServerEnd<FileMarker>,
    ) -> Result<(), GetBlobError> {
        let blob_id = blob_id.to_string();
        let (first, last) = blob_id.split_at(2);
        let path = format!("{}/{}", first, last);

        let () = self
            .blobs_dir
            .open(
                fdio::OPEN_RIGHT_READABLE | fdio::OPEN_FLAG_DESCRIBE,
                fdio::MODE_TYPE_FILE,
                &path,
                ServerEnd::new(blob.into_channel()),
            )
            .map_err(|e| {
                fx_log_info!("while opening blob {}: {:#}", path, anyhow!(e));
                GetBlobError::ErrorOpeningBlob
            })?;

        Ok(())
    }
}
