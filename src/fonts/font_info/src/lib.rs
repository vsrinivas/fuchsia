// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod sources;
mod vmo_stream;

use {
    anyhow::{format_err, Error},
    char_set::CharSet,
    freetype_ffi::{
        FT_Add_Default_Modules, FT_Done_Face, FT_Done_Library, FT_Err_Ok, FT_Face,
        FT_Get_First_Char, FT_Get_Next_Char, FT_Library, FT_New_Library, FT_Open_Face, FT_MEMORY,
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
#[derive(Debug)]
pub struct FontInfoLoaderImpl {
    library: FTLibrary,
}

impl FontInfoLoaderImpl {
    pub fn new() -> Result<FontInfoLoaderImpl, Error> {
        let library = FTLibrary::new()?;
        Ok(Self { library })
    }

    pub fn load_font_info<S, E>(&self, source: S, index: u32) -> Result<FontInfo, Error>
    where
        S: TryInto<FontAssetSource, Error = E>,
        E: Sync + Send + Into<Error>,
    {
        let source: FontAssetSource = source.try_into().map_err(|e| e.into())?;
        let open_args: FTOpenArgs = source.try_into()?;

        let face = self.library.open_face(open_args, index)?;
        let code_points = face.code_points().collect();

        Ok(FontInfo { char_set: CharSet::new(code_points) })
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

#[derive(Debug)]
struct FTLibrary {
    ft_library: FT_Library,
}

/// Rusty wrapper around `FT_Library`.
impl FTLibrary {
    pub fn new() -> Result<FTLibrary, Error> {
        // Unsafe to call FreeType FFI. On success, stores the `FT_Library` pointer in `FTLibrary`,
        // which calls `FT_Done_Library` in `drop()`.
        unsafe {
            let mut ft_library = ptr::null_mut();
            if FT_New_Library(&FT_MEMORY, &mut ft_library) != FT_Err_Ok {
                return Err(format_err!("Failed to initialize FreeType library."));
            }
            FT_Add_Default_Modules(ft_library);
            Ok(FTLibrary { ft_library })
        }
    }

    /// Opens the font file with the given `open_args` and reads the face at the given index.
    pub fn open_face(&self, open_args: FTOpenArgs, index: u32) -> Result<FTFace, Error> {
        // Unsafe to call FreeType FFI. On success stores the `FT_Face` pointer in `FTFace`, which
        // calls `FT_Done_Face` in `drop()`.
        unsafe {
            let mut ft_face: FT_Face = ptr::null_mut();
            let err = FT_Open_Face(self.ft_library, open_args.as_ref(), index as i64, &mut ft_face);

            if err != FT_Err_Ok {
                return Err(format_err!(
                    "Failed to parse font {} from {:?}: FreeType error {}",
                    index,
                    open_args.source,
                    err
                ));
            }

            Ok(FTFace { ft_face, open_args })
        }
    }
}

impl std::ops::Drop for FTLibrary {
    fn drop(&mut self) {
        // Unsafe to call FreeType FFI.
        // This is required in order to clean up the FreeType library instance.
        unsafe {
            FT_Done_Library(self.ft_library);
        }
    }
}

/// Wrapper around the native `FT_Face`, providing a more Rusty API and retaining some structs that
/// need to stay alive in memory.
struct FTFace {
    ft_face: FT_Face,

    /// Retains the stream or file path that backs the `FT_Face`.
    #[allow(dead_code)]
    open_args: FTOpenArgs,
}

impl FTFace {
    /// Returns an iterator over the code points (as `u32`s) covered by this font face.
    pub fn code_points<'f>(&'f self) -> CodePointsIterator<'f> {
        CodePointsIterator { face: self, state: CodePointsIteratorState::Uninitialized }
    }
}

impl std::ops::Drop for FTFace {
    fn drop(&mut self) {
        // Unsafe to call FreeType FFI.
        // This is required in order to clean up the FreeType face instance.
        unsafe {
            FT_Done_Face(self.ft_face);
        }
    }
}

/// State of a [`CodePointsIterator`].
#[derive(Debug, Copy, Clone)]
enum CodePointsIteratorState {
    Uninitialized,
    /// Stores the previous code point returned.
    Active(u64),
    /// No more code points forthcoming.
    Fused,
}

/// Iterates over the code points (as `u32`s) in an `FTFace`.
pub struct CodePointsIterator<'f> {
    face: &'f FTFace,
    state: CodePointsIteratorState,
}

impl<'f> CodePointsIterator<'f> {
    fn next_internal(&mut self, prev_code_point: Option<u64>) -> Option<u32> {
        let mut glyph_index: u32 = 0;
        // Unsafe to call FreeType FFI. Enumerate character map with FT_Get_First_Char() and
        // FT_Get_Next_Char().
        let code_point = unsafe {
            match prev_code_point {
                Some(prev_code_point) => {
                    FT_Get_Next_Char(self.face.ft_face, prev_code_point, &mut glyph_index)
                }
                None => FT_Get_First_Char(self.face.ft_face, &mut glyph_index),
            }
        };
        // Per the FreeType docs, the glyph index will always be zero after the last code point in
        // the face is returned.
        if glyph_index > 0 {
            self.state = CodePointsIteratorState::Active(code_point);
            Some(code_point as u32)
        } else {
            self.state = CodePointsIteratorState::Fused;
            None
        }
    }
}

impl<'f> std::iter::Iterator for CodePointsIterator<'f> {
    type Item = u32;

    fn next(&mut self) -> Option<Self::Item> {
        match self.state.clone() {
            CodePointsIteratorState::Uninitialized => self.next_internal(None),
            CodePointsIteratorState::Active(prev_code_point) => {
                self.next_internal(Some(prev_code_point))
            }
            CodePointsIteratorState::Fused => None,
        }
    }
}

impl<'f> std::iter::FusedIterator for CodePointsIterator<'f> {}
