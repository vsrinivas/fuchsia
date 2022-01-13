// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_ui_composition as fland, fidl_fuchsia_ui_pointer as fptr,
    fidl_fuchsia_ui_views as fviews, fuchsia_scenic::flatland,
};

// This matches fuchsia.ui.pointer.Point2; once RFC-0052 is implemented, we can just use that type.
pub type Point2 = [f32; 2];

// Internal event loop messages used by flatland-view-provider-example.  The example communicates
// asynchronously with a number of (mostly Scenic) protocols; InternalMessage helps to centralize
// the app's responses in a single handler, and thus greatly simplifies control flow.
pub enum InternalMessage {
    CreateView(fviews::ViewCreationToken, fviews::ViewIdentityOnCreation),
    OnPresentError {
        error: fland::FlatlandError,
    },
    OnNextFrameBegin {
        additional_present_credits: u32,
        future_presentation_infos: Vec<flatland::PresentationInfo>,
    },
    #[allow(dead_code)]
    OnFramePresented {
        frame_presented_info: fidl_fuchsia_scenic_scheduling::FramePresentedInfo,
    },
    Relayout {
        width: u32,
        height: u32,
    },
    FocusChanged {
        is_focused: bool,
    },
    TouchEvent {
        timestamp: i64,
        interaction: fptr::TouchInteractionId,
        phase: fptr::EventPhase,
        position_in_viewport: Point2,
    },
    MouseEvent {
        timestamp: i64,
        trace_flow_id: u64,
        position_in_viewport: Point2,
        scroll_v: Option<i64>,
        scroll_h: Option<i64>,
        pressed_buttons: Option<Vec<u8>>,
    },
}
