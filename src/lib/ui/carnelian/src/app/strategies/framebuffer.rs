// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{
        strategies::base::AppStrategy, Config, FrameBufferPtr, InternalSender, MessageInternal,
        RenderOptions,
    },
    drawing::DisplayRotation,
    geometry::IntSize,
    input::{self, listen_for_user_input, report::InputReportHandler, DeviceId},
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
use euclid::size2;
use fidl_fuchsia_input_report as hid_input_report;
use fidl_fuchsia_ui_scenic::ScenicProxy;
use fuchsia_async::{self as fasync};
use futures::{channel::mpsc::UnboundedSender, TryFutureExt};
use keymaps::Keymap;
use std::collections::HashMap;

pub(crate) struct AutoRepeatContext {
    app_sender: UnboundedSender<MessageInternal>,
    view_id: ViewKey,
    #[allow(unused)]
    keyboard_autorepeat_task: Option<fasync::Task<()>>,
    repeat_interval: std::time::Duration,
}

pub(crate) trait AutoRepeatTimer {
    fn schedule_autorepeat_timer(&mut self, device_id: &DeviceId);
    fn continue_autorepeat_timer(&mut self, device_id: &DeviceId);
    fn cancel_autorepeat_timer(&mut self) {}
}

impl AutoRepeatContext {
    pub(crate) fn new(app_sender: &UnboundedSender<MessageInternal>, view_id: ViewKey) -> Self {
        Self {
            app_sender: app_sender.clone(),
            view_id,
            keyboard_autorepeat_task: None,
            repeat_interval: Config::get().keyboard_autorepeat_slow_interval,
        }
    }

    fn schedule(&mut self, device_id: &DeviceId) {
        let timer = fasync::Timer::new(fuchsia_async::Time::after(self.repeat_interval.into()));
        let app_sender = self.app_sender.clone();
        let device_id = device_id.clone();
        let view_id = self.view_id;
        let task = fasync::Task::local(async move {
            timer.await;
            app_sender
                .unbounded_send(MessageInternal::KeyboardAutoRepeat(device_id, view_id))
                .expect("unbounded_send");
        });
        self.keyboard_autorepeat_task = Some(task);
    }
}

impl AutoRepeatTimer for AutoRepeatContext {
    fn schedule_autorepeat_timer(&mut self, device_id: &DeviceId) {
        self.repeat_interval = Config::get().keyboard_autorepeat_slow_interval;
        self.schedule(device_id);
    }

    fn continue_autorepeat_timer(&mut self, device_id: &DeviceId) {
        self.repeat_interval =
            (self.repeat_interval * 3 / 4).max(Config::get().keyboard_autorepeat_fast_interval);
        self.schedule(device_id);
    }

    fn cancel_autorepeat_timer(&mut self) {
        let task = self.keyboard_autorepeat_task.take();
        if let Some(task) = task {
            fasync::Task::local(async move {
                task.cancel().await;
            })
            .detach();
        }
    }
}

pub(crate) struct FrameBufferAppStrategy<'a> {
    pub frame_buffer: FrameBufferPtr,
    pub display_rotation: DisplayRotation,
    pub keymap: &'a Keymap<'a>,
    pub view_key: ViewKey,
    pub input_report_handlers: HashMap<DeviceId, InputReportHandler<'a>>,
    pub context: AutoRepeatContext,
}

#[async_trait(?Send)]
impl<'a> AppStrategy for FrameBufferAppStrategy<'a> {
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
            strategy_params.display_rotation,
            render_options,
            &strategy_params.size,
            strategy_params.pixel_format,
            app_sender.clone(),
            strategy_params.frame_buffer.clone(),
            strategy_params.display_resource_release_delay,
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
        Some(size2(config.width as i32, config.height as i32))
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
        handler.handle_input_report(device_id, input_report, &mut self.context)
    }

    fn handle_register_input_device(
        &mut self,
        device_id: &input::DeviceId,
        device_descriptor: &hid_input_report::DeviceDescriptor,
    ) {
        let frame_buffer_size = self.get_frame_buffer_size().expect("frame_buffer_size");
        self.input_report_handlers.insert(
            device_id.clone(),
            InputReportHandler::new(
                device_id.clone(),
                frame_buffer_size,
                self.display_rotation,
                device_descriptor,
                self.keymap,
            ),
        );
    }

    fn handle_keyboard_autorepeat(&mut self, device_id: &input::DeviceId) -> Vec<input::Event> {
        let handler = self.input_report_handlers.get_mut(device_id).expect("input_report_handler");
        handler.handle_keyboard_autorepeat(device_id, &mut self.context)
    }
}
