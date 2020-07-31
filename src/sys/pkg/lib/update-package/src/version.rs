// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around reading the version file.

use {fidl_fuchsia_io::DirectoryProxy, thiserror::Error};

/// An error encountered while reading the version.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ReadVersionError {
    #[error("while opening the file: {0}")]
    OpenFile(#[from] io_util::node::OpenError),

    #[error("while reading the file: {0}")]
    ReadFile(#[from] io_util::file::ReadError),
}

pub(crate) async fn read_version(proxy: &DirectoryProxy) -> Result<String, ReadVersionError> {
    let file =
        io_util::directory::open_file(proxy, "version", fidl_fuchsia_io::OPEN_RIGHT_READABLE)
            .await?;
    Ok(io_util::file::read_to_string(&file).await?)
}

#[cfg(test)]
mod tests {
    use {super::*, crate::TestUpdatePackage, fuchsia_async as fasync, matches::assert_matches};

    #[fasync::run_singlethreaded(test)]
    async fn read_version_success_file_exists() {
        let p = TestUpdatePackage::new().add_file("version", "123").await;
        assert_eq!(p.version().await.unwrap(), "123");
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_version_fail_file_does_not_exist() {
        let p = TestUpdatePackage::new();
        assert_matches!(p.version().await, Err(ReadVersionError::OpenFile(_)));
    }
}
