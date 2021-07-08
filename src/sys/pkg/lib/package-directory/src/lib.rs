// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::FileProxy, fuchsia_archive::AsyncReader, fuchsia_pkg::MetaContents,
    std::collections::HashMap, thiserror::Error,
};

#[derive(Clone, Debug, PartialEq)]
struct MetaFileLocation {
    offset: u64,
    length: u64,
}

#[allow(dead_code)]
struct RootDir {
    blobfs: blobfs::Client,
    hash: fuchsia_hash::Hash,
    meta_far: FileProxy,
    meta_files: HashMap<String, MetaFileLocation>,
    non_meta_files: HashMap<String, fuchsia_hash::Hash>,
    // Once populated, this option must never be cleared.
    meta_far_vmo: parking_lot::RwLock<Option<fidl_fuchsia_mem::Buffer>>,
}

#[derive(Error, Debug)]
pub enum RootDirError {
    #[error("while opening a node")]
    OpenMetaFar(#[source] io_util::node::OpenError),

    #[error("while instantiating a fuchsia archive reader")]
    ArchiveReader(#[source] fuchsia_archive::Error),

    #[error("while reading meta/contents")]
    ReadMetaContents(#[source] fuchsia_archive::Error),

    #[error("while deserializing meta/contents")]
    DeserializeMetaContents(#[source] fuchsia_pkg::MetaContentsError),
}

#[allow(dead_code)]
impl RootDir {
    pub async fn new(
        blobfs: blobfs::Client,
        hash: fuchsia_hash::Hash,
    ) -> Result<Self, RootDirError> {
        let meta_far =
            blobfs.open_blob_for_read_no_describe(&hash).map_err(RootDirError::OpenMetaFar)?;

        let reader = io_util::file::AsyncFile::from_proxy(Clone::clone(&meta_far));
        let mut async_reader =
            AsyncReader::new(reader).await.map_err(RootDirError::ArchiveReader)?;
        let reader_list = async_reader.list();

        let mut meta_files = HashMap::with_capacity(reader_list.size_hint().0);

        for entry in reader_list {
            if entry.path().starts_with("meta/") {
                meta_files.insert(
                    String::from(entry.path()),
                    MetaFileLocation { offset: entry.offset(), length: entry.length() },
                );
            }
        }

        let meta_contents_bytes = async_reader
            .read_file("meta/contents")
            .await
            .map_err(RootDirError::ReadMetaContents)?;

        let non_meta_files: HashMap<_, _> = MetaContents::deserialize(&meta_contents_bytes[..])
            .map_err(RootDirError::DeserializeMetaContents)?
            .into_contents()
            .into_iter()
            .collect();

        let meta_far_vmo = parking_lot::RwLock::new(None);

        Ok(RootDir { blobfs, hash, meta_far, meta_files, non_meta_files, meta_far_vmo })
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_io::DirectoryMarker, fuchsia_pkg_testing::PackageBuilder};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn lifecycle() {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let blobid = fuchsia_hash::Hash::from([0u8; 32]);
        let blobfs_client = blobfs::Client::new(proxy);

        drop(server_end);

        let _ = RootDir::new(blobfs_client, blobid).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn check_fields_meta_far_only() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");

        let (metafar_blob, _) = package.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        let meta_files: HashMap<String, MetaFileLocation> = [
            (String::from("meta/contents"), MetaFileLocation { offset: 4096, length: 0 }),
            (String::from("meta/package"), MetaFileLocation { offset: 4096, length: 38 }),
        ]
        .iter()
        .cloned()
        .collect();

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();

        assert_eq!(root_dir.meta_files, meta_files);
        assert_eq!(root_dir.non_meta_files, HashMap::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn check_fields() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();

        let (blobfs_fake, blobfs_client) = fuchsia_pkg_testing::blobfs::Fake::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }

        let meta_files: HashMap<String, MetaFileLocation> = [
            (String::from("meta/contents"), MetaFileLocation { offset: 4096, length: 74 }),
            (String::from("meta/package"), MetaFileLocation { offset: 12288, length: 39 }),
            (String::from("meta/file"), MetaFileLocation { offset: 8192, length: 9 }),
        ]
        .iter()
        .cloned()
        .collect();

        let non_meta_files: HashMap<String, fuchsia_hash::Hash> = [(
            String::from("resource"),
            "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"
                .parse::<fuchsia_hash::Hash>()
                .unwrap(),
        )]
        .iter()
        .cloned()
        .collect();

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();

        assert_eq!(root_dir.meta_files, meta_files);
        assert_eq!(root_dir.non_meta_files, non_meta_files);
    }
}
