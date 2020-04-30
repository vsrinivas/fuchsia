// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod pkgfs;
use {
    crate::pkgfs::Pkgfs, fidl::endpoints::ClientEnd, fidl_fuchsia_io::DirectoryMarker,
    thiserror::Error,
};

#[derive(Debug, Error)]
pub enum UpdateError {
    #[error("error launching pkgfs")]
    PkgfsLaunchError(#[source] anyhow::Error),
}

/// Installs all packages and writes the Fuchsia ZBI from the latest build on the given channel.
///
/// If successful, a reboot should be the only thing necessary to boot Fuchsia.
///
/// # Arguments
/// * `blobfs` - The root directory of the blobfs we are installing to. The blobfs must work, but
///     there is no requirement on the state of any blobs (i.e. an empty blobfs, or one with missing or
///     corrupt blobs is ok)
/// * `paver_connector` - a directory which contains a service file named fuchsia.paver.Paver
/// * `repository_config_file` - A json-serialized fidl_fuchsia_pkg_ext::RepositoryConfigs file
/// * `channel_name` - The channel to update from.
pub async fn download_and_apply_update(
    blobfs: ClientEnd<DirectoryMarker>,
    _paver_connector: ClientEnd<DirectoryMarker>,
    _repository_config_file: std::fs::File,
    _channel_name: &str,
) -> Result<(), UpdateError> {
    let _pkgfs = Pkgfs::launch(blobfs).map_err(UpdateError::PkgfsLaunchError)?;
    unimplemented!();
}
