// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::{Status, VmoChildOptions},
    thiserror::Error,
};

/// An error encountered while opening an image.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum OpenImageError {
    #[error("while opening the file: {0}")]
    OpenFile(#[from] io_util::node::OpenError),

    #[error("while calling get_buffer: {0}")]
    FidlGetBuffer(#[source] fidl::Error),

    #[error("while obtaining vmo of file: {0}")]
    GetBuffer(fuchsia_zircon::Status),

    #[error("remote reported success without providing a vmo")]
    MissingBuffer,

    #[error("while converting vmo to a resizable vmo: {0}")]
    CloneBuffer(fuchsia_zircon::Status),
}

pub(crate) async fn open(proxy: &DirectoryProxy, name: &str) -> Result<Buffer, OpenImageError> {
    let file =
        io_util::directory::open_file(proxy, name, fidl_fuchsia_io::OPEN_RIGHT_READABLE).await?;

    let (status, buffer) = file
        .get_buffer(fidl_fuchsia_io::VMO_FLAG_READ)
        .await
        .map_err(OpenImageError::FidlGetBuffer)?;
    Status::ok(status).map_err(OpenImageError::GetBuffer)?;
    let buffer = buffer.ok_or(OpenImageError::MissingBuffer)?;

    // The paver service requires VMOs that are resizable, and blobfs does not give out resizable
    // VMOs. Fortunately, a copy-on-write child clone of the vmo can be made resizable.
    let vmo = buffer
        .vmo
        .create_child(VmoChildOptions::COPY_ON_WRITE | VmoChildOptions::RESIZABLE, 0, buffer.size)
        .map_err(OpenImageError::CloneBuffer)?;

    Ok(Buffer { size: buffer.size, vmo })
}

#[cfg(test)]
mod tests {
    use {super::*, crate::UpdatePackage, matches::assert_matches};

    const TEST_PATH: &str = "test/update-package-lib-test";
    const TEST_PATH_IN_NAMESPACE: &str = "/pkg/test/update-package-lib-test";

    fn open_this_package_as_update_package() -> UpdatePackage {
        let pkg = io_util::directory::open_in_namespace(
            "/pkg",
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        )
        .unwrap();

        UpdatePackage::new(pkg)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_present_image_succeeds() {
        let pkg = open_this_package_as_update_package();

        assert_matches!(pkg.open_image(TEST_PATH).await, Ok(_));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_missing_image_fails() {
        let pkg = open_this_package_as_update_package();

        assert_matches!(
            pkg.open_image("missing").await,
            Err(OpenImageError::OpenFile(io_util::node::OpenError::OpenError(Status::NOT_FOUND)))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_image_buffer_matches_expected_contents() {
        let pkg = open_this_package_as_update_package();

        let buffer = pkg.open_image(TEST_PATH).await.unwrap();

        let expected = std::fs::read(TEST_PATH_IN_NAMESPACE).unwrap();
        assert_eq!(expected.len() as u64, buffer.size);

        let mut actual = vec![0; buffer.size as usize];
        buffer.vmo.read(&mut actual[..], 0).unwrap();

        assert_eq!(expected, actual);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_image_buffer_is_resizable() {
        let pkg = open_this_package_as_update_package();

        let buffer = pkg.open_image(TEST_PATH).await.unwrap();

        assert_eq!(buffer.vmo.set_size(buffer.size * 2), Ok(()));
    }
}
