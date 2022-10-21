// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::ServeNeededBlobsError, fidl_fuchsia_pkg as fpkg, fidl_fuchsia_pkg_ext as fpkg_ext,
    fuchsia_hash::Hash, futures::future::FutureExt as _, std::collections::HashSet,
};

/// fuchsia.pkg/PackageCache.Get helper for extracting needed blob hashes (content blobs and
/// subpackage meta.fars) from package_directory::RootDirs and sending them to a stream that can be
/// used to serve a BlobInfo iterator, like for fuchsia.pkg/NeededBlobs.GetMissingBlobs.
#[derive(Debug)]
pub(super) struct MissingBlobs<'a> {
    /// Unbounded because `self.visit()` is recursive, so a single call to `self.cache()` can
    /// result in unbounded writes to the channel (depending on how many uncached subpackage
    /// meta.fars are encountered). If the channel were bounded, calls to `self.cache()` could
    /// block, which would block `handle_open_blobs` and the PackageCache.Get client might be
    /// waiting on progress writing a blob before it reads more from the MissingBlobs iterator,
    /// which would deadlock the Get.
    /// Could be made bounded by using an explicit queue instead of having the recursion between
    /// self.visit <-> self.send_subpackages and then batching all the hashes into a single
    /// channel write.
    /// Should not be an issue with memory use because the channel will contain at most all the
    /// unique hashes of the package being cached.
    sender: futures::channel::mpsc::UnboundedSender<Vec<fpkg::BlobInfo>>,
    blobfs: blobfs::Client,
    /// The hashes (both content blobs and subpackage meta.fars) that have been sent with `sender`.
    sent_hashes: HashSet<Hash>,
    /// The subpackage meta.far hashes that have been sent with `sender`.
    sent_subpackages: HashSet<Hash>,
    /// The hashes in `sent_hashes` that have not yet been `self.cache(hash)`.
    sent_but_not_cached_hashes: HashSet<Hash>,
    /// Subpackage meta.far hashes that have had their RootDir's recursed over.
    /// May contain hashes that are not in `sent_subpackages` if the meta.far was already in
    /// blobfs.
    visited_subpackages: HashSet<Hash>,
    blob_recorder: Box<dyn BlobRecorder + 'a>,
    subpackages_config: crate::SubpackagesConfig,
}

enum RootDirOrHash<'a> {
    RootDir(&'a package_directory::RootDir<blobfs::Client>),
    Hash(Hash),
}

/// During the caching process, MissingBlobs will call `record` with all of a package's content and
/// subpackage blobs (regardless of whether the blobs are already cached).
/// There may be multiple calls to record and blobs may be recorded more than once.
/// The Future returned by `record` is guaranteed to complete before any of the recorded blobs are
/// sent to the paired receiver created by `MissingBlobs::new`.
/// In practice, this means that the returned Future can be used to protect blobs from GC before
/// the caching process (fed from the paired receiver) begins caching the blob.
pub(super) trait BlobRecorder: std::fmt::Debug + Send + Sync {
    fn record(
        &self,
        blobs: HashSet<Hash>,
    ) -> futures::future::BoxFuture<'_, Result<(), anyhow::Error>>;
}

impl<'a> MissingBlobs<'a> {
    pub(super) async fn new(
        blobfs: blobfs::Client,
        subpackages_config: crate::SubpackagesConfig,
        root_dir: &package_directory::RootDir<blobfs::Client>,
        blob_recorder: Box<dyn BlobRecorder + 'a>,
    ) -> Result<
        (MissingBlobs<'a>, futures::channel::mpsc::UnboundedReceiver<Vec<fpkg::BlobInfo>>),
        ServeNeededBlobsError,
    > {
        let (sender, recv) = futures::channel::mpsc::unbounded();
        let mut self_ = Self {
            sender,
            blobfs,
            sent_hashes: HashSet::new(),
            sent_subpackages: HashSet::new(),
            sent_but_not_cached_hashes: HashSet::new(),
            visited_subpackages: HashSet::new(),
            blob_recorder,
            subpackages_config,
        };
        let () = self_.visit(RootDirOrHash::RootDir(root_dir)).await?;
        let () = self_.close_sender_if_all_sent_subpackages_cached();
        Ok((self_, recv))
    }

    /// Indicate that `hash` has been successfully cached. If `hash` is the meta.far of a
    /// (possibly transitive) subpackage, `cache` will recurse over its subpackages and
    /// content blobs.
    pub(super) async fn cache(&mut self, hash: &Hash) -> Result<(), ServeNeededBlobsError> {
        if !self.sent_but_not_cached_hashes.remove(hash) {
            return Err(ServeNeededBlobsError::BlobNotNeeded(*hash));
        }
        if self.sent_subpackages.contains(hash) {
            let () = self.visit(RootDirOrHash::Hash(*hash)).await?;
        }
        let () = self.close_sender_if_all_sent_subpackages_cached();
        Ok(())
    }

    /// Send, if necessary, the content blobs and subpackage meta.fars of a package.
    /// Should be called at least once for each meta.far (the package proper and each
    /// (transitive) subpackage) in a package.
    /// Short-circuits if called on a meta.far more than once.
    async fn visit(
        &mut self,
        root_dir_or_hash: RootDirOrHash<'_>,
    ) -> Result<(), ServeNeededBlobsError> {
        let root_dir_storage;
        let root_dir = match root_dir_or_hash {
            RootDirOrHash::RootDir(root_dir) => {
                if !self.visited_subpackages.insert(*root_dir.hash()) {
                    return Ok(());
                }
                &root_dir
            }
            RootDirOrHash::Hash(hash) => {
                if !self.visited_subpackages.insert(hash) {
                    return Ok(());
                }
                root_dir_storage = package_directory::RootDir::new(self.blobfs.clone(), hash)
                    .await
                    .map_err(ServeNeededBlobsError::CreateSubpackageRootDir)?;
                &root_dir_storage
            }
        };

        if self.subpackages_config == crate::SubpackagesConfig::Enable {
            let () = self
                .send_subpackages(
                    root_dir
                        .subpackages()
                        .await
                        .map_err(ServeNeededBlobsError::ReadSubpackages)?
                        .into_subpackages()
                        .into_values()
                        .collect(),
                )
                .await?;
        }

        let () = self.send_content(root_dir.external_file_hashes().copied().collect()).await?;
        Ok(())
    }

    /// For each subpackage meta.far:
    ///   * recurse if it is already in blobfs
    ///   * otherwise send it if it hasn't already been sent
    fn send_subpackages(
        &mut self,
        subpackages: HashSet<Hash>,
    ) -> futures::future::BoxFuture<'_, Result<(), ServeNeededBlobsError>> {
        async move {
            if subpackages.is_empty() {
                return Ok(());
            }

            let () = self
                .blob_recorder
                .record(subpackages.clone())
                .await
                .map_err(ServeNeededBlobsError::RecordingSubpackageBlobs)?;

            let mut missing = self.blobfs.filter_to_missing_blobs(&subpackages).await;

            for present in subpackages.difference(&missing) {
                let () = self.visit(RootDirOrHash::Hash(*present)).await?;
            }

            self.sent_subpackages.extend(missing.iter().copied());
            for hash in &self.sent_hashes {
                missing.remove(hash);
            }

            let () = self
                .sender
                .unbounded_send(to_sorted_vec(&missing))
                .map_err(|e| e.into_send_error())
                .map_err(ServeNeededBlobsError::SendNeededSubpackageBlobs)?;
            self.sent_hashes.extend(missing.iter().copied());
            self.sent_but_not_cached_hashes.extend(missing);
            Ok(())
        }
        .boxed()
    }

    /// Send content blob hashes that are not in blobfs and haven't already been sent.
    async fn send_content(
        &mut self,
        mut to_send: HashSet<Hash>,
    ) -> Result<(), ServeNeededBlobsError> {
        let () = self
            .blob_recorder
            .record(to_send.clone())
            .await
            .map_err(ServeNeededBlobsError::RecordingContentBlobs)?;
        for hash in &self.sent_hashes {
            to_send.remove(hash);
        }
        let to_send = self.blobfs.filter_to_missing_blobs(&to_send).await;
        let () = self
            .sender
            .unbounded_send(to_sorted_vec(&to_send))
            .map_err(|e| e.into_send_error())
            .map_err(ServeNeededBlobsError::SendNeededContentBlobs)?;
        self.sent_hashes.extend(to_send.iter().copied());
        self.sent_but_not_cached_hashes.extend(to_send);
        Ok(())
    }

    /// Returns true iff `hash` has been sent with self.sender but not yet `self.cache(hash)`'d.
    pub fn should_cache(&self, hash: &Hash) -> bool {
        self.sent_but_not_cached_hashes.contains(hash)
    }

    /// The number of known remaining blobs to cache.
    /// Once this is zero it will always be zero (at which point the package has been cached),
    /// otherwise the number may increase or decrease as more subpackage meta.fars and content
    /// blobs are cached.
    pub fn count_not_cached(&self) -> usize {
        self.sent_but_not_cached_hashes.len()
    }

    /// Closes `self.sender` if all the subpackage meta.fars sent with `self.sender` have been
    /// cached.
    /// If all sent subpackages have been cached, then, assuming there are no more subpackages to
    /// send, the package has been cached.
    /// In practice, closing the sender allows the GetMissingBlobs iterator to terminate
    /// immediately after sending all content blobs for packages that have no subpackages,
    /// otherwise the iterator would hang until the client wrote all the content blobs via
    /// NeededBlobs.OpenBlob.
    /// This behavior is not necessary but can be convenient for clients (and prevents the
    /// cache_service.rs unit tests (that were written before subpackages were added) from
    /// hanging).
    fn close_sender_if_all_sent_subpackages_cached(&self) {
        if let None = self.sent_subpackages.intersection(&self.sent_but_not_cached_hashes).next() {
            let () = self.sender.close_channel();
        }
    }
}

/// The FIDL doesn't require that the Vec be sorted, but doing so helps make Gets and therefore
/// resolves deterministic.
fn to_sorted_vec(missing: &HashSet<Hash>) -> Vec<fpkg::BlobInfo> {
    let mut missing = missing
        .iter()
        .map(|hash| fpkg::BlobInfo::from(fpkg_ext::BlobInfo { blob_id: (*hash).into(), length: 0 }))
        .collect::<Vec<_>>();
    missing.sort_unstable();
    missing
}

#[cfg(test)]
mod tests {
    use {
        super::*, blobfs_ramdisk::BlobfsRamdisk, fuchsia_pkg_testing::PackageBuilder,
        futures::stream::StreamExt as _, package_directory::RootDir,
    };

    #[derive(Clone, Debug)]
    struct MockBlobRecorder {
        blobs: std::sync::Arc<async_lock::RwLock<HashSet<Hash>>>,
    }

    impl MockBlobRecorder {
        fn new() -> Self {
            Self { blobs: std::sync::Arc::new(async_lock::RwLock::new(HashSet::new())) }
        }

        async fn hashes(&self) -> HashSet<Hash> {
            self.blobs.read().await.clone()
        }
    }

    impl BlobRecorder for MockBlobRecorder {
        fn record(
            &self,
            blobs: HashSet<Hash>,
        ) -> futures::future::BoxFuture<'_, Result<(), anyhow::Error>> {
            async move { Ok(self.blobs.write().await.extend(blobs)) }.boxed()
        }
    }

    async fn read_receiver(
        recv: futures::channel::mpsc::UnboundedReceiver<Vec<fpkg::BlobInfo>>,
    ) -> Vec<Hash> {
        recv.concat()
            .await
            .into_iter()
            .map(|blob_info| {
                assert_eq!(blob_info.length, 0);
                fpkg_ext::BlobId::from(blob_info.blob_id).into()
            })
            .collect()
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn sends_content_blob() {
        let blob_recorder = MockBlobRecorder::new();
        let blobfs = BlobfsRamdisk::start().unwrap();
        let pkg = PackageBuilder::new("pkg")
            .add_resource_at("content-blob", "blob-contents".as_bytes())
            .build()
            .await
            .unwrap();
        let (meta_far, content_blobs) = pkg.contents();
        blobfs.add_blob_from(&meta_far.merkle, meta_far.contents.as_slice()).unwrap();
        let root_dir = RootDir::new(blobfs.client(), *pkg.meta_far_merkle_root()).await.unwrap();

        let (missing_blobs, recv) = MissingBlobs::new(
            blobfs.client(),
            crate::SubpackagesConfig::Enable,
            &root_dir,
            Box::new(blob_recorder.clone()),
        )
        .await
        .unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 1);
        drop(missing_blobs);

        assert_eq!(read_receiver(recv).await, vec![*content_blobs.keys().next().unwrap()]);
        assert_eq!(
            blob_recorder.hashes().await,
            HashSet::from_iter([*content_blobs.keys().next().unwrap()])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn filters_content_blobs_from_blobfs() {
        let blob_recorder = MockBlobRecorder::new();
        let blobfs = BlobfsRamdisk::start().unwrap();
        let pkg = PackageBuilder::new("pkg")
            .add_resource_at("filter-me", "blob-contents".as_bytes())
            .build()
            .await
            .unwrap();
        pkg.write_to_blobfs_dir(&blobfs.root_dir().unwrap());
        let root_dir = RootDir::new(blobfs.client(), *pkg.meta_far_merkle_root()).await.unwrap();

        let (missing_blobs, recv) = MissingBlobs::new(
            blobfs.client(),
            crate::SubpackagesConfig::Enable,
            &root_dir,
            Box::new(blob_recorder.clone()),
        )
        .await
        .unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 0);
        drop(missing_blobs);

        assert_eq!(read_receiver(recv).await, vec![]);
        assert_eq!(
            blob_recorder.hashes().await,
            HashSet::from_iter([*pkg.contents().1.keys().next().unwrap()])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn deduplicates_content_blobs_within_root_dir() {
        let blob_recorder = MockBlobRecorder::new();
        let blobfs = BlobfsRamdisk::start().unwrap();
        let pkg = PackageBuilder::new("pkg")
            .add_resource_at("content-blob", "blob-contents".as_bytes())
            .add_resource_at("duplicate", "blob-contents".as_bytes())
            .build()
            .await
            .unwrap();
        let (meta_far, content_blobs) = pkg.contents();
        blobfs.add_blob_from(&meta_far.merkle, meta_far.contents.as_slice()).unwrap();
        let root_dir = RootDir::new(blobfs.client(), *pkg.meta_far_merkle_root()).await.unwrap();

        let (missing_blobs, recv) = MissingBlobs::new(
            blobfs.client(),
            crate::SubpackagesConfig::Enable,
            &root_dir,
            Box::new(blob_recorder.clone()),
        )
        .await
        .unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 1);
        drop(missing_blobs);

        assert_eq!(read_receiver(recv).await, vec![*content_blobs.keys().next().unwrap()]);
        assert_eq!(
            blob_recorder.hashes().await,
            HashSet::from_iter([*content_blobs.keys().next().unwrap()])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn deduplicates_content_blobs_across_root_dirs() {
        let blob_recorder = MockBlobRecorder::new();
        let blobfs = BlobfsRamdisk::start().unwrap();
        let subpackage = PackageBuilder::new("subpackage")
            .add_resource_at("content-blob", "blob-contents".as_bytes())
            .build()
            .await
            .unwrap();
        let (meta_far, content_blobs) = subpackage.contents();
        blobfs.add_blob_from(&meta_far.merkle, meta_far.contents.as_slice()).unwrap();

        let superpackage = PackageBuilder::new("superpackage")
            .add_resource_at("duplicate", "blob-contents".as_bytes())
            .add_subpackage("my-subpackage", &subpackage)
            .build()
            .await
            .unwrap();
        let (meta_far, _) = superpackage.contents();
        blobfs.add_blob_from(&meta_far.merkle, meta_far.contents.as_slice()).unwrap();
        let superpackage_root_dir =
            RootDir::new(blobfs.client(), *superpackage.meta_far_merkle_root()).await.unwrap();

        let (missing_blobs, recv) = MissingBlobs::new(
            blobfs.client(),
            crate::SubpackagesConfig::Enable,
            &superpackage_root_dir,
            Box::new(blob_recorder.clone()),
        )
        .await
        .unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 1);
        drop(missing_blobs);

        assert_eq!(read_receiver(recv).await, vec![*content_blobs.keys().next().unwrap()]);
        assert_eq!(
            blob_recorder.hashes().await,
            HashSet::from_iter([
                *content_blobs.keys().next().unwrap(),
                *subpackage.meta_far_merkle_root()
            ])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn sends_subpackage_meta_far() {
        let blob_recorder = MockBlobRecorder::new();
        let blobfs = BlobfsRamdisk::start().unwrap();
        let subpackage = PackageBuilder::new("subpackage").build().await.unwrap();

        let superpackage = PackageBuilder::new("superpackage")
            .add_subpackage("my-subpackage", &subpackage)
            .build()
            .await
            .unwrap();
        let (meta_far, _) = superpackage.contents();
        blobfs.add_blob_from(&meta_far.merkle, meta_far.contents.as_slice()).unwrap();
        let superpackage_root_dir =
            RootDir::new(blobfs.client(), *superpackage.meta_far_merkle_root()).await.unwrap();

        let (missing_blobs, recv) = MissingBlobs::new(
            blobfs.client(),
            crate::SubpackagesConfig::Enable,
            &superpackage_root_dir,
            Box::new(blob_recorder.clone()),
        )
        .await
        .unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 1);
        drop(missing_blobs);

        assert_eq!(read_receiver(recv).await, vec![*subpackage.meta_far_merkle_root()]);
        assert_eq!(
            blob_recorder.hashes().await,
            HashSet::from_iter([*subpackage.meta_far_merkle_root()])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn already_cached_subpackage_meta_far_is_recursed_instead_of_sent() {
        let blob_recorder = MockBlobRecorder::new();
        let blobfs = BlobfsRamdisk::start().unwrap();
        let subsubpackage = PackageBuilder::new("subsubpackage").build().await.unwrap();

        let subpackage = PackageBuilder::new("subpackage")
            .add_subpackage("subsubpackage", &subsubpackage)
            .build()
            .await
            .unwrap();
        let (meta_far, _) = subpackage.contents();
        blobfs.add_blob_from(&meta_far.merkle, meta_far.contents.as_slice()).unwrap();

        let superpackage = PackageBuilder::new("superpackage")
            .add_subpackage("my-subpackage", &subpackage)
            .build()
            .await
            .unwrap();
        let (meta_far, _) = superpackage.contents();
        blobfs.add_blob_from(&meta_far.merkle, meta_far.contents.as_slice()).unwrap();
        let superpackage_root_dir =
            RootDir::new(blobfs.client(), *superpackage.meta_far_merkle_root()).await.unwrap();

        let (missing_blobs, recv) = MissingBlobs::new(
            blobfs.client(),
            crate::SubpackagesConfig::Enable,
            &superpackage_root_dir,
            Box::new(blob_recorder.clone()),
        )
        .await
        .unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 1);
        drop(missing_blobs);

        assert_eq!(read_receiver(recv).await, vec![*subsubpackage.meta_far_merkle_root()]);
        assert_eq!(
            blob_recorder.hashes().await,
            HashSet::from_iter([
                *subpackage.meta_far_merkle_root(),
                *subsubpackage.meta_far_merkle_root()
            ])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn deduplicates_content_blobs_with_subpackage_meta_fars() {
        let blob_recorder = MockBlobRecorder::new();
        let blobfs = BlobfsRamdisk::start().unwrap();
        let subpackage = PackageBuilder::new("subpackage").build().await.unwrap();
        let (subpackage_meta_far, _) = subpackage.contents();

        let superpackage = PackageBuilder::new("superpackage")
            .add_resource_at("meta-far-as-content-blob", subpackage_meta_far.contents.as_slice())
            .add_subpackage("my-subpackage", &subpackage)
            .build()
            .await
            .unwrap();
        let (superpackage_meta_far, superpackage_content_blobs) = superpackage.contents();
        blobfs
            .add_blob_from(&superpackage_meta_far.merkle, superpackage_meta_far.contents.as_slice())
            .unwrap();
        let superpackage_root_dir =
            RootDir::new(blobfs.client(), *superpackage.meta_far_merkle_root()).await.unwrap();

        let (missing_blobs, recv) = MissingBlobs::new(
            blobfs.client(),
            crate::SubpackagesConfig::Enable,
            &superpackage_root_dir,
            Box::new(blob_recorder.clone()),
        )
        .await
        .unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 1);
        drop(missing_blobs);

        assert_eq!(
            read_receiver(recv).await,
            vec![superpackage_content_blobs.into_keys().next().unwrap()]
        );
        assert_eq!(
            blob_recorder.hashes().await,
            HashSet::from_iter([*subpackage.meta_far_merkle_root(),])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn deduplicates_subpackage_meta_fars_with_content_blobs() {
        let blob_recorder = MockBlobRecorder::new();
        let blobfs = BlobfsRamdisk::start().unwrap();
        let subsubpackage = PackageBuilder::new("subsubpackage").build().await.unwrap();
        let (subsubpackage_meta_far, _) = subsubpackage.contents();

        let subpackage = PackageBuilder::new("subpackage")
            .add_subpackage("subsub", &subsubpackage)
            .add_resource_at("subpackage-blob", "subpackage-blob-contents".as_bytes())
            .build()
            .await
            .unwrap();
        let (subpackage_meta_far, subpackage_content_blobs) = subpackage.contents();

        let superpackage = PackageBuilder::new("superpackage")
            .add_subpackage("my-subpackage", &subpackage)
            .add_resource_at("meta-far-as-content-blob", subsubpackage_meta_far.contents.as_slice())
            .build()
            .await
            .unwrap();
        let (superpackage_meta_far, _) = superpackage.contents();
        blobfs
            .add_blob_from(&superpackage_meta_far.merkle, superpackage_meta_far.contents.as_slice())
            .unwrap();
        let superpackage_root_dir =
            RootDir::new(blobfs.client(), *superpackage.meta_far_merkle_root()).await.unwrap();

        // Sends the subsubpackage meta.far but only as a content blob.
        let (mut missing_blobs, recv) = MissingBlobs::new(
            blobfs.client(),
            crate::SubpackagesConfig::Enable,
            &superpackage_root_dir,
            Box::new(blob_recorder.clone()),
        )
        .await
        .unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 2);
        assert!(missing_blobs.should_cache(&subsubpackage_meta_far.merkle));
        assert!(!missing_blobs.sent_subpackages.contains(&subsubpackage_meta_far.merkle));

        // Upgrades the hash of the subsubpackage meta.far from a content blob to a subpackage hash
        // but doesn't re-send (only appears once in the receiver).
        blobfs
            .add_blob_from(&subpackage_meta_far.merkle, subpackage_meta_far.contents.as_slice())
            .unwrap();
        let () = missing_blobs.cache(&subpackage_meta_far.merkle).await.unwrap();
        assert_eq!(missing_blobs.count_not_cached(), 2);
        assert!(missing_blobs.should_cache(&subsubpackage_meta_far.merkle));
        assert!(missing_blobs.sent_subpackages.contains(&subsubpackage_meta_far.merkle));
        drop(missing_blobs);

        assert_eq!(
            read_receiver(recv).await,
            vec![
                subpackage_meta_far.merkle,
                subsubpackage_meta_far.merkle,
                *subpackage_content_blobs.keys().next().unwrap(),
            ]
        );
        assert_eq!(
            blob_recorder.hashes().await,
            HashSet::from_iter([
                *subpackage.meta_far_merkle_root(),
                *subsubpackage.meta_far_merkle_root(),
                *subpackage_content_blobs.keys().next().unwrap(),
            ])
        );
    }
}
