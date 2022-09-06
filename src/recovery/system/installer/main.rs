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
        facets::{RiveFacet, TextFacet, TextFacetOptions, TextHorizontalAlignment},
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, AppAssistantPtr, AppSender, MessageTarget, Point, Size, ViewAssistant,
    ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::{point2, size2};
use fidl_fuchsia_boot::ArgumentsMarker;
use fidl_fuchsia_hardware_display::VirtconMode;
use fuchsia_async::{self as fasync};
use fuchsia_watch::PathEvent;
use fuchsia_zircon::Event;
use futures::StreamExt;
use recovery_ui_config::Config as UiConfig;
use rive_rs::{self as rive};
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
const HEADING_COLOR: Color = Color::new();

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
            INSTALLER_HEADLINE,
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
}

struct InstallerViewAssistant {
    app_sender: AppSender,
    view_key: ViewKey,
    scene_details: Option<SceneDetails>,
    face: FontFace,
    heading: &'static str,
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
        heading: &'static str,
        automated: bool,
    ) -> Result<InstallerViewAssistant, Error> {
        let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;

        Ok(InstallerViewAssistant {
            app_sender,
            view_key,
            scene_details: None,
            face,
            heading,
            menu_state_machine: MenuStateMachine::new(),
            installation_paths: InstallationPaths::new(),
            file,
            automated,
            prev_state: MenuState::Warning,
        })
    }

    fn update(&mut self) {
        self.scene_details = None;
        self.app_sender.request_render(self.view_key);
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
                        // User agrees to wipe disk and isntall
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
        self.update();
    }
}

impl ViewAssistant for InstallerViewAssistant {
    fn resize(&mut self, _new_size: &Size) -> Result<(), Error> {
        self.scene_details = None;
        Ok(())
    }

    fn get_scene(&mut self, size: Size) -> Option<&mut Scene> {
        let scene_details = self.scene_details.take().unwrap_or_else(|| {
            let min_dimension = size.width.min(size.height);
            let logo_edge = min_dimension * 0.24;
            let text_size = min_dimension / 10.0;
            let top_margin = 0.255;

            // Set background colour
            let bg_color = match self.menu_state_machine.get_state() {
                MenuState::Warning => WARN_BG_COLOR,
                MenuState::Error => WARN_BG_COLOR,
                _ => BG_COLOR,
            };

            let mut builder =
                SceneBuilder::new().background_color(bg_color).round_scene_corners(true);

            let logo_size: Size = size2(logo_edge, logo_edge);
            // Calculate position for the logo image
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
            let heading_text_location = point2(size.width / 2.0, top_margin + (size.height * 0.05));
            builder.text(
                self.face.clone(),
                &self.heading,
                text_size,
                heading_text_location,
                TextFacetOptions {
                    horizontal_alignment: TextHorizontalAlignment::Center,
                    color: HEADING_COLOR,
                    ..TextFacetOptions::default()
                },
            );

            // Build menu
            menu_builder(
                &mut builder,
                &mut self.menu_state_machine,
                size,
                heading_text_location,
                &self.face,
            );

            SceneDetails { scene: builder.build() }
        });

        self.scene_details = Some(scene_details);
        Some(&mut self.scene_details.as_mut().unwrap().scene)
    }

    fn render(
        &mut self,
        render_context: &mut RenderContext,
        ready_event: Event,
        view_context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let scene = self.get_scene_with_contexts(render_context, view_context).unwrap();
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
                        self.menu_state_machine.get_error_msg()
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

fn menu_builder(
    builder: &mut SceneBuilder,
    menu_state_machine: &mut MenuStateMachine,
    target_size: Size,
    heading_location: Point,
    face: &FontFace,
) {
    // Installer subheading properties
    let subheading_size = target_size.width.min(target_size.height) / 15.0;
    let subheading_x = heading_location.x;
    let subheading_y = heading_location.y + (subheading_size * 2.0);
    let subheading_location = point2(subheading_x, subheading_y);
    let text_options = TextFacetOptions {
        horizontal_alignment: TextHorizontalAlignment::Center,
        ..TextFacetOptions::default()
    };

    // Render subheading
    let subheading_facet = TextFacet::with_options(
        face.clone(),
        &menu_state_machine.get_heading(),
        subheading_size,
        text_options,
    );
    builder.facet_at_location(subheading_facet, subheading_location);

    // Default button properties
    let mut text_size: f32 = target_size.width.min(target_size.height) / 20.0;

    // Render state specific things
    let menu_state = menu_state_machine.get_state();
    match menu_state {
        MenuState::SelectInstall => {
            render_buttons_vec(
                builder,
                menu_state_machine,
                target_size,
                subheading_location,
                face,
                text_size,
            );
        }
        MenuState::SelectDisk => {
            text_size = target_size.width.min(target_size.height) / 33.0;
            render_buttons_vec(
                builder,
                menu_state_machine,
                target_size,
                subheading_location,
                face,
                text_size,
            );
        }
        MenuState::Warning => {
            // TODO(fxbug.dev/92116):): figure out \n alignment quirk so this can be one message
            // Additional messages
            let warn_facet = TextFacet::with_options(
                face.clone(),
                menu::CONST_WARN_MESSAGE,
                subheading_size,
                text_options,
            );
            let warn_msg_y = subheading_location.y + (subheading_size * 2.0);
            let warn_msg_location = point2(subheading_x, warn_msg_y);
            builder.facet_at_location(warn_facet, warn_msg_location);

            // Proceed message
            let proceed_facet = TextFacet::with_options(
                face.clone(),
                menu::CONST_WARN_PROCEED,
                subheading_size,
                text_options,
            );
            let proceed_msg_y = warn_msg_y + (subheading_size * 2.0);
            let proceed_msg_location = point2(subheading_x, proceed_msg_y);
            builder.facet_at_location(proceed_facet, proceed_msg_location);

            // Render buttons further down for warning screen
            render_buttons_vec(
                builder,
                menu_state_machine,
                target_size,
                proceed_msg_location,
                face,
                text_size,
            );
        }
        MenuState::Progress => {
            // progress message
            text_size = target_size.width.min(target_size.height) / 25.0;
            let progress_facet = TextFacet::with_options(
                face.clone(),
                &menu_state_machine.get_error_msg(),
                text_size,
                text_options,
            );
            let progress_msg_y = subheading_location.y + (text_size * 5.0);
            let progress_msg_location = point2(subheading_x, progress_msg_y);
            builder.facet_at_location(progress_facet, progress_msg_location);
        }
        MenuState::Error => {
            // Render body
            let error_facet = TextFacet::with_options(
                face.clone(),
                &menu_state_machine.get_error_msg(),
                subheading_size,
                text_options,
            );
            let err_msg_y = subheading_location.y + (subheading_size * 2.0);
            let err_msg_location = point2(subheading_x, err_msg_y);
            builder.facet_at_location(error_facet, err_msg_location);

            // Ask to restart
            let restart_facet = TextFacet::with_options(
                face.clone(),
                menu::CONST_ERR_RESTART,
                subheading_size,
                text_options,
            );
            let restart_msg_y = err_msg_y + (subheading_size * 2.0);
            let restart_msg_location = point2(subheading_x, restart_msg_y);
            builder.facet_at_location(restart_facet, restart_msg_location);
        }
    }
}

fn render_buttons_vec(
    builder: &mut SceneBuilder,
    menu_state_machine: &mut MenuStateMachine,
    target_size: Size,
    heading_location: Point,
    face: &FontFace,
    text_size: f32,
) {
    let menu_button_x: f32 = target_size.width * 0.2;
    let menu_button_y: f32 = heading_location.y + target_size.width.min(target_size.height) * 0.2;
    let menu_button_spacer: f32 = text_size * 1.5;

    let menu_buttons = menu_state_machine.get_buttons();

    let mut button_spacer: f32 = 0.0;

    for button in menu_buttons.iter() {
        let mut text_options = TextFacetOptions {
            horizontal_alignment: TextHorizontalAlignment::Left,
            ..TextFacetOptions::default()
        };

        if button.is_selected() {
            text_options.color = Color::white();
        };

        let new_menu_button =
            TextFacet::with_options(face.clone(), &button.get_text(), text_size, text_options);
        builder.facet_at_location(
            new_menu_button,
            point2(menu_button_x, menu_button_y + button_spacer),
        );

        button_spacer += menu_button_spacer;
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
