// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    freetype_ffi::{FT_Open_Args, FT_Stream, FT_OPEN_PATHNAME, FT_OPEN_STREAM},
    std::{convert::TryFrom, ffi::CString, fmt, ptr},
};

/// Describes the source of a font asset to be parsed.
pub enum FontAssetSource {
    /// Byte stream (e.g. from a VMO)
    Stream(Box<dyn FTStreamProvider>),
    /// Path to a local file
    FilePath(String),
}

impl fmt::Debug for FontAssetSource {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FontAssetSource::Stream(_) => write!(f, "FontAssetSource::Stream"),
            FontAssetSource::FilePath(path) => write!(f, "FontAssetSource::FilePath(\"{}\")", path),
        }
    }
}

/// Converts a `FontAssetSource` to a `FT_Open_Args`, which is required for reading a font with
/// FreeType.
impl TryFrom<&FontAssetSource> for FT_Open_Args {
    type Error = Error;

    fn try_from(value: &FontAssetSource) -> Result<Self, Error> {
        match value {
            FontAssetSource::Stream(provider) => Ok(FT_Open_Args {
                flags: FT_OPEN_STREAM,
                memory_base: ptr::null(),
                memory_size: 0,
                pathname: ptr::null(),
                // Unsafe to call FreeType FFI.
                // Caller must ensure that the returned `FT_Open_Args` is not used after the
                // `FontAssetSource` is dropped.
                stream: unsafe { provider.ft_stream() },
                driver: ptr::null_mut(),
                num_params: 0,
                params: ptr::null_mut(),
            }),
            FontAssetSource::FilePath(path) => {
                let pathname = CString::new(&path[..])?.into_raw();
                Ok(FT_Open_Args {
                    flags: FT_OPEN_PATHNAME,
                    memory_base: ptr::null(),
                    memory_size: 0,
                    pathname,
                    stream: ptr::null_mut(),
                    driver: ptr::null_mut(),
                    num_params: 0,
                    params: ptr::null_mut(),
                })
            }
        }
    }
}

/// Provides a [FreeType stream](freetype_ffi::FTStream) for reading files.
pub trait FTStreamProvider {
    /// Unsafe to call FreeType FFI.
    /// Caller must ensure that the returned [FT_Stream] is not used after `self` is dropped.
    unsafe fn ft_stream(&self) -> FT_Stream;
}

/// Converts a [`fidl_fuchsia_mem::Buffer`] into a `FontAssetSource` by opening a stream from the
/// VMO represented by the buffer.
#[cfg(target_os = "fuchsia")]
impl TryFrom<fidl_fuchsia_mem::Buffer> for FontAssetSource {
    type Error = Error;

    fn try_from(buffer: fidl_fuchsia_mem::Buffer) -> Result<FontAssetSource, Error> {
        use crate::vmo_stream::VmoStream;
        Ok(FontAssetSource::Stream(Box::new(VmoStream::new(buffer.vmo, buffer.size as usize)?)))
    }
}
