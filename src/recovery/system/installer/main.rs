// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use carnelian::{
    app::{Config, ViewCreationParameters},
    color::Color,
    drawing::{load_font, DisplayRotation, FontFace},
    input, make_message,
    render::{rive::load_rive, Context as RenderContext},
    scene::{
        facets::{
            FacetId, RiveFacet, SetColorMessage, SetTextMessage, TextFacet, TextFacetOptions,
            TextHorizontalAlignment,
        },
        group::GroupId,
        layout::{Alignment, CrossAxisAlignment},
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, AppAssistantPtr, AppSender, MessageTarget, Point, Size, ViewAssistant,
    ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::{point2, size2};
use fidl_fuchsia_boot::ArgumentsMarker;
use fidl_fuchsia_hardware_display::VirtconMode;
use fuchsia_async as fasync;
use fuchsia_watch::PathEvent;
use fuchsia_zircon::Event;
use futures::StreamExt;
use recovery_ui_config::Config as UiConfig;
use rive_rs as rive;
use std::path::PathBuf;
use std::sync::Mutex;

use fuchsia_zircon as zx;

mod menu;
use menu::{Key, MenuButtonType, MenuEvent, MenuState, MenuStateMachine};

pub mod installer;
use installer::{
    find_install_source, get_bootloader_type, paver_connect, set_active_configuration,
    BootloaderType,
};
use recovery_util::block::{get_block_device, get_block_devices, BlockDevice};

pub mod partition;
use partition::Partition;

const INSTALLER_HEADLINE: &'static str = "Fuchsia Workstation Installer";

const BG_COLOR: Color = Color { r: 238, g: 23, b: 128, a: 255 };
const WARN_BG_COLOR: Color = Color { r: 158, g: 11, b: 0, a: 255 };
const TEXT_COLOR: Color = Color::new(); // Black
const SELECTED_BUTTON_COLOR: Color = Color::white();

// Menu interaction
const HID_USAGE_KEY_UP: u32 = 82;
const HID_USAGE_KEY_DOWN: u32 = 81;
const HID_USAGE_KEY_ENTER: u32 = 40;

const LOGO_IMAGE_PATH: &str = "/pkg/data/logo.riv";

enum InstallerMessages {
    MenuUp,
    MenuDown,
    MenuEnter,
    Error(String),
    GotInstallSource(BlockDevice),
    GotBootloaderType(BootloaderType),
    GotInstallDestinations(Vec<BlockDevice>),
    GotBlockDevices(Vec<BlockDevice>),
    ProgressUpdate(String),
}

/// Installer
#[derive(Clone, Debug, PartialEq)]
struct InstallationPaths {
    install_source: Option<BlockDevice>,
    install_target: Option<BlockDevice>,
    bootloader_type: Option<BootloaderType>,
    install_destinations: Vec<BlockDevice>,
    available_disks: Vec<BlockDevice>,
}

impl InstallationPaths {
    pub fn new() -> InstallationPaths {
        InstallationPaths {
            install_source: None,
            install_target: None,
            bootloader_type: None,
            install_destinations: Vec::new(),
            available_disks: Vec::new(),
        }
    }
}

struct InstallerAppAssistant {
    display_rotation: DisplayRotation,
    automated: bool,
}

impl InstallerAppAssistant {
    fn new(display_rotation: DisplayRotation, automated: bool) -> Self {
        Self { display_rotation: display_rotation, automated }
    }
}

impl AppAssistant for InstallerAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_with_parameters(
        &mut self,
        params: ViewCreationParameters,
    ) -> Result<ViewAssistantPtr, Error> {
        let file = load_rive(LOGO_IMAGE_PATH).ok();
        Ok(Box::new(InstallerViewAssistant::new(
            params.app_sender,
            params.view_key,
            file,
            self.automated,
        )?))
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.view_mode = carnelian::app::ViewMode::Direct;
        config.virtcon_mode = Some(VirtconMode::Forced);
        config.display_rotation = self.display_rotation;
        config.buffer_count = Some(1);
    }
}

struct SceneDetails {
    scene: Scene,
    size: Size,
    background: FacetId,
    subheading: FacetId,
    message: Option<FacetId>,
    buttons: Vec<FacetId>,
    message_group: GroupId,
    button_group: GroupId,
}

struct InstallerViewAssistant {
    app_sender: AppSender,
    view_key: ViewKey,
    scene_details: Option<SceneDetails>,
    face: FontFace,
    menu_state_machine: MenuStateMachine,
    installation_paths: InstallationPaths,
    file: Option<rive::File>,
    automated: bool,
    prev_state: MenuState,
}

impl InstallerViewAssistant {
    fn new(
        app_sender: AppSender,
        view_key: ViewKey,
        file: Option<rive::File>,
        automated: bool,
    ) -> Result<InstallerViewAssistant, Error> {
        let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;

        Ok(InstallerViewAssistant {
            app_sender,
            view_key,
            scene_details: None,
            face,
            menu_state_machine: MenuStateMachine::new(),
            installation_paths: InstallationPaths::new(),
            file,
            automated,
            prev_state: MenuState::Warning,
        })
    }

    fn update(&mut self) {
        // Update the existing scene with the current state of the menu.
        // We don't know what has changed so just update everything.
        if let Some(scene_details) = self.scene_details.as_mut() {
            scene_details.scene.send_message(
                &scene_details.background,
                Box::new(SetColorMessage {
                    color: menu_state_to_background_color(self.menu_state_machine.get_state()),
                }),
            );
            scene_details.scene.send_message(
                &scene_details.subheading,
                Box::new(SetTextMessage { text: self.menu_state_machine.get_heading() }),
            );

            // Remove and re-add message.
            // Necessary as we can't change the font size of an existing TextFacet.
            if let Some(message) = scene_details.message {
                scene_details.scene.remove_facet_from_group(message, scene_details.message_group);
                scene_details.scene.remove_facet(message).unwrap();
            }

            let message_text_size = menu_state_to_message_text_size(
                self.menu_state_machine.get_state(),
                scene_details.size,
            );

            let message = TextFacet::with_options(
                self.face.clone(),
                &self.menu_state_machine.get_message(),
                message_text_size,
                TextFacetOptions {
                    horizontal_alignment: TextHorizontalAlignment::Center,
                    color: TEXT_COLOR,
                    ..TextFacetOptions::default()
                },
            );
            let message = scene_details.scene.add_facet(message);
            scene_details.scene.add_facet_to_group(message, scene_details.message_group, None);
            scene_details.scene.move_facet_forward(message).unwrap();
            scene_details.message = Some(message);

            // Remove and re-add buttons.
            // Necessary so we can have a variable number of buttons.
            for button in scene_details.buttons.iter() {
                scene_details.scene.remove_facet_from_group(*button, scene_details.button_group);
                scene_details.scene.remove_facet(*button).unwrap();
            }
            scene_details.buttons.clear();

            let button_text_size = menu_state_to_button_text_size(
                self.menu_state_machine.get_state(),
                scene_details.size,
            );

            for button in self.menu_state_machine.get_buttons() {
                let button = TextFacet::with_options(
                    self.face.clone(),
                    &button.get_text(),
                    button_text_size,
                    TextFacetOptions {
                        color: if button.is_selected() {
                            SELECTED_BUTTON_COLOR
                        } else {
                            TEXT_COLOR
                        },
                        ..TextFacetOptions::default()
                    },
                );

                let button = scene_details.scene.add_facet(button);
                scene_details.scene.add_facet_to_group(button, scene_details.button_group, None);
                scene_details.scene.move_facet_forward(button).unwrap();
                scene_details.buttons.push(button);
            }
        }
    }

    fn handle_installer_message(&mut self, message: &InstallerMessages) {
        match message {
            // Menu Interaction
            InstallerMessages::MenuUp => {
                self.menu_state_machine.handle_event(MenuEvent::Navigate(Key::Up));
            }
            InstallerMessages::MenuDown => {
                self.menu_state_machine.handle_event(MenuEvent::Navigate(Key::Down));
            }
            InstallerMessages::MenuEnter => {
                // Get disks if usb install selected
                match self.menu_state_machine.get_selected_button_type() {
                    MenuButtonType::USBInstall => {
                        // Get installation targets
                        fasync::Task::local(setup_installation_paths(
                            self.app_sender.clone(),
                            self.view_key,
                        ))
                        .detach();
                    }
                    MenuButtonType::Disk(target) => {
                        // Disk was selected as installation target
                        self.installation_paths.install_target = Some(target.clone());
                        self.menu_state_machine.handle_event(MenuEvent::Enter);
                    }
                    MenuButtonType::Yes => {
                        // User agrees to wipe disk and install
                        self.menu_state_machine.handle_event(MenuEvent::Enter);
                        fasync::Task::local(fuchsia_install(
                            self.app_sender.clone(),
                            self.view_key,
                            self.installation_paths.clone(),
                        ))
                        .detach();
                    }
                    _ => {
                        self.menu_state_machine.handle_event(MenuEvent::Enter);
                    }
                }
            }
            InstallerMessages::Error(error_msg) => {
                self.menu_state_machine.handle_event(MenuEvent::Error(error_msg.clone()));
            }
            InstallerMessages::GotInstallSource(install_source_path) => {
                self.installation_paths.install_source = Some(install_source_path.clone());
            }
            InstallerMessages::GotBootloaderType(bootloader_type) => {
                self.installation_paths.bootloader_type = Some(bootloader_type.clone());
            }
            InstallerMessages::GotInstallDestinations(destinations) => {
                self.installation_paths.install_destinations = destinations.clone();
                // Send disks to menu
                self.menu_state_machine
                    .handle_event(MenuEvent::GotBlockDevices(destinations.clone()));
            }
            InstallerMessages::GotBlockDevices(devices) => {
                self.installation_paths.available_disks = devices.clone();
            }
            InstallerMessages::ProgressUpdate(string) => {
                self.menu_state_machine.handle_event(MenuEvent::ProgressUpdate(string.clone()));
            }
        }

        // Render menu changes
        self.app_sender.request_render(self.view_key);
    }
}

impl ViewAssistant for InstallerViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        Ok(())
    }

    fn get_scene(&mut self, size: Size) -> Option<&mut Scene> {
        let scene_details = self.scene_details.take().unwrap_or_else(|| {
            // Create the scene from scratch based on the current menu state.

            // The scene always has a static heading at the top and logo in the corner.
            let min_dimension = size.width.min(size.height);

            let mut builder = SceneBuilder::new().round_scene_corners(true);

            // Place the logo at the bottom right.
            let logo_edge = min_dimension * 0.24;
            let logo_size: Size = size2(logo_edge, logo_edge);

            let logo_position = {
                let x = size.width * 0.8;
                let y = size.height * 0.7;
                point2(x, y)
            };

            if let Some(file) = &self.file {
                builder.facet_at_location(
                    Box::new(
                        RiveFacet::new_from_file(logo_size, &file, None).expect("facet_from_file"),
                    ),
                    logo_position,
                );
            }

            let mut subheading = None;
            let mut message_group = None;
            let mut button_group = None;

            builder.group().stack().expand().align(Alignment::top_center()).contents(
                |builder: &mut SceneBuilder| {
                    builder.group().column().contents(|builder: &mut SceneBuilder| {
                        // Place the heading at the top.
                        builder.text(
                            self.face.clone(),
                            INSTALLER_HEADLINE,
                            min_dimension / 10.0,
                            Point::zero(),
                            TextFacetOptions { color: TEXT_COLOR, ..TextFacetOptions::default() },
                        );

                        builder.space(size / 50.0);

                        // The remaining parts of the scene are dynamic:
                        //  - A subheading
                        //  - An optional message
                        //  - 0 or more buttons

                        subheading = Some(builder.text(
                            self.face.clone(),
                            &self.menu_state_machine.get_heading(),
                            min_dimension / 15.0,
                            Point::zero(),
                            TextFacetOptions { color: TEXT_COLOR, ..TextFacetOptions::default() },
                        ));

                        builder.space(size / 10.0);

                        // Allocate a group for the message.
                        message_group = Some(builder.group().column().contents(|_| {}));

                        builder.space(size / 30.0);

                        // Allocate a group for the buttons.
                        button_group = Some(
                            builder
                                .group()
                                .column()
                                .cross_align(CrossAxisAlignment::Start)
                                .contents(|_| {}),
                        );
                    });
                },
            );

            // Set background colour.
            // This must be added after everything else to be rendered at the back.
            let background = builder.rectangle(
                size,
                menu_state_to_background_color(self.menu_state_machine.get_state()),
            );

            let subheading = subheading.unwrap();
            let message_group = message_group.unwrap();
            let button_group = button_group.unwrap();
            SceneDetails {
                scene: builder.build(),
                size,
                background,
                subheading,
                message: None,
                buttons: vec![],
                message_group,
                button_group,
            }
        });

        self.scene_details = Some(scene_details);

        // Fill in the dynamic parts of the scene.
        self.update();

        Some(&mut self.scene_details.as_mut().unwrap().scene)
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let scene = self.get_scene_with_contexts(render_context, view_context).unwrap();
        scene.layout(view_context.size);
        scene.render(render_context, ready_event, view_context)?;

        if self.automated && self.menu_state_machine.get_state() != self.prev_state {
            let old_state = self.prev_state;
            self.prev_state = self.menu_state_machine.get_state();
            match self.menu_state_machine.get_state() {
                MenuState::SelectInstall | MenuState::SelectDisk | MenuState::Warning => {
                    tracing::info!(
                        "installer: {:?}, proceeding to next screen",
                        self.menu_state_machine.get_state()
                    );
                    self.app_sender.queue_message(
                        MessageTarget::View(self.view_key),
                        make_message(InstallerMessages::MenuEnter),
                    );
                }
                MenuState::Progress => tracing::info!("Install in progress"),
                MenuState::Error => {
                    tracing::info!(
                        "install failed :(. Old state: {:?} Error message: {}",
                        old_state,
                        self.menu_state_machine.get_message()
                    )
                }
            }
        }

        Ok(())
    }

    fn handle_keyboard_event(
        &mut self,
        context: &mut ViewAssistantContext,
        _event: &input::Event,
        keyboard_event: &input::keyboard::Event,
    ) -> Result<(), Error> {
        if keyboard_event.phase == input::keyboard::Phase::Pressed {
            let pressed_key = keyboard_event.hid_usage;
            match pressed_key {
                HID_USAGE_KEY_UP => {
                    context.queue_message(make_message(InstallerMessages::MenuUp));
                }
                HID_USAGE_KEY_DOWN => {
                    context.queue_message(make_message(InstallerMessages::MenuDown));
                }
                HID_USAGE_KEY_ENTER => {
                    context.queue_message(make_message(InstallerMessages::MenuEnter));
                }
                _ => {}
            }
        }

        Ok(())
    }

    fn handle_message(&mut self, message: carnelian::Message) {
        if let Some(message) = message.downcast_ref::<InstallerMessages>() {
            self.handle_installer_message(message);
        }
    }
}

fn menu_state_to_background_color(state: MenuState) -> Color {
    match state {
        MenuState::Warning => WARN_BG_COLOR,
        MenuState::Error => WARN_BG_COLOR,
        _ => BG_COLOR,
    }
}

fn menu_state_to_message_text_size(state: MenuState, screen_size: Size) -> f32 {
    let base = screen_size.width.min(screen_size.height);
    match state {
        MenuState::Warning => base / 20.0,
        MenuState::Progress => base / 25.0,
        MenuState::Error => base / 20.0,
        _ => 0.0,
    }
}

fn menu_state_to_button_text_size(state: MenuState, screen_size: Size) -> f32 {
    let base = screen_size.width.min(screen_size.height);
    match state {
        MenuState::SelectInstall => base / 20.0,
        MenuState::SelectDisk => base / 33.0,
        MenuState::Warning => base / 20.0,
        _ => 0.0,
    }
}

async fn get_installation_paths(app_sender: AppSender, view_key: ViewKey) -> Result<(), Error> {
    let block_devices = get_block_devices().await?;
    let bootloader_type = get_bootloader_type().await?;

    // Send got bootloader message
    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::GotBootloaderType(bootloader_type)),
    );

    // Find the location of the installer
    let install_source = find_install_source(&block_devices, bootloader_type).await?;

    // Send got installer messgae
    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::GotInstallSource(install_source.clone())),
    );

    // Make list of available destinations for installation
    let mut destinations = Vec::new();
    for block_device in block_devices.iter() {
        if block_device != install_source {
            destinations.push(block_device.clone());
        }
    }

    // Send error if no destinations found
    if destinations.is_empty() {
        return Err(anyhow!("Found no block devices for installation."));
    };

    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::GotBlockDevices(block_devices)),
    );

    // Else end destinations
    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::GotInstallDestinations(
            destinations.into_iter().filter(|d| d.is_disk()).collect(),
        )),
    );

    Ok(())
}

async fn setup_installation_paths(app_sender: AppSender, view_key: ViewKey) {
    match get_installation_paths(app_sender.clone(), view_key).await {
        Ok(_install_source) => {
            tracing::info!("Found installer & block devices ");
        }
        Err(e) => {
            // Send error
            app_sender.queue_message(
                MessageTarget::View(view_key),
                make_message(InstallerMessages::Error(e.to_string())),
            );
            tracing::info!("ERROR getting install target: {}", e);
        }
    };
}

async fn fuchsia_install(
    app_sender: AppSender,
    view_key: ViewKey,
    installation_paths: InstallationPaths,
) {
    // Execute install
    match do_install(app_sender.clone(), view_key, installation_paths).await {
        Ok(_) => {
            app_sender.queue_message(
                MessageTarget::View(view_key),
                make_message(InstallerMessages::ProgressUpdate(String::from(
                    "Success! Please restart your computer",
                ))),
            );
        }
        Err(e) => {
            tracing::error!("Error while installing: {:#}", e);
            app_sender.queue_message(
                MessageTarget::View(view_key),
                make_message(InstallerMessages::Error(String::from(format!(
                    "Error {}, please restart",
                    e
                )))),
            );
        }
    }
}

async fn do_install(
    app_sender: AppSender,
    view_key: ViewKey,
    installation_paths: InstallationPaths,
) -> Result<(), Error> {
    let install_target =
        installation_paths.install_target.ok_or(anyhow!("No installation target?"))?;
    let install_source =
        installation_paths.install_source.ok_or(anyhow!("No installation source?"))?;
    let bootloader_type = installation_paths.bootloader_type.unwrap();

    // TODO(fxbug.dev/100712): Remove this once flake is resolved.
    tracing::info!(
        "Installing to {} ({}), source {} ({})",
        install_target.topo_path,
        install_target.class_path,
        install_source.topo_path,
        install_source.class_path
    );

    let (paver, data_sink) =
        paver_connect(&install_target.class_path).context("Could not contact paver")?;

    tracing::info!("Wiping old partition tables...");
    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from(
            "Wiping old partition tables...",
        ))),
    );
    data_sink.wipe_partition_tables().await?;
    tracing::info!("Initializing Fuchsia partition tables...");
    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from(
            "Initializing Fuchsia partition tables...",
        ))),
    );
    data_sink.initialize_partition_tables().await?;
    tracing::info!("Success.");

    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from("Getting source partitions"))),
    );
    let to_install = Partition::get_partitions(
        &install_source,
        &installation_paths.available_disks,
        bootloader_type,
    )
    .await
    .context("Getting source partitions")?;

    let num_partitions = to_install.len();
    let mut current_partition = 1;
    for part in to_install {
        app_sender.queue_message(
            MessageTarget::View(view_key),
            make_message(InstallerMessages::ProgressUpdate(String::from(format!(
                "paving partition {} of {}",
                current_partition, num_partitions
            )))),
        );

        let sender = app_sender
            .create_cross_thread_sender::<InstallerMessages>(MessageTarget::View(view_key));

        let prev_percent = Mutex::new(0 as i64);

        let progress_callback = Box::new(move |data_read, data_total| {
            if data_total == 0 {
                return;
            }
            let cur_percent: i64 =
                unsafe { (((data_read as f64) / (data_total as f64)) * 100.0).to_int_unchecked() };
            let mut prev = prev_percent.lock().unwrap();
            if cur_percent == *prev {
                return;
            }
            *prev = cur_percent;

            sender
                .unbounded_send(InstallerMessages::ProgressUpdate(String::from(format!(
                    "paving partition {} of {}: {}%",
                    current_partition, num_partitions, cur_percent
                ))))
                .expect("unbounded send failed");
        });

        tracing::info!("Paving partition: {:?}", part);
        if let Err(e) = part.pave(&data_sink, progress_callback).await {
            tracing::error!("Failed ({:?})", e);
        } else {
            tracing::info!("OK");
            if part.is_ab() {
                tracing::info!("Paving partition: {:?} [-B]", part);
                if let Err(e) = part.pave_b(&data_sink).await {
                    tracing::error!("Failed ({:?})", e);
                } else {
                    tracing::info!("OK");
                }
            }
        }

        current_partition += 1;
    }

    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from("Flushing Partitions"))),
    );
    zx::Status::ok(data_sink.flush().await.context("Sending flush")?)
        .context("Flushing partitions")?;

    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from(
            "Setting active configuration for the new system",
        ))),
    );
    set_active_configuration(&paver)
        .await
        .context("Setting active configuration for the new system")?;

    app_sender.queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from("Configuration complete!!"))),
    );

    Ok(())
}

/// Wait for a display to become available.
async fn wait_for_display() -> Result<(), Error> {
    let mut stream =
        fuchsia_watch::watch("/dev/class/display-controller").await.context("Starting watch")?;
    while let Some(element) = stream.next().await {
        match element {
            PathEvent::Added(_, _) | PathEvent::Existing(_, _) => return Ok(()),
            _ => {}
        }
    }
    Err(anyhow!("Didn't find a display device"))
}

/// Check to see if we're doing a non-interactive installs.
/// Non-interactive installs are very limited and will likely only work on systems with a single
/// disk.
/// They are intended to be used in end-to-end tests.
async fn check_is_interactive() -> Result<bool, Error> {
    let proxy = fuchsia_component::client::connect_to_protocol::<ArgumentsMarker>()
        .context("Connecting to boot arguments service")?;
    let automated =
        proxy.get_bool("installer.non-interactive", false).await.context("Getting bool")?;
    tracing::info!(
        "workstation installer: {}doing automated install.",
        if automated { "" } else { "not " }
    );

    if automated {
        wait_for_install_disk().await.context("Waiting for install disk")?;
    }
    Ok(automated)
}

/// Wait for an installation source to become present on the system.
async fn wait_for_install_disk() -> Result<(), Error> {
    let mut stream = fuchsia_watch::watch("/dev/class/block").await.context("Starting watch")?;
    let bootloader_type = get_bootloader_type().await?;
    let mut devices = vec![];
    while let Some(element) = stream.next().await {
        match element {
            PathEvent::Added(path, _) | PathEvent::Existing(path, _) => {
                match get_block_device(path.to_str().unwrap().to_owned()).await {
                    Ok(Some(bd)) => {
                        devices.push(bd);
                        if let Ok(_) = find_install_source(&devices, bootloader_type).await {
                            return Ok(());
                        }
                    }
                    _ => {}
                }
            }
            _ => {}
        }
    }
    Err(anyhow!("Didn't find an install disk"))
}

#[fuchsia::main]
fn main() -> Result<(), Error> {
    tracing::info!("workstation installer: started.");

    // Before we give control to carnelian, wait until a display driver is bound.
    let (display_result, interactive_result) =
        fuchsia_async::LocalExecutor::new().context("Creating executor")?.run_singlethreaded(
            async move { futures::join!(wait_for_display(), check_is_interactive()) },
        );
    display_result.context("Waiting for display controller")?;
    let automated = interactive_result.context("Fetching installer boot arguments")?;

    let config = UiConfig::take_from_startup_handle();
    let display_rotation = match config.display_rotation {
        0 => DisplayRotation::Deg0,
        180 => DisplayRotation::Deg180,
        // Carnelian uses an inverted z-axis for rotation
        90 => DisplayRotation::Deg270,
        270 => DisplayRotation::Deg90,
        val => {
            tracing::error!("Invalid display_rotation {}, defaulting to 0 degrees", val);
            DisplayRotation::Deg0
        }
    };

    App::run(Box::new(move |_| {
        Box::pin(async move {
            let assistant = Box::new(InstallerAppAssistant::new(display_rotation, automated));
            Ok::<AppAssistantPtr, Error>(assistant)
        })
    }))
}
