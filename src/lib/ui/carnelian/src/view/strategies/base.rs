// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::FrameBufferPtr,
    geometry::{IntSize, Size},
    input,
    message::Message,
    view::{ViewAssistantPtr, ViewDetails},
};
use async_trait::async_trait;
use fidl_fuchsia_ui_views::{ViewRef, ViewRefControl, ViewToken};
use fuchsia_framebuffer::PixelFormat;
use fuchsia_zircon::{Duration, Time};

#[async_trait(?Send)]
pub(crate) trait ViewStrategy {
    fn initial_metrics(&self) -> Size {
        Size::zero()
    }

    fn initial_physical_size(&self) -> Size {
        Size::zero()
    }

    fn initial_logical_size(&self) -> Size {
        Size::zero()
    }

    fn setup(&mut self, _view_details: &ViewDetails, _view_assistant: &mut ViewAssistantPtr);
    async fn render(
        &mut self,
        view_details: &ViewDetails,
        view_assistant: &mut ViewAssistantPtr,
    ) -> bool;
    fn present(&mut self, view_details: &ViewDetails);
    fn present_done(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _info: fidl_fuchsia_scenic_scheduling::FramePresentedInfo,
    ) {
    }
    fn present_submitted(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _info: fidl_fuchsia_scenic_scheduling::FuturePresentationTimes,
    ) {
    }
    fn handle_scenic_input_event(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _: &fidl_fuchsia_ui_input::InputEvent,
    ) -> Vec<Message>;

    fn handle_input_event(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _: &input::Event,
    ) -> Vec<Message> {
        Vec::new()
    }

    fn handle_focus(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _: bool,
    );

    fn image_freed(&mut self, _image_id: u64, _collection_id: u32) {}

    fn handle_vsync_parameters_changed(&mut self, _phase: Time, _interval: Duration) {}

    fn handle_vsync_cookie(&mut self, _cookie: u64) {}

    fn render_requested(&mut self) {}
}

pub(crate) type ViewStrategyPtr = Box<dyn ViewStrategy>;

pub(crate) struct FrameBufferParams {
    pub size: IntSize,
    pub frame_buffer: FrameBufferPtr,
    pub pixel_format: PixelFormat,
}

pub(crate) struct ScenicParams {
    pub view_token: ViewToken,
    pub control_ref: ViewRefControl,
    pub view_ref: ViewRef,
}

pub(crate) enum ViewStrategyParams {
    Scenic(ScenicParams),
    FrameBuffer(FrameBufferParams),
}
