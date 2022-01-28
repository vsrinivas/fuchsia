// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod sources;
mod vmo_stream;

use {
    anyhow::{format_err, Error},
    byteorder::{BigEndian, ReadBytesExt},
    char_set::CharSet,
    freetype_ffi::{
        FT_Add_Default_Modules, FT_Done_Face, FT_Done_Library, FT_Err_Ok, FT_Face,
        FT_Get_First_Char, FT_Get_Next_Char, FT_Get_Postscript_Name, FT_Get_Sfnt_Name,
        FT_Get_Sfnt_Name_Count, FT_Library, FT_New_Library, FT_Open_Face, FT_SfntName, FT_MEMORY,
        TT_MS_ID_SYMBOL_CS, TT_MS_ID_UNICODE_CS, TT_MS_LANGID_ENGLISH_UNITED_STATES,
        TT_NAME_ID_FULL_NAME, TT_PLATFORM_MICROSOFT,
    },
    std::{convert::TryInto, ffi::CStr, io::Cursor, ops::Range, ptr},
};

pub use crate::sources::FTOpenArgs;
pub use crate::sources::FontAssetSource;

/// Contains information parsed from a font file.
#[derive(Default, Clone, Debug, Eq, PartialEq)]
pub struct FontInfo {
    pub char_set: CharSet,
    /// The "Postscript name" of the font face.
    pub postscript_name: Option<String>,
    /// The user-friendly "full name" of the font face.
    pub full_name: Option<String>,
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

/// Reads information from font files using the FreeType library.
///
/// This class is tested in `../tests/tests.rs`.
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
        let postscript_name = face.postscript_name().ok().filter(|x| !x.is_empty());
        let full_name = face.full_name()?.filter(|x| !x.is_empty());

        Ok(FontInfo { char_set: CharSet::new(code_points), postscript_name, full_name })
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

    /// Returns the face's Postscript name.
    pub fn postscript_name(&self) -> Result<String, Error> {
        // Unsafe to call FreeType FFI.
        let postscript_name = unsafe {
            // According to the FT docs, this is supposed to be an ASCII string, so it should
            // trivially convert to UTF-8.
            //
            // Note: Must use c_char, not i8 or u8, because c_char's definition depends on the 
            // architecture.
            let postscript_name =
                FT_Get_Postscript_Name(self.ft_face) as *const std::os::raw::c_char;
            let postscript_name = CStr::from_ptr(postscript_name);
            postscript_name
                .to_str()
                .map_err(|e| format_err!("Failed to decode Postscript name. Error: {:?}", e))?
                .to_string()
        };
        Ok(postscript_name)
    }

    /// Returns the face's "full name", if present.
    pub fn full_name(&self) -> Result<Option<String>, Error> {
        self.get_name(TT_NAME_ID_FULL_NAME)
    }

    /// Finds and decodes the first supported name entry that has the given name ID.
    ///
    /// A "name ID" is a record identifier in the `name` TrueType table, which can also contain
    /// items like copyright info, URLs, etc. See
    /// https://docs.microsoft.com/en-us/typography/opentype/spec/name#name-ids.
    fn get_name(&self, name_id: u16) -> Result<Option<String>, Error> {
        for name_entry in self.sfnt_names() {
            let name_entry = name_entry?;
            if let Some(name) = FTFace::decode_name_if_matches(&name_entry, name_id)? {
                return Ok(Some(name));
            }
        }
        Ok(None)
    }

    fn sfnt_name_count(&self) -> u32 {
        unsafe { FT_Get_Sfnt_Name_Count(self.ft_face) }
    }

    fn sfnt_names<'a>(&'a self) -> SfntNamesIterator<'a> {
        SfntNamesIterator { face: self, iter: 0..self.sfnt_name_count() }
    }

    /// If the given `FT_SfntName` contains the requested `name_id` and the encoding is in a
    /// supported format, returns the decoded name string. Otherwise, returns `None`.
    ///
    /// Presently, this only looks at en-US name entries.
    fn decode_name_if_matches(
        name_entry: &FT_SfntName,
        name_id: u16,
    ) -> Result<Option<String>, Error> {
        if name_entry.name_id != name_id {
            return Ok(None);
        }
        match (name_entry.platform_id, name_entry.encoding_id, name_entry.language_id) {
            (TT_PLATFORM_MICROSOFT, TT_MS_ID_SYMBOL_CS, TT_MS_LANGID_ENGLISH_UNITED_STATES)
            | (TT_PLATFORM_MICROSOFT, TT_MS_ID_UNICODE_CS, TT_MS_LANGID_ENGLISH_UNITED_STATES) => {
                let name = FTFace::decode_name_utf16_be(name_entry)?;
                Ok(Some(name))
            }
            _ => Ok(None),
        }
    }

    /// Decodes the the given `FT_SfntName` as a UTF-16 Big Endian string.
    fn decode_name_utf16_be(name_entry: &FT_SfntName) -> Result<String, Error> {
        let as_u8 = unsafe {
            std::slice::from_raw_parts(name_entry.string, name_entry.string_len as usize)
        };
        // FT_SfntName.string_len is in bytes. One UTF-16 code unit is two bytes.
        let num_code_units = (name_entry.string_len / 2) as usize;
        let mut reader = Cursor::new(as_u8);
        let mut as_u16 = Vec::with_capacity(num_code_units);
        for _i in 0..num_code_units {
            let ch = reader.read_u16::<BigEndian>()?;
            as_u16.push(ch);
        }
        Ok(String::from_utf16(as_u16.as_slice())?)
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

/// Iterator over the `name` table in a font face.
pub struct SfntNamesIterator<'f> {
    face: &'f FTFace,
    iter: Range<u32>,
}

impl<'f> SfntNamesIterator<'f> {
    fn get(&self, idx: u32) -> Result<FT_SfntName, Error> {
        let mut entry = FT_SfntName::default();
        let err_code = unsafe { FT_Get_Sfnt_Name(self.face.ft_face, idx, &mut entry) };
        if err_code == FT_Err_Ok {
            Ok(entry)
        } else {
            Err(format_err!("FreeType error while getting name {}: {}", idx, err_code))
        }
    }
}

impl<'f> Iterator for SfntNamesIterator<'f> {
    type Item = Result<FT_SfntName, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(|idx| self.get(idx))
    }
}

impl<'f> ExactSizeIterator for SfntNamesIterator<'f> {
    fn len(&self) -> usize {
        self.iter.len()
    }
}
