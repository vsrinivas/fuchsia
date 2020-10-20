// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{
        strategies::base::AppStrategy, FrameBufferPtr, InternalSender, MessageInternal,
        RenderOptions,
    },
    drawing::DisplayRotation,
    geometry::IntSize,
    input::{self, listen_for_user_input, DeviceId, InputReportHandler},
    view::{
        strategies::{
            base::{ViewStrategyParams, ViewStrategyPtr},
            framebuffer::FrameBufferViewStrategy,
        },
        ViewKey,
    },
};
use anyhow::{bail, Error};
use async_trait::async_trait;
use fidl_fuchsia_input_report as hid_input_report;
use fidl_fuchsia_ui_scenic::ScenicProxy;
use fuchsia_async::{self as fasync};
use futures::{channel::mpsc::UnboundedSender, TryFutureExt};
use std::collections::HashMap;

pub(crate) struct FrameBufferAppStrategy {
    pub frame_buffer: FrameBufferPtr,
    pub display_rotation: DisplayRotation,
    pub view_key: ViewKey,
    pub input_report_handlers: HashMap<DeviceId, InputReportHandler>,
}

#[async_trait(?Send)]
impl AppStrategy for FrameBufferAppStrategy {
    async fn create_view_strategy(
        &self,
        key: ViewKey,
        render_options: RenderOptions,
        app_sender: UnboundedSender<MessageInternal>,
        strategy_params: ViewStrategyParams,
    ) -> Result<ViewStrategyPtr, Error> {
        let strategy_params = match strategy_params {
            ViewStrategyParams::FrameBuffer(params) => params,
            _ => bail!(
                "Incorrect ViewStrategyParams passed to create_view_strategy for frame buffer"
            ),
        };
        let strat_ptr = FrameBufferViewStrategy::new(
            key,
            render_options,
            &strategy_params.size,
            strategy_params.pixel_format,
            app_sender.clone(),
            strategy_params.frame_buffer.clone(),
        )
        .await?;
        Ok(strat_ptr)
    }

    fn supports_scenic(&self) -> bool {
        return false;
    }

    fn get_scenic_proxy(&self) -> Option<&ScenicProxy> {
        return None;
    }

    fn get_frame_buffer_size(&self) -> Option<IntSize> {
        let config = self.frame_buffer.borrow().get_config();
        Some(IntSize::new(config.width as i32, config.height as i32))
    }

    fn get_pixel_size(&self) -> u32 {
        let config = self.frame_buffer.borrow().get_config();
        config.pixel_size_bytes
    }

    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat {
        let config = self.frame_buffer.borrow().get_config();
        config.format
    }

    fn get_linear_stride_bytes(&self) -> u32 {
        let config = self.frame_buffer.borrow().get_config();
        config.linear_stride_bytes() as u32
    }

    fn get_frame_buffer(&self) -> Option<FrameBufferPtr> {
        Some(self.frame_buffer.clone())
    }

    async fn post_setup(
        &mut self,
        _pixel_format: fuchsia_framebuffer::PixelFormat,
        internal_sender: &InternalSender,
    ) -> Result<(), Error> {
        let view_key = self.view_key;
        let input_report_sender = internal_sender.clone();
        fasync::Task::local(
            listen_for_user_input(view_key, input_report_sender)
                .unwrap_or_else(|e: anyhow::Error| eprintln!("error: listening for input {:?}", e)),
        )
        .detach();
        Ok(())
    }

    fn handle_input_report(
        &mut self,
        device_id: &input::DeviceId,
        input_report: &hid_input_report::InputReport,
    ) -> Vec<input::Event> {
        let handler = self.input_report_handlers.get_mut(device_id).expect("input_report_handler");
        handler.handle_input_report(device_id, input_report)
    }

    fn handle_register_input_device(
        &mut self,
        device_id: &input::DeviceId,
        device_descriptor: &hid_input_report::DeviceDescriptor,
    ) {
        let frame_buffer_size = self.get_frame_buffer_size().expect("frame_buffer_size");
        self.input_report_handlers.insert(
            device_id.clone(),
            InputReportHandler::new(frame_buffer_size, self.display_rotation, device_descriptor),
        );
    }
}
