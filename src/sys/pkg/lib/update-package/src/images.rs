// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{image::Image, update_mode::UpdateMode},
    fidl_fuchsia_io::DirectoryProxy,
    std::collections::BTreeSet,
    thiserror::Error,
};

const SUBTYPE_SUFFIX: &str = "[_type]";

/// An error encountered while resolving images.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ResolveImagesError {
    #[error("while listing files in the update package: {0}")]
    ListCandidates(#[from] files_async::Error),
}

/// An error encountered while verifying an [`UnverifiedImageList`].
#[derive(Debug, Error, PartialEq, Eq)]
pub enum VerifyError {
    #[error("images list did not contain an entry for 'zbi' or 'zbi.signed'")]
    MissingZbi,

    #[error("images list unexpectedly contained an entry for 'zbi' or 'zbi.signed'")]
    UnexpectedZbi,
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
        self.0.iter().any(|image| image.filename() == "zbi" || image.filename() == "zbi.signed")
    }

    /// Verify that this image list is appropriate for the given update mode.
    ///
    /// * `UpdateMode::Normal` - a kernel image (zbi or zbi.signed) is required.
    /// * `UpdateMode::ForceRecovery` - a kernel image must not be present.
    pub fn verify(self, mode: UpdateMode) -> Result<ImageList, VerifyError> {
        let contains_zbi_entry = self.contains_zbi_entry();
        match mode {
            UpdateMode::Normal if !contains_zbi_entry => Err(VerifyError::MissingZbi),
            UpdateMode::ForceRecovery if contains_zbi_entry => Err(VerifyError::UnexpectedZbi),
            _ => Ok(ImageList(self.0)),
        }
    }
}

async fn list_dir_files(proxy: &DirectoryProxy) -> Result<BTreeSet<String>, ResolveImagesError> {
    let entries = files_async::readdir(proxy).await?;

    let names = entries
        .into_iter()
        .filter_map(|entry| match entry.kind {
            files_async::DirentKind::File => Some(entry.name),
            _ => None,
        })
        .collect();

    Ok(names)
}

fn resolve(requests: &[String], available: &BTreeSet<String>) -> UnverifiedImageList {
    let mut res = vec![];

    for request in requests {
        match request.strip_suffix(SUBTYPE_SUFFIX) {
            Some(prefix) => {
                for candidate in available.iter() {
                    if let Some(request) = Image::matches_base(candidate, prefix) {
                        res.push(request);
                    }
                }
            }
            None => {
                if available.contains(request) {
                    res.push(Image::new(request));
                }
            }
        }
    }

    UnverifiedImageList(res)
}

pub(crate) async fn resolve_images(
    proxy: &DirectoryProxy,
    requests: &[String],
) -> Result<UnverifiedImageList, ResolveImagesError> {
    let files = list_dir_files(proxy).await?;

    Ok(resolve(requests, &files))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{TestUpdatePackage, UpdatePackage},
        fidl_fuchsia_io::DirectoryMarker,
        maplit::btreeset,
        matches::assert_matches,
        std::sync::Arc,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::pcb::read_only_static, pseudo_directory,
        },
    };

    #[test]
    fn verify_mode_normal_requires_zbi() {
        let images = vec![Image::new("foo"), Image::new("zbi")];
        assert_eq!(
            UnverifiedImageList(images.clone()).verify(UpdateMode::Normal),
            Ok(ImageList(images))
        );

        let images = vec![Image::new("foo"), Image::new("zbi.signed")];
        assert_eq!(
            UnverifiedImageList(images.clone()).verify(UpdateMode::Normal),
            Ok(ImageList(images))
        );

        assert_eq!(
            UnverifiedImageList(vec![
                Image::new("foo"),
                Image::new("bar"),
                Image::new("bootloader"),
            ])
            .verify(UpdateMode::Normal),
            Err(VerifyError::MissingZbi)
        );
    }

    #[test]
    fn verify_mode_force_recovery_requires_no_zbi() {
        let images = vec![Image::new("foo"), Image::new("bar"), Image::new("bootloader")];
        assert_eq!(
            UnverifiedImageList(images.clone()).verify(UpdateMode::ForceRecovery),
            Ok(ImageList(images))
        );

        assert_eq!(
            UnverifiedImageList(vec![Image::new("foo"), Image::new("zbi")])
                .verify(UpdateMode::ForceRecovery),
            Err(VerifyError::UnexpectedZbi)
        );

        assert_eq!(
            UnverifiedImageList(vec![Image::new("foo"), Image::new("zbi.signed")])
                .verify(UpdateMode::ForceRecovery),
            Err(VerifyError::UnexpectedZbi)
        );
    }

    #[test]
    fn image_list_filter_filters_preserving_order() {
        assert_eq!(
            ImageList(vec![Image::new("foo"), Image::new("bar"), Image::new("bootloader")])
                .filter(|image| image.name() != "bar"),
            ImageList(vec![Image::new("foo"), Image::new("bootloader")]),
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

    fn spawn_vfs(dir: Arc<vfs::directory::immutable::simple::Simple>) -> DirectoryProxy {
        let (proxy, proxy_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let scope = ExecutionScope::from_executor(Box::new(fuchsia_async::EHandle::local()));
        dir.open(
            scope,
            io_util::OPEN_RIGHT_READABLE,
            0,
            vfs::path::Path::empty(),
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
        let (proxy, _) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let pkg = UpdatePackage::new(proxy);

        assert_matches!(pkg.resolve_images(&[]).await, Err(ResolveImagesError::ListCandidates(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_images_intersects_requests_and_available() {
        let pkg = update_pkg_with_files(&["a", "b", "c"]).await;

        assert_eq!(
            pkg.resolve_images(&["b".to_owned(), "c".to_owned(), "e".to_owned()]).await.unwrap(),
            UnverifiedImageList(vec![Image::new("b"), Image::new("c"),])
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_images_expands_subtypes() {
        let pkg =
            update_pkg_with_files(&["firmware", "firmware_foo", "firmware_bar", "not_firmware"])
                .await;

        assert_eq!(
            pkg.resolve_images(&["firmware[_type]".to_owned()]).await.unwrap(),
            UnverifiedImageList(vec![
                Image::new("firmware"),
                Image::join("firmware", "bar"),
                Image::join("firmware", "foo"),
            ])
        );
    }

    #[test]
    fn resolve_ignores_subtype_entry_with_underscore_and_no_contents() {
        assert_eq!(
            resolve(&["firmware[_type]".to_owned()], &btreeset! { "firmware_".to_owned() }),
            UnverifiedImageList(vec![])
        );
    }

    #[test]
    fn resolve_ignores_subtype_entry_without_underscore_and_subtype() {
        // firmware2 doesn't follow the {base}_{subtype} format, should be ignored.
        assert_eq!(
            resolve(
                &["firmware[_type]".to_owned()],
                &btreeset! { "firmware_a".to_owned(), "firmware2".to_owned() }
            ),
            UnverifiedImageList(vec![Image::join("firmware", "a")])
        );
    }

    #[test]
    fn resolve_ignores_missing_entries() {
        assert_eq!(
            resolve(
                &["present".to_owned(), "not_present".to_owned()],
                &btreeset! { "present".to_owned() }
            ),
            UnverifiedImageList(vec![Image::new("present")])
        );
    }

    #[test]
    fn resolve_allows_underscores_in_filenames() {
        assert_eq!(
            resolve(
                &["no_subtype".to_owned(), "with_subtype[_type]".to_owned()],
                &btreeset! { "no_subtype".to_owned(), "with_subtype_a_b".to_owned() }
            ),
            UnverifiedImageList(vec![Image::new("no_subtype"), Image::join("with_subtype", "a_b")])
        );
    }

    #[test]
    fn resolve_allows_missing_subtypes() {
        assert_eq!(
            resolve(&["firmware[_type]".to_owned()], &btreeset! { "not_firmware".to_owned() }),
            UnverifiedImageList(vec![])
        );
    }

    #[test]
    fn resolve_preserves_request_order() {
        assert_eq!(
            resolve(
                &["z_first".to_owned(), "middle".to_owned(), "a_last".to_owned()],
                &btreeset! { "a_last".to_owned(), "middle".to_owned(), "z_first".to_owned() }
            ),
            UnverifiedImageList(vec![
                Image::new("z_first"),
                Image::new("middle"),
                Image::new("a_last"),
            ])
        );
    }
}
