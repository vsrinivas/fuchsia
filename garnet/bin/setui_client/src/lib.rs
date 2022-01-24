// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use argh::FromArgs;
use fidl_fuchsia_settings::{ConfigurationInterfaces, LightState, LightValue, Theme};
use fuchsia_component::client::connect_to_protocol;
use regex::Regex;
use std::num::ParseIntError;

pub mod accessibility;
pub mod audio;
pub mod display;
pub mod do_not_disturb;
pub mod factory_reset;
pub mod input;
pub mod intl;
pub mod keyboard;
pub mod light;
pub mod night_mode;
pub mod privacy;
pub mod setup;
pub mod utils;
pub mod volume_policy;

// SettingClient exercises the functionality found in SetUI service. Currently,
// action parameters are specified at as individual arguments, but the goal is
// to eventually parse details from a JSON file input.
#[derive(FromArgs, Debug)]
/// Get or set setting values. Pass --help to each subcommand for additional info. Calling the
/// subcommands without any parameters treats it as a get.
pub struct SettingClient {
    #[argh(subcommand)]
    pub nested: SettingClientSubcommands,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand)]
pub enum SettingClientSubcommands {
    // Operations that use the new interfaces.
    Accessibility(Accessibility),
    Audio(Audio),
    Display(Display),
    DoNotDisturb(DoNotDisturb),
    FactoryReset(FactoryReset),
    Input(Input),
    // TODO(fxbug.dev/65686): Move back into input when the clients are migrated over.
    // TODO(fxbug.dev/66186): Support multiple input devices to be set.
    // For simplicity, currently only supports setting one input device at a time.
    Input2(Input),
    Intl(Intl),
    Keyboard(Keyboard),
    Light(LightGroup),
    NightMode(NightMode),
    Privacy(Privacy),
    Setup(Setup),
    VolumePolicy(VolumePolicy),
}

#[derive(FromArgs, Debug, Clone, Default)]
#[argh(subcommand, name = "accessibility")]
/// pass no options to get current settings
pub struct Accessibility {
    #[argh(option, short = 'a')]
    /// when set to 'true', will turn on an audio track for videos that includes a description
    /// of what is occurring in the video
    pub audio_description: Option<bool>,

    #[argh(option, short = 's')]
    /// when set to 'true', will read aloud elements of the screen selected by the user
    pub screen_reader: Option<bool>,

    #[argh(option, short = 'i')]
    /// when set to 'true', will invert the colors on the screen
    pub color_inversion: Option<bool>,

    #[argh(option, short = 'm')]
    /// when set to 'true', will interpret triple-taps on the touchscreen as a command to zoom in
    pub enable_magnification: Option<bool>,

    #[argh(option, short = 'c', from_str_fn(str_to_color_blindness_type))]
    /// configures the type of color-blindness to correct for. Valid options are none, protanomaly,
    /// deuteranomaly, and tritanomaly
    pub color_correction: Option<fidl_fuchsia_settings::ColorBlindnessType>,

    #[argh(subcommand)]
    pub caption_options: Option<CaptionCommands>,
}

#[derive(FromArgs, Debug, Clone)]
#[argh(subcommand)]
pub enum CaptionCommands {
    CaptionOptions(CaptionOptions),
}

#[derive(FromArgs, Debug, Clone)]
#[argh(subcommand, name = "caption_options")]
/// configuration for which sources get closed caption and how they look
pub struct CaptionOptions {
    #[argh(option, short = 'm')]
    /// enable closed captions for media sources of audio
    pub for_media: Option<bool>,

    #[argh(option, short = 't')]
    /// enable closed captions for Text-To-Speech sources of audio
    pub for_tts: Option<bool>,

    #[argh(option, short = 'w', from_str_fn(str_to_color))]
    /// border color used around the closed captions window. Valid options are red, green, and blue
    pub window_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    #[argh(option, short = 'b', from_str_fn(str_to_color))]
    /// border color used around the closed captions window. Valid options are red, green, and blue
    pub background_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    // CaptionFontStyle options below
    #[argh(option, short = 'f', from_str_fn(str_to_font_family))]
    /// font family for captions as specified by 47 CFR §79.102(k). Valid options are unknown,
    /// monospaced_serif, proportional_serif, monospaced_sans_serif, proportional_sans_serif,
    /// casual, cursive, and small_capitals
    pub font_family: Option<fidl_fuchsia_settings::CaptionFontFamily>,

    #[argh(option, short = 'c', from_str_fn(str_to_color))]
    /// color of the closed caption text. Valid options are red, green, and blue
    pub font_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    #[argh(option, short = 'r')]
    /// size of closed captions text relative to the default captions size, specified in the
    /// range [0.5, 2] as per 47 CFR §79.103(c)(4)
    pub relative_size: Option<f32>,

    #[argh(option, short = 'e', from_str_fn(str_to_edge_style))]
    /// edge style for fonts as specified in 47 CFR §79.103(c)(7). Valid options are none,
    /// drop_shadow, raised, depressed, outline
    pub char_edge_style: Option<fidl_fuchsia_settings::EdgeStyle>,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "audio")]
/// get or set audio settings
pub struct Audio {
    // AudioStreams
    /// which stream should be modified. Valid options are background, media, interruption,
    /// system_agent, and communication
    #[argh(option, short = 't', from_str_fn(str_to_audio_stream))]
    stream: Option<fidl_fuchsia_media::AudioRenderUsage>,

    /// which source is changing the stream. Valid options are user, system, and
    /// system_with_feedback
    #[argh(option, short = 's', from_str_fn(str_to_audio_source))]
    source: Option<fidl_fuchsia_settings::AudioStreamSettingSource>,

    // UserVolume
    /// the volume level specified as a float in the range [0, 1]
    #[argh(option, short = 'l')]
    level: Option<f32>,

    /// whether or not the volume is muted
    #[argh(option, short = 'v')]
    volume_muted: Option<bool>,

    // AudioInput
    /// whether or not the input, e.g. microphone, is muted
    #[argh(option, short = 'm')]
    input_muted: Option<bool>,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "display")]
/// get or set display settings
pub struct Display {
    /// the brightness value specified as a float in the range [0, 1]
    #[argh(option, short = 'b')]
    brightness: Option<f32>,

    /// the brightness values used to control auto brightness as a float in the range [0, 1]
    #[argh(option, short = 'o')]
    auto_brightness_level: Option<f32>,

    /// when set to 'true', enables auto brightness
    #[argh(option, short = 'a')]
    auto_brightness: Option<bool>,

    /// when passed, reads values from the light sensor rather than display brightness
    #[argh(switch, short = 'l')]
    light_sensor: bool,

    /// which low light mode setting to enable. Valid options are enable, disable, and
    /// disable_immediately
    #[argh(option, short = 'm', from_str_fn(str_to_low_light_mode))]
    low_light_mode: Option<fidl_fuchsia_settings::LowLightMode>,

    /// which theme to set for the device. Valid options are default, dark, light, darkauto, and
    /// lightauto
    #[argh(option, short = 't', from_str_fn(str_to_theme))]
    theme: Option<fidl_fuchsia_settings::Theme>,

    /// when set to 'true' the screen is enabled
    #[argh(option, short = 's')]
    screen_enabled: Option<bool>,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "do_not_disturb")]
/// get or set DnD settings
pub struct DoNotDisturb {
    /// when set to 'true', allows the device to enter do not disturb mode
    #[argh(option, short = 'u')]
    user_dnd: Option<bool>,

    /// when set to 'true', forces the device into do not disturb mode
    #[argh(option, short = 'n')]
    night_mode_dnd: Option<bool>,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "factory_reset")]
/// get or set factory reset settings
pub struct FactoryReset {
    /// when set to 'true', factory reset can be performed on the device
    #[argh(option, short = 'l')]
    is_local_reset_allowed: Option<bool>,
}

#[derive(FromArgs, Debug, Clone)]
#[argh(subcommand, name = "input_device")]
/// get or set input settings
pub struct Input {
    #[argh(option, short = 't', from_str_fn(str_to_device_type))]
    /// the type of input device. Valid options are camera and microphone
    device_type: Option<fidl_fuchsia_settings::DeviceType>,

    #[argh(option, short = 'n', long = "name")]
    /// the name of the device. Must be unique within a device type
    device_name: Option<String>,

    #[argh(option, short = 's', long = "state", from_str_fn(str_to_device_state))]
    /// the device state flags, pass a comma separated string of the values available, active,
    /// muted, disabled and error. E.g. "-s available,active"
    device_state: Option<fidl_fuchsia_settings::DeviceState>,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "intl")]
/// get or set internationalization settings
pub struct Intl {
    /// a valid timezone matching the data available at https://www.iana.org/time-zones
    #[argh(option, short = 'z', from_str_fn(str_to_time_zone))]
    time_zone: Option<fidl_fuchsia_intl::TimeZoneId>,

    #[argh(option, short = 'u', from_str_fn(str_to_temperature_unit))]
    /// the unit to use for temperature. Valid options are celsius and fahrenheit
    temperature_unit: Option<fidl_fuchsia_intl::TemperatureUnit>,

    #[argh(option, short = 'l', from_str_fn(str_to_locale))]
    /// list of locales, separated by spaces, formatted by Unicode BCP-47 Locale Identifier, e.g.
    /// en-us
    locales: Vec<fidl_fuchsia_intl::LocaleId>,

    /// the hour cycle to use. Valid options are h11 for 12-hour clock with 0:10 am after midnight,
    /// h12 for 12-hour clock with 12:10am after midnight, h23 for 24-hour clock with 0:10 after
    /// midnight, and h24 for 24-hour clock with 24:10 after midnight
    #[argh(option, short = 'h', from_str_fn(str_to_hour_cycle))]
    hour_cycle: Option<fidl_fuchsia_settings::HourCycle>,

    #[argh(switch)]
    /// if set, this flag will set locales as an empty list. Overrides the locales arguments
    clear_locales: bool,
}

#[derive(FromArgs, Debug, PartialEq, Clone, Copy)]
#[argh(subcommand, name = "keyboard")]
/// get or set keyboard settings
pub struct Keyboard {
    /// keymap selection for the keyboard. Valid options are UsQwerty, FrAzerty, and UsDvorak.
    #[argh(option, short = 'k', from_str_fn(str_to_keymap))]
    keymap: Option<fidl_fuchsia_input::KeymapId>,

    /// delay value of autorepeat values for the keyboard. Values should be a positive integer plus
    /// an SI time unit. Valid units are s, ms. If this value and autorepeat_period are zero, the
    /// autorepeat field of KeyboardSettings will be cleaned as None.
    #[argh(option, short = 'd', from_str_fn(str_to_duration))]
    autorepeat_delay: Option<i64>,

    /// period value of autorepeat values for the keyboard. Values should be a positive integer plus
    /// an SI time unit. Valid units are s, ms. If this value and autorepeat_delay are zero, the
    /// autorepeat field of KeyboardSettings will be cleaned as None.
    #[argh(option, short = 'p', from_str_fn(str_to_duration))]
    autorepeat_period: Option<i64>,
}

#[derive(FromArgs, Debug, Clone)]
#[argh(subcommand, name = "light")]
/// get or set light settings
pub struct LightGroup {
    #[argh(option, short = 'n')]
    /// name of a light group to set values for. Required if setting the value of a light group
    pub name: Option<String>,

    #[argh(option, short = 's')]
    /// repeated parameter for a list of simple on/off values to set for a light group.
    pub simple: Vec<bool>,

    #[argh(option, short = 'b')]
    /// repeated parameter for a list of floating point brightness values in the range [0, 1] for a
    /// light group
    pub brightness: Vec<f64>,

    #[argh(option, short = 'r', from_str_fn(str_to_rgb))]
    /// repeated parameter for a list of RGB values to set for a light group. Values should be in
    /// the range [0, 1] and specified as a comma-separated list of the red, green, and blue
    /// components. Ex. 0.1,0.4,0.23
    pub rgb: Vec<fidl_fuchsia_ui_types::ColorRgb>,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "night_mode")]
/// get or set night mode settings
pub struct NightMode {
    /// when 'true', enables night mode
    #[argh(option, short = 'n')]
    night_mode_enabled: Option<bool>,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "privacy")]
/// get or set privacy settings
pub struct Privacy {
    /// when 'true', is considered to be user giving consent to have their data shared with product
    /// owner, e.g. for metrics collection and crash reporting
    #[argh(option, short = 'u')]
    user_data_sharing_consent: Option<bool>,
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "setup")]
/// get or set setup settings
pub struct Setup {
    /// a supported group of interfaces, specified as a comma-delimited string of the valid values
    /// eth and wifi, e.g. "-i eth,wifi" or "-i wifi"
    #[argh(option, short = 'i', long = "interfaces", from_str_fn(str_to_interfaces))]
    configuration_interfaces: Option<ConfigurationInterfaces>,
}
#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "volume_policy")]
/// configure volume policy
// Reads and modifies volume policies that affect the behavior of the fuchsia.settings.audio.
// To list the policies, run the subcommand without any arguments.
pub struct VolumePolicy {
    /// adds a policy transform.
    #[argh(subcommand)]
    add: Option<VolumePolicyCommands>,

    /// removes a policy transform by its policy ID.
    #[argh(option, short = 'r')]
    remove: Option<u32>,
}

#[derive(FromArgs, Debug, Clone)]
#[argh(subcommand)]
pub enum VolumePolicyCommands {
    AddPolicy(VolumePolicyOptions),
}

#[derive(FromArgs, Debug, Clone)]
#[argh(subcommand, name = "add")]
/// adds a new volume policy
pub struct VolumePolicyOptions {
    /// target stream to apply the policy transform to. Valid options are background, media,
    /// interruption, system_agent, and communication
    #[argh(positional, from_str_fn(str_to_audio_stream))]
    pub target: fidl_fuchsia_media::AudioRenderUsage,

    /// the minimum allowed value for the target
    #[argh(option)]
    pub min: Option<f32>,

    /// the maximum allowed value for the target
    #[argh(option)]
    pub max: Option<f32>,
}

impl Into<Vec<LightState>> for LightGroup {
    fn into(self) -> Vec<LightState> {
        if self.simple.len() > 0 {
            return self
                .simple
                .clone()
                .into_iter()
                .map(|val| LightState { value: Some(LightValue::On(val)), ..LightState::EMPTY })
                .collect::<Vec<_>>();
        }

        if self.brightness.len() > 0 {
            return self
                .brightness
                .clone()
                .into_iter()
                .map(|val| LightState {
                    value: Some(LightValue::Brightness(val)),
                    ..LightState::EMPTY
                })
                .collect::<Vec<_>>();
        }

        if self.rgb.len() > 0 {
            return self
                .rgb
                .clone()
                .into_iter()
                .map(|val| LightState { value: Some(LightValue::Color(val)), ..LightState::EMPTY })
                .collect::<Vec<_>>();
        }

        return Vec::new();
    }
}

pub async fn run_command(command: SettingClient) -> Result<(), Error> {
    match command.nested {
        SettingClientSubcommands::Display(Display {
            brightness,
            auto_brightness_level,
            auto_brightness,
            light_sensor,
            low_light_mode,
            theme,
            screen_enabled,
        }) => {
            let display_service = connect_to_protocol::<fidl_fuchsia_settings::DisplayMarker>()
                .context("Failed to connect to display service")?;
            utils::handle_mixed_result(
                "Display",
                display::command(
                    display_service,
                    brightness,
                    auto_brightness,
                    auto_brightness_level,
                    light_sensor,
                    low_light_mode,
                    theme,
                    screen_enabled,
                )
                .await,
            )
            .await?;
        }
        SettingClientSubcommands::DoNotDisturb(DoNotDisturb { user_dnd, night_mode_dnd }) => {
            let dnd_service = connect_to_protocol::<fidl_fuchsia_settings::DoNotDisturbMarker>()
                .context("Failed to connect to do_not_disturb service")?;
            utils::handle_mixed_result(
                "DoNoDisturb",
                do_not_disturb::command(dnd_service, user_dnd, night_mode_dnd).await,
            )
            .await?;
        }
        SettingClientSubcommands::FactoryReset(FactoryReset { is_local_reset_allowed }) => {
            let factory_reset_service =
                connect_to_protocol::<fidl_fuchsia_settings::FactoryResetMarker>()
                    .context("Failed to connect to factory_reset service")?;
            utils::handle_mixed_result(
                "FactoryReset",
                factory_reset::command(factory_reset_service, is_local_reset_allowed).await,
            )
            .await?;
        }
        SettingClientSubcommands::Intl(Intl {
            time_zone,
            temperature_unit,
            locales,
            hour_cycle,
            clear_locales,
        }) => {
            let intl_service = connect_to_protocol::<fidl_fuchsia_settings::IntlMarker>()
                .context("Failed to connect to intl service")?;
            utils::handle_mixed_result(
                "Intl",
                intl::command(
                    intl_service,
                    time_zone,
                    temperature_unit,
                    locales,
                    hour_cycle,
                    clear_locales,
                )
                .await,
            )
            .await?;
        }
        SettingClientSubcommands::Light(light_group) => {
            let light_mode_service = connect_to_protocol::<fidl_fuchsia_settings::LightMarker>()
                .context("Failed to connect to light service")?;
            utils::handle_mixed_result(
                "Light",
                light::command(light_mode_service, light_group).await,
            )
            .await?;
        }
        SettingClientSubcommands::NightMode(NightMode { night_mode_enabled }) => {
            let night_mode_service =
                connect_to_protocol::<fidl_fuchsia_settings::NightModeMarker>()
                    .context("Failed to connect to night mode service")?;
            utils::handle_mixed_result(
                "NightMode",
                night_mode::command(night_mode_service, night_mode_enabled).await,
            )
            .await?;
        }
        SettingClientSubcommands::Accessibility(accessibility_options) => {
            let accessibility_service =
                connect_to_protocol::<fidl_fuchsia_settings::AccessibilityMarker>()
                    .context("Failed to connect to accessibility service")?;

            utils::handle_mixed_result(
                "Accessibility",
                accessibility::command(accessibility_service, accessibility_options).await,
            )
            .await?;
        }
        SettingClientSubcommands::Privacy(Privacy { user_data_sharing_consent }) => {
            let privacy_service = connect_to_protocol::<fidl_fuchsia_settings::PrivacyMarker>()
                .context("Failed to connect to privacy service")?;
            utils::handle_mixed_result(
                "Privacy",
                privacy::command(privacy_service, user_data_sharing_consent).await,
            )
            .await?;
        }
        SettingClientSubcommands::Audio(Audio {
            stream,
            source,
            level,
            volume_muted,
            input_muted,
        }) => {
            let audio_service = connect_to_protocol::<fidl_fuchsia_settings::AudioMarker>()
                .context("Failed to connect to audio service")?;
            utils::handle_mixed_result(
                "Audio",
                audio::command(audio_service, stream, source, level, volume_muted, input_muted)
                    .await,
            )
            .await?;
        }
        SettingClientSubcommands::Input(Input { device_type, device_name, device_state }) => {
            let input_service = connect_to_protocol::<fidl_fuchsia_settings::InputMarker>()
                .context("Failed to connect to input service")?;
            utils::handle_mixed_result(
                "Input",
                input::command(input_service, device_type, device_name, device_state).await,
            )
            .await?;
        }
        // TODO(fxbug.dev/65686): Remove when clients are ported to new interface.
        SettingClientSubcommands::Input2(Input { device_type, device_name, device_state }) => {
            let input_service = connect_to_protocol::<fidl_fuchsia_settings::InputMarker>()
                .context("Failed to connect to input2 service")?;
            utils::handle_mixed_result(
                "Input2",
                input::command(input_service, device_type, device_name, device_state).await,
            )
            .await?;
        }
        SettingClientSubcommands::Setup(Setup { configuration_interfaces }) => {
            let setup_service = connect_to_protocol::<fidl_fuchsia_settings::SetupMarker>()
                .context("Failed to connect to setup service")?;
            utils::handle_mixed_result(
                "Setup",
                setup::command(setup_service, configuration_interfaces).await,
            )
            .await?;
        }
        SettingClientSubcommands::VolumePolicy(VolumePolicy { add, remove }) => {
            let setup_service =
                connect_to_protocol::<fidl_fuchsia_settings_policy::VolumePolicyControllerMarker>()
                    .context("Failed to connect to volume policy service")?;
            utils::handle_mixed_result(
                "Volume policy",
                volume_policy::command(setup_service, add, remove).await,
            )
            .await?;
        }
        SettingClientSubcommands::Keyboard(keyboard) => {
            let keyboard_service = connect_to_protocol::<fidl_fuchsia_settings::KeyboardMarker>()
                .context("Failed to connect to keyboard service")?;
            utils::handle_mixed_result(
                "Keyboard",
                keyboard::command(keyboard_service, keyboard).await,
            )
            .await?;
        }
    }
    Ok(())
}

fn str_to_time_zone(src: &str) -> Result<fidl_fuchsia_intl::TimeZoneId, String> {
    Ok(fidl_fuchsia_intl::TimeZoneId { id: src.to_string() })
}

fn str_to_locale(src: &str) -> Result<fidl_fuchsia_intl::LocaleId, String> {
    Ok(fidl_fuchsia_intl::LocaleId { id: src.to_string() })
}

fn str_to_device_type(src: &str) -> Result<fidl_fuchsia_settings::DeviceType, String> {
    let device_type = src.to_lowercase();
    match device_type.as_ref() {
        "microphone" | "m" => Ok(fidl_fuchsia_settings::DeviceType::Microphone),
        "camera" | "c" => Ok(fidl_fuchsia_settings::DeviceType::Camera),
        _ => Err(String::from("Unidentified device type")),
    }
}

fn str_to_device_state(src: &str) -> Result<fidl_fuchsia_settings::DeviceState, String> {
    use fidl_fuchsia_settings::ToggleStateFlags;

    Ok(fidl_fuchsia_settings::DeviceState {
        toggle_flags: Some(src.to_lowercase().split(",").fold(
            Ok(fidl_fuchsia_settings::ToggleStateFlags::empty()),
            |acc, flag| {
                acc.and_then(|acc| {
                    Ok(match flag {
                        "available" | "v" => ToggleStateFlags::AVAILABLE,
                        "active" | "a" => ToggleStateFlags::ACTIVE,
                        "muted" | "m" => ToggleStateFlags::MUTED,
                        "disabled" | "d" => ToggleStateFlags::DISABLED,
                        "error" | "e" => ToggleStateFlags::ERROR,
                        flag => {
                            return Err(format!("Unrecognized ToggleStateFlags value {:?}", flag))
                        }
                    } | acc)
                })
            },
        )?),
        ..fidl_fuchsia_settings::DeviceState::EMPTY
    })
}

fn str_to_low_light_mode(src: &str) -> Result<fidl_fuchsia_settings::LowLightMode, String> {
    match src {
        "enable" | "e" => Ok(fidl_fuchsia_settings::LowLightMode::Enable),
        "disable" | "d" => Ok(fidl_fuchsia_settings::LowLightMode::Disable),
        "disable_immediately" | "i" => Ok(fidl_fuchsia_settings::LowLightMode::DisableImmediately),
        _ => Err(String::from("Couldn't parse low light mode")),
    }
}

fn str_to_theme(src: &str) -> Result<fidl_fuchsia_settings::Theme, String> {
    match src {
        "default" => Ok(Theme {
            theme_type: Some(fidl_fuchsia_settings::ThemeType::Default),
            ..Theme::EMPTY
        }),
        "dark" => {
            Ok(Theme { theme_type: Some(fidl_fuchsia_settings::ThemeType::Dark), ..Theme::EMPTY })
        }
        "light" => {
            Ok(Theme { theme_type: Some(fidl_fuchsia_settings::ThemeType::Light), ..Theme::EMPTY })
        }
        "darkauto" => Ok(Theme {
            theme_type: Some(fidl_fuchsia_settings::ThemeType::Dark),
            theme_mode: Some(fidl_fuchsia_settings::ThemeMode::AUTO),
            ..Theme::EMPTY
        }),
        "lightauto" => Ok(Theme {
            theme_type: Some(fidl_fuchsia_settings::ThemeType::Light),
            theme_mode: Some(fidl_fuchsia_settings::ThemeMode::AUTO),
            ..Theme::EMPTY
        }),
        _ => Err(String::from("Couldn't parse theme.")),
    }
}

fn str_to_interfaces(src: &str) -> Result<ConfigurationInterfaces, String> {
    src.to_lowercase().split(",").fold(Ok(ConfigurationInterfaces::empty()), |acc, flag| {
        acc.and_then(|acc| {
            Ok(match flag {
                "eth" | "ethernet" => ConfigurationInterfaces::ETHERNET,
                "wireless" | "wifi" => ConfigurationInterfaces::WIFI,
                bad_ifc => return Err(format!("Unknown interface: {:?}", bad_ifc)),
            } | acc)
        })
    })
}

fn str_to_color(src: &str) -> Result<fidl_fuchsia_ui_types::ColorRgba, String> {
    Ok(match src.to_lowercase().as_str() {
        "red" | "r" => {
            fidl_fuchsia_ui_types::ColorRgba { red: 255.0, green: 0.0, blue: 0.0, alpha: 255.0 }
        }
        "green" | "g" => {
            fidl_fuchsia_ui_types::ColorRgba { red: 0.0, green: 2.055, blue: 0.0, alpha: 255.0 }
        }
        "blue" | "b" => {
            fidl_fuchsia_ui_types::ColorRgba { red: 0.0, green: 0.0, blue: 255.0, alpha: 255.0 }
        }
        _ => return Err(String::from("Couldn't parse color")),
    })
}

/// Converts a comma-separated string of RGB values into a fidl_fuchsia_ui_types::ColorRgb.
fn str_to_rgb(src: &str) -> Result<fidl_fuchsia_ui_types::ColorRgb, String> {
    let mut part_iter =
        src.split(',').map(|p| p.parse::<f32>().map_err(|_| "failed to parse color value"));

    let color = {
        let local_ref = &mut part_iter;
        color_from_parts(local_ref)
    };
    match (color, part_iter.next()) {
        (Some(Ok(color)), None) => Ok(color),
        (Some(Err(err)), _) => Err(err),
        _ => Err(String::from("wrong number of values")),
    }
}

fn color_from_parts<'a, T>(
    part_iter: &mut T,
) -> Option<Result<fidl_fuchsia_ui_types::ColorRgb, String>>
where
    T: Iterator<Item = Result<f32, &'a str>>,
{
    Some(Ok(fidl_fuchsia_ui_types::ColorRgb {
        red: match part_iter.next()? {
            Ok(c) => c,
            Err(e) => return Some(Err(e.to_string())),
        },
        green: match part_iter.next()? {
            Ok(c) => c,
            Err(e) => return Some(Err(e.to_string())),
        },
        blue: match part_iter.next()? {
            Ok(c) => c,
            Err(e) => return Some(Err(e.to_string())),
        },
    }))
}

fn str_to_font_family(src: &str) -> Result<fidl_fuchsia_settings::CaptionFontFamily, String> {
    Ok(match src.to_lowercase().as_str() {
        "unknown" => fidl_fuchsia_settings::CaptionFontFamily::Unknown,
        "monospaced_serif" => fidl_fuchsia_settings::CaptionFontFamily::MonospacedSerif,
        "proportional_serif" => fidl_fuchsia_settings::CaptionFontFamily::ProportionalSerif,
        "monospaced_sans_serif" => fidl_fuchsia_settings::CaptionFontFamily::MonospacedSansSerif,
        "proportional_sans_serif" => {
            fidl_fuchsia_settings::CaptionFontFamily::ProportionalSansSerif
        }
        "casual" => fidl_fuchsia_settings::CaptionFontFamily::Casual,
        "cursive" => fidl_fuchsia_settings::CaptionFontFamily::Cursive,
        "small_capitals" => fidl_fuchsia_settings::CaptionFontFamily::SmallCapitals,
        _ => return Err(String::from("Couldn't parse font family")),
    })
}

fn str_to_edge_style(src: &str) -> Result<fidl_fuchsia_settings::EdgeStyle, String> {
    Ok(match src.to_lowercase().as_str() {
        "none" => fidl_fuchsia_settings::EdgeStyle::None,
        "drop_shadow" => fidl_fuchsia_settings::EdgeStyle::DropShadow,
        "raised" => fidl_fuchsia_settings::EdgeStyle::Raised,
        "depressed" => fidl_fuchsia_settings::EdgeStyle::Depressed,
        "outline" => fidl_fuchsia_settings::EdgeStyle::Outline,
        _ => return Err(String::from("Couldn't parse edge style")),
    })
}

fn str_to_temperature_unit(src: &str) -> Result<fidl_fuchsia_intl::TemperatureUnit, String> {
    match src.to_lowercase().as_str() {
        "c" | "celsius" => Ok(fidl_fuchsia_intl::TemperatureUnit::Celsius),
        "f" | "fahrenheit" => Ok(fidl_fuchsia_intl::TemperatureUnit::Fahrenheit),
        _ => Err(String::from("Couldn't parse temperature")),
    }
}

fn str_to_hour_cycle(src: &str) -> Result<fidl_fuchsia_settings::HourCycle, String> {
    match src.to_lowercase().as_str() {
        "unknown" => Ok(fidl_fuchsia_settings::HourCycle::Unknown),
        "h11" => Ok(fidl_fuchsia_settings::HourCycle::H11),
        "h12" => Ok(fidl_fuchsia_settings::HourCycle::H12),
        "h23" => Ok(fidl_fuchsia_settings::HourCycle::H23),
        "h24" => Ok(fidl_fuchsia_settings::HourCycle::H24),
        _ => Err(String::from("Couldn't parse hour cycle")),
    }
}

fn str_to_color_blindness_type(
    src: &str,
) -> Result<fidl_fuchsia_settings::ColorBlindnessType, String> {
    match src.to_lowercase().as_str() {
        "none" | "n" => Ok(fidl_fuchsia_settings::ColorBlindnessType::None),
        "protanomaly" | "p" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Protanomaly),
        "deuteranomaly" | "d" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly),
        "tritanomaly" | "t" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly),
        _ => Err(String::from("Couldn't parse color blindness type")),
    }
}

fn str_to_audio_stream(src: &str) -> Result<fidl_fuchsia_media::AudioRenderUsage, String> {
    match src.to_lowercase().as_str() {
        "background" | "b" => Ok(fidl_fuchsia_media::AudioRenderUsage::Background),
        "media" | "m" => Ok(fidl_fuchsia_media::AudioRenderUsage::Media),
        "interruption" | "i" => Ok(fidl_fuchsia_media::AudioRenderUsage::Interruption),
        "system_agent" | "systemagent" | "system agent" | "s" => {
            Ok(fidl_fuchsia_media::AudioRenderUsage::SystemAgent)
        }
        "communication" | "c" => Ok(fidl_fuchsia_media::AudioRenderUsage::Communication),
        _ => Err(String::from("Couldn't parse audio stream type")),
    }
}

fn str_to_audio_source(
    src: &str,
) -> Result<fidl_fuchsia_settings::AudioStreamSettingSource, String> {
    match src.to_lowercase().as_str() {
        "user" | "u" => Ok(fidl_fuchsia_settings::AudioStreamSettingSource::User),
        "system" | "s" => Ok(fidl_fuchsia_settings::AudioStreamSettingSource::System),
        "system_with_feedback" | "f" => {
            Ok(fidl_fuchsia_settings::AudioStreamSettingSource::SystemWithFeedback)
        }
        _ => Err(String::from("Couldn't parse audio source type")),
    }
}

/// Converts a single string of keymap id value into a fidl_fuchsia_input::KeymapId.
fn str_to_keymap(src: &str) -> Result<fidl_fuchsia_input::KeymapId, String> {
    Ok(match src.to_lowercase().as_str() {
        "usqwerty" => fidl_fuchsia_input::KeymapId::UsQwerty,
        "frazerty" => fidl_fuchsia_input::KeymapId::FrAzerty,
        "usdvorak" => fidl_fuchsia_input::KeymapId::UsDvorak,
        _ => return Err(String::from("Couldn't parse keymap id.")),
    })
}

/// Converts a single string of number and unit into a number interpreting as nanoseconds.
fn str_to_duration(src: &str) -> Result<i64, String> {
    // This regex matches a string that starts with at least one digit, follows by zero or more
    // whitespace, and ends with zero or more letters. Digits and letters are captured.
    let re = Regex::new(r"^(\d+)\s*([A-Za-z]*)$").unwrap();
    let captures = re.captures(src).ok_or_else(|| {
        String::from("Invalid input, please pass in number and units as <123ms> or <0>.")
    })?;

    let num: i64 = captures[1].parse().map_err(|e: ParseIntError| e.to_string())?;
    let unit = captures[2].to_string();

    Ok(match unit.to_lowercase().as_str() {
        "s" | "second" | "seconds" => num * 1_000_000_000,
        "ms" | "millisecond" | "milliseconds" => num * 1_000_000,
        _ => {
            if unit.is_empty() && num == 0 {
                0
            } else {
                return Err(String::from("Couldn't parse duration, please specify a valid unit."));
            }
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Unit test for str_to_audio_stream.
    #[test]
    fn test_str_to_audio_stream() {
        println!("Running test_str_to_audio_stream");
        let test_cases = vec![
            "Background",
            "MEDIA",
            "interruption",
            "SYSTEM_AGENT",
            "SystemAgent",
            "system agent",
            "Communication",
            "unexpected_stream_type",
        ];
        let expected = vec![
            Ok(fidl_fuchsia_media::AudioRenderUsage::Background),
            Ok(fidl_fuchsia_media::AudioRenderUsage::Media),
            Ok(fidl_fuchsia_media::AudioRenderUsage::Interruption),
            Ok(fidl_fuchsia_media::AudioRenderUsage::SystemAgent),
            Ok(fidl_fuchsia_media::AudioRenderUsage::SystemAgent),
            Ok(fidl_fuchsia_media::AudioRenderUsage::SystemAgent),
            Ok(fidl_fuchsia_media::AudioRenderUsage::Communication),
            Err(String::from("Couldn't parse audio stream type")),
        ];
        let mut results = vec![];
        for test_case in test_cases {
            results.push(str_to_audio_stream(test_case));
        }
        for (expected, result) in expected.iter().zip(results.iter()) {
            assert_eq!(expected, result);
        }
    }

    /// Unit test for str_to_audio_source.
    #[test]
    fn test_str_to_audio_source() {
        println!("Running test_str_to_audio_source");
        let test_cases = vec!["USER", "system", "unexpected_source_type"];
        let expected = vec![
            Ok(fidl_fuchsia_settings::AudioStreamSettingSource::User),
            Ok(fidl_fuchsia_settings::AudioStreamSettingSource::System),
            Err(String::from("Couldn't parse audio source type")),
        ];
        let mut results = vec![];
        for test_case in test_cases {
            results.push(str_to_audio_source(test_case));
        }
        for (expected, result) in expected.iter().zip(results.iter()) {
            assert_eq!(expected, result);
        }
    }
}

#[cfg(test)]
mod interface_tests;
