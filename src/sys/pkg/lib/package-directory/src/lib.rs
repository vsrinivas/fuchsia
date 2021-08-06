// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    std::{convert::TryInto as _, sync::Arc},
    vfs::directory::connection::io1::DerivedConnection as _,
};

mod filesystem;
mod meta_file;
mod root_dir;

pub use filesystem::Filesystem;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("while opening the meta.far")]
    OpenMetaFar(#[source] io_util::node::OpenError),

    #[error("while instantiating a fuchsia archive reader")]
    ArchiveReader(#[source] fuchsia_archive::Error),

    #[error("while reading meta/contents")]
    ReadMetaContents(#[source] fuchsia_archive::Error),

    #[error("while deserializing meta/contents")]
    DeserializeMetaContents(#[source] fuchsia_pkg::MetaContentsError),
}

/// Serves a package directory for the package with hash `meta_far` on `server_end`.
/// The connection rights are set by `flags`, used the same as the `flags` parameter of
///   fuchsia.io/Directory.Open.
/// Consider using an instance of `package_directory::Filesystem` for `filesystem`.
pub async fn serve(
    scope: vfs::execution_scope::ExecutionScope,
    blobfs: blobfs::Client,
    meta_far: fuchsia_hash::Hash,
    filesystem: Arc<dyn vfs::filesystem::Filesystem>,
    flags: u32,
    server_end: ServerEnd<DirectoryMarker>,
) -> Result<(), Error> {
    let () = vfs::directory::immutable::connection::io1::ImmutableConnection::create_connection(
        scope,
        vfs::directory::connection::util::OpenDirectory::new(Arc::new(
            root_dir::RootDir::new(blobfs, meta_far, filesystem).await?,
        )),
        flags,
        server_end.into_channel().into(),
    );
    Ok(())
}

fn usize_to_u64_safe(u: usize) -> u64 {
    let ret: u64 = u.try_into().unwrap();
    static_assertions::assert_eq_size_val!(u, ret);
    ret
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        std::sync::Arc,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve() {
        let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(3 * 4096));

        crate::serve(
            vfs::execution_scope::ExecutionScope::new(),
            blobfs_client,
            metafar_blob.merkle,
            filesystem,
            fidl_fuchsia_io::OPEN_RIGHT_READABLE,
            server_end,
        )
        .await
        .unwrap();

        assert_eq!(
            files_async::readdir(&proxy).await.unwrap(),
            vec![files_async::DirEntry {
                name: "meta".to_string(),
                kind: files_async::DirentKind::Directory
            }]
        );
    }
}
