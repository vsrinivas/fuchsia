// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around verifying the board file.

use {fidl_fuchsia_io::DirectoryProxy, fuchsia_zircon::Status, thiserror::Error};

/// An error encountered while verifying the board.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum VerifyBoardError {
    #[error("while opening the file: {0}")]
    OpenFile(#[from] io_util::node::OpenError),

    #[error("while reading the file: {0}")]
    ReadFile(#[from] io_util::file::ReadError),

    #[error("expected board name {} found {}", expected, found)]
    VerifyContents { expected: String, found: String },
}

pub(crate) async fn verify_board(
    proxy: &DirectoryProxy,
    expected_contents: &str,
) -> Result<(), VerifyBoardError> {
    let file =
        match io_util::directory::open_file(proxy, "board", fidl_fuchsia_io::OPEN_RIGHT_READABLE)
            .await
        {
            Ok(file) => Ok(file),
            Err(io_util::node::OpenError::OpenError(Status::NOT_FOUND)) => return Ok(()),
            Err(e) => Err(e),
        }?;

    let contents = io_util::file::read_to_string(&file).await?;

    if expected_contents != contents {
        return Err(VerifyBoardError::VerifyContents {
            expected: expected_contents.to_string(),
            found: contents,
        });
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, crate::TestUpdatePackage, fuchsia_async as fasync, matches::assert_matches};

    #[fasync::run_singlethreaded(test)]
    async fn verify_board_success_file_exists() {
        let p = TestUpdatePackage::new().add_file("board", "kourtney").await;
        assert_matches!(p.verify_board("kourtney").await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_board_success_file_does_not_exist() {
        let p = TestUpdatePackage::new();
        assert_matches!(p.verify_board("kim").await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_board_failure_verify_contents() {
        let p = TestUpdatePackage::new().add_file("board", "khloe").await;
        assert_matches!(
            p.verify_board("kendall").await,
            Err(VerifyBoardError::VerifyContents { expected, found })
                if expected=="kendall" && found=="khloe"
        );
    }
}
