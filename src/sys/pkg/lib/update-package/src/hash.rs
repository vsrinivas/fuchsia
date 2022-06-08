// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_io as fio, fuchsia_hash::Hash, thiserror::Error};

/// An error encountered while extracting the package hash.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum HashError {
    #[error("opening the 'meta' file")]
    Open(#[source] fuchsia_fs::node::OpenError),

    #[error("reading the 'meta' file")]
    Read(#[source] fuchsia_fs::file::ReadError),

    #[error("parsing the 'meta' file")]
    Parse(#[source] fuchsia_hash::ParseHashError),
}

pub(crate) async fn hash(proxy: &fio::DirectoryProxy) -> Result<Hash, HashError> {
    let meta =
        fuchsia_fs::directory::open_file(proxy, "meta", fuchsia_fs::OpenFlags::RIGHT_READABLE)
            .await
            .map_err(HashError::Open)?;
    let contents = fuchsia_fs::file::read_to_string(&meta).await.map_err(HashError::Read)?;
    contents.parse::<Hash>().map_err(HashError::Parse)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        fuchsia_fs::directory::open_in_namespace,
        std::{fs::File, io::Write as _},
        tempfile::tempdir,
    };

    #[fasync::run_singlethreaded(test)]
    async fn open_error() {
        let temp_dir = tempdir().expect("/tmp to exist");
        let proxy = open_in_namespace(
            temp_dir.path().to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .expect("temp dir to open");

        assert_matches!(hash(&proxy).await, Err(HashError::Open(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn parse_error() {
        let temp_dir = tempdir().expect("/tmp to exist");
        File::create(temp_dir.path().join("meta")).unwrap();
        let proxy = open_in_namespace(
            temp_dir.path().to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .expect("temp dir to open");

        assert_matches!(hash(&proxy).await, Err(HashError::Parse(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn success() {
        let temp_dir = tempdir().expect("/tmp to exist");
        let mut meta = File::create(temp_dir.path().join("meta")).unwrap();
        let hex = "0000000000000000000000000000000000000000000000000000000000000000";
        meta.write_all(hex.as_bytes()).unwrap();
        let proxy = open_in_namespace(
            temp_dir.path().to_str().unwrap(),
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )
        .expect("temp dir to open");

        assert_matches!(hash(&proxy).await, Ok(hash) if hash == hex.parse().unwrap());
    }
}
