// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use fidl_fuchsia_ldsvc as fldsvc;
use fuchsia_zircon as zx;
use std::sync::Arc;
use tracing::warn;

// When this feature is enabled, process-resolver will use fuchsia.pkg.PackageResolver
// to resolve packages from the universe set.
#[cfg(feature = "auto_update_packages")]
mod auto_update;
#[cfg(feature = "auto_update_packages")]
pub use auto_update::serve;
#[cfg(feature = "auto_update_packages")]
pub const AUTO_UPDATE_ENABLED: bool = true;

// When this feature is disabled, process-resolver will use the /pkgfs/packages directory
// to resolve packages from the base set only.
#[cfg(not(feature = "auto_update_packages"))]
mod base_set_only;
#[cfg(not(feature = "auto_update_packages"))]
pub use base_set_only::serve;
#[cfg(not(feature = "auto_update_packages"))]
pub const AUTO_UPDATE_ENABLED: bool = false;

pub async fn get_binary_and_loader_from_pkg_dir(
    pkg_dir: &fio::DirectoryProxy,
    bin_path: &str,
    pkg_url: &str,
) -> Result<
    (fidl::Vmo, Option<fidl::endpoints::ClientEnd<fidl_fuchsia_ldsvc::LoaderMarker>>),
    zx::Status,
> {
    // Open the binary from the package dir as an executable VMO
    let binary = fuchsia_fs::directory::open_file(
        pkg_dir,
        bin_path,
        fio::OpenFlags::RIGHT_EXECUTABLE | fio::OpenFlags::RIGHT_READABLE,
    )
    .await
    .map_err(|e| {
        if let fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND) = e {
            zx::Status::NOT_FOUND
        } else {
            warn!("Could not open {} in {}: {:?}", bin_path, pkg_url, e);
            zx::Status::IO
        }
    })?;
    let bin_vmo = binary
        .get_backing_memory(fio::VmoFlags::EXECUTE | fio::VmoFlags::READ)
        .await
        .map_err(|_| zx::Status::INTERNAL)?
        .map_err(|e| {
            warn!("Could not get a VMO for {} in {}: {:?}", bin_path, pkg_url, e);
            zx::Status::IO
        })?;

    // Construct a loader from the package library dir
    let ldsvc = match fuchsia_fs::directory::open_directory(
        pkg_dir,
        "lib",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await
    {
        Ok(lib_dir) => {
            let (ldsvc, server_end) =
                fidl::endpoints::create_endpoints::<fldsvc::LoaderMarker>().unwrap();
            let server_end = server_end.into_channel();
            library_loader::start(Arc::new(lib_dir), server_end);
            Some(ldsvc)
        }
        Err(e) => {
            warn!("Could not open /lib dir of {}: {:?}", pkg_url, e);
            None
        }
    };

    Ok((bin_vmo, ldsvc))
}
