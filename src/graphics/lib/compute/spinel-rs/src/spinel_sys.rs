// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use std::fmt;

use thiserror::Error;

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
                                 This is a bug. Please file an issue in SPN.", e)
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
    SpnErrorNotImplemented,
    SpnErrorContextLost,
    SpnErrorPathBuilderLost,
    SpnErrorRasterBuilderLost,
    SpnErrorRasterBuilderSealed,
    SpnErrorRasterBuilderTooManyRasters,
    SpnErrorRenderExtensionInvalid,
    SpnErrorRenderExtensionVkSubmitInfoWaitCountExceeded,
    SpnErrorLayerIdInvalid,
    SpnErrorLayerNotEmpty,
    SpnErrorPoolEmpty,
    SpnErrorCondvarWait,
    SpnErrorTransformWeakrefInvalid,
    SpnErrorStrokeStyleWeakrefInvalid,
    SpnErrorCommandNotReady,
    SpnErrorCommandNotCompleted,
    SpnErrorCommandNotStarted,
    SpnErrorCommandNotReadyOrCompleted,
    SpnErrorCompositionSealed,
    SpnErrorStylingSealed,
    SpnErrorHandleInvalid,
    SpnErrorHandleOverflow,
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(C)]
pub struct SpnContext {
    private: usize,
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(C)]
pub struct SpnPathBuilder {
    private: usize,
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(C)]
pub struct SpnPath {
    private: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(C)]
pub struct SpnRasterBuilder {
    private: usize,
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(C)]
pub struct SpnRaster {
    private: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(C)]
pub struct SpnStyling {
    private: usize,
}

#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
#[repr(C)]
pub struct SpnGroupId {
    private: u32,
}

#[derive(Debug)]
#[repr(C)]
pub struct SpnRenderSubmit {
    pub ext: *const u8,
    pub spn_styling: SpnStyling,
    pub spn_composition: SpnStyling,
    pub tile_clip: [u32; 4],
}

#[cfg(not(feature = "spinel-null"))]
#[link(name = "spinel_null", kind = "static")]
extern "C" {
    pub fn spn_context_retain(context: SpnContext) -> SpnResult;
    pub fn spn_context_release(context: SpnContext) -> SpnResult;
    pub fn spn_context_reset(context: SpnContext) -> SpnResult;
    pub fn spn_context_yield(context: SpnContext) -> SpnResult;
    pub fn spn_context_wait(context: SpnContext) -> SpnResult;

    pub fn spn_path_builder_create(context: SpnContext, builder: *mut SpnPathBuilder) -> SpnResult;
    pub fn spn_path_builder_retain(builder: SpnPathBuilder) -> SpnResult;
    pub fn spn_path_builder_release(builder: SpnPathBuilder) -> SpnResult;
    pub fn spn_path_builder_flush(builder: SpnPathBuilder) -> SpnResult;

    pub fn spn_path_begin(builder: SpnPathBuilder) -> SpnResult;
    pub fn spn_path_end(builder: SpnPathBuilder, path: *mut SpnPath) -> SpnResult;
    pub fn spn_path_retain(context: SpnContext, paths: *const SpnPath, count: u32) -> SpnResult;
    pub fn spn_path_release(context: SpnContext, paths: *const SpnPath, count: u32) -> SpnResult;

    pub fn spn_path_move_to(builder: SpnPathBuilder, x: f32, y: f32) -> SpnResult;
    pub fn spn_path_line_to(builder: SpnPathBuilder, x: f32, y: f32) -> SpnResult;
    pub fn spn_path_quad_to(
        builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
    ) -> SpnResult;
    pub fn spn_path_quad_smooth_to(builder: SpnPathBuilder, x2: f32, y2: f32) -> SpnResult;
    pub fn spn_path_cubic_to(
        builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
    ) -> SpnResult;
    pub fn spn_path_cubic_smooth_to(
        builder: SpnPathBuilder,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
    ) -> SpnResult;
    pub fn spn_path_rat_quad_to(
        builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        w0: f32,
    ) -> SpnResult;
    pub fn spn_path_rat_cubic_to(
        builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
        w0: f32,
        w1: f32,
    ) -> SpnResult;

    pub fn spn_path_ellipse(
        builder: SpnPathBuilder,
        cx: f32,
        cy: f32,
        rx: f32,
        ry: f32,
    ) -> SpnResult;

    pub fn spn_raster_builder_create(
        context: SpnContext,
        builder: *mut SpnRasterBuilder,
    ) -> SpnResult;
    pub fn spn_raster_builder_retain(builder: SpnRasterBuilder) -> SpnResult;
    pub fn spn_raster_builder_release(builder: SpnRasterBuilder) -> SpnResult;
    pub fn spn_raster_builder_flush(builder: SpnRasterBuilder) -> SpnResult;

    pub fn spn_raster_begin(builder: SpnRasterBuilder) -> SpnResult;
    pub fn spn_raster_end(builder: SpnRasterBuilder, raster: *mut SpnRaster) -> SpnResult;
    pub fn spn_raster_release(
        context: SpnContext,
        rasters: *const SpnRaster,
        count: u32,
    ) -> SpnResult;
    pub fn spn_raster_retain(
        context: SpnContext,
        rasters: *const SpnRaster,
        count: u32,
    ) -> SpnResult;

    pub fn spn_raster_fill(
        raster_builder: SpnRasterBuilder,
        paths: *const SpnPath,
        _transform_weakrefs: *const u8,
        transforms: *const *const f32,
        _clip_weakrefs: *const u8,
        clips: *const *const f32,
        count: u32,
    ) -> SpnResult;

    pub fn spn_composition_create(context: SpnContext, composition: *mut SpnStyling) -> SpnResult;
    pub fn spn_composition_clone(
        context: SpnContext,
        composition: SpnStyling,
        clone: *mut SpnStyling,
    ) -> SpnResult;
    pub fn spn_composition_retain(composition: SpnStyling) -> SpnResult;
    pub fn spn_composition_release(composition: SpnStyling) -> SpnResult;

    pub fn spn_composition_place(
        composition: SpnStyling,
        rasters: *const SpnRaster,
        layer_ids: *const u32,
        txtys: *const [i32; 2],
        count: u32,
    ) -> SpnResult;
    pub fn spn_composition_seal(composition: SpnStyling) -> SpnResult;
    pub fn spn_composition_unseal(composition: SpnStyling) -> SpnResult;
    pub fn spn_composition_reset(composition: SpnStyling) -> SpnResult;
    pub fn spn_composition_get_bounds(composition: SpnStyling, bounds: *const [i32; 4]);

    pub fn spn_styling_create(
        context: SpnContext,
        styling: *mut SpnStyling,
        dwords_count: u32,
        layers_count: u32,
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
        cmds: *mut *mut u32,
    ) -> SpnResult;
    pub fn spn_styling_group_leave(
        styling: SpnStyling,
        group_id: SpnGroupId,
        n: u32,
        cmds: *mut *mut u32,
    ) -> SpnResult;

    pub fn spn_styling_group_parents(
        styling: SpnStyling,
        group_id: SpnGroupId,
        n: u32,
        parents: *mut *mut SpnGroupId,
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
        cmds: *mut *mut u32,
    ) -> SpnResult;

    pub fn spn_styling_layer_fill_rgba_encoder(cmds: *mut u32, rgba: *const [f32; 4]);
    pub fn spn_styling_background_over_encoder(cmds: *mut u32, rgba: *const [f32; 4]);

    pub fn spn_render(context: SpnContext, spn_render_submit: *const SpnRenderSubmit) -> SpnResult;
}

// TODO: For now, Spinel is replaced with a null implementation that always returns SPN_SUCCESS.
//       When Spinel is up-and-running, this test case should be removed.
#[cfg(feature = "spinel-null")]
mod spinel_null {
    #![allow(unused_variables)]

    use super::*;

    pub unsafe extern "C" fn spn_context_retain(context: SpnContext) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_context_release(context: SpnContext) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_context_reset(context: SpnContext) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_context_yield(context: SpnContext) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_context_wait(context: SpnContext) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_path_builder_create(
        context: SpnContext,
        builder: *mut SpnPathBuilder,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_builder_retain(builder: SpnPathBuilder) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_builder_release(builder: SpnPathBuilder) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_builder_flush(builder: SpnPathBuilder) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_path_begin(builder: SpnPathBuilder) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_end(
        builder: SpnPathBuilder,
        path: *mut SpnPath,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_retain(
        context: SpnContext,
        paths: *const SpnPath,
        count: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_release(
        context: SpnContext,
        paths: *const SpnPath,
        count: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_path_move_to(
        builder: SpnPathBuilder,
        x: f32,
        y: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_line_to(
        builder: SpnPathBuilder,
        x: f32,
        y: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_quad_to(
        builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_quad_smooth_to(
        builder: SpnPathBuilder,
        x2: f32,
        y2: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_cubic_to(
        builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_cubic_smooth_to(
        builder: SpnPathBuilder,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_rat_quad_to(
        builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        w0: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_path_rat_cubic_to(
        builder: SpnPathBuilder,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
        w0: f32,
        w1: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_path_ellipse(
        builder: SpnPathBuilder,
        cx: f32,
        cy: f32,
        rx: f32,
        ry: f32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_raster_builder_create(
        context: SpnContext,
        builder: *mut SpnRasterBuilder,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_raster_builder_retain(builder: SpnRasterBuilder) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_raster_builder_release(builder: SpnRasterBuilder) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_raster_builder_flush(builder: SpnRasterBuilder) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_raster_begin(builder: SpnRasterBuilder) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_raster_end(
        builder: SpnRasterBuilder,
        raster: *mut SpnRaster,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_raster_release(
        context: SpnContext,
        rasters: *const SpnRaster,
        count: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_raster_retain(
        context: SpnContext,
        rasters: *const SpnRaster,
        count: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_raster_fill(
        raster_builder: SpnRasterBuilder,
        paths: *const SpnPath,
        _transform_weakrefs: *const u8,
        transforms: *const *const f32,
        _clip_weakrefs: *const u8,
        clips: *const *const f32,
        count: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_composition_create(
        context: SpnContext,
        composition: *mut SpnStyling,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_composition_clone(
        context: SpnContext,
        composition: SpnStyling,
        clone: *mut SpnStyling,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_composition_retain(composition: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_composition_release(composition: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_composition_place(
        composition: SpnStyling,
        rasters: *const SpnRaster,
        layer_ids: *const u32,
        txtys: *const [i32; 2],
        count: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_composition_seal(composition: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_composition_unseal(composition: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_composition_reset(composition: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_composition_get_bounds(
        composition: SpnStyling,
        bounds: *const [i32; 4],
    ) {
    }

    pub unsafe extern "C" fn spn_styling_create(
        context: SpnContext,
        styling: *mut SpnStyling,
        dwords_count: u32,
        layers_count: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_retain(styling: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_release(styling: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_seal(styling: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_unseal(styling: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_reset(styling: SpnStyling) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_styling_group_alloc(
        styling: SpnStyling,
        group_id: *mut SpnGroupId,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_group_enter(
        styling: SpnStyling,
        group_id: SpnGroupId,
        n: u32,
        cmds: *mut *mut u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_group_leave(
        styling: SpnStyling,
        group_id: SpnGroupId,
        n: u32,
        cmds: *mut *mut u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_styling_group_parents(
        styling: SpnStyling,
        group_id: SpnGroupId,
        n: u32,
        parents: *mut *mut SpnGroupId,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_group_range_lo(
        styling: SpnStyling,
        group_id: SpnGroupId,
        layer_lo: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_group_range_hi(
        styling: SpnStyling,
        group_id: SpnGroupId,
        layer_hi: u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
    pub unsafe extern "C" fn spn_styling_group_layer(
        styling: SpnStyling,
        group_id: SpnGroupId,
        layer_id: u32,
        n: u32,
        cmds: *mut *mut u32,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }

    pub unsafe extern "C" fn spn_styling_layer_fill_rgba_encoder(
        cmds: *mut u32,
        rgba: *const [f32; 4],
    ) {
    }
    pub unsafe extern "C" fn spn_styling_background_over_encoder(
        cmds: *mut u32,
        rgba: *const [f32; 4],
    ) {
    }

    pub unsafe extern "C" fn spn_render(
        context: SpnContext,
        spn_render_submit: *const SpnRenderSubmit,
    ) -> SpnResult {
        SpnResult::SpnSuccess
    }
}

#[cfg(feature = "spinel-null")]
pub use spinel_null::*;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn spinel_null() {
        let context: SpnContext = SpnContext::default();

        unsafe {
            spn_context_release(context).res().unwrap();
        }
    }
}
