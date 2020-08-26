// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::{Status, VmoChildOptions},
    std::fmt,
    thiserror::Error,
};

/// An error encountered while opening an image.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum OpenImageError {
    #[error("while opening the file: {0}")]
    OpenFile(#[from] io_util::node::OpenError),

    #[error("while calling get_buffer: {0}")]
    FidlGetBuffer(#[source] fidl::Error),

    #[error("while obtaining vmo of file: {0}")]
    GetBuffer(fuchsia_zircon::Status),

    #[error("remote reported success without providing a vmo")]
    MissingBuffer,

    #[error("while converting vmo to a resizable vmo: {0}")]
    CloneBuffer(fuchsia_zircon::Status),
}

/// An identifier for an image that can be paved.
#[derive(Clone, PartialEq, Eq)]
pub struct Image {
    filename: String,

    /// Byte offset of '_' character separating name and subtype, if this image has a non-empty
    /// subtype.
    type_separator: Option<usize>,
}

impl Image {
    /// Construct an Image using the given filename that does not contain a subtype.
    pub fn new(filename: impl Into<String>) -> Self {
        Self { filename: filename.into(), type_separator: None }
    }

    /// Attempt to construct an Image using the given `filename` if it is of the form "{name}" or
    /// "{name}_{subtype}" where name is equal to the given `base`, or return None if `filename`
    /// does not start with `base`.
    ///
    /// # Examples
    ///
    /// `foo_bar` starts with `foo`, so it matches:
    /// ```
    /// let image = Image::matches_base("foo_bar", "foo").unwrap();
    /// assert_eq!(image.subtype(), Some("bar"));
    /// ```
    ///
    /// `foo_bar` doesn't start with `bar`, so it doesn't match:
    /// ```
    /// let image = Image::matches_base("foo_bar", "bar");
    /// assert_eq!(image, None);
    /// ```
    pub(crate) fn matches_base(filename: impl Into<String>, base: &str) -> Option<Self> {
        let filename = filename.into();

        match filename.strip_prefix(base) {
            None | Some("_") => None,
            Some("") => Some(Self { filename, type_separator: None }),
            Some(subtype) if !subtype.starts_with('_') => None,
            Some(subtype) => {
                Some(Self { type_separator: Some(filename.len() - subtype.len()), filename })
            }
        }
    }

    /// Test helper to construct an Image from a given name and optional subtype.
    pub fn join<'a>(name: &'a str, subtype: impl Into<Option<&'a str>>) -> Self {
        match subtype.into() {
            Some("") | None => Self { filename: name.to_owned(), type_separator: None },
            Some(subtype) => {
                Self { filename: format!("{}_{}", name, subtype), type_separator: Some(name.len()) }
            }
        }
    }

    /// The filename of the image relative to the update package.
    pub fn filename(&self) -> &str {
        &self.filename
    }

    /// The name of this image as understood by the system updater.
    pub fn name(&self) -> &str {
        match self.type_separator {
            Some(n) => &self.filename[..n],
            None => &self.filename[..],
        }
    }

    /// The name of this image as understood by the system updater.
    pub fn classify(&self) -> Option<ImageClass> {
        match self.name() {
            "zbi" | "zbi.signed" => Some(ImageClass::Zbi),
            "fuchsia.vbmeta" => Some(ImageClass::ZbiVbmeta),
            "zedboot" | "zedboot.signed" | "recovery" => Some(ImageClass::Recovery),
            "recovery.vbmeta" => Some(ImageClass::RecoveryVbmeta),
            "bootloader" | "firmware" => {
                // Keep support for update packages still using the older "bootloader" file, which
                // is handled identically to "firmware" but without subtype support.
                Some(ImageClass::Firmware)
            }
            _ => None,
        }
    }

    /// The particular type of this image as understood by the paver service, if present.
    pub fn subtype(&self) -> Option<&str> {
        self.type_separator.map(|n| &self.filename[(n + 1)..])
    }
}

impl fmt::Debug for Image {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Image")
            .field("filename", &self.filename())
            .field("name", &self.name())
            .field("subtype", &self.subtype())
            .field("class", &self.classify())
            .finish()
    }
}

/// A classification for the type of an image.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImageClass {
    /// Kernel image
    Zbi,

    /// Metadata for [`ImageClass::Zbi`]
    ZbiVbmeta,

    /// Recovery image
    Recovery,

    /// Metadata for [`ImageClass::Recovery`]
    RecoveryVbmeta,

    /// Bootloader firmware
    Firmware,
}

impl ImageClass {
    /// Determines if this image class would target a recovery partition.
    pub fn targets_recovery(self) -> bool {
        match self {
            ImageClass::Recovery | ImageClass::RecoveryVbmeta => true,
            ImageClass::Zbi | ImageClass::ZbiVbmeta | ImageClass::Firmware => false,
        }
    }
}

pub(crate) async fn open(proxy: &DirectoryProxy, image: &Image) -> Result<Buffer, OpenImageError> {
    let file = io_util::directory::open_file(
        proxy,
        image.filename(),
        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
    )
    .await?;

    let (status, buffer) = file
        .get_buffer(fidl_fuchsia_io::VMO_FLAG_READ)
        .await
        .map_err(OpenImageError::FidlGetBuffer)?;
    Status::ok(status).map_err(OpenImageError::GetBuffer)?;
    let buffer = buffer.ok_or(OpenImageError::MissingBuffer)?;

    // The paver service requires VMOs that are resizable, and blobfs does not give out resizable
    // VMOs. Fortunately, a copy-on-write child clone of the vmo can be made resizable.
    let vmo = buffer
        .vmo
        .create_child(VmoChildOptions::COPY_ON_WRITE | VmoChildOptions::RESIZABLE, 0, buffer.size)
        .map_err(OpenImageError::CloneBuffer)?;

    Ok(Buffer { size: buffer.size, vmo })
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::UpdatePackage, matches::assert_matches, proptest::prelude::*,
        proptest_derive::Arbitrary,
    };

    const TEST_PATH: &str = "test/update-package-lib-test";
    const TEST_PATH_IN_NAMESPACE: &str = "/pkg/test/update-package-lib-test";

    #[test]
    fn image_new() {
        assert_eq!(Image::new("foo"), Image { filename: "foo".to_owned(), type_separator: None });
    }

    #[test]
    fn image_matches_base() {
        assert_eq!(Image::matches_base("foo_bar", "bar"), None);
        assert_eq!(
            Image::matches_base("foobar", "foobar"),
            Some(Image { filename: "foobar".to_owned(), type_separator: None })
        );
        assert_eq!(
            Image::matches_base("foo_bar", "foo"),
            Some(Image { filename: "foo_bar".to_owned(), type_separator: Some(3) })
        );
        assert_eq!(
            Image::matches_base("foo_a", "foo"),
            Some(Image { filename: "foo_a".to_owned(), type_separator: Some(3) })
        );
    }

    #[test]
    fn image_matches_base_rejects_underscore_with_no_subtype() {
        assert_eq!(Image::matches_base("foo_", "foo"), None);
    }

    #[test]
    fn image_matches_base_rejects_no_underscore_before_subtype() {
        assert_eq!(Image::matches_base("foo2", "foo"), None);
    }

    #[test]
    fn image_join() {
        assert_eq!(Image::join("foo", None), Image::new("foo"));
        assert_eq!(Image::join("foo", ""), Image::new("foo"));
        assert_eq!(Image::join("foo", "bar"), Image::matches_base("foo_bar", "foo").unwrap());
    }

    #[test]
    fn image_typed_accessors() {
        let with_subtype = Image::matches_base("foo_bar", "foo").unwrap();
        assert_eq!(with_subtype.filename(), "foo_bar");
        assert_eq!(with_subtype.name(), "foo");
        assert_eq!(with_subtype.subtype(), Some("bar"));

        let empty_subtype = Image::matches_base("foo", "foo").unwrap();
        assert_eq!(empty_subtype.filename(), "foo");
        assert_eq!(empty_subtype.name(), "foo");
        assert_eq!(empty_subtype.subtype(), None);
    }

    #[test]
    fn recovery_images_target_recovery() {
        for name in &["zedboot", "zedboot.signed", "recovery", "recovery.vbmeta"] {
            assert!(
                Image::new(*name).classify().unwrap().targets_recovery(),
                "image {} should target recovery",
                name
            );
        }
    }

    #[test]
    fn non_recovery_images_do_not_target_recovery() {
        for name in &["zbi", "zbi.signed", "fuchsia.vbmeta", "firmware", "unknown"] {
            if let Some(image) = Image::new(*name).classify() {
                assert!(!image.targets_recovery(), "image {} should not target recovery", name);
            }
        }
    }

    fn open_this_package_as_update_package() -> UpdatePackage {
        let pkg = io_util::directory::open_in_namespace(
            "/pkg",
            io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
        )
        .unwrap();

        UpdatePackage::new(pkg)
    }

    #[derive(Debug, Arbitrary)]
    enum ImageConstructor {
        New,
        MatchesBase,
        Join,
    }

    prop_compose! {
        fn arb_image()(
            constructor: ImageConstructor,
            name: String,
            subtype: Option<String>,
        ) -> Image {
            let subtype = subtype.as_ref().map(String::as_str);
            let image = Image::join(&name, subtype);

            match constructor {
                ImageConstructor::New => Image::new(image.filename()),
                ImageConstructor::MatchesBase => {
                    Image::matches_base(image.filename(), image.name()).unwrap()
                }
                ImageConstructor::Join => image,
            }
        }
    }

    proptest! {
        #[test]
        fn image_accessors_do_not_panic(image in arb_image()) {
            image.filename();
            image.name();
            image.subtype();
            image.classify();
            format!("{:?}", image);
        }

        #[test]
        fn filename_starts_with_name(image in arb_image()) {
            prop_assert!(image.filename().starts_with(image.name()));
        }

        #[test]
        fn filename_ends_with_underscore_subtype_if_present(image in arb_image()) {
            if let Some(subtype) = image.subtype() {
                let suffix = format!("_{}", subtype);
                prop_assert!(image.filename().ends_with(&suffix));
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_present_image_succeeds() {
        let pkg = open_this_package_as_update_package();

        assert_matches!(pkg.open_image(&Image::new(TEST_PATH)).await, Ok(_));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_missing_image_fails() {
        let pkg = open_this_package_as_update_package();

        assert_matches!(
            pkg.open_image(&Image::new("missing")).await,
            Err(OpenImageError::OpenFile(io_util::node::OpenError::OpenError(Status::NOT_FOUND)))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_image_buffer_matches_expected_contents() {
        let pkg = open_this_package_as_update_package();

        let buffer = pkg.open_image(&Image::new(TEST_PATH)).await.unwrap();

        let expected = std::fs::read(TEST_PATH_IN_NAMESPACE).unwrap();
        assert_eq!(expected.len() as u64, buffer.size);

        let mut actual = vec![0; buffer.size as usize];
        buffer.vmo.read(&mut actual[..], 0).unwrap();

        assert_eq!(expected, actual);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_image_buffer_is_resizable() {
        let pkg = open_this_package_as_update_package();

        let buffer = pkg.open_image(&Image::new(TEST_PATH)).await.unwrap();

        assert_eq!(buffer.vmo.set_size(buffer.size * 2), Ok(()));
    }
}
