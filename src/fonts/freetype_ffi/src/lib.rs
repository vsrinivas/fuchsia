// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(improper_ctypes)]
#![allow(unused)]
#![allow(non_snake_case)]
#![allow(non_camel_case_types)]
#![allow(non_upper_case_globals)]

use libc::{self, c_char, c_int, c_long, c_uchar, c_uint, c_ulong, c_ushort, c_void, size_t};

type FT_Alloc_Func = unsafe extern "C" fn(FT_Memory, c_long) -> *mut c_void;
type FT_Free_Func = unsafe extern "C" fn(FT_Memory, *mut c_void);
type FT_Realloc_Func = unsafe extern "C" fn(FT_Memory, c_long, c_long, *mut c_void) -> *mut c_void;

type FT_Stream_IoFunc = unsafe extern "C" fn(FT_Stream, c_ulong, *mut c_uchar, c_ulong) -> c_ulong;
type FT_Stream_CloseFunc = unsafe extern "C" fn(FT_Stream);

#[repr(C)]
pub struct FT_MemoryRec {
    pub user: libc::intptr_t,
    pub alloc: FT_Alloc_Func,
    pub free: FT_Free_Func,
    pub realloc: FT_Realloc_Func,
}

pub struct FT_Parameter {
    tag: c_ulong,
    data: *mut c_void,
}

#[repr(C)]
pub struct FT_Open_Args {
    pub flags: c_uint,
    pub memory_base: *const c_uchar,
    pub memory_size: c_long,
    pub pathname: *const c_char,
    pub stream: FT_Stream,
    pub driver: FT_Module,
    pub num_params: c_int,
    pub params: *mut FT_Parameter,
}

#[repr(C)]
pub struct FT_StreamRec {
    pub base: *const c_uchar,
    pub size: c_ulong,
    pub pos: c_ulong,

    pub descriptor: FT_StreamDesc,
    pub pathname: FT_StreamDesc,
    pub read: FT_Stream_IoFunc,
    pub close: FT_Stream_CloseFunc,

    pub memory: FT_Memory,
    pub cursor: *mut c_uchar,
    pub limit: *mut c_uchar,
}

#[derive(Debug)]
#[repr(C)]
pub struct FT_SfntName {
    pub platform_id: c_ushort,
    pub encoding_id: c_ushort,
    pub language_id: c_ushort,
    pub name_id: c_ushort,
    /// NOT null-terminated!
    pub string: *mut c_uchar,
    pub string_len: c_uint,
}

impl Default for FT_SfntName {
    fn default() -> Self {
        Self {
            platform_id: Default::default(),
            encoding_id: Default::default(),
            language_id: Default::default(),
            name_id: Default::default(),
            string: std::ptr::null_mut(),
            string_len: Default::default(),
        }
    }
}

pub type FT_Memory = *const FT_MemoryRec;
pub type FT_Error = c_int;
pub type FT_Library = *mut c_void;
pub type FT_Stream = *const FT_StreamRec;
pub type FT_StreamDesc = *mut c_void;
pub type FT_Module = *const c_void;
pub type FT_Face = *mut c_void;

pub const FT_Err_Ok: FT_Error = 0;

pub const FT_OPEN_STREAM: c_uint = 0x2;
pub const FT_OPEN_PATHNAME: c_uint = 0x4;

// Font name IDs
pub const TT_NAME_ID_FONT_FAMILY: c_ushort = 1;
pub const TT_NAME_ID_FULL_NAME: c_ushort = 4;
pub const TT_NAME_ID_PS_NAME: c_ushort = 6;

// Platform IDs
pub const TT_PLATFORM_MICROSOFT: c_ushort = 3;

// Encoding IDs
pub const TT_MS_ID_SYMBOL_CS: c_ushort = 0;
pub const TT_MS_ID_UNICODE_CS: c_ushort = 1;

// Language IDs
pub const TT_MS_LANGID_ENGLISH_UNITED_STATES: c_ushort = 0x0409;

#[cfg_attr(target_os = "fuchsia", link(name = "freetype2"))]
#[cfg_attr(not(target_os = "fuchsia"), link(name = "freetype2_for_rust_host", kind = "static"))]
extern "C" {
    pub fn FT_New_Library(memory: FT_Memory, alibrary: *mut FT_Library) -> FT_Error;
    pub fn FT_Done_Library(library: FT_Library) -> FT_Error;
    pub fn FT_Add_Default_Modules(library: FT_Library);
    pub fn FT_New_Memory_Face(
        library: FT_Library,
        file_base: *const c_uchar,
        file_size: c_long,
        face_index: c_long,
        aface: *mut FT_Face,
    ) -> FT_Error;
    pub fn FT_Open_Face(
        library: FT_Library,
        args: *const FT_Open_Args,
        face_index: c_long,
        aface: *mut FT_Face,
    ) -> FT_Error;
    pub fn FT_Done_Face(face: FT_Face) -> FT_Error;
    pub fn FT_Get_First_Char(face: FT_Face, agindex: *mut c_uint) -> c_ulong;
    pub fn FT_Get_Next_Char(face: FT_Face, charcode: c_ulong, agindex: *mut c_uint) -> c_ulong;
    pub fn FT_Get_Postscript_Name(face: FT_Face) -> *const c_uchar;
    pub fn FT_Get_Sfnt_Name_Count(face: FT_Face) -> c_uint;
    pub fn FT_Get_Sfnt_Name(face: FT_Face, idx: c_uint, aname: *mut FT_SfntName) -> FT_Error;
}

extern "C" fn ft_alloc(_memory: FT_Memory, size: c_long) -> *mut c_void {
    unsafe { libc::malloc(size as size_t) }
}

extern "C" fn ft_free(_memory: FT_Memory, block: *mut c_void) {
    unsafe { libc::free(block) }
}

extern "C" fn ft_realloc(
    _memory: FT_Memory,
    _cur_size: c_long,
    new_size: c_long,
    block: *mut c_void,
) -> *mut c_void {
    unsafe { libc::realloc(block, new_size as size_t) }
}

pub static FT_MEMORY: FT_MemoryRec =
    FT_MemoryRec { user: 0, alloc: ft_alloc, free: ft_free, realloc: ft_realloc };
