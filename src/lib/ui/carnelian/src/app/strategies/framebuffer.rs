// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    app::{
        strategies::base::AppStrategy, BoxedGammaValues, Config, InternalSender, MessageInternal,
    },
    drawing::DisplayRotation,
    geometry::IntSize,
    input::{self, listen_for_user_input, report::InputReportHandler, DeviceId},
    view::{
        strategies::{
            base::{DisplayDirectParams, ViewStrategyParams, ViewStrategyPtr},
            display_direct::DisplayDirectViewStrategy,
        },
        ViewKey, USE_FIRST_VIEW,
    },
};
use anyhow::{bail, ensure, Context, Error};
use async_trait::async_trait;
use euclid::size2;
use fidl::endpoints::{self};
use fidl_fuchsia_hardware_display::{
    ControllerEvent, ControllerMarker, ControllerProxy, ProviderSynchronousProxy, VirtconMode,
};
use fidl_fuchsia_input_report as hid_input_report;
use fidl_fuchsia_ui_scenic::ScenicProxy;
use fuchsia_async::{self as fasync};
use fuchsia_vfs_watcher as vfs_watcher;
use fuchsia_zircon::{self as zx, Status};
use futures::{channel::mpsc::UnboundedSender, StreamExt, TryFutureExt, TryStreamExt};
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use keymaps::Keymap;
use std::{
    collections::HashMap,
    fmt::Debug,
    fs::{self, OpenOptions},
    path::{Path, PathBuf},
    rc::Rc,
};

async fn watch_directory_async(
    dir: PathBuf,
    app_sender: UnboundedSender<MessageInternal>,
) -> Result<(), Error> {
    let dir_proxy =
        open_directory_in_namespace(dir.to_str().expect("to_str"), OPEN_RIGHT_READABLE)?;
    let mut watcher = vfs_watcher::Watcher::new(dir_proxy).await?;
    fasync::Task::local(async move {
        while let Some(msg) = (watcher.try_next()).await.expect("msg") {
            match msg.event {
                vfs_watcher::WatchEvent::ADD_FILE | vfs_watcher::WatchEvent::EXISTING => {
                    let device_path = dir.join(msg.filename);
                    app_sender
                        .unbounded_send(MessageInternal::NewDisplayController(device_path))
                        .expect("unbounded_send");
                }
                _ => (),
            }
        }
    })
    .detach();
    Ok(())
}

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

const DISPLAY_CONTROLLER_PATH: &'static str = "/dev/class/display-controller";

pub type ControllerProxyPtr = Rc<ControllerProxy>;

pub fn first_display_device_path() -> Option<PathBuf> {
    let mut entries = fs::read_dir(DISPLAY_CONTROLLER_PATH).ok()?;
    entries.next()?.ok().map(|entry| entry.path())
}

pub struct DisplayController {
    #[allow(unused)]
    display_controller: zx::Channel,
    pub controller: ControllerProxyPtr,
}

impl DisplayController {
    pub(crate) async fn open<P: AsRef<Path> + Debug>(
        path: P,
        virtcon_mode: &Option<VirtconMode>,
        app_sender: &UnboundedSender<MessageInternal>,
    ) -> Result<Self, Error> {
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(path)
            .context("while opening device file")?;
        let channel = fdio::clone_channel(&file).context("while cloning channel")?;
        let provider = ProviderSynchronousProxy::new(channel);
        let (device_client, device_server) = zx::Channel::create()?;
        let (dc_client, dc_server) = endpoints::create_endpoints::<ControllerMarker>()?;
        let status = if virtcon_mode.is_some() {
            provider.open_virtcon_controller(device_server, dc_server, zx::Time::INFINITE)
        } else {
            provider.open_controller(device_server, dc_server, zx::Time::INFINITE)
        }?;
        ensure!(
            status == zx::sys::ZX_OK,
            "Failed to open display controller {}",
            Status::from_raw(status)
        );
        let controller = dc_client.into_proxy()?;

        if let Some(virtcon_mode) = virtcon_mode {
            controller.set_virtcon_mode(*virtcon_mode as u8)?;
        }

        let mut event_stream = controller.take_event_stream();
        let app_sender = app_sender.clone();
        let f = async move {
            loop {
                if let Some(event) = event_stream.next().await {
                    if let Ok(event) = event {
                        app_sender
                            .unbounded_send(MessageInternal::DisplayControllerEvent(event))
                            .expect("unbounded_send");
                    }
                }
            }
        };
        fasync::Task::local(f).detach();
        controller.enable_vsync(true).context("enable_vsync failed")?;

        Ok(Self { controller: Rc::new(controller), display_controller: device_client })
    }

    pub(crate) async fn watch_displays(app_sender: UnboundedSender<MessageInternal>) {
        watch_directory_async(PathBuf::from(DISPLAY_CONTROLLER_PATH), app_sender)
            .await
            .expect("watch_directory_async");
    }
}

pub type DisplayId = u64;

pub(crate) struct DisplayDirectAppStrategy<'a> {
    pub display_controller: Option<DisplayController>,
    pub display_rotation: DisplayRotation,
    pub keymap: &'a Keymap<'a>,
    pub input_report_handlers: HashMap<DeviceId, InputReportHandler<'a>>,
    pub context: AutoRepeatContext,
    pub app_sender: UnboundedSender<MessageInternal>,
    pub owned: bool,
    pub size: Option<IntSize>,
}

impl<'a> DisplayDirectAppStrategy<'a> {
    pub fn new(
        display_controller: Option<DisplayController>,
        keymap: &'a Keymap<'a>,
        app_sender: UnboundedSender<MessageInternal>,
        app_config: &Config,
    ) -> DisplayDirectAppStrategy<'a> {
        DisplayDirectAppStrategy {
            display_controller: display_controller,
            display_rotation: app_config.display_rotation,
            keymap,
            input_report_handlers: HashMap::new(),
            context: AutoRepeatContext::new(&app_sender, 0),
            app_sender,
            owned: false,
            size: None,
        }
    }

    async fn handle_displays_changed(
        &mut self,
        added: Vec<fidl_fuchsia_hardware_display::Info>,
        removed: Vec<u64>,
    ) -> Result<(), Error> {
        let display_controller = self.display_controller.as_ref().expect("display_controller");
        for display_id in removed {
            self.app_sender
                .unbounded_send(MessageInternal::CloseView(display_id as ViewKey))
                .expect("unbounded");
        }

        for info in added {
            let display_id = info.id;
            if self.size.is_none() {
                let mode = info.modes[0];
                self.size =
                    Some(size2(mode.horizontal_resolution, mode.vertical_resolution).to_i32());
            }
            self.app_sender
                .unbounded_send(MessageInternal::CreateView(ViewStrategyParams::DisplayDirect(
                    DisplayDirectParams {
                        display_id,
                        controller: display_controller.controller.clone(),
                        info,
                    },
                )))
                .expect("send");
        }

        Ok(())
    }
}

#[async_trait(?Send)]
impl<'a> AppStrategy for DisplayDirectAppStrategy<'a> {
    async fn create_view_strategy(
        &self,
        key: ViewKey,
        app_sender: UnboundedSender<MessageInternal>,
        strategy_params: ViewStrategyParams,
    ) -> Result<ViewStrategyPtr, Error> {
        let strategy_params = match strategy_params {
            ViewStrategyParams::DisplayDirect(params) => params,
            _ => bail!(
                "Incorrect ViewStrategyParams passed to create_view_strategy for frame buffer"
            ),
        };
        ensure!(
            key == strategy_params.display_id,
            "key = {}, display_id = {}",
            key,
            strategy_params.display_id
        );
        Ok(DisplayDirectViewStrategy::new(
            strategy_params.controller,
            app_sender.clone(),
            strategy_params.info,
        )
        .await?)
    }

    fn supports_scenic(&self) -> bool {
        return false;
    }

    fn get_scenic_proxy(&self) -> Option<&ScenicProxy> {
        return None;
    }

    fn get_frame_buffer_size(&self) -> Option<IntSize> {
        self.size
    }

    async fn post_setup(&mut self, internal_sender: &InternalSender) -> Result<(), Error> {
        let view_key = USE_FIRST_VIEW;
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

    async fn handle_new_display_controller(&mut self, display_path: PathBuf) {
        if self.display_controller.is_none() {
            let display_controller = DisplayController::open(
                &display_path,
                &Config::get().virtcon_mode,
                &self.app_sender,
            )
            .await
            .expect("DisplayController::open");
            self.display_controller = Some(display_controller);
        }
    }

    async fn handle_display_controller_event(&mut self, event: ControllerEvent) {
        match event {
            ControllerEvent::OnDisplaysChanged { added, removed } => {
                self.handle_displays_changed(added, removed)
                    .await
                    .expect("handle_displays_changed");
            }
            ControllerEvent::OnClientOwnershipChange { has_ownership } => {
                self.owned = has_ownership;
                self.app_sender
                    .unbounded_send(MessageInternal::OwnershipChanged(has_ownership))
                    .expect("unbounded_send");
            }
            ControllerEvent::OnVsync { .. } => {
                panic!("App strategy should not see vsync events");
            }
        }
    }

    fn set_virtcon_mode(&mut self, virtcon_mode: VirtconMode) {
        self.display_controller
            .as_ref()
            .expect("display_controller")
            .controller
            .set_virtcon_mode(virtcon_mode as u8)
            .expect("set_virtcon_mode");
    }

    fn import_and_set_gamma_table(
        &mut self,
        display_id: u64,
        gamma_table_id: u64,
        mut r: BoxedGammaValues,
        mut g: BoxedGammaValues,
        mut b: BoxedGammaValues,
    ) {
        let display_controller = self.display_controller.as_ref().expect("display_controller");
        display_controller
            .controller
            .import_gamma_table(gamma_table_id, &mut r, &mut g, &mut b)
            .expect("import_gamma_table");
        display_controller
            .controller
            .set_display_gamma_table(display_id, gamma_table_id)
            .expect("set_display_gamma_table");
    }
}
