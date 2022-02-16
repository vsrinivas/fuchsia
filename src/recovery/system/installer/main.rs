// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use argh::FromArgs;
use carnelian::{
    app::Config,
    color::Color,
    drawing::{load_font, DisplayRotation, FontFace},
    input, make_message,
    render::{rive::load_rive, Context as RenderContext},
    scene::{
        facets::{RiveFacet, TextFacet, TextFacetOptions, TextHorizontalAlignment},
        scene::{Scene, SceneBuilder},
    },
    App, AppAssistant, AppAssistantPtr, AppSender, AssistantCreatorFunc, LocalBoxFuture,
    MessageTarget, Point, Size, ViewAssistant, ViewAssistantContext, ViewAssistantPtr, ViewKey,
};
use euclid::{point2, size2};
use fuchsia_async::{self as fasync};
use fuchsia_zircon::Event;
use rive_rs::{self as rive};
use std::path::PathBuf;

use fuchsia_zircon as zx;
use std::io::{self, Write};

mod menu;
use menu::{Key, MenuButtonType, MenuEvent, MenuState, MenuStateMachine};

pub mod installer;
use installer::{
    find_install_source, get_block_devices, get_bootloader_type, paver_connect,
    set_active_configuration, BootloaderType,
};

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
    GotInstallSource(String),
    GotBootloaderType(BootloaderType),
    GotInstallDestinations(Vec<String>),
    ProgressUpdate(String),
}

/// Installer
#[derive(Debug, FromArgs)]
#[argh(name = "recovery")]
struct Args {
    /// rotate
    #[argh(option)]
    rotation: Option<DisplayRotation>,
}
#[derive(Clone, Debug, PartialEq)]
struct InstallationPaths {
    install_source: String,
    install_target: String,
    bootloader_type: Option<BootloaderType>,
    install_destinations: Vec<String>,
}

impl InstallationPaths {
    pub fn new() -> InstallationPaths {
        InstallationPaths {
            install_source: String::new(),
            install_target: String::new(),
            bootloader_type: None,
            install_destinations: Vec::new(),
        }
    }
}

struct InstallerAppAssistant {
    app_sender: AppSender,
    display_rotation: DisplayRotation,
}

impl InstallerAppAssistant {
    fn new(app_sender: AppSender) -> Self {
        let args: Args = argh::from_env();
        Self { app_sender, display_rotation: args.rotation.unwrap_or(DisplayRotation::Deg0) }
    }
}

impl AppAssistant for InstallerAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant(&mut self, view_key: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let file = load_rive(LOGO_IMAGE_PATH).ok();
        Ok(Box::new(InstallerViewAssistant::new(
            &self.app_sender,
            view_key,
            file,
            INSTALLER_HEADLINE,
        )?))
    }

    fn filter_config(&mut self, config: &mut Config) {
        config.display_rotation = self.display_rotation;
    }
}

struct RenderResources {
    scene: Scene,
}

impl RenderResources {
    fn new(
        _render_context: &mut RenderContext,
        file: &Option<rive::File>,
        target_size: Size,
        heading: &str,
        face: &FontFace,
        menu_state_machine: &mut MenuStateMachine,
    ) -> Self {
        let min_dimension = target_size.width.min(target_size.height);
        let logo_edge = min_dimension * 0.24;
        let text_size = min_dimension / 10.0;
        let top_margin = 0.255;

        // Set background colour
        let bg_color = match menu_state_machine.get_state() {
            MenuState::Warning => WARN_BG_COLOR,
            MenuState::Error => WARN_BG_COLOR,
            _ => BG_COLOR,
        };

        let mut builder = SceneBuilder::new().background_color(bg_color).round_scene_corners(true);

        let logo_size: Size = size2(logo_edge, logo_edge);
        // Calculate position for the logo image
        let logo_position = {
            let x = target_size.width * 0.8;
            let y = target_size.height * 0.7;
            point2(x, y)
        };

        if let Some(file) = file {
            builder.facet_at_location(
                Box::new(
                    RiveFacet::new_from_file(logo_size, &file, None).expect("facet_from_file"),
                ),
                logo_position,
            );
        }
        let heading_text_location =
            point2(target_size.width / 2.0, top_margin + (target_size.height * 0.05));
        builder.text(
            face.clone(),
            &heading,
            text_size,
            heading_text_location,
            TextFacetOptions {
                horizontal_alignment: TextHorizontalAlignment::Center,
                color: HEADING_COLOR,
                ..TextFacetOptions::default()
            },
        );

        // Build menu
        menu_builder(&mut builder, menu_state_machine, target_size, heading_text_location, face);

        Self { scene: builder.build() }
    }
}

struct InstallerViewAssistant {
    face: FontFace,
    heading: &'static str,
    menu_state_machine: MenuStateMachine,
    installation_paths: InstallationPaths,
    app_sender: AppSender,
    view_key: ViewKey,
    file: Option<rive::File>,
    render_resources: Option<RenderResources>,
}

impl InstallerViewAssistant {
    fn new(
        app_sender: &AppSender,
        view_key: ViewKey,
        file: Option<rive::File>,
        heading: &'static str,
    ) -> Result<InstallerViewAssistant, Error> {
        InstallerViewAssistant::setup(app_sender, view_key)?;

        let face = load_font(PathBuf::from("/pkg/data/fonts/Roboto-Regular.ttf"))?;

        Ok(InstallerViewAssistant {
            face,
            heading: heading,
            menu_state_machine: MenuStateMachine::new(),
            installation_paths: InstallationPaths::new(),
            app_sender: app_sender.clone(),
            view_key: 0,
            file,
            render_resources: None,
        })
    }

    fn setup(_: &AppSender, _: ViewKey) -> Result<(), Error> {
        Ok(())
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
                    MenuButtonType::Disk => {
                        // Disk was selected as installation target
                        self.installation_paths.install_target =
                            self.menu_state_machine.get_selected_button_text();
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
                self.installation_paths.install_source = install_source_path.clone();
            }
            InstallerMessages::GotBootloaderType(bootloader_type) => {
                self.installation_paths.bootloader_type = Some(bootloader_type.clone());
            }
            InstallerMessages::GotInstallDestinations(destinations) => {
                self.installation_paths.install_destinations = destinations.to_vec();
                // Send disks to menu
                self.menu_state_machine
                    .handle_event(MenuEvent::GotBlockDevices(destinations.to_vec()));
            }
            InstallerMessages::ProgressUpdate(string) => {
                self.menu_state_machine.handle_event(MenuEvent::ProgressUpdate(string.clone()));
            }
        }

        // Render menu changes
        self.render_resources = None;
        self.app_sender.request_render(self.view_key);
    }
}

impl ViewAssistant for InstallerViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        self.view_key = context.key;
        Ok(())
    }

    fn render(
        &mut self,
        _render_context: &mut RenderContext,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        // Emulate the size that Carnelian passes when the display is rotated
        let target_size = context.size;

        if self.render_resources.is_none() {
            self.render_resources = Some(RenderResources::new(
                _render_context,
                &self.file,
                target_size,
                self.heading,
                &self.face,
                &mut self.menu_state_machine,
            ));
        }

        let render_resources = self.render_resources.as_mut().unwrap();
        render_resources.scene.render(_render_context, ready_event, context)?;
        context.request_render();
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

fn make_app_assistant_fut(
    app_sender: &AppSender,
) -> LocalBoxFuture<'_, Result<AppAssistantPtr, Error>> {
    let f = async move {
        let assistant = Box::new(InstallerAppAssistant::new(app_sender.clone()));
        Ok::<AppAssistantPtr, Error>(assistant)
    };
    Box::pin(f)
}

fn make_app_assistant() -> AssistantCreatorFunc {
    Box::new(make_app_assistant_fut)
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
    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::GotBootloaderType(bootloader_type)),
    );

    // Find the location of the installer
    let install_source =
        find_install_source(block_devices.iter().map(|(part, _)| part).collect(), bootloader_type)
            .await?;

    // Send got installer messgae
    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::GotInstallSource(install_source.clone())),
    );

    // Make list of available destinations for installation
    let mut destinations = Vec::new();
    for block_device in block_devices.iter() {
        if &block_device.0 != install_source {
            destinations.push(block_device.0.clone());
        }
    }

    // Send error if no destinations found
    if destinations.len() <= 0 {
        return Err(anyhow!("Found no block devices for installation."));
    };

    // Else end destinations
    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::GotInstallDestinations(destinations.clone())),
    );

    Ok(())
}

async fn setup_installation_paths(app_sender: AppSender, view_key: ViewKey) {
    match get_installation_paths(app_sender.clone(), view_key).await {
        Ok(_install_source) => {
            println!("Found installer & block devices ");
        }
        Err(e) => {
            // Send error
            app_sender.clone().queue_message(
                MessageTarget::View(view_key),
                make_message(InstallerMessages::Error(e.to_string())),
            );
            println!("ERROR getting install target: {}", e);
        }
    };
}

async fn fuchsia_install(
    app_sender: AppSender,
    view_key: ViewKey,
    installation_paths: InstallationPaths,
) {
    let block_device_path = installation_paths.install_target.clone();

    // Check that block device path is OK
    if !block_device_path.starts_with("/dev/") {
        app_sender.clone().queue_message(
            MessageTarget::View(view_key),
            make_message(InstallerMessages::Error(String::from("Invalid block device path!"))),
        );
    } else {
        // Execute install
        match do_install(app_sender.clone(), view_key, installation_paths).await {
            Ok(_) => {
                print!("INSTALL SUCCESS");
                app_sender.clone().queue_message(
                    MessageTarget::View(view_key),
                    make_message(InstallerMessages::ProgressUpdate(String::from(
                        "Success! Please restart your computer",
                    ))),
                );
            }
            Err(e) => {
                print!("ERROR INSTALLING: {}", e);
                app_sender.clone().queue_message(
                    MessageTarget::View(view_key),
                    make_message(InstallerMessages::Error(String::from(format!(
                        "Error {}, please restart",
                        e
                    )))),
                );
            }
        }
    }
}

async fn do_install(
    app_sender: AppSender,
    view_key: ViewKey,
    installation_paths: InstallationPaths,
) -> Result<(), Error> {
    let block_device_path = installation_paths.install_target;
    let install_source = installation_paths.install_source;
    let bootloader_type = installation_paths.bootloader_type.unwrap();

    let (paver, data_sink) =
        paver_connect(&block_device_path).context("Could not contact paver")?;

    println!("Wiping old partition tables...");
    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from(
            "Wiping old partition tables...",
        ))),
    );
    data_sink.wipe_partition_tables().await?;
    println!("Initializing Fuchsia partition tables...");
    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from(
            "Initializing Fuchsia partition tables...",
        ))),
    );
    data_sink.initialize_partition_tables().await?;
    println!("Success.");

    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from("Getting source partitions"))),
    );
    let to_install = Partition::get_partitions(&install_source, bootloader_type)
        .await
        .context("Getting source partitions")?;

    let num_partitions = to_install.len();
    let mut current_partition = 1;
    for part in to_install {
        app_sender.clone().queue_message(
            MessageTarget::View(view_key),
            make_message(InstallerMessages::ProgressUpdate(String::from(format!(
                "paving partition {} of {}",
                current_partition, num_partitions
            )))),
        );
        current_partition += 1;

        print!("{:?}... ", part);
        io::stdout().flush()?;
        if part.pave(&data_sink).await.is_err() {
            println!("Failed");
        } else {
            println!("OK");
            if part.is_ab() {
                print!("{:?} [-B]... ", part);
                io::stdout().flush()?;
                if part.pave_b(&data_sink).await.is_err() {
                    println!("Failed");
                } else {
                    println!("OK");
                }
            }
        }
    }

    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from("Flushing Partitions"))),
    );
    zx::Status::ok(data_sink.flush().await.context("Sending flush")?)
        .context("Flushing partitions")?;

    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from(
            "Setting active configuration for the new system",
        ))),
    );
    set_active_configuration(&paver)
        .await
        .context("Setting active configuration for the new system")?;

    app_sender.clone().queue_message(
        MessageTarget::View(view_key),
        make_message(InstallerMessages::ProgressUpdate(String::from("Configureation complete!!"))),
    );

    Ok(())
}

fn main() -> Result<(), Error> {
    println!("workstation installer: started.");
    App::run(make_app_assistant())
}
