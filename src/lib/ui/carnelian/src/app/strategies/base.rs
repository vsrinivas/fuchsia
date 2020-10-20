// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{
        strategies::{framebuffer::FrameBufferAppStrategy, scenic::ScenicAppStrategy},
        AppAssistantPtr, FrameBufferPtr, InternalSender, MessageInternal, RenderOptions,
    },
    geometry::IntSize,
    input::{self},
    view::{
        strategies::base::{
            FrameBufferParams, ViewStrategyParams, ViewStrategyPtr, DISPLAY_ROTATION,
        },
        ViewKey,
    },
};
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_input_report as hid_input_report;
use fidl_fuchsia_ui_scenic::ScenicMarker;
use fidl_fuchsia_ui_scenic::ScenicProxy;
use fuchsia_async::{self as fasync};
use fuchsia_component::client::connect_to_service;
use fuchsia_framebuffer::{FrameBuffer, FrameUsage, VSyncMessage};
use fuchsia_zircon::{Duration, Time};
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    StreamExt, TryFutureExt,
};
use std::{cell::RefCell, collections::HashMap, rc::Rc};

// This trait exists to keep the scenic implementation and the software
// framebuffer implementations as separate as possible.
// At the moment this abstraction is quite leaky, but it is good
// enough and can be refined with experience.
#[async_trait(?Send)]
pub(crate) trait AppStrategy {
    async fn create_view_strategy(
        &self,
        key: ViewKey,
        render_options: RenderOptions,
        app_sender: UnboundedSender<MessageInternal>,
        strategy_params: ViewStrategyParams,
    ) -> Result<ViewStrategyPtr, Error>;
    fn supports_scenic(&self) -> bool;
    fn create_view_for_testing(&self, _: &UnboundedSender<MessageInternal>) -> Result<(), Error> {
        Ok(())
    }
    fn start_services(
        &self,
        _outgoing_services_names: Vec<&'static str>,
        _app_sender: UnboundedSender<MessageInternal>,
    ) -> Result<(), Error> {
        Ok(())
    }
    fn get_scenic_proxy(&self) -> Option<&ScenicProxy>;
    fn get_frame_buffer(&self) -> Option<FrameBufferPtr> {
        None
    }
    fn get_frame_buffer_size(&self) -> Option<IntSize>;
    fn get_pixel_size(&self) -> u32;
    fn get_pixel_format(&self) -> fuchsia_framebuffer::PixelFormat;
    fn get_linear_stride_bytes(&self) -> u32;
    async fn post_setup(
        &mut self,
        _pixel_format: fuchsia_framebuffer::PixelFormat,
        _internal_sender: &InternalSender,
    ) -> Result<(), Error>;
    fn handle_input_report(
        &mut self,
        _device_id: &input::DeviceId,
        _input_report: &hid_input_report::InputReport,
    ) -> Vec<input::Event> {
        Vec::new()
    }
    fn handle_register_input_device(
        &mut self,
        _device_id: &input::DeviceId,
        _device_descriptor: &hid_input_report::DeviceDescriptor,
    ) {
    }
}

pub(crate) type AppStrategyPtr = Box<dyn AppStrategy>;

// Tries to create a framebuffer. If that fails, assume Scenic is running.
pub(crate) async fn create_app_strategy(
    assistant: &AppAssistantPtr,
    next_view_key: ViewKey,
    internal_sender: &InternalSender,
) -> Result<AppStrategyPtr, Error> {
    let render_options = assistant.get_render_options();

    let usage = if render_options.use_spinel { FrameUsage::Gpu } else { FrameUsage::Cpu };

    let (sender, mut receiver) = unbounded::<VSyncMessage>();
    let fb = FrameBuffer::new(usage, None, Some(sender)).await;
    if fb.is_err() {
        let scenic = connect_to_service::<ScenicMarker>()?;
        Ok::<AppStrategyPtr, Error>(Box::new(ScenicAppStrategy { scenic }))
    } else {
        let fb = fb.unwrap();
        let vsync_interval =
            Duration::from_nanos(100_000_000_000 / fb.get_config().refresh_rate_e2 as i64);
        let vsync_internal_sender = internal_sender.clone();

        // TODO: improve scheduling of updates
        fasync::Task::local(
            async move {
                while let Some(VSyncMessage { display_id: _, timestamp, cookie, .. }) =
                    receiver.next().await
                {
                    vsync_internal_sender
                        .unbounded_send(MessageInternal::HandleVSyncParametersChanged(
                            Time::from_nanos(timestamp as i64),
                            vsync_interval,
                            cookie,
                        ))
                        .expect("unbounded_send");
                    vsync_internal_sender
                        .unbounded_send(MessageInternal::RenderAllViews)
                        .expect("unbounded_send");
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                println!("error {:#?}", e);
            }),
        )
        .detach();

        let config = fb.get_config();
        let size = IntSize::new(config.width as i32, config.height as i32);

        let frame_buffer_ptr = Rc::new(RefCell::new(fb));

        let strat = FrameBufferAppStrategy {
            frame_buffer: frame_buffer_ptr.clone(),
            display_rotation: DISPLAY_ROTATION,
            view_key: next_view_key,
            input_report_handlers: HashMap::new(),
        };

        internal_sender
            .unbounded_send(MessageInternal::CreateView(ViewStrategyParams::FrameBuffer(
                FrameBufferParams {
                    frame_buffer: frame_buffer_ptr,
                    pixel_format: strat.get_pixel_format(),
                    size,
                },
            )))
            .unwrap_or_else(|err| panic!("unbounded send failed: {}", err));

        Ok(Box::new(strat))
    }
}
