// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::{Status, VmoChildOptions},
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

/// An identifier for an image type which corresponds to the file's name without
/// a subtype.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
#[cfg_attr(test, derive(proptest_derive::Arbitrary))]
pub enum ImageType {
    /// Kernel image.
    Zbi,

    /// Kernel image.
    ZbiSigned,

    /// Metadata for the kernel image.
    FuchsiaVbmeta,

    /// Recovery image.
    Zedboot,

    /// Recovery image.
    ZedbootSigned,

    /// Recovery image.
    Recovery,

    /// Metadata for recovery image.
    RecoveryVbmeta,

    /// Bootloader.
    Bootloader,

    /// Firmware
    Firmware,
}

impl ImageType {
    /// The name of the ImageType.
    pub fn name(&self) -> &'static str {
        match self {
            Self::Zbi => "zbi",
            Self::ZbiSigned => "zbi.signed",
            Self::FuchsiaVbmeta => "fuchsia.vbmeta",
            Self::Zedboot => "zedboot",
            Self::ZedbootSigned => "zedboot.signed",
            Self::Recovery => "recovery",
            Self::RecoveryVbmeta => "recovery.vbmeta",
            Self::Bootloader => "bootloader",
            Self::Firmware => "firmware",
        }
    }
}

/// An identifier for an image that can be paved.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Image {
    imagetype: ImageType,
    filename: String,
}

impl Image {
    /// Construct an Image using the given imagetype and optional subtype.
    pub fn new(imagetype: ImageType, subtype: Option<&str>) -> Self {
        let filename = match subtype {
            None => imagetype.name().to_string(),
            Some(subtype) => format!("{}_{}", imagetype.name(), subtype),
        };
        Self { imagetype, filename }
    }

    /// The imagetype of the image relative to the update package.
    pub fn imagetype(&self) -> ImageType {
        self.imagetype
    }

    /// The name of this image as understood by the system updater.
    pub fn classify(&self) -> ImageClass {
        match self.imagetype() {
            ImageType::Zbi | ImageType::ZbiSigned => ImageClass::Zbi,
            ImageType::FuchsiaVbmeta => ImageClass::ZbiVbmeta,
            ImageType::Zedboot | ImageType::ZedbootSigned | ImageType::Recovery => {
                ImageClass::Recovery
            }
            ImageType::RecoveryVbmeta => ImageClass::RecoveryVbmeta,
            ImageType::Bootloader | ImageType::Firmware => ImageClass::Firmware,
        }
    }

    /// Attempt to construct an Image using the given `filename` if it is of the form "{name}" or
    /// "{name}_{subtype}" where name is equal to the given `imagetype.name()`, or return None if
    /// `filename` does not start with `imagetype.name()`.
    ///
    /// # Examples
    ///
    /// `firmware_bar` starts with `firmware`, so it matches:
    /// ```
    /// let image = Image::matches_base("firmware_bar", ImageType::Firmware).unwrap();
    /// assert_eq!(image.subtype(), Some("bar"));
    /// ```
    ///
    /// `firmware_zbi` doesn't start with `zbi`, so it doesn't match:
    /// ```
    /// let image = Image::matches_base("firmware_zbi", ImageType::Zbi);
    /// assert_eq!(image, None);
    /// ```
    pub(crate) fn matches_base(filename: impl Into<String>, imagetype: ImageType) -> Option<Self> {
        let filename = filename.into();

        match filename.strip_prefix(imagetype.name()) {
            None | Some("_") => None,
            Some("") => Some(Self { imagetype, filename }),
            Some(subtype) if !subtype.starts_with('_') => None,
            Some(_) => Some(Self { imagetype, filename }),
        }
    }

    /// The particular type of this image as understood by the paver service, if present.
    pub fn subtype(&self) -> Option<&str> {
        if self.filename.len() == self.imagetype.name().len() {
            return None;
        }

        Some(&self.filename[(self.imagetype.name().len() + 1)..])
    }

    /// The filename of the image relative to the update package.
    pub fn name(&self) -> &str {
        &self.filename
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
    let file =
        io_util::directory::open_file(proxy, &image.name(), fidl_fuchsia_io::OPEN_RIGHT_READABLE)
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
        super::*, crate::TestUpdatePackage, matches::assert_matches, proptest::prelude::*,
        proptest_derive::Arbitrary,
    };

    #[test]
    fn image_new() {
        assert_eq!(
            Image::new(ImageType::Zbi, None),
            Image { imagetype: ImageType::Zbi, filename: "zbi".to_string() }
        );
    }

    #[test]
    fn recovery_images_target_recovery() {
        assert!(
            Image::new(ImageType::Zedboot, None).classify().targets_recovery(),
            "image zedboot should target recovery",
        );
        assert!(
            Image::new(ImageType::ZedbootSigned, None).classify().targets_recovery(),
            "image zedboot.signed should target recovery",
        );
        assert!(
            Image::new(ImageType::Recovery, None).classify().targets_recovery(),
            "image recovery should target recovery",
        );
        assert!(
            Image::new(ImageType::RecoveryVbmeta, None).classify().targets_recovery(),
            "image recovery.vbmeta should target recovery",
        );
    }

    #[test]
    fn non_recovery_images_do_not_target_recovery() {
        assert!(
            !Image::new(ImageType::Zbi, None).classify().targets_recovery(),
            "image zbi should not target recovery",
        );
        assert!(
            !Image::new(ImageType::ZbiSigned, None).classify().targets_recovery(),
            "image zbi.signed not should target recovery",
        );
        assert!(
            !Image::new(ImageType::FuchsiaVbmeta, None).classify().targets_recovery(),
            "image fuchsia.vbmeta should not target recovery",
        );
        assert!(
            !Image::new(ImageType::Firmware, None).classify().targets_recovery(),
            "image firmware should not target recovery",
        );
    }

    #[test]
    fn image_matches_base() {
        assert_eq!(Image::matches_base("foo_bar", ImageType::Zbi), None);
        assert_eq!(
            Image::matches_base("firmware", ImageType::Firmware),
            Some(Image { imagetype: ImageType::Firmware, filename: "firmware".to_string() })
        );
        assert_eq!(
            Image::matches_base("firmware_bar", ImageType::Firmware),
            Some(Image { imagetype: ImageType::Firmware, filename: "firmware_bar".to_string() })
        );
    }

    #[test]
    fn image_matches_base_rejects_underscore_with_no_subtype() {
        assert_eq!(Image::matches_base("firmware_", ImageType::Firmware), None);
    }

    #[test]
    fn image_matches_base_rejects_no_underscore_before_subtype() {
        assert_eq!(Image::matches_base("zbi3", ImageType::Zbi), None);
    }

    #[test]
    fn test_image_typed_accessors() {
        let image = Image::new(ImageType::Zbi, None);
        assert_eq!(image.name(), "zbi");
        assert_eq!(image.imagetype(), ImageType::Zbi);
        assert_eq!(image.subtype(), None);

        let image = Image::new(ImageType::Zbi, Some("ibz"));
        assert_eq!(image.name(), "zbi_ibz");
        assert_eq!(image.imagetype(), ImageType::Zbi);
        assert_eq!(image.subtype(), Some("ibz"));
    }

    #[derive(Debug, Arbitrary)]
    enum ImageConstructor {
        New,
        MatchesBase,
    }

    prop_compose! {
        fn arb_image()(
            constructor: ImageConstructor,
            imagetype: ImageType,
            subtype: Option<String>,
        ) -> Image {
            let subtype = subtype.as_ref().map(String::as_str);
            let image = Image::new(imagetype, subtype);

            match constructor {
                ImageConstructor::New => image,
                ImageConstructor::MatchesBase => {
                    Image::matches_base(imagetype.name(), imagetype).unwrap()
                }
            }
        }
    }

    proptest! {
        #[test]
        fn image_accessors_do_not_panic(image in arb_image()) {
            image.name();
            image.subtype();
            image.classify();
            format!("{:?}", image);
        }

        #[test]
        fn filename_starts_with_imagetype(image in arb_image()) {
            prop_assert!(image.name().starts_with(image.imagetype().name()));
        }

        #[test]
        fn filename_ends_with_underscore_subtype_if_present(image in arb_image()) {
            if let Some(subtype) = image.subtype() {
                let suffix = format!("_{}", subtype);
                prop_assert!(image.name().ends_with(&suffix));
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_present_image_succeeds() {
        assert_matches!(
            TestUpdatePackage::new()
                .add_file("zbi", "zbi contents")
                .await
                .open_image(&Image::new(ImageType::Zbi, None))
                .await,
            Ok(_)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_missing_image_fails() {
        assert_matches!(
            TestUpdatePackage::new().open_image(&Image::new(ImageType::Zbi, None)).await,
            Err(OpenImageError::OpenFile(io_util::node::OpenError::OpenError(Status::NOT_FOUND)))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_image_buffer_matches_expected_contents() {
        let update_pkg = TestUpdatePackage::new().add_file("zbi", "zbi contents").await;
        let buffer = update_pkg.open_image(&Image::new(ImageType::Zbi, None)).await.unwrap();
        let expected = &b"zbi contents"[..];
        assert_eq!(expected.len() as u64, buffer.size);

        let mut actual = vec![0; buffer.size as usize];
        buffer.vmo.read(&mut actual[..], 0).unwrap();

        assert_eq!(expected, actual);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_image_buffer_is_resizable() {
        let update_pkg = TestUpdatePackage::new().add_file("zbi", "zbi contents").await;
        let buffer = update_pkg.open_image(&Image::new(ImageType::Zbi, None)).await.unwrap();
        assert_eq!(buffer.vmo.set_size(buffer.size * 2), Ok(()));
    }
}
