// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd, fidl_fuchsia_io::DirectoryMarker, std::sync::Arc,
    vfs::directory::connection::io1::DerivedConnection as _,
};

mod meta_file;
mod root_dir;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("while opening a node")]
    OpenMetaFar(#[source] io_util::node::OpenError),

    #[error("while instantiating a fuchsia archive reader")]
    ArchiveReader(#[source] fuchsia_archive::Error),

    #[error("while reading meta/contents")]
    ReadMetaContents(#[source] fuchsia_archive::Error),

    #[error("while deserializing meta/contents")]
    DeserializeMetaContents(#[source] fuchsia_pkg::MetaContentsError),
}

pub async fn serve(
    scope: vfs::execution_scope::ExecutionScope,
    blobfs: blobfs::Client,
    meta_far: fuchsia_hash::Hash,
    flags: u32,
    server_end: ServerEnd<DirectoryMarker>,
) -> Result<(), Error> {
    let () = vfs::directory::immutable::connection::io1::ImmutableConnection::create_connection(
        scope,
        vfs::directory::connection::util::OpenDirectory::new(Arc::new(
            root_dir::RootDir::new(blobfs, meta_far).await?,
        )),
        flags,
        server_end.into_channel().into(),
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn serve() {
        let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        crate::serve(
            vfs::execution_scope::ExecutionScope::new(),
            blobfs_client,
            metafar_blob.merkle,
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
