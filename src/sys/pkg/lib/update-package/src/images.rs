// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        image::{Image, ImageClass, ImageType},
        update_mode::UpdateMode,
    },
    fidl_fuchsia_io as fio,
    fuchsia_hash::Hash,
    fuchsia_url::{AbsoluteComponentUrl, ParseError, PinnedAbsolutePackageUrl},
    fuchsia_zircon_status::Status,
    serde::{Deserialize, Serialize},
    std::{
        collections::{BTreeMap, BTreeSet, HashSet},
        path::Path,
    },
    thiserror::Error,
};

/// An error encountered while resolving images.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ResolveImagesError {
    #[error("while listing files in the update package")]
    ListCandidates(#[source] fuchsia_fs::directory::Error),
}

/// An error encountered while verifying an [`UnverifiedImageList`].
#[derive(Debug, Error, PartialEq, Eq)]
#[allow(missing_docs)]
pub enum VerifyError {
    #[error("images list did not contain an entry for 'zbi' or 'zbi.signed'")]
    MissingZbi,

    #[error("images list unexpectedly contained an entry for 'zbi' or 'zbi.signed'")]
    UnexpectedZbi,
}

/// An error encountered while handling [`ImageMetadata`].
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ImageMetadataError {
    #[error("while reading the image")]
    Io(#[source] std::io::Error),

    #[error("invalid resource path")]
    InvalidResourcePath(#[source] ParseError),
}

/// An error encountered while loading the images.json manifest.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ImagePackagesError {
    #[error("`images.json` not present in update package")]
    NotFound,

    #[error("while opening `images.json`")]
    Open(#[source] fuchsia_fs::node::OpenError),

    #[error("while reading `images.json`")]
    Read(#[source] fuchsia_fs::file::ReadError),

    #[error("while parsing `images.json`")]
    Parse(#[source] serde_json::error::Error),
}

/// A resolved sequence of images that have been verified for a particular update mode.
#[derive(Debug, PartialEq, Eq)]
pub struct ImageList(Vec<Image>);

impl ImageList {
    /// Filters the image list using the provided callback to the images for which the callback
    /// returns true.
    pub fn filter<F>(self, f: F) -> Self
    where
        F: Fn(&Image) -> bool,
    {
        Self(self.0.into_iter().filter(f).collect())
    }
}

impl std::ops::Deref for ImageList {
    type Target = [Image];
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// A resolved sequence of images that may or may not contain the required entries for a particular
/// update mode.
#[derive(Debug, PartialEq, Eq)]
pub struct UnverifiedImageList(Vec<Image>);

impl UnverifiedImageList {
    fn contains_zbi_entry(&self) -> bool {
        self.0.iter().any(|image| image.classify() == ImageClass::Zbi)
    }

    /// Verify that this image list is appropriate for the given update mode.
    ///
    /// * `UpdateMode::Normal` - a non-recovery kernel image (zbi or zbi.signed) is required.
    /// * `UpdateMode::ForceRecovery` - a non-recovery kernel image must not be present.
    pub fn verify(self, mode: UpdateMode) -> Result<ImageList, VerifyError> {
        let contains_zbi_entry = self.contains_zbi_entry();
        match mode {
            UpdateMode::Normal if !contains_zbi_entry => Err(VerifyError::MissingZbi),
            UpdateMode::ForceRecovery if contains_zbi_entry => Err(VerifyError::UnexpectedZbi),
            _ => Ok(ImageList(self.0)),
        }
    }
}

async fn list_dir_files(
    proxy: &fio::DirectoryProxy,
) -> Result<BTreeSet<String>, ResolveImagesError> {
    let entries =
        fuchsia_fs::directory::readdir(proxy).await.map_err(ResolveImagesError::ListCandidates)?;

    let names = entries
        .into_iter()
        .filter_map(|entry| match entry.kind {
            fuchsia_fs::directory::DirentKind::File => Some(entry.name),
            _ => None,
        })
        .collect();

    Ok(names)
}

fn resolve(requests: &[ImageType], available: &BTreeSet<String>) -> UnverifiedImageList {
    let mut res = vec![];
    for request in requests {
        for candidate in available.iter() {
            if let Some(image) = Image::matches_base(candidate.to_string(), *request) {
                res.push(image);
            }
        }
    }

    UnverifiedImageList(res)
}

pub(crate) async fn resolve_images(
    proxy: &fio::DirectoryProxy,
    requests: &[ImageType],
) -> Result<UnverifiedImageList, ResolveImagesError> {
    let files = list_dir_files(proxy).await?;
    Ok(resolve(requests, &files))
}

/// A builder of [`ImagePackagesManifest`].
#[derive(Debug, Clone)]
pub struct ImagePackagesManifestBuilder {
    slots: ImagePackagesSlots,
}

/// A versioned [`ImagePackagesManifest`].
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
#[serde(tag = "version", content = "contents", deny_unknown_fields)]
#[allow(missing_docs)]
pub enum VersionedImagePackagesManifest {
    #[serde(rename = "1")]
    Version1(ImagePackagesManifest),
}

/// A manifest describing the various images and firmware packages that should be fetched and
/// written during a system update, as well as metadata about those images and where to find them.
#[derive(Serialize, Debug, PartialEq, Eq, Clone)]
pub struct ImagePackagesManifest {
    partitions: Vec<AssemblyImageFormat>,
    firmware: Vec<FirmwareImageFormat>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
#[serde(deny_unknown_fields)]
pub struct FirmwareImageFormat {
    r#type: String,
    size: u64,
    hash: Hash,
    url: AbsoluteComponentUrl,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
#[serde(deny_unknown_fields)]
pub struct AssemblyImageFormat {
    slot: Slot,
    r#type: SlotImage,
    size: u64,
    hash: Hash,
    url: AbsoluteComponentUrl,
}

/// Location for where an image should be written.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone, Copy, Hash)]
#[serde(rename_all = "lowercase")]
pub enum Slot {
    /// The primary Fuchsia boot slot or slots, typically A or B, depending on which is currently
    /// in use.
    Fuchsia,

    /// The recovery boot slot.
    Recovery,
}

/// Image asset type.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone, Copy, Hash)]
#[serde(rename_all = "lowercase")]
pub enum SlotImage {
    /// A Zircon Boot Image.
    Zbi,

    /// Verified Boot Metadata.
    Vbmeta,
}

impl From<ImagePackagesManifest> for ImagePackagesSlots {
    fn from(manifest: ImagePackagesManifest) -> Self {
        ImagePackagesSlots {
            fuchsia: manifest.fuchsia(),
            recovery: manifest.recovery(),
            firmware: manifest.firmware(),
        }
    }
}

/// A manifest describing the various image packages, all of which are optional, and each contains
/// a fuchsia-pkg URI for that package and a description of the assets it contains.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct ImagePackagesSlots {
    fuchsia: Option<BootSlot>,
    recovery: Option<BootSlot>,
    firmware: BTreeMap<String, ImageMetadata>,
}

/// Metadata for artifacts unique to an A/B/R boot slot.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct BootSlot {
    /// The zircon boot image.
    zbi: ImageMetadata,

    /// The optional slot metadata.
    vbmeta: Option<ImageMetadata>,
}

impl BootSlot {
    /// Returns an immutable borrow to the ZBI designated in this boot slot.
    pub fn zbi(&self) -> &ImageMetadata {
        &self.zbi
    }

    /// Returns an immutable borrow to the VBMeta designated in this boot slot, if one exists.
    pub fn vbmeta(&self) -> Option<&ImageMetadata> {
        self.vbmeta.as_ref()
    }
}

/// Metadata necessary to determine if a payload matches an image without needing to have the
/// actual image.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct ImageMetadata {
    /// The size of the image, in bytes.
    size: u64,

    /// The sha256 hash of the image. Note this is not the merkle root of a
    /// `fuchsia_merkle::MerkleTree`. It is the content hash of the image.
    hash: Hash,

    /// The URL of the image in its package.
    url: AbsoluteComponentUrl,
}

impl FirmwareImageFormat {
    /// Creates a new [`FirmwareImageFormat`] from the given image metadata and target subtype.
    pub fn new_from_metadata(subtype: impl Into<String>, metadata: ImageMetadata) -> Self {
        Self { r#type: subtype.into(), size: metadata.size, hash: metadata.hash, url: metadata.url }
    }

    fn key(&self) -> &str {
        &self.r#type
    }

    /// Returns the [`ImageMetadata`] for this image.
    pub fn metadata(&self) -> ImageMetadata {
        ImageMetadata { size: self.size, hash: self.hash, url: self.url.clone() }
    }
}

impl AssemblyImageFormat {
    /// Creates a new [`AssemblyImageFormat`] from the given image metadata and target slot/type.
    pub fn new_from_metadata(slot: Slot, kind: SlotImage, metadata: ImageMetadata) -> Self {
        Self { slot, r#type: kind, size: metadata.size, hash: metadata.hash, url: metadata.url }
    }

    fn key(&self) -> (Slot, SlotImage) {
        (self.slot, self.r#type)
    }

    /// Returns the [`ImageMetadata`] for this image.
    pub fn metadata(&self) -> ImageMetadata {
        ImageMetadata { size: self.size, hash: self.hash, url: self.url.clone() }
    }
}

impl ImagePackagesManifest {
    /// Returns a [`ImagePackagesManifestBuilder`] with no configured images.
    pub fn builder() -> ImagePackagesManifestBuilder {
        ImagePackagesManifestBuilder {
            slots: ImagePackagesSlots {
                fuchsia: None,
                recovery: None,
                firmware: Default::default(),
            },
        }
    }

    fn image(&self, slot: Slot, kind: SlotImage) -> Option<&AssemblyImageFormat> {
        self.partitions.iter().find(|image| image.slot == slot && image.r#type == kind)
    }

    fn image_metadata(&self, slot: Slot, kind: SlotImage) -> Option<ImageMetadata> {
        self.image(slot, kind).map(|image| image.metadata())
    }

    fn slot_metadata(&self, slot: Slot) -> Option<BootSlot> {
        let zbi = self.image_metadata(slot, SlotImage::Zbi);
        let vbmeta = self.image_metadata(slot, SlotImage::Vbmeta);

        zbi.map(|zbi| BootSlot { zbi, vbmeta })
    }

    /// Returns metadata for the fuchsia boot slot, if present.
    pub fn fuchsia(&self) -> Option<BootSlot> {
        self.slot_metadata(Slot::Fuchsia)
    }

    /// Returns metadata for the recovery boot slot, if present.
    pub fn recovery(&self) -> Option<BootSlot> {
        self.slot_metadata(Slot::Recovery)
    }

    /// Returns metadata for the firmware images.
    pub fn firmware(&self) -> BTreeMap<String, ImageMetadata> {
        self.firmware.iter().map(|image| (image.r#type.to_owned(), image.metadata())).collect()
    }
}

impl ImageMetadata {
    /// Returns new image metadata that designates the given `size` and `hash`, which can be found
    /// at the given `url`.
    pub fn new(size: u64, hash: Hash, url: AbsoluteComponentUrl) -> Self {
        Self { size, hash, url }
    }

    /// Returns the size of the image, in bytes.
    pub fn size(&self) -> u64 {
        self.size
    }

    /// Returns the sha256 hash of the image.
    pub fn hash(&self) -> Hash {
        self.hash
    }

    /// Returns the url of the image.
    pub fn url(&self) -> &AbsoluteComponentUrl {
        &self.url
    }

    /// Compute the size and hash for the image file located at `path`, determining the image's
    /// fuchsia-pkg URL using the given base `url` and `resource` path within the package.
    pub fn for_path(
        path: &Path,
        url: PinnedAbsolutePackageUrl,
        resource: String,
    ) -> Result<Self, ImageMetadataError> {
        use sha2::{Digest, Sha256};

        let mut hasher = Sha256::new();
        let mut file = std::fs::File::open(path).map_err(ImageMetadataError::Io)?;
        let size = std::io::copy(&mut file, &mut hasher).map_err(ImageMetadataError::Io)?;
        let hash = Hash::from(*AsRef::<[u8; 32]>::as_ref(&hasher.finalize()));

        let url = AbsoluteComponentUrl::from_package_url_and_resource(url.into(), resource)
            .map_err(ImageMetadataError::InvalidResourcePath)?;

        Ok(Self { size, hash, url })
    }
}

impl ImagePackagesManifestBuilder {
    /// Configures the "fuchsia" images package to use the given zbi metadata, and optional
    /// vbmeta metadata.
    pub fn fuchsia_package(
        &mut self,
        zbi: ImageMetadata,
        vbmeta: Option<ImageMetadata>,
    ) -> &mut Self {
        self.slots.fuchsia = Some(BootSlot { zbi, vbmeta });
        self
    }

    /// Configures the "recovery" images package to use the given zbi metadata, and optional
    /// vbmeta metadata.
    pub fn recovery_package(
        &mut self,
        zbi: ImageMetadata,
        vbmeta: Option<ImageMetadata>,
    ) -> &mut Self {
        self.slots.recovery = Some(BootSlot { zbi, vbmeta });
        self
    }

    /// Configures the "firmware" images package from a BTreeMap of ImageMetadata
    pub fn firmware_package(&mut self, firmware: BTreeMap<String, ImageMetadata>) -> &mut Self {
        self.slots.firmware = firmware;
        self
    }

    /// Returns the constructed manifest.
    pub fn build(self) -> VersionedImagePackagesManifest {
        let mut partitions = vec![];
        let mut firmware = vec![];

        if let Some(slot) = self.slots.fuchsia {
            partitions.push(AssemblyImageFormat::new_from_metadata(
                Slot::Fuchsia,
                SlotImage::Zbi,
                slot.zbi,
            ));
            if let Some(vbmeta) = slot.vbmeta {
                partitions.push(AssemblyImageFormat::new_from_metadata(
                    Slot::Fuchsia,
                    SlotImage::Vbmeta,
                    vbmeta,
                ));
            }
        }

        if let Some(slot) = self.slots.recovery {
            partitions.push(AssemblyImageFormat::new_from_metadata(
                Slot::Recovery,
                SlotImage::Zbi,
                slot.zbi,
            ));
            if let Some(vbmeta) = slot.vbmeta {
                partitions.push(AssemblyImageFormat::new_from_metadata(
                    Slot::Recovery,
                    SlotImage::Vbmeta,
                    vbmeta,
                ));
            }
        }

        for (subtype, metadata) in self.slots.firmware {
            firmware.push(FirmwareImageFormat::new_from_metadata(subtype, metadata));
        }

        VersionedImagePackagesManifest::Version1(ImagePackagesManifest { partitions, firmware })
    }
}

impl ImagePackagesSlots {
    /// Verify that this image package manifest is appropriate for the given update mode.
    ///
    /// * `UpdateMode::Normal` - a non-recovery kernel image is required.
    /// * `UpdateMode::ForceRecovery` - a non-recovery kernel image must not be present.
    pub fn verify(&self, mode: UpdateMode) -> Result<(), VerifyError> {
        let contains_zbi_entry = self.fuchsia.is_some();
        match mode {
            UpdateMode::Normal if !contains_zbi_entry => Err(VerifyError::MissingZbi),
            UpdateMode::ForceRecovery if contains_zbi_entry => Err(VerifyError::UnexpectedZbi),
            _ => Ok(()),
        }
    }

    /// Returns an immutable borrow to the boot slot image package designated as "fuchsia" in this
    /// image packages manifest.
    pub fn fuchsia(&self) -> Option<&BootSlot> {
        self.fuchsia.as_ref()
    }

    /// Returns an immutable borrow to the boot slot image package designated as "recovery" in this
    /// image packages manifest.
    pub fn recovery(&self) -> Option<&BootSlot> {
        self.recovery.as_ref()
    }

    /// Returns an immutable borrow to the boot slot image package designated as "firmware" in this
    /// image packages manifest.
    pub fn firmware(&self) -> &BTreeMap<String, ImageMetadata> {
        &self.firmware
    }
}

impl<'de> Deserialize<'de> for ImagePackagesManifest {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        use serde::de::Error;

        #[derive(Debug, Deserialize)]
        pub struct DeImagePackagesManifest {
            partitions: Vec<AssemblyImageFormat>,
            firmware: Vec<FirmwareImageFormat>,
        }

        let parsed = DeImagePackagesManifest::deserialize(deserializer)?;

        // Check for duplicate image destinations, verify URL always contains a hash,
        // and check that a zbi is present if vbmeta is present.
        {
            let mut keys = HashSet::new();
            for image in &parsed.partitions {
                if image.metadata().url().package_url().hash().is_none() {
                    return Err(D::Error::custom(format!(
                        "image url {:?} does not contain hash",
                        image.metadata().url()
                    )));
                }

                if !keys.insert(image.key()) {
                    return Err(D::Error::custom(format!(
                        "duplicate image entry: {:?}",
                        image.key()
                    )));
                }
            }

            for slot in [Slot::Fuchsia, Slot::Recovery] {
                if keys.contains(&(slot, SlotImage::Vbmeta))
                    && !keys.contains(&(slot, SlotImage::Zbi))
                {
                    return Err(D::Error::custom(format!(
                        "vbmeta without zbi entry in partition {:?}",
                        slot
                    )));
                }
            }
        }

        // Check for duplicate firmware destinations and verify that url field contains a  hash.
        {
            let mut keys = HashSet::new();
            for image in &parsed.firmware {
                if image.metadata().url().package_url().hash().is_none() {
                    return Err(D::Error::custom(format!(
                        "firmware url {:?} does not contain hash",
                        image.metadata().url()
                    )));
                }

                if !keys.insert(image.key()) {
                    return Err(D::Error::custom(format!(
                        "duplicate firmware entry: {:?}",
                        image.key()
                    )));
                }
            }
        }

        Ok(ImagePackagesManifest { partitions: parsed.partitions, firmware: parsed.firmware })
    }
}

/// Returns structured `images.json` data based on raw file contents.
pub fn parse_image_packages_json(
    contents: &[u8],
) -> Result<ImagePackagesManifest, ImagePackagesError> {
    let VersionedImagePackagesManifest::Version1(manifest) =
        serde_json::from_slice(contents).map_err(ImagePackagesError::Parse)?;

    Ok(manifest)
}

pub(crate) async fn image_packages(
    proxy: &fio::DirectoryProxy,
) -> Result<ImagePackagesManifest, ImagePackagesError> {
    let file =
        fuchsia_fs::directory::open_file(proxy, "images.json", fio::OpenFlags::RIGHT_READABLE)
            .await
            .map_err(|e| match e {
                fuchsia_fs::node::OpenError::OpenError(Status::NOT_FOUND) => {
                    ImagePackagesError::NotFound
                }
                e => ImagePackagesError::Open(e),
            })?;

    let contents = fuchsia_fs::file::read(&file).await.map_err(ImagePackagesError::Read)?;

    parse_image_packages_json(&contents)
}

#[cfg(test)]
mod legacy_tests {
    use {
        super::*,
        crate::{TestUpdatePackage, UpdatePackage},
        assert_matches::assert_matches,
        maplit::btreeset,
        std::sync::Arc,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::read_only_static, pseudo_directory,
        },
    };

    #[test]
    fn verify_mode_normal_requires_zbi() {
        let images =
            vec![Image::new(ImageType::Bootloader, None), Image::new(ImageType::Zbi, None)];
        assert_eq!(
            UnverifiedImageList(images.clone()).verify(UpdateMode::Normal),
            Ok(ImageList(images))
        );

        let images =
            vec![Image::new(ImageType::Bootloader, None), Image::new(ImageType::ZbiSigned, None)];
        assert_eq!(
            UnverifiedImageList(images.clone()).verify(UpdateMode::Normal),
            Ok(ImageList(images))
        );

        assert_eq!(
            UnverifiedImageList(vec![Image::new(ImageType::Bootloader, None),])
                .verify(UpdateMode::Normal),
            Err(VerifyError::MissingZbi)
        );
    }

    #[test]
    fn verify_mode_force_recovery_requires_no_zbi() {
        let images = vec![Image::new(ImageType::Bootloader, None)];
        assert_eq!(
            UnverifiedImageList(images.clone()).verify(UpdateMode::ForceRecovery),
            Ok(ImageList(images))
        );

        assert_eq!(
            UnverifiedImageList(vec![Image::new(ImageType::Zbi, None)])
                .verify(UpdateMode::ForceRecovery),
            Err(VerifyError::UnexpectedZbi)
        );

        assert_eq!(
            UnverifiedImageList(vec![Image::new(ImageType::ZbiSigned, None)])
                .verify(UpdateMode::ForceRecovery),
            Err(VerifyError::UnexpectedZbi)
        );
    }

    #[test]
    fn image_list_filter_filters_preserving_order() {
        assert_eq!(
            ImageList(vec![
                Image::new(ImageType::Zbi, None),
                Image::new(ImageType::ZbiSigned, None),
                Image::new(ImageType::Bootloader, None)
            ])
            .filter(|image| image.name() != "zbi.signed"),
            ImageList(vec![
                Image::new(ImageType::Zbi, None),
                Image::new(ImageType::Bootloader, None)
            ]),
        );
    }

    async fn update_pkg_with_files(names: &[&str]) -> TestUpdatePackage {
        let mut pkg = TestUpdatePackage::new();
        for name in names {
            pkg = pkg.add_file(name, "").await;
        }
        pkg
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_images() {
        let pkg = update_pkg_with_files(&["a", "b", "c"]).await;
        assert_matches!(
            list_dir_files(pkg.proxy()).await,
            Ok(names) if names == btreeset! {
                "a".to_owned(),
                "b".to_owned(),
                "c".to_owned(),
            }
        );
    }

    fn spawn_vfs(dir: Arc<vfs::directory::immutable::simple::Simple>) -> fio::DirectoryProxy {
        let (proxy, proxy_server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
            0,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(proxy_server_end.into_channel()),
        );
        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn list_images_ignores_directories() {
        let proxy = spawn_vfs(pseudo_directory! {
            "ignore_directories" => pseudo_directory! {
                "and_their_contents" => read_only_static(""),
            },
            "first" => read_only_static(""),
            "second" => read_only_static(""),
        });

        assert_eq!(
            list_dir_files(&proxy).await.unwrap(),
            btreeset! {
                "first".to_owned(),
                "second".to_owned(),
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_images_fails_on_closed_directory() {
        let (proxy, _) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let pkg = UpdatePackage::new(proxy);

        assert_matches!(pkg.resolve_images(&[]).await, Err(ResolveImagesError::ListCandidates(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_images_intersects_requests_and_available() {
        let pkg = update_pkg_with_files(&["a", "zbi", "c"]).await;

        assert_eq!(
            pkg.resolve_images(&[ImageType::Zbi, ImageType::Bootloader]).await.unwrap(),
            UnverifiedImageList(vec![Image::new(ImageType::Zbi, None),])
        );
    }

    #[test]
    fn resolve_ignores_missing_entries() {
        assert_eq!(
            resolve(&[ImageType::Zbi, ImageType::Bootloader], &btreeset! { "zbi".to_owned() }),
            UnverifiedImageList(vec![Image::new(ImageType::Zbi, None)])
        );
    }

    #[test]
    fn resolve_allows_missing_subtypes() {
        assert_eq!(
            resolve(&[ImageType::Firmware], &btreeset! { "not_firmware".to_owned() }),
            UnverifiedImageList(vec![])
        );
    }

    #[test]
    fn resolve_ignores_subtype_entry_with_underscore_and_no_contents() {
        assert_eq!(
            resolve(&[ImageType::Firmware], &btreeset! { "firmware_".to_owned() }),
            UnverifiedImageList(vec![])
        );
    }

    #[test]
    fn resolve_ignores_subtype_entry_without_underscore_and_subtype() {
        // firmware2 doesn't follow the {base}_{subtype} format, should be ignored.
        assert_eq!(
            resolve(
                &[ImageType::Firmware],
                &btreeset! { "firmware_a".to_owned(), "firmware2".to_owned() }
            ),
            UnverifiedImageList(vec![Image::new(ImageType::Firmware, Some("a"))])
        );
    }

    #[test]
    fn resolve_allows_subtypes() {
        assert_eq!(
            resolve(
                &[ImageType::Firmware],
                &btreeset! {
                "firmware_b".to_owned(),}
            ),
            UnverifiedImageList(vec![Image::new(ImageType::Firmware, Some("b")),])
        );
    }

    #[test]
    fn resolve_preserves_request_order() {
        assert_eq!(
            resolve(
                &[ImageType::Zbi, ImageType::Bootloader, ImageType::FuchsiaVbmeta],
                &btreeset! { "fuchsia.vbmeta".to_owned(), "bootloader".to_owned(), "zbi".to_owned() }
            ),
            UnverifiedImageList(vec![
                Image::new(ImageType::Zbi, None),
                Image::new(ImageType::Bootloader, None),
                Image::new(ImageType::FuchsiaVbmeta, None),
            ])
        );
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        maplit::btreemap,
        serde_json::json,
        std::{fs::File, io::Write, sync::Arc},
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::read_only_static, pseudo_directory,
        },
    };

    fn hash(n: u8) -> Hash {
        Hash::from([n; 32])
    }

    fn hashstr(n: u8) -> String {
        hash(n).to_string()
    }

    fn test_url(data: &str) -> AbsoluteComponentUrl {
        format!("fuchsia-pkg://fuchsia.com/update-images-firmware/0?hash=000000000000000000000000000000000000000000000000000000000000000a#{data}").parse().unwrap()
    }

    fn image_package_url(name: &str, hash: u8) -> PinnedAbsolutePackageUrl {
        format!("fuchsia-pkg://fuchsia.com/{name}/0?hash={}", hashstr(hash)).parse().unwrap()
    }

    fn image_package_resource_url(name: &str, hash: u8, resource: &str) -> AbsoluteComponentUrl {
        format!("fuchsia-pkg://fuchsia.com/{name}/0?hash={}#{resource}", hashstr(hash))
            .parse()
            .unwrap()
    }

    #[test]
    fn image_metadata_for_path_empty() {
        let tmp = tempfile::tempdir().expect("/tmp to exist");
        let path = tmp.path().join("empty");
        let mut f = File::create(&path).unwrap();
        f.write_all(b"").unwrap();
        drop(f);

        let resource = "resource";
        let url = image_package_url("package", 1);

        assert_eq!(
            ImageMetadata::for_path(&path, url, resource.to_string()).unwrap(),
            ImageMetadata::new(
                0,
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855".parse().unwrap(),
                image_package_resource_url("package", 1, resource)
            ),
        );
    }

    #[test]
    fn image_metadata_for_path_with_unaligned_data() {
        let tmp = tempfile::tempdir().expect("/tmp to exist");
        let path = tmp.path().join("empty");
        let mut f = File::create(&path).unwrap();
        f.write_all(&[0; 8192 + 4096]).unwrap();
        drop(f);

        let resource = "resource";

        let url = image_package_url("package", 1);

        assert_eq!(
            ImageMetadata::for_path(&path, url, resource.to_string()).unwrap(),
            ImageMetadata::new(
                8192 + 4096,
                "f3cc103136423a57975750907ebc1d367e2985ac6338976d4d5a439f50323f4a".parse().unwrap(),
                image_package_resource_url("package", 1, resource)
            ),
        );
    }

    #[test]
    fn parses_minimal_manifest() {
        let raw_json = json!({
            "version": "1",
            "contents": { "partitions" : [], "firmware" : []},
        })
        .to_string();

        let actual = parse_image_packages_json(raw_json.as_bytes()).unwrap();
        assert_eq!(actual, ImagePackagesManifest { partitions: vec![], firmware: vec![] });
    }

    #[test]
    fn builder_builds_minimal() {
        assert_eq!(
            ImagePackagesManifest::builder().build(),
            VersionedImagePackagesManifest::Version1(ImagePackagesManifest {
                partitions: vec![],
                firmware: vec![],
            }),
        );
    }

    #[test]
    fn builder_builds_populated_manifest() {
        let actual = ImagePackagesManifest::builder()
            .fuchsia_package(
                ImageMetadata::new(1, hash(1),  image_package_resource_url("update-images-fuchsia", 9, "zbi")),
                Some(ImageMetadata::new(2, hash(2), image_package_resource_url("update-images-fuchsia", 8, "vbmeta"))),
            )
            .recovery_package(
                ImageMetadata::new(3, hash(3),  image_package_resource_url("update-images-recovery", 7, "zbi")),
                None,
            )
            .firmware_package(
                    btreemap! {
                        "".to_owned() => ImageMetadata::new(5, hash(5), image_package_resource_url("update-images-firmware", 6, "a")
                    ),
                        "bl2".to_owned() => ImageMetadata::new(6, hash(6), image_package_resource_url("update-images-firmware", 5, "b")
                    ),
                    },
                )
            .clone()
            .build();
        assert_eq!(
            actual,
            VersionedImagePackagesManifest::Version1(ImagePackagesManifest {
                partitions: vec![
                    AssemblyImageFormat {
                        slot: Slot::Fuchsia,
                        r#type: SlotImage::Zbi,
                        size: 1,
                        hash: hash(1),
                        url: image_package_resource_url("update-images-fuchsia", 9, "zbi"),
                    },
                    AssemblyImageFormat {
                        slot: Slot::Fuchsia,
                        r#type: SlotImage::Vbmeta,
                        size: 2,
                        hash: hash(2),
                        url: image_package_resource_url("update-images-fuchsia", 8, "vbmeta"),
                    },
                    AssemblyImageFormat {
                        slot: Slot::Recovery,
                        r#type: SlotImage::Zbi,
                        size: 3,
                        hash: hash(3),
                        url: image_package_resource_url("update-images-recovery", 7, "zbi"),
                    },
                ],
                firmware: vec![
                    FirmwareImageFormat {
                        r#type: "".to_owned(),
                        size: 5,
                        hash: hash(5),
                        url: image_package_resource_url("update-images-firmware", 6, "a"),
                    },
                    FirmwareImageFormat {
                        r#type: "bl2".to_owned(),
                        size: 6,
                        hash: hash(6),
                        url: image_package_resource_url("update-images-firmware", 5, "b"),
                    },
                ],
            })
        );
    }

    #[test]
    fn parses_example_manifest() {
        let raw_json = json!({
            "version": "1",
            "contents":  {
                "partitions": [
                    {
                    "slot" : "fuchsia",
                    "type" : "zbi",
                    "size" : 1,
                    "hash" : hashstr(1),
                    "url" : image_package_resource_url("package", 1, "zbi")
                }, {
                    "slot" : "fuchsia",
                    "type" : "vbmeta",
                    "size" : 2,
                    "hash" : hashstr(2),
                    "url" : image_package_resource_url("package", 1, "vbmeta")
                },
                {
                    "slot" : "recovery",
                    "type" : "zbi",
                    "size" : 3,
                    "hash" : hashstr(3),
                    "url" : image_package_resource_url("package", 1, "rzbi")
                }, {
                    "slot" : "recovery",
                    "type" : "vbmeta",
                    "size" : 3,
                    "hash" : hashstr(3),
                    "url" : image_package_resource_url("package", 1, "rvbmeta")
                    },
                ],
                "firmware": [
                    {
                        "type" : "",
                        "size" : 5,
                        "hash" : hashstr(5),
                        "url" : image_package_resource_url("package", 1, "firmware")
                    }, {
                        "type" : "bl2",
                        "size" : 6,
                        "hash" : hashstr(6),
                        "url" : image_package_resource_url("package", 1, "firmware")
                    },
                ],

            }

            }
        )
        .to_string();

        let actual = parse_image_packages_json(raw_json.as_bytes()).unwrap();
        assert_eq!(
            ImagePackagesSlots::from(actual),
            ImagePackagesSlots {
                fuchsia: Some(BootSlot {
                    zbi: ImageMetadata::new(
                        1,
                        hash(1),
                        image_package_resource_url("package", 1, "zbi")
                    ),
                    vbmeta: Some(ImageMetadata::new(
                        2,
                        hash(2),
                        image_package_resource_url("package", 1, "vbmeta")
                    )),
                },),
                recovery: Some(BootSlot {
                    zbi: ImageMetadata::new(
                        3,
                        hash(3),
                        image_package_resource_url("package", 1, "rzbi")
                    ),
                    vbmeta: Some(ImageMetadata::new(
                        3,
                        hash(3),
                        image_package_resource_url("package", 1, "rvbmeta")
                    )),
                }),
                firmware: btreemap! {
                    "".to_owned() => ImageMetadata::new(5, hash(5),  image_package_resource_url("package", 1, "firmware")),
                    "bl2".to_owned() => ImageMetadata::new(6, hash(6),  image_package_resource_url("package", 1, "firmware")),
                },
            }
        );
    }

    #[test]
    fn rejects_duplicate_image_keys() {
        let raw_json = json!({
            "version": "1",
            "contents":  {
                "partitions": [ {
                    "slot" : "fuchsia",
                    "type" : "zbi",
                    "size" : 1,
                    "hash" : hashstr(1),
                    "url" : image_package_resource_url("package", 1, "zbi")
                }, {
                    "slot" : "fuchsia",
                    "type" : "zbi",
                    "size" : 1,
                    "hash" : hashstr(1),
                    "url" : image_package_resource_url("package", 1, "zbi")
                },
                ],
                "firmware": [],
            }
        })
        .to_string();

        assert_matches!(parse_image_packages_json(raw_json.as_bytes()), Err(_));
    }

    #[test]
    fn rejects_duplicate_firmware_keys() {
        let raw_json = json!({
            "version": "1",
            "contents":  {
                "partitions": [],
                "firmware": [
                    {
                        "type" : "",
                        "size" : 5,
                        "hash" : hashstr(5),
                        "url" : image_package_resource_url("package", 1, "firmware")
                    }, {
                        "type" : "",
                        "size" : 5,
                        "hash" : hashstr(5),
                        "url" : image_package_resource_url("package", 1, "firmware")
                    },
                ],
            }
        })
        .to_string();

        assert_matches!(parse_image_packages_json(raw_json.as_bytes()), Err(_));
    }

    #[test]
    fn rejects_vbmeta_without_zbi() {
        let raw_json = json!({
            "version": "1",
            "contents":  {
                "partitions": [{
                    "slot" : "fuchsia",
                    "type" : "vbmeta",
                    "size" : 1,
                    "hash" : hashstr(1),
                    "url" : image_package_resource_url("package", 1, "vbmeta")
                }],
                "firmware": [],
            }
        })
        .to_string();

        assert_matches!(parse_image_packages_json(raw_json.as_bytes()), Err(_));
    }

    #[test]
    fn rejects_urls_without_hash_paritions() {
        let raw_json = json!({
            "version": "1",
            "contents":  {
                "partitions": [{
                    "slot" : "fuchsia",
                    "type" : "zbi",
                    "size" : 1,
                    "hash" : hashstr(1),
                    "url" : "fuchsia-pkg://fuchsia.com/package/0#zbi"
                }],
                "firmware": [],
            }
        })
        .to_string();

        assert_matches!(parse_image_packages_json(raw_json.as_bytes()), Err(_));
    }

    #[test]
    fn rejects_urls_without_hash_firmware() {
        let raw_json = json!({
            "version": "1",
            "contents":  {
                "partitions": [],
                "firmware": [{
                    "type" : "",
                    "size" : 5,
                    "hash" : hashstr(5),
                    "url" : "fuchsia-pkg://fuchsia.com/package/0#firmware"
                }],
            }
        })
        .to_string();

        assert_matches!(parse_image_packages_json(raw_json.as_bytes()), Err(_));
    }

    #[test]
    fn verify_mode_normal_requires_zbi() {
        let with_zbi = ImagePackagesSlots {
            fuchsia: Some(BootSlot {
                zbi: ImageMetadata::new(1, hash(1), test_url("zbi")),
                vbmeta: None,
            }),
            recovery: None,
            firmware: btreemap! {},
        };

        assert_eq!(with_zbi.verify(UpdateMode::Normal), Ok(()));

        let without_zbi =
            ImagePackagesSlots { fuchsia: None, recovery: None, firmware: btreemap! {} };

        assert_eq!(without_zbi.verify(UpdateMode::Normal), Err(VerifyError::MissingZbi));
    }

    #[test]
    fn verify_mode_force_recovery_requires_no_zbi() {
        let with_zbi = ImagePackagesSlots {
            fuchsia: Some(BootSlot {
                zbi: ImageMetadata::new(1, hash(1), test_url("zbi")),
                vbmeta: None,
            }),
            recovery: None,
            firmware: btreemap! {},
        };

        assert_eq!(with_zbi.verify(UpdateMode::ForceRecovery), Err(VerifyError::UnexpectedZbi));

        let without_zbi =
            ImagePackagesSlots { fuchsia: None, recovery: None, firmware: btreemap! {} };

        assert_eq!(without_zbi.verify(UpdateMode::ForceRecovery), Ok(()));
    }

    fn spawn_vfs(dir: Arc<vfs::directory::immutable::simple::Simple>) -> fio::DirectoryProxy {
        let (proxy, proxy_server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
            0,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::new(proxy_server_end.into_channel()),
        );
        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn image_packages_detects_missing_manifest() {
        let proxy = spawn_vfs(pseudo_directory! {});

        assert_matches!(image_packages(&proxy).await, Err(ImagePackagesError::NotFound));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn image_packages_detects_invalid_json() {
        let proxy = spawn_vfs(pseudo_directory! {
            "images.json" => read_only_static("not json!"),
        });

        assert_matches!(image_packages(&proxy).await, Err(ImagePackagesError::Parse(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn image_packages_loads_valid_manifest() {
        let proxy = spawn_vfs(pseudo_directory! {
            "images.json" => read_only_static(r#"{
"version": "1",
"contents": { "partitions" : [], "firmware" : [] }
}"#),
        });

        assert_eq!(
            image_packages(&proxy).await.unwrap(),
            ImagePackagesManifest { partitions: vec![], firmware: vec![] }
        );
    }

    #[fuchsia::test]
    fn boot_slot_accessors() {
        let slot = BootSlot {
            zbi: ImageMetadata::new(1, hash(1), test_url("zbi")),
            vbmeta: Some(ImageMetadata::new(2, hash(2), test_url("vbmeta"))),
        };

        assert_eq!(slot.zbi(), &ImageMetadata::new(1, hash(1), test_url("zbi")));
        assert_eq!(slot.vbmeta(), Some(&ImageMetadata::new(2, hash(2), test_url("vbmeta"))));

        let slot = BootSlot { zbi: ImageMetadata::new(1, hash(1), test_url("zbi")), vbmeta: None };
        assert_eq!(slot.vbmeta(), None);
    }

    #[fuchsia::test]
    fn image_packages_manifest_accessors() {
        let slot = BootSlot {
            zbi: ImageMetadata::new(1, hash(1), test_url("zbi")),
            vbmeta: Some(ImageMetadata::new(2, hash(2), test_url("vbmeta"))),
        };

        let mut builder = ImagePackagesManifest::builder();
        builder.fuchsia_package(
            ImageMetadata::new(1, hash(1), test_url("zbi")),
            Some(ImageMetadata::new(2, hash(2), test_url("vbmeta"))),
        );
        let VersionedImagePackagesManifest::Version1(manifest) = builder.build();

        assert_eq!(manifest.fuchsia(), Some(slot.clone()));
        assert_eq!(manifest.recovery(), None);
        assert_eq!(manifest.firmware(), btreemap! {});

        let mut builder = ImagePackagesManifest::builder();
        builder.recovery_package(
            ImageMetadata::new(1, hash(1), test_url("zbi")),
            Some(ImageMetadata::new(2, hash(2), test_url("vbmeta"))),
        );
        let VersionedImagePackagesManifest::Version1(manifest) = builder.build();

        assert_eq!(manifest.fuchsia(), None);
        assert_eq!(manifest.recovery(), Some(slot));
        assert_eq!(manifest.firmware(), btreemap! {});

        let mut builder = ImagePackagesManifest::builder();
        builder.firmware_package(btreemap! {
            "".to_owned() => ImageMetadata::new(5, hash(5), image_package_resource_url("update-images-firmware", 6, "a")) }
        );
        let VersionedImagePackagesManifest::Version1(manifest) = builder.build();
        assert_eq!(manifest.fuchsia(), None);
        assert_eq!(manifest.recovery(), None);
        assert_eq!(
            manifest.firmware(),
            btreemap! {"".to_owned() => ImageMetadata::new(5, hash(5), image_package_resource_url("update-images-firmware", 6, "a"))}
        )
    }

    #[fuchsia::test]
    fn firmware_image_format_to_image_metadata() {
        let assembly_firmware = FirmwareImageFormat {
            r#type: "".to_string(),
            size: 1,
            hash: hash(1),
            url: image_package_resource_url("package", 1, "firmware"),
        };

        let image_meta_data = ImageMetadata {
            size: 1,
            hash: hash(1),
            url: image_package_resource_url("package", 1, "firmware"),
        };

        let firmware_into: ImageMetadata = assembly_firmware.metadata();

        assert_eq!(firmware_into, image_meta_data);
    }

    #[fuchsia::test]
    fn assembly_image_format_to_image_metadata() {
        let assembly_image = AssemblyImageFormat {
            slot: Slot::Fuchsia,
            r#type: SlotImage::Zbi,
            size: 1,
            hash: hash(1),
            url: image_package_resource_url("package", 1, "image"),
        };

        let image_meta_data = ImageMetadata {
            size: 1,
            hash: hash(1),
            url: image_package_resource_url("package", 1, "image"),
        };

        let image_into: ImageMetadata = assembly_image.metadata();

        assert_eq!(image_into, image_meta_data);
    }

    #[fuchsia::test]
    fn manifest_conversion_minimal() {
        let manifest = ImagePackagesManifest { partitions: vec![], firmware: vec![] };

        let slots = ImagePackagesSlots { fuchsia: None, recovery: None, firmware: btreemap! {} };

        let translated_manifest: ImagePackagesSlots = manifest.into();
        assert_eq!(translated_manifest, slots);
    }

    #[fuchsia::test]
    fn manifest_conversion_maximal() {
        let manifest = ImagePackagesManifest {
            partitions: vec![
                AssemblyImageFormat {
                    slot: Slot::Fuchsia,
                    r#type: SlotImage::Zbi,
                    size: 1,
                    hash: hash(1),
                    url: test_url("1"),
                },
                AssemblyImageFormat {
                    slot: Slot::Fuchsia,
                    r#type: SlotImage::Vbmeta,
                    size: 2,
                    hash: hash(2),
                    url: test_url("2"),
                },
                AssemblyImageFormat {
                    slot: Slot::Recovery,
                    r#type: SlotImage::Zbi,
                    size: 3,
                    hash: hash(3),
                    url: test_url("3"),
                },
                AssemblyImageFormat {
                    slot: Slot::Recovery,
                    r#type: SlotImage::Vbmeta,
                    size: 4,
                    hash: hash(4),
                    url: test_url("4"),
                },
            ],
            firmware: vec![
                FirmwareImageFormat {
                    r#type: "".to_string(),
                    size: 5,
                    hash: hash(5),
                    url: test_url("5"),
                },
                FirmwareImageFormat {
                    r#type: "bl2".to_string(),
                    size: 6,
                    hash: hash(6),
                    url: test_url("6"),
                },
            ],
        };

        let slots = ImagePackagesSlots {
            fuchsia: Some(BootSlot {
                zbi: ImageMetadata::new(1, hash(1), test_url("1")),
                vbmeta: Some(ImageMetadata::new(2, hash(2), test_url("2"))),
            }),
            recovery: Some(BootSlot {
                zbi: ImageMetadata::new(3, hash(3), test_url("3")),
                vbmeta: Some(ImageMetadata::new(4, hash(4), test_url("4"))),
            }),
            firmware: btreemap! {
                "".to_owned() => ImageMetadata::new(5, hash(5), test_url("5")),
                "bl2".to_owned() => ImageMetadata::new(6, hash(6), test_url("6")),
            },
        };

        let translated_manifest: ImagePackagesSlots = manifest.into();
        assert_eq!(translated_manifest, slots);
    }
}
