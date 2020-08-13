// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{fmt, os::raw};

use thiserror::Error;
use vk_sys as vk;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct HotsortVkTarget {
    _unused: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct HotsortVkTargetRequirements {
    pub ext_name_count: u32,
    pub ext_names: *mut *const raw::c_char,
    pub pdf: *mut vk::PhysicalDeviceFeatures,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkTarget {
    _unused: [u8; 0],
}

#[repr(C)]
#[allow(non_snake_case)]
pub struct PhysicalDeviceFeatures2 {
    pub sType: vk::StructureType,
    pub pNext: *mut raw::c_void,
    pub features: vk::PhysicalDeviceFeatures,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkTargetRequirements {
    pub qci_count: u32,
    pub qcis: *mut vk::DeviceQueueCreateInfo,
    pub ext_name_count: u32,
    pub ext_names: *mut *const raw::c_char,
    pub pdf2: *mut PhysicalDeviceFeatures2,
}

#[repr(C)]
pub struct SpnVkEnvironment {
    pub d: vk::Device,
    pub ac: *const vk::AllocationCallbacks,
    pub pc: vk::PipelineCache,
    pub pd: vk::PhysicalDevice,
    pub pdmp: vk::PhysicalDeviceMemoryProperties,
    pub qfi: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkContextCreateInfo {
    pub spinel: *const SpnVkTarget,
    pub hotsort: *mut HotsortVkTarget,
    pub block_pool_size: u64,
    pub handle_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum SpnVkRenderSubmitExtType {
    SpnVkRenderSubmitExtTypeImagePreBarrier,
    SpnVkRenderSubmitExtTypeImagePreClear,
    SpnVkRenderSubmitExtTypeImagePreProcess,
    SpnVkRenderSubmitExtTypeImageRender,
    SpnVkRenderSubmitExtTypeImagePostProcess,
    SpnVkRenderSubmitExtTypeImagePostCopyToBuffer,
    SpnVkRenderSubmitExtTypeImagePostCopyToImage,
    SpnVkRenderSubmitExtTypeImagePostBarrier,
}

pub type SpnVkRenderSubmitExtImageRenderPfn<T> =
    unsafe extern "C" fn(queue: vk::Queue, fence: vk::Fence, cb: vk::CommandBuffer, data: *const T);

#[repr(C)]
pub struct SpnVkRenderSubmitExtImageRender<T> {
    pub ext: *mut raw::c_void,
    pub type_: SpnVkRenderSubmitExtType,
    pub image: vk::Image,
    pub image_info: vk::DescriptorImageInfo,
    pub submitter_pfn: SpnVkRenderSubmitExtImageRenderPfn<T>,
    pub submitter_data: *const T,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkRenderSubmitExtImagePreBarrier {
    pub ext: *mut raw::c_void,
    pub type_: SpnVkRenderSubmitExtType,
    pub old_layout: vk::ImageLayout,
    pub src_qfi: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkRenderSubmitExtImagePreClear {
    pub ext: *mut raw::c_void,
    pub type_: SpnVkRenderSubmitExtType,
    pub color: *const vk::ClearColorValue,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkRenderSubmitExtImageProcess {
    pub ext: *mut raw::c_void,
    pub type_: SpnVkRenderSubmitExtType,
    pub access_mask: u32,
    pub pipeline: vk::Pipeline,
    pub pipeline_layout: vk::PipelineLayout,
    pub descriptor_set_count: u32,
    pub descriptor_sets: *const vk::DescriptorSet,
    pub push_offset: u32,
    pub push_size: u32,
    pub push_values: *const raw::c_void,
    pub group_count_x: u32,
    pub group_count_y: u32,
    pub group_count_z: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkRenderSubmitExtImagePostCopyToBuffer {
    pub ext: *mut raw::c_void,
    pub type_: SpnVkRenderSubmitExtType,
    pub dst: vk::Buffer,
    pub region_count: u32,
    pub regions: *const vk::BufferImageCopy,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkRenderSubmitExtImagePostCopyToImage {
    pub ext: *mut raw::c_void,
    pub type_: SpnVkRenderSubmitExtType,
    pub dst: vk::Image,
    pub dst_layout: vk::ImageLayout,
    pub region_count: u32,
    pub regions: *const vk::ImageCopy,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnVkRenderSubmitExtImagePostBarrier {
    pub ext: *mut raw::c_void,
    pub type_: SpnVkRenderSubmitExtType,
    pub new_layout: vk::ImageLayout,
    pub dst_qfi: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum SpnVkStatusExtType {
    SpnVkStatusExtTypeBlockPool,
}

#[repr(C)]
pub struct SpnVkStatusExtBlockPool {
    pub ext: *mut raw::c_void,
    pub type_: SpnVkStatusExtType,
    pub avail: u64,
    pub inuse: u64,
}

macro_rules! spinel_errors {
    ( @Error, $success:ident, $( $errors:ident ),* ) => {
        #[derive(Clone, Debug, Eq, Error, Hash, PartialEq)]
        pub enum SpnError {
            $($errors),*
        }

        impl fmt::Display for SpnError {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{:?}", self)
            }
        }

        impl SpnResult {
            pub fn res(&self) -> Result<(), SpnError> {
                match self {
                    SpnResult::$success => Ok(()),
                    $(
                        SpnResult::$errors => Err(SpnError::$errors)
                    ),*
                }
            }

            pub fn success(&self) {
                match self {
                    SpnResult::$success => (),
                    e => panic!("Unexpected error: {:?}\n\
                                 \n\
                                 This is a bug. Please file an issue in Spinel.", e),
                }
            }
        }
    };

    ( @Result, $( $variants:ident ),* ) => {
        #[repr(C)]
        #[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
        pub enum SpnResult {
            $($variants),*
        }
    };

    ( $( $variants:ident ),* $( , )? ) => {
        spinel_errors!(@Result, $($variants),*);
        spinel_errors!(@Error, $($variants),*);
    };
}

spinel_errors! {
    SpnSuccess,
    SpnErrorPartialTargetRequirements,
    SpnTimeout,
    SpnErrorNotImplemented,
    SpnErrorContextLost,
    SpnErrorPathBuilderLost,
    SpnErrorRasterBuilderLost,
    SpnErrorRasterBuilderSealed,
    SpnErrorRasterBuilderTooManyPaths,
    SpnErrorRenderExtensionInvalid,
    SpnErrorLayerIdInvalid,
    SpnErrorLayerNotEmpty,
    SpnErrorPoolEmpty,
    SpnErrorCondvarWait,
    SpnErrorTransformWeakrefInvalid,
    SpnErrorStrokeStyleWeakrefInvalid,
    SpnErrorCommandNotReady,
    SpnErrorCommandNotCompleted,
    SpnErrorCommandNoStarted,
    SpnErrorCommandNotReadyOrCompleted,
    SpnErrorCompositionSealed,
    SpnErrorCompositionTooManyRasters,
    SpnErrorStylingSealed,
    SpnErrorHandleInvalid,
    SpnErrorHandleOverflow,
}

macro_rules! spinel_type {
    ( $name:ident ) => {
        #[repr(C)]
        #[derive(Clone, Copy, Debug)]
        pub struct $name {
            _unused: usize,
        }
    };

    ( $name:ident, $size:ty ) => {
        #[repr(C)]
        #[derive(Clone, Copy, Debug, Eq, PartialEq)]
        pub struct $name {
            _unused: $size,
        }
    };
}

spinel_type!(SpnContext);
spinel_type!(SpnPathBuilder);
spinel_type!(SpnRasterBuilder);
spinel_type!(SpnComposition);
spinel_type!(SpnStyling);
spinel_type!(SpnSurface);

spinel_type!(SpnPath, u32);
spinel_type!(SpnRaster, u32);
spinel_type!(SpnGroupId, u32);

spinel_type!(SpnTransformWeakref, [u32; 2]);
spinel_type!(SpnClipWeakref, [u32; 2]);

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnTransform {
    pub sx: f32,
    pub shx: f32,
    pub tx: f32,
    pub shy: f32,
    pub sy: f32,
    pub ty: f32,
    pub w0: f32,
    pub w1: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnClip {
    pub x0: f32,
    pub y0: f32,
    pub x1: f32,
    pub y1: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnTxty {
    pub tx: i32,
    pub ty: i32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum SpnCommand {
    SpnStylingOpcodeNoop,
    SpnStylingOpcodeCoverNonzero,
    SpnStylingOpcodeCoverEvenodd,
    SpnStylingOpcodeCoverAccumulate,
    SpnStylingOpcodeCoverMask,
    SpnStylingOpcodeCoverWipZero,
    SpnStylingOpcodeCoverAccZero,
    SpnStylingOpcodeCoverMaskZero,
    SpnStylingOpcodeCoverMaskOne,
    SpnStylingOpcodeCoverMaskInvert,
    SpnStylingOpcodeColorFillSolid,
    SpnStylingOpcodeColorFillGradientLinear,
    SpnStylingOpcodeColorWipZero,
    SpnStylingOpcodeColorAccZero,
    SpnStylingOpcodeBlendOver,
    SpnStylingOpcodeBlendPlus,
    SpnStylingOpcodeBlendMultiply,
    SpnStylingOpcodeBlendKnockout,
    SpnStylingOpcodeCoverWipMoveToMask,
    SpnStylingOpcodeCoverAccMoveToMask,
    SpnStylingOpcodeColorAccOverBackground,
    SpnStylingOpcodeColorAccStoreToSurface,
    SpnStylingOpcodeColorAccTestOpacity,
    SpnStylingOpcodeColorIllZero,
    SpnStylingOpcodeColorAccMultiplyIll,
    SpnStylingOpcodeCount,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnRenderSubmit {
    pub ext: *mut raw::c_void,
    pub styling: SpnStyling,
    pub composition: SpnComposition,
    pub clip: [u32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SpnStatus {
    pub ext: *mut raw::c_void,
}

extern "C" {
    pub fn spn_vk_find_target(
        vendor_id: u32,
        device_id: u32,
        spinel_target: *mut *const SpnVkTarget,
        hotsort_target: *mut *mut HotsortVkTarget,
        error_buffer: *mut raw::c_char,
        error_buffer_size: usize,
    ) -> bool;

    pub fn hotsort_vk_target_get_requirements(
        target: *const HotsortVkTarget,
        requirements: *mut HotsortVkTargetRequirements,
    ) -> bool;

    pub fn spn_vk_target_get_feature_structures(
        target: *const SpnVkTarget,
        structures_size: *const usize,
        structures: *mut raw::c_void,
    ) -> SpnResult;

    pub fn spn_vk_target_get_requirements(
        target: *const SpnVkTarget,
        requirements: *mut SpnVkTargetRequirements,
    ) -> SpnResult;

    pub fn spn_vk_context_create(
        environment: *mut SpnVkEnvironment,
        create_info: *const SpnVkContextCreateInfo,
        context: *mut SpnContext,
    ) -> SpnResult;
    pub fn spn_vk_context_wait(
        context: SpnContext,
        imports_count: u32,
        imports: *mut vk::Fence,
        wait_all: bool,
        timeout_ns: u64,
        executing_count: *mut u32,
    ) -> SpnResult;

    pub fn spn_context_retain(context: SpnContext) -> SpnResult;
    pub fn spn_context_release(context: SpnContext) -> SpnResult;
    pub fn spn_context_reset(context: SpnContext) -> SpnResult;

    pub fn spn_context_status(context: SpnContext, status: *mut SpnStatus) -> SpnResult;

    pub fn spn_path_builder_create(
        context: SpnContext,
        path_builder: *mut SpnPathBuilder,
    ) -> SpnResult;
    pub fn spn_path_builder_retain(path_builder: SpnPathBuilder) -> SpnResult;
    pub fn spn_path_builder_release(path_builder: SpnPathBuilder) -> SpnResult;
    pub fn spn_path_builder_flush(path_builder: SpnPathBuilder) -> SpnResult;

    pub fn spn_path_builder_begin(path_builder: SpnPathBuilder) -> SpnResult;
    pub fn spn_path_builder_end(path_builder: SpnPathBuilder, path: *mut SpnPath) -> SpnResult;
    pub fn spn_path_builder_move_to(path_builder: SpnPathBuilder, x0: f32, y0: f32) -> SpnResult;
    pub fn spn_path_builder_line_to(path_builder: SpnPathBuilder, x1: f32, y1: f32) -> SpnResult;
    pub fn spn_path_builder_cubic_to(
        path_builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
    ) -> SpnResult;
    pub fn spn_path_builder_cubic_smooth_to(
        path_builder: SpnPathBuilder,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
    ) -> SpnResult;
    pub fn spn_path_builder_quad_to(
        path_builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
    ) -> SpnResult;
    pub fn spn_path_builder_quad_smooth_to(
        path_builder: SpnPathBuilder,
        x2: f32,
        y2: f32,
    ) -> SpnResult;
    pub fn spn_path_builder_rat_quad_to(
        path_builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        w1: f32,
    ) -> SpnResult;
    pub fn spn_path_builder_rat_cubic_to(
        path_builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
        w1: f32,
        w2: f32,
    ) -> SpnResult;
    pub fn spn_path_builder_ellipse(
        path_builder: SpnPathBuilder,
        cx: f32,
        cy: f32,
        rx: f32,
        ry: f32,
    ) -> SpnResult;

    pub fn spn_path_retain(context: SpnContext, paths: *const SpnPath, count: u32) -> SpnResult;
    pub fn spn_path_release(context: SpnContext, paths: *const SpnPath, count: u32) -> SpnResult;

    pub fn spn_raster_builder_create(
        context: SpnContext,
        raster_builder: *mut SpnRasterBuilder,
    ) -> SpnResult;
    pub fn spn_raster_builder_retain(raster_builder: SpnRasterBuilder) -> SpnResult;
    pub fn spn_raster_builder_release(raster_builder: SpnRasterBuilder) -> SpnResult;
    pub fn spn_raster_builder_flush(raster_builder: SpnRasterBuilder) -> SpnResult;

    pub fn spn_raster_builder_begin(raster_builder: SpnRasterBuilder) -> SpnResult;
    pub fn spn_raster_builder_end(
        raster_builder: SpnRasterBuilder,
        raster: *mut SpnRaster,
    ) -> SpnResult;
    pub fn spn_raster_builder_add(
        raster_builder: SpnRasterBuilder,
        paths: *const SpnPath,
        transform_weakrefs: *mut SpnTransformWeakref,
        transforms: *const SpnTransform,
        clip_weakrefs: *mut SpnClipWeakref,
        clips: *const SpnClip,
        count: u32,
    ) -> SpnResult;

    pub fn spn_raster_retain(
        context: SpnContext,
        rasters: *const SpnRaster,
        count: u32,
    ) -> SpnResult;
    pub fn spn_raster_release(
        context: SpnContext,
        rasters: *const SpnRaster,
        count: u32,
    ) -> SpnResult;

    pub fn spn_composition_create(
        context: SpnContext,
        composition: *mut SpnComposition,
    ) -> SpnResult;
    pub fn spn_composition_clone(
        context: SpnContext,
        composition: SpnComposition,
        clone: *mut SpnComposition,
    ) -> SpnResult;
    pub fn spn_composition_retain(composition: SpnComposition) -> SpnResult;
    pub fn spn_composition_release(composition: SpnComposition) -> SpnResult;

    pub fn spn_composition_place(
        composition: SpnComposition,
        rasters: *const SpnRaster,
        layer_ids: *const u32,
        txtys: *const SpnTxty,
        count: u32,
    ) -> SpnResult;
    pub fn spn_composition_seal(composition: SpnComposition) -> SpnResult;
    pub fn spn_composition_unseal(composition: SpnComposition) -> SpnResult;
    pub fn spn_composition_reset(composition: SpnComposition) -> SpnResult;
    pub fn spn_composition_get_bounds(composition: SpnComposition, bounds: *mut u32) -> SpnResult;
    pub fn spn_composition_set_clip(composition: SpnComposition, clip: *const u32) -> SpnResult;

    pub fn spn_styling_create(
        context: SpnContext,
        styling: *mut SpnStyling,
        layers_count: u32,
        cmds_count: u32,
    ) -> SpnResult;
    pub fn spn_styling_retain(styling: SpnStyling) -> SpnResult;
    pub fn spn_styling_release(styling: SpnStyling) -> SpnResult;
    pub fn spn_styling_seal(styling: SpnStyling) -> SpnResult;
    pub fn spn_styling_unseal(styling: SpnStyling) -> SpnResult;
    pub fn spn_styling_reset(styling: SpnStyling) -> SpnResult;

    pub fn spn_styling_group_alloc(styling: SpnStyling, group_id: *mut SpnGroupId) -> SpnResult;
    pub fn spn_styling_group_enter(
        styling: SpnStyling,
        group_id: SpnGroupId,
        n: u32,
        cmds: *mut *mut SpnCommand,
    ) -> SpnResult;
    pub fn spn_styling_group_leave(
        styling: SpnStyling,
        group_id: SpnGroupId,
        n: u32,
        cmds: *mut *mut SpnCommand,
    ) -> SpnResult;

    pub fn spn_styling_group_parents(
        styling: SpnStyling,
        group_id: SpnGroupId,
        n: u32,
        parents: *mut *mut SpnCommand,
    ) -> SpnResult;
    pub fn spn_styling_group_range_lo(
        styling: SpnStyling,
        group_id: SpnGroupId,
        layer_lo: u32,
    ) -> SpnResult;
    pub fn spn_styling_group_range_hi(
        styling: SpnStyling,
        group_id: SpnGroupId,
        layer_hi: u32,
    ) -> SpnResult;
    pub fn spn_styling_group_layer(
        styling: SpnStyling,
        group_id: SpnGroupId,
        layer_id: u32,
        n: u32,
        cmds: *mut *mut SpnCommand,
    ) -> SpnResult;

    pub fn spn_styling_layer_fill_rgba_encoder(cmds: *mut SpnCommand, rgba: *const f32);
    pub fn spn_styling_background_over_encoder(cmds: *mut SpnCommand, rgba: *const f32);

    pub fn spn_render(context: SpnContext, submit: *const SpnRenderSubmit) -> SpnResult;
}
