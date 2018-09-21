// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_snake_case)]
#![allow(non_camel_case_types)]

use libc::{self, c_int, c_long, c_uchar, c_uint, c_ulong, c_void, size_t};

type FT_Alloc_Func = extern "C" fn(FT_Memory, c_long) -> *mut c_void;
type FT_Free_Func = extern "C" fn(FT_Memory, *mut c_void);
type FT_Realloc_Func = extern "C" fn(FT_Memory, c_long, c_long, *mut c_void) -> *mut c_void;

#[repr(C)]
pub struct FT_MemoryRec {
    pub user: libc::intptr_t,
    pub alloc: FT_Alloc_Func,
    pub free: FT_Free_Func,
    pub realloc: FT_Realloc_Func,
}

pub type FT_Memory = *const FT_MemoryRec;
pub type FT_Error = c_int;
pub type FT_Library = *mut c_void;
pub type FT_Face = *mut c_void;

#[link(name = "freetype2")]
extern "C" {
    pub fn FT_New_Library(memory: FT_Memory, alibrary: *mut FT_Library) -> FT_Error;
    pub fn FT_Done_Library(library: FT_Library) -> FT_Error;
    pub fn FT_Add_Default_Modules(library: FT_Library);
    pub fn FT_New_Memory_Face(
        library: FT_Library, file_base: *const c_uchar, file_size: c_long, face_index: c_long,
        aface: *mut FT_Face,
    ) -> FT_Error;
    pub fn FT_Done_Face(face: FT_Face) -> FT_Error;
    pub fn FT_Get_First_Char(face: FT_Face, agindex: *mut c_uint) -> c_ulong;
    pub fn FT_Get_Next_Char(face: FT_Face, charcode: c_ulong, agindex: *mut c_uint) -> c_ulong;
}

extern "C" fn ft_alloc(_memory: FT_Memory, size: c_long) -> *mut c_void {
    unsafe { libc::malloc(size as size_t) }
}

extern "C" fn ft_free(_memory: FT_Memory, block: *mut c_void) {
    unsafe { libc::free(block) }
}

extern "C" fn ft_realloc(
    _memory: FT_Memory, _cur_size: c_long, new_size: c_long, block: *mut c_void,
) -> *mut c_void {
    unsafe { libc::realloc(block, new_size as size_t) }
}

pub static FT_MEMORY: FT_MemoryRec = FT_MemoryRec {
    user: 0,
    alloc: ft_alloc,
    free: ft_free,
    realloc: ft_realloc,
};
