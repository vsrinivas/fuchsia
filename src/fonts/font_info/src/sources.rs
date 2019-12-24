// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
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

/// This struct ensures that the lifetime of FT_Open_Args is handled correctly.
pub struct FTOpenArgs<'a> {
    // All suppressed "dead code" below is used to ensure the lifetime of native_open_args is in
    // sync with FontAssetSource it is based on.

    // This is the asset source based on which the args are generated.
    #[allow(dead_code)]
    source: &'a FontAssetSource,
    // Holds a reference to the C-style string from FontAssetSource, in case it is a string-based
    // source.
    #[allow(dead_code)]
    pathname: Option<CString>,
    // Holds a reference to the stream from FontAssetSource, in case it is a stream-based source.
    #[allow(dead_code)]
    stream_provider: Option<&'a Box<dyn FTStreamProvider>>,
    // The FFI type that was obtained based on the asset source.  Access it through the AsRef
    // trait implementation.
    native_open_args: FT_Open_Args,
}

/// Allows viewing FTOpenArgs as a FFI type.
impl<'a> AsRef<FT_Open_Args> for FTOpenArgs<'a> {
    /// Views FTOpenArgs (a rust type) as a FFI type `FT_Open_Args` for interfacing with low level
    /// FFI libraries.
    fn as_ref(&self) -> &FT_Open_Args {
        &self.native_open_args
    }
}

/// Converts a `FontAssetSource` to a `FT_Open_Args`, which is required for reading a font with
/// FreeType.
impl<'a> TryFrom<&'a FontAssetSource> for FTOpenArgs<'a> {
    type Error = Error;

    fn try_from(source: &'a FontAssetSource) -> Result<Self, Error> {
        let mut pathname: Option<CString> = None;
        let mut stream_provider: Option<&Box<dyn FTStreamProvider>> = None;
        let native_open_args = match source {
            FontAssetSource::Stream(ref provider) => {
                stream_provider = Some(provider);
                FT_Open_Args {
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
                }
            }
            FontAssetSource::FilePath(path) => {
                pathname = Some(CString::new(&path[..])?);
                FT_Open_Args {
                    flags: FT_OPEN_PATHNAME,
                    memory_base: ptr::null(),
                    memory_size: 0,
                    // This won't ever be `None`, since we're assigning to `pathname` just above.
                    pathname: pathname.as_ref().unwrap().as_ptr(),
                    stream: ptr::null_mut(),
                    driver: ptr::null_mut(),
                    num_params: 0,
                    params: ptr::null_mut(),
                }
            }
        };
        Ok(FTOpenArgs { source, pathname, stream_provider, native_open_args })
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
