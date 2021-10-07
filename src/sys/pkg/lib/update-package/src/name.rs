// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around verifying the name of the update package.

use {
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_pkg::{MetaPackage, MetaPackageError},
    thiserror::Error,
};

/// An error encountered while verifying the board.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum VerifyNameError {
    #[error("while opening meta/package")]
    OpenMetaPackage(#[source] io_util::node::OpenError),

    #[error("while reading meta/package")]
    ReadMetaPackage(#[source] io_util::file::ReadError),

    #[error("while reading meta/package")]
    ParseMetaPackage(#[source] MetaPackageError),

    #[error("expected package name 'update/0' found '{0:?}'")]
    Invalid(MetaPackage),
}

pub(crate) async fn verify(proxy: &DirectoryProxy) -> Result<(), VerifyNameError> {
    let file =
        io_util::directory::open_file(proxy, "meta/package", fidl_fuchsia_io::OPEN_RIGHT_READABLE)
            .await
            .map_err(VerifyNameError::OpenMetaPackage)?;
    let contents = io_util::file::read(&file).await.map_err(VerifyNameError::ReadMetaPackage)?;

    let expected =
        MetaPackage::from_name_and_variant("update".parse().unwrap(), "0".parse().unwrap());

    let actual =
        MetaPackage::deserialize(&mut &contents[..]).map_err(VerifyNameError::ParseMetaPackage)?;

    if expected != actual {
        return Err(VerifyNameError::Invalid(actual));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {super::*, crate::TestUpdatePackage, matches::assert_matches};

    fn make_meta_package(name: &str, variant: &str) -> Vec<u8> {
        let meta_package =
            MetaPackage::from_name_and_variant(name.parse().unwrap(), variant.parse().unwrap());
        let mut bytes = vec![];
        let () = meta_package.serialize(&mut bytes).unwrap();
        bytes
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn allows_expected_name_and_variant() {
        assert_matches!(
            TestUpdatePackage::new()
                .add_file("meta/package", make_meta_package("update", "0"))
                .await
                .verify_name()
                .await,
            Ok(())
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn rejects_unexpected_name() {
        assert_matches!(
            TestUpdatePackage::new()
                .add_file("meta/package", make_meta_package("invalid", "0"))
                .await
                .verify_name()
                .await,
            Err(VerifyNameError::Invalid(actual))
                if actual == MetaPackage::from_name_and_variant("invalid".parse().unwrap(), "0".parse().unwrap())
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn rejects_unexpected_variant() {
        assert_matches!(
            TestUpdatePackage::new()
                .add_file("meta/package", make_meta_package("update", "42"))
                .await
                .verify_name()
                .await,
            Err(VerifyNameError::Invalid(actual))
                if actual == MetaPackage::from_name_and_variant("update".parse().unwrap(), "42".parse().unwrap())
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn rejects_invalid_meta_package() {
        assert_matches!(
            TestUpdatePackage::new().add_file("meta/package", "bad json").await.verify_name().await,
            Err(VerifyNameError::ParseMetaPackage(_))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn rejects_missing_meta_package() {
        assert_matches!(
            TestUpdatePackage::new().verify_name().await,
            Err(VerifyNameError::OpenMetaPackage(_)) | Err(VerifyNameError::ReadMetaPackage(_))
        );
    }
}
