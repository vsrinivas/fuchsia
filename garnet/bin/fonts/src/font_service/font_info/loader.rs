// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{char_set::CharSet, vmo_stream::VmoStream},
    crate::font_service::freetype_ffi::{
        FT_Add_Default_Modules, FT_Done_Face, FT_Done_Library, FT_Get_First_Char, FT_Get_Next_Char,
        FT_Library, FT_New_Library, FT_Open_Args, FT_Open_Face, FT_MEMORY, FT_OPEN_STREAM,
    },
    failure::{format_err, Error},
    fuchsia_zircon as zx,
    std::ptr,
};

pub struct FontInfo {
    pub char_set: CharSet,
}

pub struct FontInfoLoader {
    ft_library: FT_Library,
}

impl std::ops::Drop for FontInfoLoader {
    fn drop(&mut self) {
        unsafe {
            FT_Done_Library(self.ft_library);
        }
    }
}

impl FontInfoLoader {
    pub fn new() -> Result<FontInfoLoader, Error> {
        // Unsafe to call freetype FFI. On success stores the FT_Library pointer
        // in FontInfoLoader, which calls FT_Done_Library in drop(). Must ensure
        // that all allocated memory is freed on failure.
        unsafe {
            let mut ft_library = ptr::null_mut();
            if FT_New_Library(&FT_MEMORY, &mut ft_library) != 0 {
                return Err(format_err!("Failed to initialize FreeType library."));
            }
            FT_Add_Default_Modules(ft_library);
            Ok(FontInfoLoader { ft_library })
        }
    }

    pub fn load_font_info(
        &self,
        vmo: zx::Vmo,
        vmo_size: usize,
        index: u32,
    ) -> Result<FontInfo, Error> {
        let mut codepoints: Vec<u32> = Vec::new();

        // Unsafe to call freetype FFI. Call FT_Open_Face() to load a typeface.
        // If it succeeds then enumerate character map with FT_Get_First_Char()
        // and FT_Get_Next_Char(). FT_Done_Face() must be called if the typeface
        // was initialized successfully.
        unsafe {
            let stream = VmoStream::new(vmo, vmo_size)?;
            let open_args = FT_Open_Args {
                flags: FT_OPEN_STREAM,
                memory_base: ptr::null(),
                memory_size: 0,
                pathname: ptr::null(),
                stream: stream.ft_stream(),
                driver: ptr::null_mut(),
                num_params: 0,
                params: ptr::null_mut(),
            };

            let mut ft_face = ptr::null_mut();
            let err =
                FT_Open_Face(self.ft_library, &open_args, index as libc::c_long, &mut ft_face);

            if err != 0 {
                return Err(format_err!("Failed to parse font file: error={}", err));
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
