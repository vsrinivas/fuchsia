// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{
        strategies::{
            framebuffer::{first_display_device_path, DisplayController, DisplayDirectAppStrategy},
            scenic::ScenicAppStrategy,
        },
        BoxedGammaValues, Config, InternalSender, MessageInternal, ViewMode,
    },
    geometry::IntSize,
    input::{self},
    view::{
        strategies::base::{ViewStrategyParams, ViewStrategyPtr},
        ViewKey,
    },
};
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_hardware_display::VirtconMode;
use fidl_fuchsia_input_report as hid_input_report;
use fidl_fuchsia_ui_scenic::ScenicMarker;
use fidl_fuchsia_ui_scenic::ScenicProxy;
use fuchsia_component::{
    client::connect_to_protocol,
    server::{ServiceFs, ServiceObjLocal},
};
use futures::channel::mpsc::UnboundedSender;
use keymaps::select_keymap;
use std::path::PathBuf;

// This trait exists to keep the hosted implementation and the
// direct implementations as separate as possible.
// At the moment this abstraction is quite leaky, but it is good
// enough and can be refined with experience.
#[async_trait(?Send)]
pub(crate) trait AppStrategy {
    async fn create_view_strategy(
        &self,
        key: ViewKey,
        app_sender: UnboundedSender<MessageInternal>,
        strategy_params: ViewStrategyParams,
    ) -> Result<ViewStrategyPtr, Error>;
    fn supports_scenic(&self) -> bool;
    fn create_view_for_testing(&self, _: &UnboundedSender<MessageInternal>) -> Result<(), Error> {
        Ok(())
    }
    fn start_services<'a, 'b>(
        &self,
        _app_sender: UnboundedSender<MessageInternal>,
        _fs: &'a mut ServiceFs<ServiceObjLocal<'b, ()>>,
    ) -> Result<(), Error> {
        Ok(())
    }
    fn get_scenic_proxy(&self) -> Option<&ScenicProxy>;
    fn get_frame_buffer_size(&self) -> Option<IntSize>;
    async fn post_setup(&mut self, _internal_sender: &InternalSender) -> Result<(), Error>;
    fn handle_input_report(
        &mut self,
        _device_id: &input::DeviceId,
        _input_report: &hid_input_report::InputReport,
    ) -> Vec<input::Event> {
        Vec::new()
    }
    fn handle_keyboard_autorepeat(&mut self, _device_id: &input::DeviceId) -> Vec<input::Event> {
        Vec::new()
    }
    fn handle_register_input_device(
        &mut self,
        _device_id: &input::DeviceId,
        _device_descriptor: &hid_input_report::DeviceDescriptor,
    ) {
    }
    async fn handle_new_display_controller(&mut self, _display_path: PathBuf) {}
    async fn handle_display_controller_event(
        &mut self,
        _event: fidl_fuchsia_hardware_display::ControllerEvent,
    ) {
    }
    fn set_virtcon_mode(&mut self, _virtcon_mode: VirtconMode) {}
    fn import_and_set_gamma_table(
        &mut self,
        _display_id: u64,
        _gamma_table_id: u64,
        mut _r: BoxedGammaValues,
        mut _g: BoxedGammaValues,
        mut _b: BoxedGammaValues,
    ) {
    }
}

pub(crate) type AppStrategyPtr = Box<dyn AppStrategy>;

fn make_scenic_app_strategy() -> Result<AppStrategyPtr, Error> {
    let scenic = connect_to_protocol::<ScenicMarker>()?;
    Ok::<AppStrategyPtr, Error>(Box::new(ScenicAppStrategy { scenic }))
}

fn make_direct_app_strategy(
    display_controller: Option<DisplayController>,
    app_config: &Config,
    internal_sender: InternalSender,
) -> Result<AppStrategyPtr, Error> {
    let strat = DisplayDirectAppStrategy::new(
        display_controller,
        select_keymap(&app_config.keymap_name),
        internal_sender,
        &app_config,
    );

    Ok(Box::new(strat))
}

pub(crate) async fn create_app_strategy(
    internal_sender: &InternalSender,
) -> Result<AppStrategyPtr, Error> {
    let app_config = Config::get();
    match app_config.view_mode {
        ViewMode::Auto => {
            // Tries to open the display controller. If that fails, assume we want to run as hosted.
            let display_controller = if let Some(path) = first_display_device_path() {
                DisplayController::open(&path, &app_config.virtcon_mode, &internal_sender)
                    .await
                    .ok()
            } else {
                None
            };
            if display_controller.is_none() {
                make_scenic_app_strategy()
            } else {
                make_direct_app_strategy(display_controller, app_config, internal_sender.clone())
            }
        }
        ViewMode::Direct => {
            DisplayController::watch_displays(internal_sender.clone()).await;
            make_direct_app_strategy(None, app_config, internal_sender.clone())
        }
        ViewMode::Hosted => make_scenic_app_strategy(),
    }
}
