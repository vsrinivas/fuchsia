// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod sources;
mod vmo_stream;

use {
    char_set::CharSet,
    failure::{format_err, Error},
    freetype_ffi::{
        FT_Add_Default_Modules, FT_Done_Face, FT_Done_Library, FT_Get_First_Char, FT_Get_Next_Char,
        FT_Library, FT_New_Library, FT_Open_Face, FT_MEMORY,
    },
    std::{convert::TryInto, ptr},
};

pub use crate::sources::FTOpenArgs;
pub use crate::sources::FontAssetSource;

/// Contains information parsed from a font file.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FontInfo {
    pub char_set: CharSet,
}

/// An object that can load a `FontInfo` from a `FontAssetSource`.
///
/// Separate from the [implementation][`FontInfoLoaderImpl`] to allow easier mocking for tests.
pub trait FontInfoLoader {
    /// Load information about a font from the given file source, located at the given index within
    /// the file.
    ///
    /// `S` is any type for which a conversion is defined into `FT_Open_Args`. In practice,
    /// this must be either a something that contains an `FT_Stream`, or a file path.
    ///
    /// For simplicity, use [crates::sources::FontAssetSource].
    fn load_font_info<S, E>(&self, source: S, index: u32) -> Result<FontInfo, Error>
    where
        S: TryInto<FontAssetSource, Error = E>,
        E: Sync + Send + Into<Error>;
}

/// Reads information from font files using FreeType library.
pub struct FontInfoLoaderImpl {
    ft_library: FT_Library,
}

impl std::ops::Drop for FontInfoLoaderImpl {
    fn drop(&mut self) {
        // Unsafe to call freetype FFI.
        // This is required in order to clean up the FreeType library instance.
        unsafe {
            FT_Done_Library(self.ft_library);
        }
    }
}

impl FontInfoLoaderImpl {
    pub fn new() -> Result<FontInfoLoaderImpl, Error> {
        // Unsafe to call freetype FFI. On success stores the FT_Library pointer
        // in FontInfoLoader, which calls FT_Done_Library in drop(). Must ensure
        // that all allocated memory is freed on failure.
        unsafe {
            let mut ft_library = ptr::null_mut();
            if FT_New_Library(&FT_MEMORY, &mut ft_library) != 0 {
                return Err(format_err!("Failed to initialize FreeType library."));
            }
            FT_Add_Default_Modules(ft_library);
            Ok(FontInfoLoaderImpl { ft_library })
        }
    }

    pub fn load_font_info<S, E>(&self, source: S, index: u32) -> Result<FontInfo, Error>
    where
        S: TryInto<FontAssetSource, Error = E>,
        E: Sync + Send + Into<Error>,
    {
        let mut codepoints: Vec<u32> = Vec::new();

        let source: FontAssetSource = source.try_into().map_err(|e| e.into())?;
        let open_args: FTOpenArgs<'_> = (&source).try_into()?;

        // Unsafe to call freetype FFI. Call FT_Open_Face() to load a typeface.
        // If it succeeds then enumerate character map with FT_Get_First_Char()
        // and FT_Get_Next_Char(). FT_Done_Face() must be called if the typeface
        // was initialized successfully.
        unsafe {
            let mut ft_face = ptr::null_mut();
            let err = FT_Open_Face(
                self.ft_library,
                open_args.as_ref(),
                index as libc::c_long,
                &mut ft_face,
            );

            if err != 0 {
                return Err(format_err!(
                    "Failed to parse font {} from {:?}: FreeType error {}",
                    index,
                    source,
                    err
                ));
            }

            let mut glyph_index = 0;
            let mut codepoint = FT_Get_First_Char(ft_face, &mut glyph_index);
            while glyph_index > 0 {
                codepoints.push(codepoint as u32);
                codepoint = FT_Get_Next_Char(ft_face, codepoint, &mut glyph_index);
            }

            FT_Done_Face(ft_face);
        }

        Ok(FontInfo { char_set: CharSet::new(codepoints) })
    }
}

impl FontInfoLoader for FontInfoLoaderImpl {
    fn load_font_info<S, E>(&self, source: S, index: u32) -> Result<FontInfo, Error>
    where
        S: TryInto<FontAssetSource, Error = E>,
        E: Sync + Send + Into<Error>,
    {
        FontInfoLoaderImpl::load_font_info(&self, source, index)
    }
}
