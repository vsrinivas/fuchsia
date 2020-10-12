// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub type ExtentProperties = disk_extractor_lib::ExtentProperties;
pub type Error = disk_extractor_lib::Error;
pub use disk_extractor_lib::{extractor::Extractor, options::ExtractorOptions};
use std::{
    fs::File,
    os::{raw::c_int, unix::io::FromRawFd},
};

/// A simple structure to convert rust result into C compatible error type.
#[repr(C)]
#[no_mangle]
#[derive(Copy, Clone, Debug)]
#[must_use]
pub struct CResult {
    /// Set to `true` on success and false on failure.
    pub ok: bool,

    /// If an operation has failed i.e. `ok` is false, `kind` indicates the type
    /// of error.
    pub kind: Error,
}

impl CResult {
    /// Returns ok type CResult.
    pub fn ok() -> Self {
        CResult { ok: true, kind: Error::CannotOverride }
    }
}

/// Converts Result to new CResult.
impl From<Result<(), Error>> for CResult {
    fn from(result: Result<(), Error>) -> Self {
        if result.is_ok() {
            return CResult::ok();
        }
        CResult { ok: false, kind: result.err().unwrap() }
    }
}

/// Converts enum Error to new CResult.
impl From<Error> for CResult {
    fn from(kind: Error) -> Self {
        CResult { ok: false, kind }
    }
}

/// Creates a new [`Extractor`] and returns an opaque pointer to it.
///
/// # Arguments
/// `out_fd`: File descriptor pointing to rw file. The file will be truncated to
/// zero lenght.
/// `in_file`: File descriptor pointing to readable/seekable file.
/// `options`: [`ExtractorOptions`]
///
/// Asserts on failure to truncate.
#[no_mangle]
pub extern "C" fn extractor_new(
    in_fd: c_int,
    options: ExtractorOptions,
    out_fd: c_int,
    out_extractor: *mut *mut Extractor,
) -> CResult {
    if out_extractor.is_null() || options.alignment == 0 {
        return CResult::from(Error::InvalidArgument);
    }

    let out_file = unsafe { File::from_raw_fd(out_fd) };
    let in_file = unsafe { File::from_raw_fd(in_fd) };
    let metadata_or = out_file.metadata();
    if metadata_or.is_err() || metadata_or.unwrap().len() != 0 {
        return CResult::from(Error::ReadFailed);
    }
    unsafe {
        *out_extractor =
            Box::into_raw(Box::new(Extractor::new(Box::new(in_file), options, Box::new(out_file))));
    }
    CResult::ok()
}

/// Destroys an [`Extractor`] object.
#[no_mangle]
pub extern "C" fn extractor_delete(extractor: *mut Extractor) {
    if !extractor.is_null() {
        unsafe { Box::from_raw(extractor) };
    }
}

/// Adds a new extent to the `extractor`.
///
/// # Arguments
/// `offset`: Location where the extent's data can be found in `in_fd`.
/// `size`: Size of the extent in bytes.
/// `properties`: [`ExtentProperties`]
#[no_mangle]
#[must_use]
pub extern "C" fn extractor_add(
    extractor: &mut Extractor,
    offset: u64,
    size: u64,
    properties: ExtentProperties,
) -> CResult {
    if size == 0 {
        return CResult::from(Error::InvalidRange);
    }

    return CResult::from(extractor.add(offset..(offset + size), properties, None));
}

/// Writes staged extents to out_fd.
#[no_mangle]
#[must_use]
pub extern "C" fn extractor_write(extractor: &mut Extractor) -> CResult {
    return CResult::from(extractor.write().map(|_| ()));
}

#[cfg(test)]
mod test {
    use {
        crate::extractor::*,
        disk_extractor_lib::{DataKind, ExtentKind},
        std::{io::Write, os::unix::io::IntoRawFd},
        tempfile::tempfile,
    };

    fn new_file_based_extractor() -> (*mut Extractor, ExtractorOptions, File, File) {
        let options: ExtractorOptions = Default::default();
        let out_file = tempfile().unwrap();
        let mut in_file = tempfile().unwrap();
        for i in 0..64 {
            let buf = vec![i; options.alignment as usize];
            in_file.write_all(&buf).unwrap();
        }

        let mut extractor: *mut Extractor = std::ptr::null_mut();

        assert_eq!(
            extractor_new(
                in_file.try_clone().unwrap().into_raw_fd(),
                Default::default(),
                out_file.try_clone().unwrap().into_raw_fd(),
                &mut extractor,
            )
            .ok,
            true
        );
        (extractor, options, out_file, in_file)
    }

    #[test]
    fn test_new_with_null_output_argument() {
        let (_, options, out_file, in_file) = new_file_based_extractor();
        assert_eq!(options, Default::default());
        let result = extractor_new(
            in_file.try_clone().unwrap().into_raw_fd(),
            options,
            out_file.try_clone().unwrap().into_raw_fd(),
            std::ptr::null_mut(),
        );
        assert_eq!(result.ok, false);
        assert_eq!(result.kind, Error::InvalidArgument);
    }

    #[test]
    fn test_delete_null_does_not_panic() {
        extractor_delete(std::ptr::null_mut());
    }

    #[test]
    fn test_add_zero_size_extent() {
        let (extractor, _, _, _) = new_file_based_extractor();
        let properties = disk_extractor_lib::ExtentProperties {
            extent_kind: ExtentKind::Data,
            data_kind: DataKind::Unmodified,
        };
        assert_eq!(
            extractor_add(unsafe { extractor.as_mut().unwrap() }, 10, 0, properties).kind,
            Error::InvalidRange
        );
    }

    #[test]
    fn test_write() {
        let (extractor, options, out_file, _) = new_file_based_extractor();
        let properties = disk_extractor_lib::ExtentProperties {
            extent_kind: ExtentKind::Data,
            data_kind: DataKind::Unmodified,
        };

        // Assert that the out_file has zero length.
        assert_eq!(out_file.metadata().unwrap().len(), 0);
        assert_eq!(
            extractor_add(unsafe { extractor.as_mut().unwrap() }, 0, options.alignment, properties)
                .ok,
            true
        );
        let result = extractor_write(unsafe { extractor.as_mut().unwrap() });
        assert_eq!(result.ok, true, "{:?}", result);
        // Assert that something was written to the out_file.
        assert_ne!(out_file.metadata().unwrap().len(), 0);
    }
}
