// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.types banjo file

#![allow(unused_imports, non_camel_case_types)]

use fuchsia_zircon_types as zircon_types;

pub const VECTORS_SIZE: u32 = 32;
pub const STRINGS_SIZE: u32 = 32;
pub const ARRAYS_SIZE: u32 = 32;
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct vectors {
    pub b_0_list: *const bool,
    pub b_0_count: usize,
    pub i8_0_list: *const i8,
    pub i8_0_count: usize,
    pub i16_0_list: *const i16,
    pub i16_0_count: usize,
    pub i32_0_list: *const i32,
    pub i32_0_count: usize,
    pub i64_0_list: *const i64,
    pub i64_0_count: usize,
    pub u8_0_list: *const u8,
    pub u8_0_count: usize,
    pub u16_0_list: *const u16,
    pub u16_0_count: usize,
    pub u32_0_list: *const u32,
    pub u32_0_count: usize,
    pub u64_0_list: *const u64,
    pub u64_0_count: usize,
    pub f32_0_list: *const f32,
    pub f32_0_count: usize,
    pub f64_0_list: *const f64,
    pub f64_0_count: usize,
    pub handle_0_list: *const zircon_types::zx_handle_t,
    pub handle_0_count: usize,
    pub b_1_list: *const bool,
    pub b_1_count: usize,
    pub i8_1_list: *const i8,
    pub i8_1_count: usize,
    pub i16_1_list: *const i16,
    pub i16_1_count: usize,
    pub i32_1_list: *const i32,
    pub i32_1_count: usize,
    pub i64_1_list: *const i64,
    pub i64_1_count: usize,
    pub u8_1_list: *const u8,
    pub u8_1_count: usize,
    pub u16_1_list: *const u16,
    pub u16_1_count: usize,
    pub u32_1_list: *const u32,
    pub u32_1_count: usize,
    pub u64_1_list: *const u64,
    pub u64_1_count: usize,
    pub f32_1_list: *const f32,
    pub f32_1_count: usize,
    pub f64_1_list: *const f64,
    pub f64_1_count: usize,
    pub handle_1_list: *const zircon_types::zx_handle_t,
    pub handle_1_count: usize,
    pub b_sized_0_list: *const bool,
    pub b_sized_0_count: usize,
    pub i8_sized_0_list: *const i8,
    pub i8_sized_0_count: usize,
    pub i16_sized_0_list: *const i16,
    pub i16_sized_0_count: usize,
    pub i32_sized_0_list: *const i32,
    pub i32_sized_0_count: usize,
    pub i64_sized_0_list: *const i64,
    pub i64_sized_0_count: usize,
    pub u8_sized_0_list: *const u8,
    pub u8_sized_0_count: usize,
    pub u16_sized_0_list: *const u16,
    pub u16_sized_0_count: usize,
    pub u32_sized_0_list: *const u32,
    pub u32_sized_0_count: usize,
    pub u64_sized_0_list: *const u64,
    pub u64_sized_0_count: usize,
    pub f32_sized_0_list: *const f32,
    pub f32_sized_0_count: usize,
    pub f64_sized_0_list: *const f64,
    pub f64_sized_0_count: usize,
    pub handle_sized_0_list: *const zircon_types::zx_handle_t,
    pub handle_sized_0_count: usize,
    pub b_sized_1_list: *const bool,
    pub b_sized_1_count: usize,
    pub i8_sized_1_list: *const i8,
    pub i8_sized_1_count: usize,
    pub i16_sized_1_list: *const i16,
    pub i16_sized_1_count: usize,
    pub i32_sized_1_list: *const i32,
    pub i32_sized_1_count: usize,
    pub i64_sized_1_list: *const i64,
    pub i64_sized_1_count: usize,
    pub u8_sized_1_list: *const u8,
    pub u8_sized_1_count: usize,
    pub u16_sized_1_list: *const u16,
    pub u16_sized_1_count: usize,
    pub u32_sized_1_list: *const u32,
    pub u32_sized_1_count: usize,
    pub u64_sized_1_list: *const u64,
    pub u64_sized_1_count: usize,
    pub f32_sized_1_list: *const f32,
    pub f32_sized_1_count: usize,
    pub f64_sized_1_list: *const f64,
    pub f64_sized_1_count: usize,
    pub handle_sized_1_list: *const zircon_types::zx_handle_t,
    pub handle_sized_1_count: usize,
    pub b_sized_2_list: *const bool,
    pub b_sized_2_count: usize,
    pub i8_sized_2_list: *const i8,
    pub i8_sized_2_count: usize,
    pub i16_sized_2_list: *const i16,
    pub i16_sized_2_count: usize,
    pub i32_sized_2_list: *const i32,
    pub i32_sized_2_count: usize,
    pub i64_sized_2_list: *const i64,
    pub i64_sized_2_count: usize,
    pub u8_sized_2_list: *const u8,
    pub u8_sized_2_count: usize,
    pub u16_sized_2_list: *const u16,
    pub u16_sized_2_count: usize,
    pub u32_sized_2_list: *const u32,
    pub u32_sized_2_count: usize,
    pub u64_sized_2_list: *const u64,
    pub u64_sized_2_count: usize,
    pub f32_sized_2_list: *const f32,
    pub f32_sized_2_count: usize,
    pub f64_sized_2_list: *const f64,
    pub f64_sized_2_count: usize,
    pub handle_sized_2_list: *const zircon_types::zx_handle_t,
    pub handle_sized_2_count: usize,
    pub b_nullable_0_list: *const bool,
    pub b_nullable_0_count: usize,
    pub i8_nullable_0_list: *const i8,
    pub i8_nullable_0_count: usize,
    pub i16_nullable_0_list: *const i16,
    pub i16_nullable_0_count: usize,
    pub i32_nullable_0_list: *const i32,
    pub i32_nullable_0_count: usize,
    pub i64_nullable_0_list: *const i64,
    pub i64_nullable_0_count: usize,
    pub u8_nullable_0_list: *const u8,
    pub u8_nullable_0_count: usize,
    pub u16_nullable_0_list: *const u16,
    pub u16_nullable_0_count: usize,
    pub u32_nullable_0_list: *const u32,
    pub u32_nullable_0_count: usize,
    pub u64_nullable_0_list: *const u64,
    pub u64_nullable_0_count: usize,
    pub f32_nullable_0_list: *const f32,
    pub f32_nullable_0_count: usize,
    pub f64_nullable_0_list: *const f64,
    pub f64_nullable_0_count: usize,
    pub handle_nullable_0_list: *const zircon_types::zx_handle_t,
    pub handle_nullable_0_count: usize,
    pub b_nullable_1_list: *const bool,
    pub b_nullable_1_count: usize,
    pub i8_nullable_1_list: *const i8,
    pub i8_nullable_1_count: usize,
    pub i16_nullable_1_list: *const i16,
    pub i16_nullable_1_count: usize,
    pub i32_nullable_1_list: *const i32,
    pub i32_nullable_1_count: usize,
    pub i64_nullable_1_list: *const i64,
    pub i64_nullable_1_count: usize,
    pub u8_nullable_1_list: *const u8,
    pub u8_nullable_1_count: usize,
    pub u16_nullable_1_list: *const u16,
    pub u16_nullable_1_count: usize,
    pub u32_nullable_1_list: *const u32,
    pub u32_nullable_1_count: usize,
    pub u64_nullable_1_list: *const u64,
    pub u64_nullable_1_count: usize,
    pub f32_nullable_1_list: *const f32,
    pub f32_nullable_1_count: usize,
    pub f64_nullable_1_list: *const f64,
    pub f64_nullable_1_count: usize,
    pub handle_nullable_1_list: *const zircon_types::zx_handle_t,
    pub handle_nullable_1_count: usize,
    pub b_nullable_sized_0_list: *const bool,
    pub b_nullable_sized_0_count: usize,
    pub i8_nullable_sized_0_list: *const i8,
    pub i8_nullable_sized_0_count: usize,
    pub i16_nullable_sized_0_list: *const i16,
    pub i16_nullable_sized_0_count: usize,
    pub i32_nullable_sized_0_list: *const i32,
    pub i32_nullable_sized_0_count: usize,
    pub i64_nullable_sized_0_list: *const i64,
    pub i64_nullable_sized_0_count: usize,
    pub u8_nullable_sized_0_list: *const u8,
    pub u8_nullable_sized_0_count: usize,
    pub u16_nullable_sized_0_list: *const u16,
    pub u16_nullable_sized_0_count: usize,
    pub u32_nullable_sized_0_list: *const u32,
    pub u32_nullable_sized_0_count: usize,
    pub u64_nullable_sized_0_list: *const u64,
    pub u64_nullable_sized_0_count: usize,
    pub f32_nullable_sized_0_list: *const f32,
    pub f32_nullable_sized_0_count: usize,
    pub f64_nullable_sized_0_list: *const f64,
    pub f64_nullable_sized_0_count: usize,
    pub handle_nullable_sized_0_list: *const zircon_types::zx_handle_t,
    pub handle_nullable_sized_0_count: usize,
    pub b_nullable_sized_1_list: *const bool,
    pub b_nullable_sized_1_count: usize,
    pub i8_nullable_sized_1_list: *const i8,
    pub i8_nullable_sized_1_count: usize,
    pub i16_nullable_sized_1_list: *const i16,
    pub i16_nullable_sized_1_count: usize,
    pub i32_nullable_sized_1_list: *const i32,
    pub i32_nullable_sized_1_count: usize,
    pub i64_nullable_sized_1_list: *const i64,
    pub i64_nullable_sized_1_count: usize,
    pub u8_nullable_sized_1_list: *const u8,
    pub u8_nullable_sized_1_count: usize,
    pub u16_nullable_sized_1_list: *const u16,
    pub u16_nullable_sized_1_count: usize,
    pub u32_nullable_sized_1_list: *const u32,
    pub u32_nullable_sized_1_count: usize,
    pub u64_nullable_sized_1_list: *const u64,
    pub u64_nullable_sized_1_count: usize,
    pub f32_nullable_sized_1_list: *const f32,
    pub f32_nullable_sized_1_count: usize,
    pub f64_nullable_sized_1_list: *const f64,
    pub f64_nullable_sized_1_count: usize,
    pub handle_nullable_sized_1_list: *const zircon_types::zx_handle_t,
    pub handle_nullable_sized_1_count: usize,
    pub b_nullable_sized_2_list: *const bool,
    pub b_nullable_sized_2_count: usize,
    pub i8_nullable_sized_2_list: *const i8,
    pub i8_nullable_sized_2_count: usize,
    pub i16_nullable_sized_2_list: *const i16,
    pub i16_nullable_sized_2_count: usize,
    pub i32_nullable_sized_2_list: *const i32,
    pub i32_nullable_sized_2_count: usize,
    pub i64_nullable_sized_2_list: *const i64,
    pub i64_nullable_sized_2_count: usize,
    pub u8_nullable_sized_2_list: *const u8,
    pub u8_nullable_sized_2_count: usize,
    pub u16_nullable_sized_2_list: *const u16,
    pub u16_nullable_sized_2_count: usize,
    pub u32_nullable_sized_2_list: *const u32,
    pub u32_nullable_sized_2_count: usize,
    pub u64_nullable_sized_2_list: *const u64,
    pub u64_nullable_sized_2_count: usize,
    pub f32_nullable_sized_2_list: *const f32,
    pub f32_nullable_sized_2_count: usize,
    pub f64_nullable_sized_2_list: *const f64,
    pub f64_nullable_sized_2_count: usize,
    pub handle_nullable_sized_2_list: *const zircon_types::zx_handle_t,
    pub handle_nullable_sized_2_count: usize,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct handles {
    pub handle_handle: zircon_types::zx_handle_t,
    pub process_handle: zircon_types::zx_handle_t,
    pub thread_handle: zircon_types::zx_handle_t,
    pub vmo_handle: zircon_types::zx_handle_t,
    pub channel_handle: zircon_types::zx_handle_t,
    pub event_handle: zircon_types::zx_handle_t,
    pub port_handle: zircon_types::zx_handle_t,
    pub interrupt_handle: zircon_types::zx_handle_t,
    pub socket_handle: zircon_types::zx_handle_t,
    pub resource_handle: zircon_types::zx_handle_t,
    pub eventpair_handle: zircon_types::zx_handle_t,
    pub job_handle: zircon_types::zx_handle_t,
    pub vmar_handle: zircon_types::zx_handle_t,
    pub fifo_handle: zircon_types::zx_handle_t,
    pub guest_handle: zircon_types::zx_handle_t,
    pub timer_handle: zircon_types::zx_handle_t,
    pub profile_handle: zircon_types::zx_handle_t,
    pub vcpu_handle: zircon_types::zx_handle_t,
    pub iommu_handle: zircon_types::zx_handle_t,
    pub pager_handle: zircon_types::zx_handle_t,
    pub pmt_handle: zircon_types::zx_handle_t,
    pub clock_handle: zircon_types::zx_handle_t,
    pub nullable_handle_handle: zircon_types::zx_handle_t,
    pub nullable_process_handle: zircon_types::zx_handle_t,
    pub nullable_thread_handle: zircon_types::zx_handle_t,
    pub nullable_vmo_handle: zircon_types::zx_handle_t,
    pub nullable_channel_handle: zircon_types::zx_handle_t,
    pub nullable_event_handle: zircon_types::zx_handle_t,
    pub nullable_port_handle: zircon_types::zx_handle_t,
    pub nullable_interrupt_handle: zircon_types::zx_handle_t,
    pub nullable_socket_handle: zircon_types::zx_handle_t,
    pub nullable_resource_handle: zircon_types::zx_handle_t,
    pub nullable_eventpair_handle: zircon_types::zx_handle_t,
    pub nullable_job_handle: zircon_types::zx_handle_t,
    pub nullable_vmar_handle: zircon_types::zx_handle_t,
    pub nullable_fifo_handle: zircon_types::zx_handle_t,
    pub nullable_guest_handle: zircon_types::zx_handle_t,
    pub nullable_timer_handle: zircon_types::zx_handle_t,
    pub nullable_profile_handle: zircon_types::zx_handle_t,
    pub nullable_vcpu_handle: zircon_types::zx_handle_t,
    pub nullable_iommu_handle: zircon_types::zx_handle_t,
    pub nullable_pager_handle: zircon_types::zx_handle_t,
    pub nullable_pmt_handle: zircon_types::zx_handle_t,
    pub nullable_clock_handle: zircon_types::zx_handle_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct arrays {
    pub b_0: [bool; 1 as usize],
    pub i8_0: [i8; 1 as usize],
    pub i16_0: [i16; 1 as usize],
    pub i32_0: [i32; 1 as usize],
    pub i64_0: [i64; 1 as usize],
    pub u8_0: [u8; 1 as usize],
    pub u16_0: [u16; 1 as usize],
    pub u32_0: [u32; 1 as usize],
    pub u64_0: [u64; 1 as usize],
    pub f32_0: [f32; 1 as usize],
    pub f64_0: [f64; 1 as usize],
    pub handle_0: [zircon_types::zx_handle_t; 1 as usize],
    pub b_1: [bool; 32 as usize],
    pub i8_1: [i8; 32 as usize],
    pub i16_1: [i16; 32 as usize],
    pub i32_1: [i32; 32 as usize],
    pub i64_1: [i64; 32 as usize],
    pub u8_1: [u8; 32 as usize],
    pub u16_1: [u16; 32 as usize],
    pub u32_1: [u32; 32 as usize],
    pub u64_1: [u64; 32 as usize],
    pub f32_1: [f32; 32 as usize],
    pub f64_1: [f64; 32 as usize],
    pub handle_1: [zircon_types::zx_handle_t; 32 as usize],
    pub b_2: [[bool; 4 as usize]; 32 as usize],
    pub i8_2: [[i8; 4 as usize]; 32 as usize],
    pub i16_2: [[i16; 4 as usize]; 32 as usize],
    pub i32_2: [[i32; 4 as usize]; 32 as usize],
    pub i64_2: [[i64; 4 as usize]; 32 as usize],
    pub u8_2: [[u8; 4 as usize]; 32 as usize],
    pub u16_2: [[u16; 4 as usize]; 32 as usize],
    pub u32_2: [[u32; 4 as usize]; 32 as usize],
    pub u64_2: [[u64; 4 as usize]; 32 as usize],
    pub f32_2: [[f32; 4 as usize]; 32 as usize],
    pub f64_2: [[f64; 4 as usize]; 32 as usize],
    pub handle_2: [[zircon_types::zx_handle_t; 4 as usize]; 32 as usize],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct zx_types {
    pub status: zircon_types::zx_status_t,
    pub time: zircon_types::zx_time_t,
    pub duration: zircon_types::zx_duration_t,
    pub ticks: zircon_types::zx_ticks_t,
    pub koid: zircon_types::zx_koid_t,
    pub vaddr: zircon_types::zx_vaddr_t,
    pub paddr: zircon_types::zx_paddr_t,
    pub paddr32: zircon_types::zx_paddr32_t,
    pub gpaddr: zircon_types::zx_gpaddr_t,
    pub offset: zircon_types::zx_off_t,
    pub procarg: zircon_types::zx_procarg_t,
    pub signals: zircon_types::zx_signals_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct unions {
    pub s: this_is_a_union,
    pub nullable_u: *mut this_is_a_union,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct this_is_a_struct {
    pub s: *const std::os::raw::c_char,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct structs {
    pub s: this_is_a_struct,
    pub nullable_s: *mut this_is_a_struct,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct strings {
    pub s: *const std::os::raw::c_char,
    pub nullable_s: *const std::os::raw::c_char,
    pub size_0_s: [u8; 4 as usize],
    pub size_1_s: [u8; 32 as usize],
    pub nullable_size_0_s: [u8; 4 as usize],
    pub nullable_size_1_s: [u8; 32 as usize],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct primitive_types {
    pub b: bool,
    pub i8: i8,
    pub i16: i16,
    pub i32: i32,
    pub i64: i64,
    pub u8: u8,
    pub u16: u16,
    pub u32: u32,
    pub u64: u64,
    pub f32: f32,
    pub f64: f64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct interfaces {
    pub nonnullable_interface: this_is_an_interface,
    pub nullable_interface: this_is_an_interface,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct default_values {
    pub b1: bool,
    pub b2: bool,
    pub i8: i8,
    pub i16: i16,
    pub i32: i32,
    pub i64: i64,
    pub u8: u8,
    pub u16: u16,
    pub u32: u32,
    pub u64: u64,
    pub s: *const std::os::raw::c_char,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct u8_enum(pub u8);

impl u8_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for u8_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for u8_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for u8_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for u8_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for u8_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for u8_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct u64_enum(pub u64);

impl u64_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for u64_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for u64_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for u64_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for u64_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for u64_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for u64_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct u32_enum(pub u32);

impl u32_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for u32_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for u32_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for u32_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for u32_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for u32_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for u32_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct u16_enum(pub u16);

impl u16_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for u16_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for u16_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for u16_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for u16_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for u16_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for u16_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct i8_enum(pub i8);

impl i8_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for i8_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for i8_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for i8_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for i8_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for i8_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for i8_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct i64_enum(pub i64);

impl i64_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for i64_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for i64_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for i64_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for i64_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for i64_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for i64_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct i32_enum(pub i32);

impl i32_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for i32_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for i32_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for i32_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for i32_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for i32_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for i32_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct i16_enum(pub i16);

impl i16_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for i16_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for i16_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for i16_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for i16_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for i16_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for i16_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct default_enum(pub u32);

impl default_enum {
    pub const X: Self = Self(23);
}

impl std::ops::BitAnd for default_enum {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self {
        Self(self.0 & rhs.0)
    }
}

impl std::ops::BitAndAssign for default_enum {
    fn bitand_assign(&mut self, rhs: Self) {
        *self = Self(self.0 & rhs.0)
    }
}

impl std::ops::BitOr for default_enum {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

impl std::ops::BitOrAssign for default_enum {
    fn bitor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 | rhs.0)
    }
}

impl std::ops::BitXor for default_enum {
    type Output = Self;
    fn bitxor(self, rhs: Self) -> Self {
        Self(self.0 ^ rhs.0)
    }
}

impl std::ops::BitXorAssign for default_enum {
    fn bitxor_assign(&mut self, rhs: Self) {
        *self = Self(self.0 ^ rhs.0)
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union this_is_a_union {
    pub s: *const std::os::raw::c_char,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for this_is_a_union {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<this_is_a_union>")
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
pub union union_types {
    pub b: bool,
    pub i8: i8,
    pub i16: i16,
    pub i32: i32,
    pub i64: i64,
    pub u8: u8,
    pub u16: u16,
    pub u32: u32,
    pub u64: u64,
    pub f32: f32,
    pub f64: f64,
    pub b_0: [bool; 1 as usize],
    pub i8_0: [i8; 1 as usize],
    pub i16_0: [i16; 1 as usize],
    pub i32_0: [i32; 1 as usize],
    pub i64_0: [i64; 1 as usize],
    pub u8_0: [u8; 1 as usize],
    pub u16_0: [u16; 1 as usize],
    pub u32_0: [u32; 1 as usize],
    pub u64_0: [u64; 1 as usize],
    pub f32_0: [f32; 1 as usize],
    pub f64_0: [f64; 1 as usize],
    pub handle_0: [zircon_types::zx_handle_t; 1 as usize],
    pub str: *const std::os::raw::c_char,
    pub s: this_is_a_struct,
    pub u: this_is_a_union,
}

// unions can't autoderive debug, but it's useful for their parent types to
impl std::fmt::Debug for union_types {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "<union_types>")
    }
}

