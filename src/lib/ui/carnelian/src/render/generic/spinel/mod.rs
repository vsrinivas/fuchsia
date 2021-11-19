// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    ffi::CStr,
    mem::{self, MaybeUninit},
};

use euclid::default::Size2D;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_sysmem::BufferCollectionTokenMarker;
use vk_sys as vk;

use crate::{drawing::DisplayRotation, render::generic::Backend};

macro_rules! spn {
    ( $result:expr ) => {{
        let result = $result;
        assert_eq!(
            result,
            ::spinel_rs_sys::SpnResult::SpnSuccess,
            "Spinel failed with: {:?}",
            result,
        );
    }};
}

macro_rules! vk {
    ( $result:expr ) => {{
        let result = $result;
        assert_eq!(result, ::vk_sys::SUCCESS, "Vulkan failed with: {:?}", result);
    }};
}

macro_rules! cstr {
    ( $bytes:expr ) => {
        ::std::ffi::CStr::from_bytes_with_nul($bytes).expect("CStr must end with '\\0'")
    };
}

macro_rules! ptrs {(
    $struct_name:ident,
    { $($name:ident => ($($param_n:ident: $param_ty:ty),*) -> $ret:ty,)+ }) => {
        #[allow(non_snake_case)]
        pub struct $struct_name {
            $(
                pub $name: extern "system" fn($($param_ty),*) -> $ret,
            )+
        }

        impl $struct_name {
            pub fn load<F>(mut f: F) -> $struct_name
                where F: FnMut(&CStr) -> *const ::std::ffi::c_void
            {
                #[allow(non_snake_case)]
                $struct_name {
                    $(
                        $name: unsafe {
                            extern "system" fn $name($(_: $param_ty),*) {
                                panic!("function pointer `{}` not loaded", stringify!($name))
                            }
                            let name = ::std::ffi::CStr::from_bytes_with_nul_unchecked(
                                concat!("vk", stringify!($name), "\0").as_bytes());
                            let val = f(name);
                            if val.is_null() {
                                ::std::mem::transmute($name as *const ())
                            } else {
                                ::std::mem::transmute(val)
                            }
                        },
                    )+
                }
            }
            $(
                #[inline]
                #[allow(non_snake_case)]
                pub unsafe fn $name(&self $(, $param_n: $param_ty)*) -> $ret {
                    let ptr = self.$name;
                    ptr($($param_n),*)
                }
            )+
        }

        impl ::std::fmt::Debug for $struct_name {
            fn fmt(&self, fmt: &mut ::std::fmt::Formatter<'_>) -> ::std::fmt::Result {
                fmt.debug_struct(stringify!($struct_name)).finish()
            }
        }
    };
}

#[link(name = "vulkan")]
extern "C" {
    fn vkGetInstanceProcAddr(
        instance: vk::Instance,
        pName: *const ::std::os::raw::c_char,
    ) -> vk::PFN_vkVoidFunction;
}

pub fn entry_points() -> vk::EntryPoints {
    vk::EntryPoints::load(|name| unsafe { mem::transmute(vkGetInstanceProcAddr(0, name.as_ptr())) })
}

pub fn instance_pointers(instance: vk::Instance) -> vk::InstancePointers {
    vk::InstancePointers::load(|name| unsafe {
        mem::transmute(vkGetInstanceProcAddr(instance, name.as_ptr()))
    })
}

pub fn device_pointers(vk_i: &vk::InstancePointers, device: vk::Device) -> vk::DevicePointers {
    vk::DevicePointers::load(|name| unsafe {
        vk_i.GetDeviceProcAddr(device, name.as_ptr()) as *const _
    })
}

// TODO: Remove buffer collection bindings when they are upstream.
pub type BufferCollectionFUCHSIA = u64;
pub type BufferCollectionFUCHSIAX = u64;

pub const STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA: u32 = 1_001_004_000;
pub const STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA: u32 = 1_001_004_004;
pub const STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA: u32 = 1_001_004_005;
pub const STRUCTURE_TYPE_BUFFER_COLLECTION_PROPERTIES_FUCHSIA: u32 = 1_001_004_006;

#[repr(C)]
#[allow(non_snake_case)]
#[derive(Debug)]
pub struct BufferCollectionPropertiesFUCHSIA {
    sType: vk::StructureType,
    pNext: *const ::std::os::raw::c_void,
    memoryTypeBits: u32,
    count: u32,
}

#[repr(C)]
#[allow(non_snake_case)]
#[derive(Debug)]
pub struct BufferCollectionCreateInfoFUCHSIA {
    pub sType: vk::StructureType,
    pub pNext: *const ::std::os::raw::c_void,
    pub collectionToken: u32,
}

#[repr(C)]
#[allow(non_snake_case)]
#[derive(Debug)]
pub struct BufferCollectionImageCreateInfoFUCHSIA {
    sType: vk::StructureType,
    pNext: *const ::std::os::raw::c_void,
    collection: BufferCollectionFUCHSIA,
    index: u32,
}

#[repr(C)]
#[allow(non_snake_case)]
#[derive(Debug, Copy, Clone)]
pub struct SysmemColorSpaceFUCHSIAX {
    pub sType: vk::StructureType,
    pub __bindgen_padding_0: [u8; 4usize],
    pub pNext: *const ::std::os::raw::c_void,
    pub colorSpace: u32,
    pub __bindgen_padding_1: [u8; 4usize],
}
impl Default for SysmemColorSpaceFUCHSIAX {
    fn default() -> Self {
        let mut s = ::std::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::std::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}

#[repr(C)]
#[allow(non_snake_case)]
#[derive(Debug, Copy, Clone)]
pub struct ImageConstraintsInfoFUCHSIAX {
    pub sType: vk::StructureType,
    pub __bindgen_padding_0: [u8; 4usize],
    pub pNext: *const ::std::os::raw::c_void,
    pub createInfoCount: u32,
    pub __bindgen_padding_1: [u8; 4usize],
    pub pCreateInfos: *const vk::ImageCreateInfo,
    pub pFormatConstraints: *const ImageFormatConstraintsInfoFUCHSIAX,
    pub minBufferCount: u32,
    pub maxBufferCount: u32,
    pub minBufferCountForCamping: u32,
    pub minBufferCountForDedicatedSlack: u32,
    pub minBufferCountForSharedSlack: u32,
    pub flags: vk::Flags,
}
impl Default for ImageConstraintsInfoFUCHSIAX {
    fn default() -> Self {
        let mut s = ::std::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::std::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}

#[repr(C)]
#[allow(non_snake_case)]
#[derive(Debug, Copy, Clone)]
pub struct ImageFormatConstraintsInfoFUCHSIAX {
    pub sType: vk::StructureType,
    pub __bindgen_padding_0: [u8; 4usize],
    pub pNext: *const ::std::os::raw::c_void,
    pub requiredFormatFeatures: vk::FormatFeatureFlags,
    pub flags: vk::Flags,
    pub sysmemFormat: u64,
    pub colorSpaceCount: u32,
    pub __bindgen_padding_1: [u8; 4usize],
    pub pColorSpaces: *const SysmemColorSpaceFUCHSIAX,
}
impl Default for ImageFormatConstraintsInfoFUCHSIAX {
    fn default() -> Self {
        let mut s = ::std::mem::MaybeUninit::<Self>::uninit();
        unsafe {
            ::std::ptr::write_bytes(s.as_mut_ptr(), 0, 1);
            s.assume_init()
        }
    }
}

ptrs!(FuchsiaExtensionPointers, {
    CreateBufferCollectionFUCHSIA => (
        device: vk::Device,
        pImportInfo: *const BufferCollectionCreateInfoFUCHSIA,
        pAllocator: *const vk::AllocationCallbacks,
        pCollection: *mut BufferCollectionFUCHSIA) -> vk::Result,
    CreateBufferCollectionFUCHSIAX => (
        device: vk::Device,
        pImportInfo: *const BufferCollectionCreateInfoFUCHSIA,
        pAllocator: *const vk::AllocationCallbacks,
        pCollection: *mut BufferCollectionFUCHSIA) -> vk::Result,
    SetBufferCollectionConstraintsFUCHSIA => (
        device: vk::Device,
        collection: BufferCollectionFUCHSIA,
        pImageInfo: *const vk::ImageCreateInfo) -> vk::Result,
    SetBufferCollectionImageConstraintsFUCHSIAX => (
        device: vk::Device,
        collection: BufferCollectionFUCHSIAX,
        pImageConstraintsInfo: *const ImageConstraintsInfoFUCHSIAX) -> vk::Result,
    DestroyBufferCollectionFUCHSIA => (
        device: vk::Device,
        collection: BufferCollectionFUCHSIA,
        pAllocator: *const vk::AllocationCallbacks) -> (),
    GetBufferCollectionPropertiesFUCHSIA => (
        device: vk::Device,
        collection: BufferCollectionFUCHSIA,
        pProperties: *mut BufferCollectionPropertiesFUCHSIA) -> vk::Result,
});

pub unsafe fn init<T>(f: impl FnOnce(*mut T)) -> T {
    let mut value = MaybeUninit::uninit();
    f(value.as_mut_ptr());
    value.assume_init()
}

mod composition;
mod context;
mod image;
mod path;
mod raster;

pub use composition::SpinelComposition;
pub use context::InnerContext;
pub use context::SpinelContext;
pub use image::SpinelImage;
pub use path::{SpinelPath, SpinelPathBuilder};
pub use raster::{SpinelRaster, SpinelRasterBuilder};

#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Spinel;

impl Backend for Spinel {
    type Image = SpinelImage;
    type Context = SpinelContext;
    type Path = SpinelPath;
    type PathBuilder = SpinelPathBuilder;
    type Raster = SpinelRaster;
    type RasterBuilder = SpinelRasterBuilder;
    type Composition = SpinelComposition;

    fn new_context(
        token: ClientEnd<BufferCollectionTokenMarker>,
        size: Size2D<u32>,
        display_rotation: DisplayRotation,
    ) -> SpinelContext {
        SpinelContext::new(token, size, display_rotation)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_endpoints;

    use euclid::size2;

    use crate::{drawing::DisplayRotation, render::generic};

    #[test]
    fn spinel_init() {
        generic::tests::run(|| {
            let (token, _) =
                create_endpoints::<BufferCollectionTokenMarker>().expect("create_endpoint");
            Spinel::new_context(token, size2(100, 100), DisplayRotation::Deg0);
        });
    }
}
