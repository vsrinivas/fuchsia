// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{image::Image, image::ImageClass, image::ImageType, update_mode::UpdateMode},
    fidl_fuchsia_io as fio,
    fuchsia_hash::Hash,
    fuchsia_url::pkg_url::PinnedPkgUrl,
    fuchsia_zircon_status::Status,
    serde::{Deserialize, Serialize},
    std::collections::{BTreeSet, HashMap},
    thiserror::Error,
};

/// An error encountered while resolving images.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ResolveImagesError {
    #[error("while listing files in the update package")]
    ListCandidates(#[source] files_async::Error),
}

/// An error encountered while verifying an [`UnverifiedImageList`].
#[derive(Debug, Error, PartialEq, Eq)]
pub enum VerifyError {
    #[error("images list did not contain an entry for 'zbi' or 'zbi.signed'")]
    MissingZbi,

    #[error("images list unexpectedly contained an entry for 'zbi' or 'zbi.signed'")]
    UnexpectedZbi,
}

/// An error encountered while loading the images.json manifest.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ImagePackagesError {
    #[error("`images.json` not present in update package")]
    NotFound,

    #[error("while opening `images.json`")]
    Open(#[source] io_util::node::OpenError),

    #[error("while reading `images.json`")]
    Read(#[source] io_util::file::ReadError),

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
    let entries = files_async::readdir(proxy).await.map_err(ResolveImagesError::ListCandidates)?;

    let names = entries
        .into_iter()
        .filter_map(|entry| match entry.kind {
            files_async::DirentKind::File => Some(entry.name),
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

/// A versioned [`ImagePackagesManifest`].
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
#[serde(tag = "version", content = "contents", deny_unknown_fields)]
#[allow(missing_docs)]
pub enum VersionedImagePackagesManifest {
    #[serde(rename = "1")]
    Version1(ImagePackagesManifest),
}

/// A manifest describing the various image packages, all of which are optional, and each contains
/// a fuchsia-pkg URI for that package and a description of the assets it contains.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
#[serde(deny_unknown_fields)]
pub struct ImagePackagesManifest {
    fuchsia: Option<BootSlotImagePackage>,
    recovery: Option<BootSlotImagePackage>,
    firmware: Option<FirmwareImagePackage>,
}

/// An image package representing an A/B/R bootslot, expected to have a "zbi" and optional
/// "vbmeta" entry, and nothing else.
pub type BootSlotImagePackage = ImagePackage<BootSlot>;

/// An image package that can hold arbitrary assets.
pub type FirmwareImagePackage = ImagePackage<HashMap<String, ImageMetadata>>;

/// Metadata for an image package.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
#[serde(deny_unknown_fields)]
pub struct ImagePackage<Images> {
    /// The pavable images stored in this package.
    images: Images,

    /// Pinned fuchsia-pkg URI of package containing these images.
    url: PinnedPkgUrl,
}

/// Metadata for artifacts unique to an A/B/R boot slot.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
#[serde(deny_unknown_fields)]
pub struct BootSlot {
    /// The zircon boot image.
    zbi: ImageMetadata,

    /// The optional slot metadata.
    vbmeta: Option<ImageMetadata>,
}

/// Metadata necessary to determine if a payload matches an image without needing to have the
/// actual image.
#[derive(Serialize, Deserialize, Debug, PartialEq, Eq, Clone)]
#[serde(deny_unknown_fields)]
pub struct ImageMetadata {
    /// The size of the image.
    size: u64,

    /// The sha256 hash of the image.
    hash: Hash,
}

impl ImagePackagesManifest {
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
}

/// Returns structured `images.json` data based on raw file contents.
fn parse_image_packages_json(contents: &[u8]) -> Result<ImagePackagesManifest, ImagePackagesError> {
    let manifest = match serde_json::from_slice(contents).map_err(ImagePackagesError::Parse)? {
        VersionedImagePackagesManifest::Version1(manifest) => manifest,
    };

    Ok(manifest)
}

pub(crate) async fn image_packages(
    proxy: &fio::DirectoryProxy,
) -> Result<ImagePackagesManifest, ImagePackagesError> {
    let file = io_util::directory::open_file(&proxy, "images.json", fio::OpenFlags::RIGHT_READABLE)
        .await
        .map_err(|e| match e {
            io_util::node::OpenError::OpenError(Status::NOT_FOUND) => ImagePackagesError::NotFound,
            e => ImagePackagesError::Open(e),
        })?;

    let contents = io_util::file::read(&file).await.map_err(|e| ImagePackagesError::Read(e))?;

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
            io_util::OpenFlags::RIGHT_READABLE,
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
        maplit::hashmap,
        serde_json::json,
        std::sync::Arc,
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

    #[test]
    fn parses_minimal_manifest() {
        let raw_json = json!({
            "version": "1",
            "contents": {},
        })
        .to_string();

        let actual = parse_image_packages_json(raw_json.as_bytes()).unwrap();
        assert_eq!(actual, ImagePackagesManifest { fuchsia: None, recovery: None, firmware: None });
    }

    #[test]
    fn parses_example_manifest() {
        let raw_json = json!({
            "version": "1",
            "contents": {
                "fuchsia": {
                    "images": {
                        "zbi": {
                            "size": 1,
                            "hash": hashstr(1),
                        },
                        "vbmeta": {
                            "size": 2,
                            "hash": hashstr(2),
                        },
                    },
                    "url": "fuchsia-pkg://fuchsia.com/update-images-fuchsia/0?hash=000000000000000000000000000000000000000000000000000000000000000a"
                },
                "recovery": {
                    "images": {
                        "zbi": {
                            "size": 3,
                            "hash": hashstr(3),
                        },
                        "vbmeta": {
                            "size": 4,
                            "hash": hashstr(4),
                        },
                    },
                    "url": "fuchsia-pkg://fuchsia.com/update-images-recovery/0?hash=000000000000000000000000000000000000000000000000000000000000000b"
                },
                "firmware": {
                    "images": {
                        "": {
                            "size": 5,
                            "hash": hashstr(5),
                        },
                        "2bl": {
                            "size": 6,
                            "hash": hashstr(6),
                        },
                    },
                    "url": "fuchsia-pkg://fuchsia.com/update-images-firmware/0?hash=000000000000000000000000000000000000000000000000000000000000000c"
                }
            }
        }).to_string();

        let actual = parse_image_packages_json(raw_json.as_bytes()).unwrap();
        assert_eq!(
            actual,
            ImagePackagesManifest {
                fuchsia: Some(BootSlotImagePackage {
                    url: "fuchsia-pkg://fuchsia.com/update-images-fuchsia/0?hash=000000000000000000000000000000000000000000000000000000000000000a".parse().unwrap(),
                    images: BootSlot {
                        zbi: ImageMetadata { size: 1, hash: hash(1) },
                        vbmeta: Some(ImageMetadata { size: 2, hash: hash(2) }),
                    },
                }),
                recovery: Some(BootSlotImagePackage {
                    url: "fuchsia-pkg://fuchsia.com/update-images-recovery/0?hash=000000000000000000000000000000000000000000000000000000000000000b".parse().unwrap(),
                    images: BootSlot {
                        zbi: ImageMetadata { size: 3, hash: hash(3) },
                        vbmeta: Some(ImageMetadata { size: 4, hash: hash(4) }),
                    },
                }),
                firmware: Some(FirmwareImagePackage {
                    url: "fuchsia-pkg://fuchsia.com/update-images-firmware/0?hash=000000000000000000000000000000000000000000000000000000000000000c".parse().unwrap(),
                    images: hashmap! {
                        "".to_owned() => ImageMetadata { size: 5, hash: hash(5) },
                        "2bl".to_owned() => ImageMetadata { size: 6, hash: hash(6) },
                    },
                }),
            }
        );
    }

    #[test]
    fn verify_mode_normal_requires_zbi() {
        let with_zbi = ImagePackagesManifest {
            fuchsia: Some(BootSlotImagePackage {
                url: "fuchsia-pkg://fuchsia.com/update-images-fuchsia/0?hash=000000000000000000000000000000000000000000000000000000000000000a".parse().unwrap(),
                images: BootSlot {
                    zbi: ImageMetadata { size: 1, hash: hash(1) },
                    vbmeta: None,
                },
            }),
            recovery: None,
            firmware: None,
        };

        assert_eq!(with_zbi.verify(UpdateMode::Normal), Ok(()));

        let without_zbi = ImagePackagesManifest { fuchsia: None, recovery: None, firmware: None };

        assert_eq!(without_zbi.verify(UpdateMode::Normal), Err(VerifyError::MissingZbi));
    }

    #[test]
    fn verify_mode_force_recovery_requires_no_zbi() {
        let with_zbi = ImagePackagesManifest {
            fuchsia: Some(BootSlotImagePackage {
                url: "fuchsia-pkg://fuchsia.com/update-images-fuchsia/0?hash=000000000000000000000000000000000000000000000000000000000000000a".parse().unwrap(),
                images: BootSlot {
                    zbi: ImageMetadata { size: 1, hash: hash(1) },
                    vbmeta: None,
                },
            }),
            recovery: None,
            firmware: None,
        };

        assert_eq!(with_zbi.verify(UpdateMode::ForceRecovery), Err(VerifyError::UnexpectedZbi));

        let without_zbi = ImagePackagesManifest { fuchsia: None, recovery: None, firmware: None };

        assert_eq!(without_zbi.verify(UpdateMode::ForceRecovery), Ok(()));
    }

    fn spawn_vfs(dir: Arc<vfs::directory::immutable::simple::Simple>) -> fio::DirectoryProxy {
        let (proxy, proxy_server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            io_util::OpenFlags::RIGHT_READABLE,
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
"contents": {}
}"#),
        });

        assert_eq!(
            image_packages(&proxy).await.unwrap(),
            ImagePackagesManifest { fuchsia: None, recovery: None, firmware: None }
        );
    }
}
