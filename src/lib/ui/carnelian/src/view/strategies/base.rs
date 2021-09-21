// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::{
    app::strategies::framebuffer::ControllerProxyPtr,
    geometry::Size,
    input,
    message::Message,
    view::{ViewAssistantPtr, ViewDetails},
    ViewKey,
};
use async_trait::async_trait;
use fidl_fuchsia_ui_views::{ViewRef, ViewRefControl, ViewToken};

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

    fn handle_scenic_key_event(
        &mut self,
        _view_details: &ViewDetails,
        _view_assistant: &mut ViewAssistantPtr,
        _: &fidl_fuchsia_ui_input3::KeyEvent,
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

    fn render_requested(&mut self) {}

    fn ownership_changed(&mut self, _owned: bool) {}
    fn drop_display_resources(&mut self) {}

    fn handle_on_next_frame_begin(
        &mut self,
        _info: &fidl_fuchsia_ui_composition::OnNextFrameBeginValues,
    ) {
    }

    async fn handle_display_controller_event(
        &mut self,
        _event: fidl_fuchsia_hardware_display::ControllerEvent,
    ) {
    }

    fn close(&mut self) {}
}

pub(crate) type ViewStrategyPtr = Box<dyn ViewStrategy>;

#[derive(Debug)]
pub(crate) struct DisplayDirectParams {
    pub controller: ControllerProxyPtr,
    pub display_id: u64,
    pub info: fidl_fuchsia_hardware_display::Info,
}

#[derive(Debug)]
pub(crate) struct ScenicParams {
    pub view_token: ViewToken,
    pub control_ref: ViewRefControl,
    pub view_ref: ViewRef,
}

#[derive(Debug)]
pub(crate) struct FlatlandParams {
    pub args: fidl_fuchsia_ui_app::CreateView2Args,
}

#[derive(Debug)]
pub(crate) enum ViewStrategyParams {
    Scenic(ScenicParams),
    Flatland(FlatlandParams),
    DisplayDirect(DisplayDirectParams),
}

impl ViewStrategyParams {
    pub fn view_key(&self) -> Option<ViewKey> {
        match self {
            ViewStrategyParams::DisplayDirect(params) => Some(params.display_id),
            _ => None,
        }
    }
}
