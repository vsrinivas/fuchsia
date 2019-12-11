// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    carnelian::Point,
    euclid::Transform2D,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_sysmem::{
        AllocatorMarker, BufferCollectionConstraints, BufferCollectionSynchronousProxy,
        BufferCollectionTokenMarker, BufferMemoryConstraints, BufferUsage, ColorSpace,
        ColorSpaceType, FormatModifier, HeapType, ImageFormatConstraints,
        PixelFormat as SysmemPixelFormat, PixelFormatType, CPU_USAGE_WRITE_OFTEN,
        FORMAT_MODIFIER_LINEAR,
    },
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::{self as zx, prelude::*},
    spinel_rs_sys::{SpnCommand, SpnResult::*, *},
    std::{
        cell::RefCell,
        collections::{BTreeMap, HashSet},
        convert::TryFrom,
        f32,
        ffi::CStr,
        mem,
        os::raw::{c_char, c_void},
        ptr,
        rc::Rc,
        sync::Arc,
    },
    vk_sys as vk,
};

// Required spinel libraries.
#[link(name = "spinel", kind = "static")]
#[link(name = "compute_common", kind = "static")]
#[link(name = "compute_common_vk", kind = "static")]
#[link(name = "hotsort_vk", kind = "static")]
#[link(name = "hotsort_vk_hs_nvidia_sm35_u64", kind = "static")]
#[link(name = "hotsort_vk_hs_intel_gen8_u64", kind = "static")]
#[link(name = "hotsort_vk_hs_amd_gcn3_u64", kind = "static")]
#[link(name = "spinel_vk", kind = "static")]
#[link(name = "spinel_vk_find_target", kind = "static")]
#[link(name = "spinel_vk_spn_nvidia_sm50", kind = "static")]
#[link(name = "spinel_vk_spn_intel_gen8", kind = "static")]
#[link(name = "spinel_vk_spn_amd_gcn3", kind = "static")]
extern "C" {}

pub enum Path {
    Spinel(SpinelPath),
    Mold(mold::Path),
}

pub trait PathBuilder {
    fn begin(&mut self);
    fn end(&mut self) -> Path;
    fn move_to(&mut self, p: &Point);
    fn line_to(&mut self, p: &Point);
}

pub enum Raster {
    Spinel(SpinelRaster),
    Mold(mold::Raster),
}

pub enum GroupId {
    Spinel(SpnGroupId),
    Mold(u32),
}

pub trait RasterBuilder {
    fn begin(&mut self);
    fn end(&mut self) -> Raster;
    fn add(&mut self, path: &Path, transform: &Transform2D<f32>, clip: &[f32; 4]);
}

pub trait Styling {
    fn seal(&mut self);
    fn unseal(&mut self);
    fn reset(&mut self);
    fn alloc_group(&mut self, range_lo: u32, range_hi: u32, background_color: &[f32; 4])
        -> GroupId;
    fn group_layer(&mut self, group_id: &GroupId, layer_id: u32, color: &[f32; 4]);
}

pub trait Composition {
    fn seal(&mut self);
    fn unseal(&mut self);
    fn reset(&mut self);
    fn set_clip(&mut self, clip: &[u32; 4]);
    fn place(&mut self, raster: &Raster, layer_id: u32);
}

pub trait Context {
    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat;
    fn styling(&mut self) -> &mut dyn Styling;
    fn path_builder(&mut self) -> &mut dyn PathBuilder;
    fn raster_builder(&mut self) -> &mut dyn RasterBuilder;
    fn composition(&mut self, index: u32) -> &mut dyn Composition;
    fn render(&mut self, index: u32, clear: bool, clear_color: &[f32; 4]);
}

//
// Spinel Implementation
//

pub struct SpinelPath {
    context: Rc<RefCell<SpnContext>>,
    path: SpnPath,
}

impl SpinelPath {
    fn new(context: Rc<RefCell<SpnContext>>, path: SpnPath) -> Self {
        Self { context, path }
    }
}

impl Drop for SpinelPath {
    fn drop(&mut self) {
        unsafe {
            spn_path_release(*self.context.borrow(), &self.path as *const _, 1);
        }
    }
}

pub struct SpinelRaster {
    context: Rc<RefCell<SpnContext>>,
    raster: SpnRaster,
}

impl SpinelRaster {
    fn new(context: Rc<RefCell<SpnContext>>, raster: SpnRaster) -> Self {
        Self { context, raster }
    }
}

impl Drop for SpinelRaster {
    fn drop(&mut self) {
        unsafe {
            spn_raster_release(*self.context.borrow(), &self.raster as *const _, 1);
        }
    }
}

struct SpinelPathBuilder {
    context: Rc<RefCell<SpnContext>>,
    path_builder: SpnPathBuilder,
}

impl SpinelPathBuilder {
    fn new(context: Rc<RefCell<SpnContext>>) -> Self {
        let path_builder = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_path_builder_create(*context.borrow(), output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            output.assume_init()
        };

        Self { context, path_builder }
    }
}

impl PathBuilder for SpinelPathBuilder {
    fn begin(&mut self) {
        unsafe {
            let status = spn_path_builder_begin(self.path_builder);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn end(&mut self) -> Path {
        let path = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_path_builder_end(self.path_builder, output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            output.assume_init()
        };
        Path::Spinel(SpinelPath::new(Rc::clone(&self.context), path))
    }

    fn move_to(&mut self, p: &Point) {
        unsafe {
            let status = spn_path_builder_move_to(self.path_builder, p.x, p.y);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn line_to(&mut self, p: &Point) {
        unsafe {
            let status = spn_path_builder_line_to(self.path_builder, p.x, p.y);
            assert_eq!(status, SpnSuccess);
        }
    }
}

impl Drop for SpinelPathBuilder {
    fn drop(&mut self) {
        unsafe {
            spn_path_builder_release(self.path_builder);
        }
    }
}

struct SpinelRasterBuilder {
    context: Rc<RefCell<SpnContext>>,
    raster_builder: SpnRasterBuilder,
}

impl SpinelRasterBuilder {
    fn new(context: Rc<RefCell<SpnContext>>) -> Self {
        let raster_builder = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_raster_builder_create(*context.borrow(), output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            output.assume_init()
        };

        Self { context, raster_builder }
    }
}

impl RasterBuilder for SpinelRasterBuilder {
    fn begin(&mut self) {
        unsafe {
            let status = spn_raster_builder_begin(self.raster_builder);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn end(&mut self) -> Raster {
        let raster = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_raster_builder_end(self.raster_builder, output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            output.assume_init()
        };
        Raster::Spinel(SpinelRaster::new(Rc::clone(&self.context), raster))
    }

    fn add(&mut self, path: &Path, transform: &Transform2D<f32>, clip: &[f32; 4]) {
        let transform = SpnTransform {
            sx: transform.m11 * 32.0,
            shx: transform.m21 * 32.0,
            tx: transform.m31 * 32.0,
            shy: transform.m12 * 32.0,
            sy: transform.m22 * 32.0,
            ty: transform.m32 * 32.0,
            w0: 0.0,
            w1: 0.0,
        };
        let clip = SpnClip { x0: clip[0], y0: clip[1], x1: clip[2], y1: clip[3] };
        match path {
            Path::Spinel(path) => unsafe {
                let status = spn_raster_builder_add(
                    self.raster_builder,
                    &path.path,
                    ptr::null_mut(),
                    &transform,
                    ptr::null_mut(),
                    &clip,
                    1,
                );
                assert_eq!(status, SpnSuccess);
            },
            _ => {
                panic!("bad path");
            }
        }
    }
}

impl Drop for SpinelRasterBuilder {
    fn drop(&mut self) {
        unsafe {
            spn_raster_builder_release(self.raster_builder);
        }
    }
}

struct SpinelStyling {
    styling: SpnStyling,
}

impl SpinelStyling {
    fn new(context: Rc<RefCell<SpnContext>>, layers_count: u32, cmds_count: u32) -> Self {
        let styling = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_styling_create(
                *context.borrow(),
                output.as_mut_ptr(),
                layers_count,
                cmds_count,
            );
            assert_eq!(status, SpnSuccess);
            output.assume_init()
        };

        Self { styling }
    }
}

impl Styling for SpinelStyling {
    fn seal(&mut self) {
        unsafe {
            let status = spn_styling_seal(self.styling);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn unseal(&mut self) {
        unsafe {
            let status = spn_styling_unseal(self.styling);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn reset(&mut self) {
        unsafe {
            let status = spn_styling_reset(self.styling);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn alloc_group(
        &mut self,
        range_lo: u32,
        range_hi: u32,
        background_color: &[f32; 4],
    ) -> GroupId {
        let group_id = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_styling_group_alloc(self.styling, output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            output.assume_init()
        };

        unsafe {
            let status = spn_styling_group_parents(self.styling, group_id, 0, ptr::null_mut());
            assert_eq!(status, SpnSuccess);
            spn_styling_group_range_lo(self.styling, group_id, range_lo);
            spn_styling_group_range_hi(self.styling, group_id, range_hi);
        }

        let cmds_enter = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_styling_group_enter(self.styling, group_id, 1, output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            std::slice::from_raw_parts_mut(output.assume_init(), 1)
        };
        cmds_enter[0] = SpnCommand::SpnStylingOpcodeColorAccZero;

        let cmds_leave = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_styling_group_leave(self.styling, group_id, 4, output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            std::slice::from_raw_parts_mut(output.assume_init(), 4)
        };

        unsafe {
            spn_styling_background_over_encoder(&mut cmds_leave[0], background_color.as_ptr());
        }
        cmds_leave[3] = SpnCommand::SpnStylingOpcodeColorAccStoreToSurface;

        GroupId::Spinel(group_id)
    }

    fn group_layer(&mut self, group_id: &GroupId, layer_id: u32, color: &[f32; 4]) {
        let group_id = match group_id {
            GroupId::Spinel(id) => *id,
            _ => panic!("invalid group id"),
        };
        let cmds = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status =
                spn_styling_group_layer(self.styling, group_id, layer_id, 6, output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            std::slice::from_raw_parts_mut(output.assume_init(), 6)
        };
        cmds[0] = SpnCommand::SpnStylingOpcodeCoverWipZero;
        cmds[1] = SpnCommand::SpnStylingOpcodeCoverNonzero;
        unsafe {
            spn_styling_layer_fill_rgba_encoder(&mut cmds[2], color.as_ptr());
        }
        cmds[5] = SpnCommand::SpnStylingOpcodeBlendOver;
    }
}

impl Drop for SpinelStyling {
    fn drop(&mut self) {
        unsafe {
            spn_styling_release(self.styling);
        }
    }
}

struct SpinelComposition {
    composition: SpnComposition,
}

impl SpinelComposition {
    fn new(context: Rc<RefCell<SpnContext>>) -> Self {
        let composition = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status = spn_composition_create(*context.borrow(), output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            output.assume_init()
        };

        Self { composition }
    }
}

impl Composition for SpinelComposition {
    fn seal(&mut self) {
        unsafe {
            let status = spn_composition_seal(self.composition);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn unseal(&mut self) {
        unsafe {
            let status = spn_composition_unseal(self.composition);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn reset(&mut self) {
        unsafe {
            let status = spn_composition_reset(self.composition);
            assert_eq!(status, SpnSuccess);
        }
    }

    fn set_clip(&mut self, clip: &[u32; 4]) {
        unsafe {
            let status = spn_composition_set_clip(self.composition, clip.as_ptr());
            assert_eq!(status, SpnSuccess);
        }
    }
    fn place(&mut self, raster: &Raster, layer_id: u32) {
        match raster {
            Raster::Spinel(raster) => unsafe {
                let status = spn_composition_place(
                    self.composition,
                    &raster.raster,
                    &layer_id,
                    ptr::null(),
                    1,
                );
                assert_eq!(status, SpnSuccess);
            },
            _ => {
                panic!("bad raster");
            }
        }
    }
}

impl Drop for SpinelComposition {
    fn drop(&mut self) {
        unsafe {
            spn_composition_release(self.composition);
        }
    }
}

// TODO: Remove buffer collection bindings when they are upstream.

type BufferCollectionFUCHSIA = u64;

const STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA: u32 = 1001004000;
const STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA: u32 = 1001004004;
const STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA: u32 = 1001004005;

#[repr(C)]
#[allow(non_snake_case)]
struct BufferCollectionCreateInfoFUCHSIA {
    sType: vk::StructureType,
    pNext: *const ::std::os::raw::c_void,
    collectionToken: u32,
}

#[repr(C)]
#[allow(non_snake_case)]
struct ImportMemoryBufferCollectionFUCHSIA {
    sType: vk::StructureType,
    pNext: *const ::std::os::raw::c_void,
    collection: BufferCollectionFUCHSIA,
    index: u32,
}

#[repr(C)]
#[allow(non_snake_case)]
struct BufferCollectionImageCreateInfoFUCHSIA {
    sType: vk::StructureType,
    pNext: *const ::std::os::raw::c_void,
    collection: BufferCollectionFUCHSIA,
    index: u32,
}

#[repr(C)]
#[allow(non_snake_case)]
struct BufferCollectionPropertiesFUCHSIA {
    sType: vk::StructureType,
    pNext: *const ::std::os::raw::c_void,
    memoryTypeBits: u32,
    count: u32,
}

macro_rules! ptrs {(
    $struct_name:ident,
    { $($name:ident => ($($param_n:ident: $param_ty:ty),*) -> $ret:ty,)+ }) => (
        #[allow(non_snake_case)]
        struct $struct_name {
            $(
                pub $name: extern "system" fn($($param_ty),*) -> $ret,
            )+
        }

        impl $struct_name {
            fn load<F>(mut f: F) -> $struct_name
                where F: FnMut(&CStr) -> *const ::std::os::raw::c_void
            {
                #[allow(non_snake_case)]
                $struct_name {
                    $(
                        $name: unsafe {
                            extern "system" fn $name($(_: $param_ty),*) {
                                panic!("function pointer `{}` not loaded", stringify!($name))
                            }
                            let name = CStr::from_bytes_with_nul_unchecked(
                                concat!("vk", stringify!($name), "\0").as_bytes());
                            let val = f(name);
                            if val.is_null() {
                                mem::transmute($name as *const ())
                            } else {
                                mem::transmute(val)
                            }
                        },
                    )+
                }
            }

            $(
                #[inline]
                #[allow(non_snake_case)]
                unsafe fn $name(&self $(, $param_n: $param_ty)*) -> $ret {
                    let ptr = self.$name;
                    ptr($($param_n),*)
                }
            )+
        }
    )
}

ptrs!(FuchsiaExtensionPointers, {
    CreateBufferCollectionFUCHSIA => (
        device: vk::Device,
        pImportInfo: *const BufferCollectionCreateInfoFUCHSIA,
        pAllocator: *const vk::AllocationCallbacks,
        pCollection: *mut BufferCollectionFUCHSIA) -> vk::Result,
    SetBufferCollectionConstraintsFUCHSIA => (
        device: vk::Device,
        collection: BufferCollectionFUCHSIA,
        pImageInfo: *const vk::ImageCreateInfo) -> vk::Result,
    DestroyBufferCollectionFUCHSIA => (
        device: vk::Device,
        collection: BufferCollectionFUCHSIA,
        pAllocator: *const vk::AllocationCallbacks) -> (),
    GetBufferCollectionPropertiesFUCHSIA => (
        device: vk::Device,
        collection: BufferCollectionFUCHSIA,
        pProperties: *mut BufferCollectionPropertiesFUCHSIA) -> vk::Result,
});

#[link(name = "vulkan")]
extern "C" {
    fn vkGetInstanceProcAddr(
        instance: vk::Instance,
        pName: *const ::std::os::raw::c_char,
    ) -> vk::PFN_vkVoidFunction;
}

const STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: u32 = 1000059000;
const STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT: u32 = 1000261000;
const STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: u32 = 1000269000;
const STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT: u32 = 1000221000;
const STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR: u32 = 1000082000;
const STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT: u32 = 1000225002;

#[repr(C)]
#[allow(non_snake_case)]
struct PhysicalDeviceHostQueryResetFeaturesEXT {
    sType: vk::StructureType,
    pNext: *const c_void,
    hostQueryReset: vk::Bool32,
}

#[repr(C)]
#[allow(non_snake_case)]
struct PhysicalDevicePipelineExecutablePropertiesFeaturesKHR {
    sType: vk::StructureType,
    pNext: *const c_void,
    pipelineExecutableInfo: vk::Bool32,
}

#[repr(C)]
#[allow(non_snake_case)]
struct PhysicalDeviceScalarBlockLayoutFeaturesEXT {
    sType: vk::StructureType,
    pNext: *const c_void,
    scalarBlockLayout: vk::Bool32,
}

#[repr(C)]
#[allow(non_snake_case)]
struct VkPhysicalDeviceShaderFloat16Int8FeaturesKHR {
    sType: vk::StructureType,
    pNext: *const c_void,
    shaderFloat16: vk::Bool32,
    shaderInt8: vk::Bool32,
}

#[repr(C)]
#[allow(non_snake_case)]
struct VkPhysicalDeviceSubgroupSizeControlFeaturesEXT {
    sType: vk::StructureType,
    pNext: *const c_void,
    subgroupSizeControl: vk::Bool32,
    computeFullSubgroups: vk::Bool32,
}

struct VulkanImage {
    device: vk::Device,
    image: vk::Image,
    sampler: vk::Sampler,
    view: vk::ImageView,
    memory: vk::DeviceMemory,
    vk: vk::DevicePointers,
    layout: vk::ImageLayout,
}

impl VulkanImage {
    fn get_image_create_info(
        width: u32,
        height: u32,
        format: vk::Format,
        p_next: *const c_void,
    ) -> vk::ImageCreateInfo {
        vk::ImageCreateInfo {
            sType: vk::STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            pNext: p_next,
            flags: 0,
            imageType: vk::IMAGE_TYPE_2D,
            format,
            extent: vk::Extent3D { width, height, depth: 1 },
            mipLevels: 1,
            arrayLayers: 1,
            samples: vk::SAMPLE_COUNT_1_BIT,
            tiling: vk::IMAGE_TILING_OPTIMAL,
            usage: vk::IMAGE_USAGE_COLOR_ATTACHMENT_BIT | vk::IMAGE_USAGE_STORAGE_BIT,
            sharingMode: vk::SHARING_MODE_EXCLUSIVE,
            queueFamilyIndexCount: 0,
            pQueueFamilyIndices: ptr::null(),
            initialLayout: vk::IMAGE_LAYOUT_UNDEFINED,
        }
    }

    fn new(
        device: vk::Device,
        vk_i: &vk::InstancePointers,
        vk_ext: &FuchsiaExtensionPointers,
        width: u32,
        height: u32,
        format: vk::Format,
        buffer_collection: BufferCollectionFUCHSIA,
        index: u32,
    ) -> Self {
        let vk = vk::DevicePointers::load(|name| unsafe {
            vk_i.GetDeviceProcAddr(device, name.as_ptr()) as *const _
        });
        let image = unsafe {
            let buffer_collection_info = BufferCollectionImageCreateInfoFUCHSIA {
                sType: STRUCTURE_TYPE_BUFFER_COLLECTION_IMAGE_CREATE_INFO_FUCHSIA,
                pNext: ptr::null(),
                collection: buffer_collection,
                index: index,
            };
            let info = Self::get_image_create_info(
                width,
                height,
                format,
                &buffer_collection_info as *const _ as *const c_void,
            );

            let mut output = mem::MaybeUninit::uninit();
            let result = vk.CreateImage(device, &info, ptr::null(), output.as_mut_ptr());
            assert_eq!(result, vk::SUCCESS);
            output.assume_init()
        };

        let mem_reqs = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            vk.GetImageMemoryRequirements(device, image, output.as_mut_ptr());
            output.assume_init()
        };

        let mem_props = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let result = vk_ext.GetBufferCollectionPropertiesFUCHSIA(
                device,
                buffer_collection,
                output.as_mut_ptr(),
            );
            assert_eq!(result, vk::SUCCESS);
            output.assume_init()
        };
        let mem_type_bits = mem_reqs.memoryTypeBits & mem_props.memoryTypeBits;
        assert_ne!(mem_type_bits, 0);
        let mem_type_index = {
            let mut bits = mem_type_bits;
            let mut index = 0;
            while (bits & 1) == 0 {
                index += 1;
                bits >>= 1;
            }
            index
        };

        let memory = unsafe {
            let import_info = ImportMemoryBufferCollectionFUCHSIA {
                sType: STRUCTURE_TYPE_IMPORT_MEMORY_BUFFER_COLLECTION_FUCHSIA,
                pNext: ptr::null(),
                collection: buffer_collection,
                index: index,
            };
            let alloc_info = vk::MemoryAllocateInfo {
                sType: vk::STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                pNext: &import_info as *const _ as *const c_void,
                allocationSize: mem_reqs.size as u64,
                memoryTypeIndex: mem_type_index,
            };

            let mut output = mem::MaybeUninit::uninit();
            let result = vk.AllocateMemory(device, &alloc_info, ptr::null(), output.as_mut_ptr());
            assert_eq!(result, vk::SUCCESS);
            output.assume_init()
        };

        unsafe {
            let result = vk.BindImageMemory(device, image, memory, 0);
            assert_eq!(result, vk::SUCCESS);
        }

        let sampler = unsafe {
            let info = vk::SamplerCreateInfo {
                sType: vk::STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                pNext: ptr::null(),
                flags: 0,
                magFilter: vk::FILTER_NEAREST,
                minFilter: vk::FILTER_NEAREST,
                mipmapMode: vk::SAMPLER_MIPMAP_MODE_NEAREST,
                addressModeU: vk::SAMPLER_ADDRESS_MODE_REPEAT,
                addressModeV: vk::SAMPLER_ADDRESS_MODE_REPEAT,
                addressModeW: vk::SAMPLER_ADDRESS_MODE_REPEAT,
                mipLodBias: 0.0,
                anisotropyEnable: vk::FALSE,
                maxAnisotropy: 1.0,
                compareEnable: vk::FALSE,
                compareOp: vk::COMPARE_OP_NEVER,
                minLod: 0.0,
                maxLod: 0.0,
                borderColor: vk::BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                unnormalizedCoordinates: vk::TRUE,
            };

            let mut output = mem::MaybeUninit::uninit();
            let result = vk.CreateSampler(device, &info, ptr::null(), output.as_mut_ptr());
            assert_eq!(result, vk::SUCCESS);
            output.assume_init()
        };

        let view = unsafe {
            let info = vk::ImageViewCreateInfo {
                sType: vk::STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                pNext: ptr::null(),
                flags: 0,
                image: image,
                viewType: vk::IMAGE_VIEW_TYPE_2D,
                format,
                components: vk::ComponentMapping {
                    r: vk::COMPONENT_SWIZZLE_R,
                    g: vk::COMPONENT_SWIZZLE_G,
                    b: vk::COMPONENT_SWIZZLE_B,
                    a: vk::COMPONENT_SWIZZLE_A,
                },
                subresourceRange: vk::ImageSubresourceRange {
                    aspectMask: vk::IMAGE_ASPECT_COLOR_BIT,
                    baseMipLevel: 0,
                    levelCount: 1,
                    baseArrayLayer: 0,
                    layerCount: 1,
                },
            };

            let mut output = mem::MaybeUninit::uninit();
            let result = vk.CreateImageView(device, &info, ptr::null(), output.as_mut_ptr());
            assert_eq!(result, vk::SUCCESS);
            output.assume_init()
        };

        VulkanImage { device, image, sampler, view, memory, vk, layout: vk::IMAGE_LAYOUT_UNDEFINED }
    }
}

impl Drop for VulkanImage {
    fn drop(&mut self) {
        unsafe {
            self.vk.DestroyImageView(self.device, self.view, ptr::null());
            self.vk.DestroySampler(self.device, self.sampler, ptr::null());
            self.vk.DestroyImage(self.device, self.image, ptr::null());
            self.vk.FreeMemory(self.device, self.memory, ptr::null());
        }
    }
}

const BUFFER_COLLECTION_EXTENSION_NAME: &'static [u8; 29usize] = b"VK_FUCHSIA_buffer_collection\0";

extern "C" fn image_dispatch_submitter(
    queue: vk::Queue,
    fence: vk::Fence,
    cb: vk::CommandBuffer,
    data: *mut ::std::os::raw::c_void,
) {
    unsafe {
        // TODO: Add acquire and release semaphores.
        let info = vk::SubmitInfo {
            sType: vk::STRUCTURE_TYPE_SUBMIT_INFO,
            pNext: ptr::null(),
            waitSemaphoreCount: 0,
            pWaitSemaphores: ptr::null(),
            pWaitDstStageMask: ptr::null(),
            commandBufferCount: 1,
            pCommandBuffers: &cb,
            signalSemaphoreCount: 0,
            pSignalSemaphores: ptr::null(),
        };
        // Safe as images outlive the spinel context.
        let image_ptr = data as *mut VulkanImage;
        let result = (*image_ptr).vk.QueueSubmit(queue, 1, &info, fence);
        assert_eq!(result, vk::SUCCESS);
    }
}

struct VulkanContext {
    device: vk::Device,
    vk_i: vk::InstancePointers,
    vk: vk::DevicePointers,
    vk_ext: FuchsiaExtensionPointers,
    buffer_collection: BufferCollectionFUCHSIA,
    width: u32,
    height: u32,
    format: vk::Format,
}

pub struct SpinelContext {
    vulkan: VulkanContext,
    images: BTreeMap<u32, Box<VulkanImage>>,
    _environment: Box<SpnVkEnvironment>,
    context: Rc<RefCell<SpnContext>>,
    compositions: BTreeMap<u32, SpinelComposition>,
    styling: SpinelStyling,
    path_builder: SpinelPathBuilder,
    raster_builder: SpinelRasterBuilder,
}

impl SpinelContext {
    pub fn new(
        token: ClientEnd<BufferCollectionTokenMarker>,
        config: &fuchsia_framebuffer::Config,
        app_name: *const u8,
        block_pool_size: u64,
        handle_count: u32,
        layers_count: u32,
        cmds_count: u32,
    ) -> Self {
        let entry_points = {
            vk::EntryPoints::load(|name| unsafe {
                mem::transmute(vkGetInstanceProcAddr(0, name.as_ptr()))
            })
        };

        macro_rules! vulkan_version {
            ( $major:expr, $minor:expr, $patch:expr ) => {
                ($major as u32) << 22 | ($minor as u32) << 12 | ($patch as u32)
            };
        }

        let app_info = vk::ApplicationInfo {
            sType: vk::STRUCTURE_TYPE_APPLICATION_INFO,
            pNext: ptr::null(),
            pApplicationName: app_name as *const c_char,
            applicationVersion: 0,
            pEngineName: ptr::null(),
            engineVersion: 0,
            apiVersion: vulkan_version!(1, 1, 0),
        };
        let instance = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let create_info = vk::InstanceCreateInfo {
                sType: vk::STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                pNext: ptr::null(),
                flags: 0,
                pApplicationInfo: &app_info,
                enabledLayerCount: 0,
                ppEnabledLayerNames: ptr::null(),
                enabledExtensionCount: 0,
                ppEnabledExtensionNames: ptr::null(),
            };
            let result =
                entry_points.CreateInstance(&create_info, ptr::null(), output.as_mut_ptr());
            assert_eq!(result, vk::SUCCESS);
            output.assume_init()
        };

        let vk_i = {
            vk::InstancePointers::load(|name| unsafe {
                mem::transmute(vkGetInstanceProcAddr(instance, name.as_ptr()))
            })
        };

        let physical_devices: Vec<vk::PhysicalDevice> = unsafe {
            let mut num = 0;
            let result = vk_i.EnumeratePhysicalDevices(instance, &mut num, ptr::null_mut());
            assert_eq!(result, vk::SUCCESS);

            let mut output = Vec::with_capacity(num as usize);
            let result = vk_i.EnumeratePhysicalDevices(instance, &mut num, output.as_mut_ptr());
            assert_eq!(result, vk::SUCCESS);
            output.set_len(num as usize);
            output
        };
        assert_ne!(physical_devices.len(), 0);
        // TODO: Ability to select physical device.
        let physical_device = physical_devices[0];

        let pdmp = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            vk_i.GetPhysicalDeviceMemoryProperties(physical_device, output.as_mut_ptr());
            output.assume_init()
        };

        let pdp = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            vk_i.GetPhysicalDeviceProperties(physical_device, output.as_mut_ptr());
            output.assume_init()
        };

        println!(
            "{:X} : {:X} : {:?}",
            pdp.vendorID,
            pdp.deviceID,
            unsafe { CStr::from_ptr(pdp.deviceName.as_ptr()) }.to_str().unwrap()
        );

        let format = {
            macro_rules! format {
                ($var:tt) => {
                    (vk::$var, stringify!($var))
                };
            }
            static FORMATS: &'static [(vk::Format, &str)] =
                &[format!(FORMAT_B8G8R8A8_UNORM), format!(FORMAT_R8G8B8A8_UNORM)];

            let mut preferred_format = vk::FORMAT_UNDEFINED;
            let mut preferred_format_features = 0;

            for format in FORMATS {
                let properties = unsafe {
                    let mut output = mem::MaybeUninit::uninit();
                    vk_i.GetPhysicalDeviceFormatProperties(
                        physical_device,
                        format.0,
                        output.as_mut_ptr(),
                    );
                    output.assume_init()
                };

                // Storage is required.
                if properties.optimalTilingFeatures & vk::FORMAT_FEATURE_STORAGE_IMAGE_BIT == 0 {
                    continue;
                }
                if preferred_format == vk::FORMAT_UNDEFINED {
                    preferred_format = format.0;
                    preferred_format_features = properties.optimalTilingFeatures;
                }
                // Prefer formats that support sampling.
                if (properties.optimalTilingFeatures & vk::FORMAT_FEATURE_SAMPLED_IMAGE_BIT != 0)
                    && (preferred_format_features & vk::FORMAT_FEATURE_SAMPLED_IMAGE_BIT == 0)
                {
                    preferred_format = format.0;
                    preferred_format_features = properties.optimalTilingFeatures;
                }
                println!("{}{}", format.1, if preferred_format == format.0 { " *" } else { "" });
            }
            preferred_format
        };
        assert_ne!(format, vk::FORMAT_UNDEFINED);

        let (spn_target, hs_target) = unsafe {
            let mut spn_output = mem::MaybeUninit::uninit();
            let mut hs_output = mem::MaybeUninit::uninit();
            let status = spn_vk_find_target(
                pdp.vendorID,
                pdp.deviceID,
                spn_output.as_mut_ptr(),
                hs_output.as_mut_ptr(),
                ptr::null_mut(),
                0,
            );
            assert_eq!(status, true);
            (spn_output.assume_init(), hs_output.assume_init() as *mut _)
        };

        let mut spn_tr = unsafe {
            let mut output = mem::MaybeUninit::zeroed();
            let status = spn_vk_target_get_requirements(spn_target, output.as_mut_ptr());
            assert_eq!(status, SpnErrorPartialTargetRequirements);
            output.assume_init()
        };
        let mut hs_tr = unsafe {
            let mut output = mem::MaybeUninit::zeroed();
            let status = hotsort_vk_target_get_requirements(hs_target, output.as_mut_ptr());
            assert_eq!(status, false);
            output.assume_init()
        };
        let mut qcis = unsafe {
            let num = spn_tr.qci_count as usize;
            let mut output: Vec<vk::DeviceQueueCreateInfo> = Vec::with_capacity(num);
            output.set_len(num as usize);
            output
        };
        let mut ext_names = unsafe {
            let num = 1 + spn_tr.ext_name_count as usize + hs_tr.ext_name_count as usize;
            let mut output: Vec<*const c_char> = Vec::with_capacity(num);
            output.set_len(num as usize);
            output
        };
        ext_names[0] = BUFFER_COLLECTION_EXTENSION_NAME.as_ptr() as *const c_char;

        let mut feature1 = unsafe {
            let output = mem::MaybeUninit::<PhysicalDeviceHostQueryResetFeaturesEXT>::zeroed();
            output.assume_init()
        };
        feature1.sType = STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
        let mut feature2 = unsafe {
            let output =
                mem::MaybeUninit::<PhysicalDevicePipelineExecutablePropertiesFeaturesKHR>::zeroed();
            output.assume_init()
        };
        feature2.sType = STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR;
        let mut feature3 = unsafe {
            let output = mem::MaybeUninit::<PhysicalDeviceScalarBlockLayoutFeaturesEXT>::zeroed();
            output.assume_init()
        };
        feature3.sType = STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT;
        let mut feature4 = unsafe {
            let output = mem::MaybeUninit::<VkPhysicalDeviceShaderFloat16Int8FeaturesKHR>::zeroed();
            output.assume_init()
        };
        feature4.sType = STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;
        let mut feature5 = unsafe {
            let output =
                mem::MaybeUninit::<VkPhysicalDeviceSubgroupSizeControlFeaturesEXT>::zeroed();
            output.assume_init()
        };
        feature5.sType = STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
        feature1.pNext = &mut feature2 as *mut _ as *mut c_void;
        feature2.pNext = &mut feature3 as *mut _ as *mut c_void;
        feature3.pNext = &mut feature4 as *mut _ as *mut c_void;
        feature4.pNext = &mut feature5 as *mut _ as *mut c_void;
        let mut pdf2 = unsafe {
            let output = mem::MaybeUninit::<PhysicalDeviceFeatures2>::zeroed();
            output.assume_init()
        };
        pdf2.sType = STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        pdf2.pNext = &mut feature1 as *mut _ as *mut c_void;

        spn_tr.qcis = qcis.as_mut_ptr();
        spn_tr.ext_names =
            if spn_tr.ext_name_count > 0 { &mut ext_names[1] } else { ptr::null_mut() };
        spn_tr.pdf2 = &mut pdf2;
        unsafe {
            let status = spn_vk_target_get_requirements(spn_target, &mut spn_tr);
            assert_eq!(status, SpnSuccess);
        };
        hs_tr.ext_names = if hs_tr.ext_name_count > 0 {
            &mut ext_names[1 + spn_tr.ext_name_count as usize]
        } else {
            ptr::null_mut()
        };
        hs_tr.pdf = &mut pdf2.features;
        unsafe {
            let status = hotsort_vk_target_get_requirements(hs_target, &mut hs_tr);
            assert_eq!(status, true);
        };

        let device = unsafe {
            let info = vk::DeviceCreateInfo {
                sType: vk::STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                pNext: pdf2.pNext,
                flags: 0,
                queueCreateInfoCount: qcis.len() as u32,
                pQueueCreateInfos: qcis.as_ptr(),
                enabledLayerCount: 0,
                ppEnabledLayerNames: ptr::null(),
                enabledExtensionCount: ext_names.len() as u32,
                ppEnabledExtensionNames: ext_names.as_ptr(),
                pEnabledFeatures: &pdf2.features,
            };

            let mut output = mem::MaybeUninit::uninit();
            let result =
                vk_i.CreateDevice(physical_device, &info, ptr::null(), output.as_mut_ptr());
            assert_eq!(result, vk::SUCCESS);
            output.assume_init()
        };
        let vk = vk::DevicePointers::load(|name| unsafe {
            vk_i.GetDeviceProcAddr(device, name.as_ptr()) as *const _
        });

        let vk_ext = FuchsiaExtensionPointers::load(|name| unsafe {
            vk_i.GetDeviceProcAddr(device, name.as_ptr()) as *const _
        });

        let buffer_collection = unsafe {
            let info = BufferCollectionCreateInfoFUCHSIA {
                sType: STRUCTURE_TYPE_BUFFER_COLLECTION_CREATE_INFO_FUCHSIA,
                pNext: ptr::null(),
                collectionToken: token.into_channel().into_raw(),
            };

            let mut output = mem::MaybeUninit::uninit();
            let result = vk_ext.CreateBufferCollectionFUCHSIA(
                device,
                &info,
                ptr::null(),
                output.as_mut_ptr(),
            );
            assert_eq!(result, vk::SUCCESS);
            output.assume_init()
        };

        unsafe {
            let info = VulkanImage::get_image_create_info(
                config.width,
                config.height,
                format,
                ptr::null(),
            );
            let result =
                vk_ext.SetBufferCollectionConstraintsFUCHSIA(device, buffer_collection, &info);
            assert_eq!(result, vk::SUCCESS);
        };

        let mut environment = Box::new(SpnVkEnvironment {
            d: device,
            ac: ptr::null(),
            pc: 0,
            pd: physical_device,
            qfi: 0,
            pdmp: pdmp,
        });

        let create_info = SpnVkContextCreateInfo {
            block_pool_size,
            handle_count,
            spinel: spn_target,
            hotsort: hs_target,
        };

        let context = unsafe {
            let mut output = mem::MaybeUninit::uninit();
            let status =
                spn_vk_context_create(&mut *environment, &create_info, output.as_mut_ptr());
            assert_eq!(status, SpnSuccess);
            output.assume_init()
        };

        let context = Rc::new(RefCell::new(context));
        let styling = SpinelStyling::new(Rc::clone(&context), layers_count, cmds_count);
        let path_builder = SpinelPathBuilder::new(Rc::clone(&context));
        let raster_builder = SpinelRasterBuilder::new(Rc::clone(&context));

        Self {
            vulkan: VulkanContext {
                device,
                vk_i,
                vk,
                vk_ext,
                buffer_collection,
                width: config.width,
                height: config.height,
                format,
            },
            images: BTreeMap::new(),
            _environment: environment,
            context,
            compositions: BTreeMap::new(),
            styling,
            path_builder,
            raster_builder,
        }
    }
}

impl Drop for SpinelContext {
    fn drop(&mut self) {
        self.images.clear();
        unsafe {
            self.vulkan.vk_ext.DestroyBufferCollectionFUCHSIA(
                self.vulkan.device,
                self.vulkan.buffer_collection,
                ptr::null(),
            );
        }
        unsafe {
            self.vulkan.vk.DestroyDevice(self.vulkan.device, ptr::null());
        }
    }
}

impl Context for SpinelContext {
    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        match self.vulkan.format {
            vk::FORMAT_R8G8B8A8_UNORM => fuchsia_framebuffer::PixelFormat::Abgr8888,
            _ => fuchsia_framebuffer::PixelFormat::Argb8888,
        }
    }

    fn styling(&mut self) -> &mut dyn Styling {
        &mut self.styling
    }

    fn path_builder(&mut self) -> &mut dyn PathBuilder {
        &mut self.path_builder
    }

    fn raster_builder(&mut self) -> &mut dyn RasterBuilder {
        &mut self.raster_builder
    }

    fn composition(&mut self, index: u32) -> &mut dyn Composition {
        let context = &self.context;
        self.compositions.entry(index).or_insert_with(|| SpinelComposition::new(Rc::clone(context)))
    }

    fn render(&mut self, index: u32, clear: bool, clear_color: &[f32; 4]) {
        let vulkan = &self.vulkan;
        let image = self.images.entry(index).or_insert_with(|| {
            Box::new(VulkanImage::new(
                vulkan.device,
                &vulkan.vk_i,
                &vulkan.vk_ext,
                vulkan.width,
                vulkan.height,
                vulkan.format,
                vulkan.buffer_collection,
                index,
            ))
        });
        let mut rs_image_post_barrier = SpnVkRenderSubmitExtImagePostBarrier {
            ext: ptr::null_mut(),
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePostBarrier,
            new_layout: vk::IMAGE_LAYOUT_PRESENT_SRC_KHR,
            dst_qfi: vk::QUEUE_FAMILY_IGNORED,
        };
        let mut rs_image_pre_barrier = SpnVkRenderSubmitExtImagePreBarrier {
            ext: &mut rs_image_post_barrier as *mut _ as *mut c_void,
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePreBarrier,
            old_layout: image.layout,
            src_qfi: vk::QUEUE_FAMILY_IGNORED,
        };
        image.layout = vk::IMAGE_LAYOUT_PRESENT_SRC_KHR;
        let rs_clear_color = vk::ClearColorValue { float32: *clear_color };
        let mut rs_image_clear = SpnVkRenderSubmitExtImagePreClear {
            ext: &mut rs_image_pre_barrier as *mut _ as *mut c_void,
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImagePreClear,
            color: &rs_clear_color,
        };
        let mut rs_image = SpnVkRenderSubmitExtImageRender {
            ext: if clear {
                &mut rs_image_clear as *mut _ as *mut c_void
            } else {
                &mut rs_image_pre_barrier as *mut _ as *mut c_void
            },
            type_: SpnVkRenderSubmitExtType::SpnVkRenderSubmitExtTypeImageRender,
            image: image.image,
            image_info: vk::DescriptorImageInfo {
                sampler: image.sampler,
                imageView: image.view,
                imageLayout: vk::IMAGE_LAYOUT_GENERAL,
            },
            submitter_pfn: image_dispatch_submitter,
            // Safe as image outlive context.
            submitter_data: image as &mut VulkanImage as *mut _ as *mut c_void,
        };
        let composition = &self.compositions[&index];
        let rs = SpnRenderSubmit {
            ext: &mut rs_image as *mut _ as *mut c_void,
            styling: self.styling.styling,
            composition: composition.composition,
            clip: [0, 0, std::u32::MAX, std::u32::MAX],
        };
        let status = unsafe { spn_render(*self.context.borrow(), &rs) };
        assert_eq!(status, SpnSuccess);

        // Unsealing the composition will block until rendering has completed.
        // TODO(reveman): Use fence instead of blocking.
        unsafe {
            let status = spn_composition_unseal(composition.composition);
            assert_eq!(status, SpnSuccess);
        }
    }
}

//
// Mold Implementation
//

struct MoldPathBuilder {
    path_builder: spinel_mold::PathBuilder,
}

impl MoldPathBuilder {
    fn new() -> Self {
        Self { path_builder: spinel_mold::PathBuilder::new() }
    }
}

impl PathBuilder for MoldPathBuilder {
    fn begin(&mut self) {}

    fn end(&mut self) -> Path {
        Path::Mold(self.path_builder.build())
    }

    fn move_to(&mut self, p: &Point) {
        self.path_builder.move_to(p.x, p.y);
    }

    fn line_to(&mut self, p: &Point) {
        self.path_builder.line_to(p.x, p.y);
    }
}

struct MoldRasterBuilder {
    raster_builder: spinel_mold::RasterBuilder,
}

impl MoldRasterBuilder {
    fn new() -> Self {
        Self { raster_builder: spinel_mold::RasterBuilder::new() }
    }
}

impl RasterBuilder for MoldRasterBuilder {
    fn begin(&mut self) {}

    fn end(&mut self) -> Raster {
        Raster::Mold(self.raster_builder.build())
    }

    fn add(&mut self, path: &Path, transform: &Transform2D<f32>, _clip: &[f32; 4]) {
        let transform: [f32; 9] = [
            transform.m11,
            transform.m21,
            transform.m31,
            transform.m12,
            transform.m22,
            transform.m32,
            0.0,
            0.0,
            1.0,
        ];
        match path {
            Path::Mold(path) => self.raster_builder.push_path(path.clone(), &transform),
            _ => {
                panic!("bad path");
            }
        }
    }
}

struct MoldStyling {
    styling: spinel_mold::Styling,
}

impl MoldStyling {
    fn new() -> Self {
        Self { styling: spinel_mold::Styling::new() }
    }
}

impl Styling for MoldStyling {
    fn seal(&mut self) {}

    fn unseal(&mut self) {}

    fn reset(&mut self) {
        self.styling.reset();
    }

    fn alloc_group(
        &mut self,
        range_lo: u32,
        range_hi: u32,
        background_color: &[f32; 4],
    ) -> GroupId {
        let group_id = self.styling.group_alloc();
        self.styling.group_range_lo(group_id, range_lo);
        self.styling.group_range_hi(group_id, range_hi);

        let cmds_enter = self.styling.group_enter(group_id, 1);
        unsafe {
            cmds_enter.write(spinel_mold::SPN_STYLING_OPCODE_COLOR_ACC_ZERO);
        }

        let mut cmds_leave = self.styling.group_leave(group_id, 4);
        let mut bytes = [0u8; 4];
        for i in 0..4 {
            bytes[i] = u8::try_from((background_color[i] * 255.0).round() as u32)
                .expect("RGBA colors must be between 0.0 and 1.0");
        }
        unsafe {
            cmds_leave.write(spinel_mold::SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND);
            cmds_leave = cmds_leave.add(1);
            cmds_leave.write(u32::from_be_bytes(bytes));
            cmds_leave = cmds_leave.add(1);
            cmds_leave.write(0);
            cmds_leave = cmds_leave.add(1);
            cmds_leave.write(spinel_mold::SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE);
        }

        GroupId::Mold(group_id)
    }

    fn group_layer(&mut self, group_id: &GroupId, layer_id: u32, color: &[f32; 4]) {
        let group_id = match group_id {
            GroupId::Mold(id) => *id,
            _ => panic!("invalid group id"),
        };
        let mut cmds = self.styling.layer(group_id, layer_id, 6);
        unsafe {
            cmds.write(spinel_mold::SPN_STYLING_OPCODE_COVER_WIP_ZERO);
            cmds = cmds.add(1);
            cmds.write(spinel_mold::SPN_STYLING_OPCODE_COVER_NONZERO);
            cmds = cmds.add(1);
        }
        let mut bytes = [0u8; 4];
        for i in 0..4 {
            bytes[i] = u8::try_from((color[i] * 255.0).round() as u32)
                .expect("RGBA colors must be between 0.0 and 1.0");
        }
        unsafe {
            cmds.write(spinel_mold::SPN_STYLING_OPCODE_COLOR_FILL_SOLID);
            cmds = cmds.add(1);
            cmds.write(u32::from_be_bytes(bytes));
            cmds = cmds.add(1);
            cmds.write(0);
            cmds = cmds.add(1);
            cmds.write(spinel_mold::SPN_STYLING_OPCODE_BLEND_OVER);
        }
    }
}

struct MoldComposition {
    composition: spinel_mold::Composition,
    tile_clip: [u32; 4],
}

impl MoldComposition {
    fn new() -> Self {
        Self { composition: spinel_mold::Composition::new(), tile_clip: [0; 4] }
    }
}

impl Composition for MoldComposition {
    fn seal(&mut self) {}

    fn unseal(&mut self) {}

    fn reset(&mut self) {
        self.composition.reset();
    }

    fn set_clip(&mut self, clip: &[u32; 4]) {
        self.tile_clip = *clip;
    }
    fn place(&mut self, raster: &Raster, layer_id: u32) {
        match raster {
            Raster::Mold(raster) => {
                self.composition.place(layer_id, raster.clone());
            }
            _ => {
                panic!("bad raster");
            }
        }
    }
}

#[derive(Clone)]
struct MoldColorBuffer {
    mapping: Arc<mapped_vmo::Mapping>,
    stride: usize,
}

impl mold::ColorBuffer for MoldColorBuffer {
    fn pixel_format(&self) -> mold::PixelFormat {
        mold::PixelFormat::BGRA8888
    }

    fn stride(&self) -> usize {
        self.stride
    }

    unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize) {
        self.mapping.write_at(offset, std::slice::from_raw_parts(src, len));
    }
}

struct MoldImage {
    vmo: zx::Vmo,
    size_bytes: u64,
    color_buffer: MoldColorBuffer,
    map: Option<mold::tile::Map>,
    old_prints: HashSet<u32>,
    new_prints: HashSet<u32>,
}

impl MoldImage {
    fn new(buffer_collection: &mut BufferCollectionSynchronousProxy, index: u32) -> Self {
        let (status, buffers) = buffer_collection
            .wait_for_buffers_allocated(zx::Time::after(10.second()))
            .expect("wait_for_buffers_allocated");
        assert_eq!(status, zx::sys::ZX_OK);

        let vmo_buffer = &buffers.buffers[index as usize];
        let vmo = vmo_buffer
            .vmo
            .as_ref()
            .expect("vmo_buffer")
            .duplicate_handle(zx::Rights::SAME_RIGHTS)
            .expect("duplicating buffer vmo");

        let size_bytes = buffers.settings.buffer_settings.size_bytes;
        let mapping = Arc::new(
            mapped_vmo::Mapping::create_from_vmo(
                &vmo,
                size_bytes as usize,
                zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::MAP_RANGE
                    | zx::VmarFlags::REQUIRE_NON_RESIZABLE,
            )
            .expect("Mapping::create_from_vmo"),
        );

        assert_eq!(buffers.settings.has_image_format_constraints, true);
        let bytes_per_row = buffers.settings.image_format_constraints.min_bytes_per_row;
        let divisor = buffers.settings.image_format_constraints.bytes_per_row_divisor;
        let bytes_per_row = ((bytes_per_row + divisor - 1) / divisor) * divisor;

        MoldImage {
            vmo,
            size_bytes: size_bytes as u64,
            color_buffer: MoldColorBuffer { mapping, stride: (bytes_per_row / 4) as usize },
            map: None,
            old_prints: HashSet::new(),
            new_prints: HashSet::new(),
        }
    }
}

pub struct MoldContext {
    buffer_collection: BufferCollectionSynchronousProxy,
    images: BTreeMap<u32, MoldImage>,
    compositions: BTreeMap<u32, MoldComposition>,
    styling: MoldStyling,
    path_builder: MoldPathBuilder,
    raster_builder: MoldRasterBuilder,
}

impl MoldContext {
    fn linear_image_format_constraints(width: u32, height: u32) -> ImageFormatConstraints {
        ImageFormatConstraints {
            pixel_format: SysmemPixelFormat {
                type_: PixelFormatType::Bgra32,
                has_format_modifier: true,
                format_modifier: FormatModifier { value: FORMAT_MODIFIER_LINEAR },
            },
            color_spaces_count: 1,
            color_space: [ColorSpace { type_: ColorSpaceType::Srgb }; 32],
            min_coded_width: width,
            max_coded_width: std::u32::MAX,
            min_coded_height: height,
            max_coded_height: std::u32::MAX,
            min_bytes_per_row: width * 4,
            max_bytes_per_row: std::u32::MAX,
            max_coded_width_times_coded_height: std::u32::MAX,
            layers: 1,
            coded_width_divisor: 1,
            coded_height_divisor: 1,
            bytes_per_row_divisor: 4,
            start_offset_divisor: 1,
            display_width_divisor: 1,
            display_height_divisor: 1,
            required_min_coded_width: 0,
            required_max_coded_width: 0,
            required_min_coded_height: 0,
            required_max_coded_height: 0,
            required_min_bytes_per_row: 0,
            required_max_bytes_per_row: 0,
        }
    }

    fn buffer_memory_constraints(width: u32, height: u32) -> BufferMemoryConstraints {
        BufferMemoryConstraints {
            min_size_bytes: width * height * 4,
            max_size_bytes: std::u32::MAX,
            physically_contiguous_required: false,
            secure_required: false,
            ram_domain_supported: true,
            cpu_domain_supported: true,
            inaccessible_domain_supported: false,
            heap_permitted_count: 1,
            heap_permitted: [HeapType::SystemRam; 32],
        }
    }

    fn buffer_collection_constraints(width: u32, height: u32) -> BufferCollectionConstraints {
        BufferCollectionConstraints {
            usage: BufferUsage {
                none: 0,
                cpu: CPU_USAGE_WRITE_OFTEN,
                vulkan: 0,
                display: 0,
                video: 0,
            },
            min_buffer_count_for_camping: 0,
            min_buffer_count_for_dedicated_slack: 0,
            min_buffer_count_for_shared_slack: 0,
            min_buffer_count: 1,
            max_buffer_count: std::u32::MAX,
            has_buffer_memory_constraints: true,
            buffer_memory_constraints: Self::buffer_memory_constraints(width, height),
            image_format_constraints_count: 1,
            image_format_constraints: [Self::linear_image_format_constraints(width, height); 32],
        }
    }

    pub fn new(
        token: ClientEnd<BufferCollectionTokenMarker>,
        config: &fuchsia_framebuffer::Config,
    ) -> Self {
        let sysmem = connect_to_service::<AllocatorMarker>().unwrap();
        let (collection_client, collection_request) = zx::Channel::create().unwrap();
        sysmem
            .bind_shared_collection(
                ClientEnd::new(token.into_channel()),
                ServerEnd::new(collection_request),
            )
            .unwrap();
        let mut buffer_collection = BufferCollectionSynchronousProxy::new(collection_client);
        let mut constraints = Self::buffer_collection_constraints(config.width, config.height);
        buffer_collection
            .set_constraints(true, &mut constraints)
            .expect("Sending buffer constraints to sysmem");

        Self {
            buffer_collection,
            images: BTreeMap::new(),
            compositions: BTreeMap::new(),
            styling: MoldStyling::new(),
            path_builder: MoldPathBuilder::new(),
            raster_builder: MoldRasterBuilder::new(),
        }
    }
}

impl Context for MoldContext {
    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        fuchsia_framebuffer::PixelFormat::Argb8888
    }

    fn styling(&mut self) -> &mut dyn Styling {
        &mut self.styling
    }

    fn path_builder(&mut self) -> &mut dyn PathBuilder {
        &mut self.path_builder
    }

    fn raster_builder(&mut self) -> &mut dyn RasterBuilder {
        &mut self.raster_builder
    }

    fn composition(&mut self, index: u32) -> &mut dyn Composition {
        self.compositions.entry(index).or_insert_with(|| MoldComposition::new())
    }

    fn render(&mut self, index: u32, _clear: bool, _clear_color: &[f32; 4]) {
        let buffer_collection = &mut self.buffer_collection;
        let image =
            self.images.entry(index).or_insert_with(|| MoldImage::new(buffer_collection, index));

        let composition = self.compositions.get_mut(&index).unwrap();
        let width = (composition.tile_clip[2] - composition.tile_clip[0]) as usize;
        let height = (composition.tile_clip[3] - composition.tile_clip[1]) as usize;
        let mut map = image
            .map
            .take()
            .filter(|map| map.width() == width || map.height() == height)
            .unwrap_or_else(|| mold::tile::Map::new(width, height));

        self.styling.styling.prints(&mut composition.composition, &mut map, &mut image.new_prints);

        for &id in image.old_prints.difference(&image.new_prints) {
            map.remove(id);
        }
        image.old_prints.clear();
        mem::swap(&mut image.old_prints, &mut image.new_prints);

        map.render(image.color_buffer.clone());

        image
            .vmo
            .op_range(zx::VmoOp::CACHE_CLEAN_INVALIDATE, 0, image.size_bytes)
            .expect("cache clean");

        image.map = Some(map);
    }
}
