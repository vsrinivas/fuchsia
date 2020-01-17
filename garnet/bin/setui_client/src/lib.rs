use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_device_manager::*,
    fidl_fuchsia_devicesettings::*,
    fidl_fuchsia_settings::ConfigurationInterfaces,
    fidl_fuchsia_setui::LoginOverride,
    fidl_fuchsia_setui::*,
    fuchsia_component::client::connect_to_service,
    structopt::StructOpt,
};

pub mod accessibility;
pub mod audio;
pub mod client;
pub mod device;
pub mod display;
pub mod do_not_disturb;
pub mod intl;
pub mod night_mode;
pub mod privacy;
pub mod setup;
pub mod system;

/// SettingClient exercises the functionality found in SetUI service. Currently,
/// action parameters are specified at as individual arguments, but the goal is
/// to eventually parse details from a JSON file input.
#[derive(StructOpt, Debug)]
#[structopt(name = "setui_client", about = "set setting values")]
pub enum SettingClient {
    // Allows for updating a setting value
    #[structopt(name = "mutate")]
    Mutate {
        #[structopt(short = "t", long = "type")]
        setting_type: String,

        #[structopt(short = "v", long = "value")]
        value: String,

        #[structopt(short = "r", long = "remove_users")]
        remove_users: bool,
    },

    // Retrieves a setting value
    #[structopt(name = "get")]
    Get {
        #[structopt(short = "t", long = "type")]
        setting_type: String,
    },

    // Operations that use the new interfaces.
    #[structopt(name = "system")]
    System {
        #[structopt(short = "m", long = "login_mode")]
        login_mode: Option<String>,
    },

    #[structopt(name = "accessibility")]
    Accessibility(AccessibilityOptions),

    #[structopt(name = "audio")]
    Audio {
        #[structopt(flatten)]
        streams: AudioStreams,

        #[structopt(flatten)]
        input: AudioInput,
    },

    // Operations that use the Device interface.
    #[structopt(name = "device")]
    Device { build_tag: Option<String> },

    #[structopt(name = "display")]
    Display {
        #[structopt(short = "b", long = "brightness")]
        brightness: Option<f32>,

        #[structopt(short = "a", long = "auto_brightness")]
        auto_brightness: Option<bool>,

        #[structopt(short = "l", long = "light_sensor")]
        light_sensor: bool,
    },

    #[structopt(name = "do_not_disturb")]
    DoNotDisturb {
        #[structopt(short = "u", long = "user_dnd")]
        user_dnd: Option<bool>,

        #[structopt(short = "n", long = "night_mode_dnd")]
        night_mode_dnd: Option<bool>,
    },

    #[structopt(name = "intl")]
    Intl {
        #[structopt(short = "z", long, parse(from_str = "str_to_time_zone"))]
        time_zone: Option<fidl_fuchsia_intl::TimeZoneId>,

        #[structopt(short = "u", long, parse(try_from_str = "str_to_temperature_unit"))]
        // Valid options are Celsius and Fahrenheit, or just "c" and "f".
        temperature_unit: Option<fidl_fuchsia_intl::TemperatureUnit>,

        #[structopt(short, long, parse(from_str = "str_to_locale"))]
        /// List of locales, separated by spaces.
        locales: Vec<fidl_fuchsia_intl::LocaleId>,

        #[structopt(long)]
        /// If set, this flag will set locales as an empty list. Overrides the locales arguments.
        clear_locales: bool,
    },

    #[structopt(name = "night_mode")]
    NightMode {
        #[structopt(short, long)]
        night_mode_status: Option<bool>,
    },

    #[structopt(name = "privacy")]
    Privacy {
        #[structopt(short, long)]
        user_data_sharing_consent: Option<bool>,
    },

    #[structopt(name = "setup")]
    Setup {
        #[structopt(short = "i", long = "interfaces", parse(from_str = "str_to_interfaces"))]
        configuration_interfaces: Option<ConfigurationInterfaces>,
    },
}

#[derive(StructOpt, Debug, Clone, Copy, Default)]
pub struct AccessibilityOptions {
    #[structopt(short = "a", long)]
    pub audio_description: Option<bool>,

    #[structopt(short = "s", long)]
    pub screen_reader: Option<bool>,

    #[structopt(short = "i", long)]
    pub color_inversion: Option<bool>,

    #[structopt(short = "m", long)]
    pub enable_magnification: Option<bool>,

    #[structopt(short = "c", long, parse(try_from_str = "str_to_color_blindness_type"))]
    pub color_correction: Option<fidl_fuchsia_settings::ColorBlindnessType>,

    #[structopt(subcommand)]
    pub caption_options: Option<CaptionCommands>,
}

#[derive(StructOpt, Debug, Clone, Copy)]
pub enum CaptionCommands {
    #[structopt(name = "captions")]
    CaptionOptions(CaptionOptions),
}

#[derive(StructOpt, Debug, Clone, Copy)]
pub struct CaptionOptions {
    #[structopt(short = "m", long)]
    /// Enable closed captions for media sources of audio.
    pub for_media: Option<bool>,

    #[structopt(short = "t", long)]
    /// Enable closed captions for Text-To-Speech sources of audio.
    pub for_tts: Option<bool>,

    #[structopt(short, long, parse(try_from_str = "str_to_color"))]
    /// Border color used around the closed captions window. Valid options are red, green, or blue,
    /// or just the first letter of each color (r, g, b).
    pub window_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    #[structopt(short, long, parse(try_from_str = "str_to_color"))]
    /// Border color used around the closed captions window. Valid options are red, green, or blue,
    /// or just the first letter of each color (r, g, b).
    pub background_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    #[structopt(flatten)]
    pub style: CaptionFontStyle,
}

#[derive(StructOpt, Debug, Clone, Copy)]
pub struct CaptionFontStyle {
    #[structopt(short, long, parse(try_from_str = "str_to_font_family"))]
    /// Font family for captions, specified by 47 CFR §79.102(k). Valid options are unknown,
    /// monospaced_serif, proportional_serif, monospaced_sans_serif, proportional_sans_serif,
    /// casual, cursive, and small_capitals,
    pub font_family: Option<fidl_fuchsia_settings::CaptionFontFamily>,

    #[structopt(short = "c", long, parse(try_from_str = "str_to_color"))]
    /// Color of the closed cpation text. Valid options are red, green, or blue, or just the first
    /// letter of each color (r, g, b).
    pub font_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    #[structopt(short, long)]
    /// Size of closed captions text relative to the default captions size. A range of [0.5, 2] is
    /// guaranteed to be supported (as 47 CFR §79.103(c)(4) establishes).
    pub relative_size: Option<f32>,

    #[structopt(short = "e", long, parse(try_from_str = "str_to_edge_style"))]
    /// Edge style for fonts as specified in 47 CFR §79.103(c)(7), valid options are none,
    /// drop_shadow, raised, depressed, and outline.
    pub char_edge_style: Option<fidl_fuchsia_settings::EdgeStyle>,
}

#[derive(StructOpt, Debug)]
pub struct AudioStreams {
    #[structopt(short = "t", long = "stream", parse(try_from_str = "str_to_audio_stream"))]
    stream: Option<fidl_fuchsia_media::AudioRenderUsage>,
    #[structopt(short = "s", long = "source", parse(try_from_str = "str_to_audio_source"))]
    source: Option<fidl_fuchsia_settings::AudioStreamSettingSource>,
    #[structopt(flatten)]
    user_volume: UserVolume,
}

#[derive(StructOpt, Debug)]
struct UserVolume {
    #[structopt(short = "l", long = "level")]
    level: Option<f32>,

    #[structopt(short = "v", long = "volume_muted")]
    volume_muted: Option<bool>,
}

#[derive(StructOpt, Debug)]
pub struct AudioInput {
    #[structopt(short = "m", long = "input_muted")]
    input_muted: Option<bool>,
}

pub async fn run_command(command: SettingClient) -> Result<(), Error> {
    match command {
        SettingClient::Mutate { setting_type, value, remove_users } => {
            let setui = connect_to_service::<SetUiServiceMarker>()
                .context("Failed to connect to setui service")?;

            client::mutate(setui, setting_type, value).await?;

            if remove_users {
                let device_settings = connect_to_service::<DeviceSettingsManagerMarker>()
                    .context("Failed to connect to devicesettings service")?;
                device_settings.set_integer("FactoryReset", 1).await?;
                let device_admin = connect_to_service::<AdministratorMarker>()
                    .context("Failed to connect to deviceadmin service")?;
                device_admin.suspend(SUSPEND_FLAG_REBOOT).await?;
            }
        }
        SettingClient::Get { setting_type } => {
            let setui = connect_to_service::<SetUiServiceMarker>()
                .context("Failed to connect to setui service")?;
            let description = describe_setting(client::get(setui, setting_type.clone()).await?)?;
            println!("value for setting[{}]:{}", setting_type, description);
        }
        SettingClient::System { login_mode } => {
            let system_service = connect_to_service::<fidl_fuchsia_settings::SystemMarker>()
                .context("Failed to connect to system service")?;
            let output = system::command(system_service, login_mode).await?;
            println!("System: {}", output);
        }
        SettingClient::Device { build_tag } => {
            let _build_tag = build_tag.clone();
            if let Some(_build_tag_val) = build_tag {
                panic!("Cannot set device settings");
            }
            let device_service = connect_to_service::<fidl_fuchsia_settings::DeviceMarker>()
                .context("Failed to connect to device service")?;
            let output = device::command(device_service).await?;
            println!("Device: {}", output);
        }
        SettingClient::Display { brightness, auto_brightness, light_sensor } => {
            let display_service = connect_to_service::<fidl_fuchsia_settings::DisplayMarker>()
                .context("Failed to connect to display service")?;
            let output =
                display::command(display_service, brightness, auto_brightness, light_sensor)
                    .await?;
            println!("Display: {}", output);
        }
        SettingClient::DoNotDisturb { user_dnd, night_mode_dnd } => {
            let dnd_service = connect_to_service::<fidl_fuchsia_settings::DoNotDisturbMarker>()
                .context("Failed to connect to do_not_disturb service")?;
            let output = do_not_disturb::command(dnd_service, user_dnd, night_mode_dnd).await?;
            println!("DoNotDisturb: {}", output);
        }
        SettingClient::Intl { time_zone, temperature_unit, locales, clear_locales } => {
            let intl_service = connect_to_service::<fidl_fuchsia_settings::IntlMarker>()
                .context("Failed to connect to intl service")?;
            let output =
                intl::command(intl_service, time_zone, temperature_unit, locales, clear_locales)
                    .await?;
            println!("Intl: {}", output);
        }
        SettingClient::NightMode { night_mode_status } => {
            let night_mode_service = connect_to_service::<fidl_fuchsia_settings::NightModeMarker>()
                .context("Failed to connect to night mode service")?;
            let output = night_mode::command(night_mode_service, night_mode_status).await?;
            println!("NightMode: {}", output);
        }
        SettingClient::Accessibility(accessibility_options) => {
            let accessibility_service =
                connect_to_service::<fidl_fuchsia_settings::AccessibilityMarker>()
                    .context("Failed to connect to accessibility service")?;

            let output =
                accessibility::command(accessibility_service, accessibility_options).await?;
            println!("Accessibility: {}", output);
        }
        SettingClient::Privacy { user_data_sharing_consent } => {
            let privacy_service = connect_to_service::<fidl_fuchsia_settings::PrivacyMarker>()
                .context("Failed to connect to privacy service")?;
            let output = privacy::command(privacy_service, user_data_sharing_consent).await?;
            println!("Privacy: {}", output);
        }
        SettingClient::Audio { streams, input } => {
            let audio_service = connect_to_service::<fidl_fuchsia_settings::AudioMarker>()
                .context("Failed to connect to audio service")?;
            let stream = streams.stream;
            let source = streams.source;
            let level = streams.user_volume.level;
            let volume_muted = streams.user_volume.volume_muted;
            let input_muted = input.input_muted;
            let output =
                audio::command(audio_service, stream, source, level, volume_muted, input_muted)
                    .await?;
            println!("Audio: {}", output);
        }
        SettingClient::Setup { configuration_interfaces } => {
            let setup_service = connect_to_service::<fidl_fuchsia_settings::SetupMarker>()
                .context("Failed to connect to setup service")?;
            let output = setup::command(setup_service, configuration_interfaces).await?;
            println!("Setup: {}", output);
        }
    }
    Ok(())
}

fn describe_setting(setting: SettingsObject) -> Result<String, Error> {
    match setting.setting_type {
        SettingType::Unknown => {
            if let SettingData::StringValue(data) = setting.data {
                Ok(data)
            } else {
                Err(anyhow::format_err!("malformed data for SettingType::Unknown"))
            }
        }
        SettingType::Account => {
            if let SettingData::Account(data) = setting.data {
                Ok(describe_login_override(data.mode)?)
            } else {
                Err(anyhow::format_err!("malformed data for SettingType::Account"))
            }
        }
        _ => Err(anyhow::format_err!("unhandled type")),
    }
}

fn describe_login_override(login_override_option: Option<LoginOverride>) -> Result<String, Error> {
    if login_override_option == None {
        return Ok("none".to_string());
    }

    match login_override_option.unwrap() {
        LoginOverride::AutologinGuest => Ok(client::LOGIN_OVERRIDE_AUTOLOGINGUEST.to_string()),
        LoginOverride::None => Ok(client::LOGIN_OVERRIDE_NONE.to_string()),
        LoginOverride::AuthProvider => Ok(client::LOGIN_OVERRIDE_AUTH.to_string()),
    }
}

fn str_to_time_zone(src: &&str) -> fidl_fuchsia_intl::TimeZoneId {
    fidl_fuchsia_intl::TimeZoneId { id: src.to_string() }
}

fn str_to_locale(src: &str) -> fidl_fuchsia_intl::LocaleId {
    fidl_fuchsia_intl::LocaleId { id: src.to_string() }
}

fn str_to_interfaces(src: &&str) -> ConfigurationInterfaces {
    let mut interfaces = ConfigurationInterfaces::empty();

    for interface in src.split(",") {
        match interface.to_lowercase().as_str() {
            "eth" | "ethernet" => {
                interfaces = interfaces | ConfigurationInterfaces::Ethernet;
            }
            "wireless" | "wifi" => {
                interfaces = interfaces | ConfigurationInterfaces::Wifi;
            }
            _ => {}
        }
    }

    return interfaces;
}

fn str_to_color(src: &str) -> Result<fidl_fuchsia_ui_types::ColorRgba, &str> {
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
        _ => return Err("Couldn't parse color"),
    })
}

fn str_to_font_family(src: &str) -> Result<fidl_fuchsia_settings::CaptionFontFamily, &str> {
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
        _ => return Err("Couldn't parse font family"),
    })
}

fn str_to_edge_style(src: &str) -> Result<fidl_fuchsia_settings::EdgeStyle, &str> {
    Ok(match src.to_lowercase().as_str() {
        "none" => fidl_fuchsia_settings::EdgeStyle::None,
        "drop_shadow" => fidl_fuchsia_settings::EdgeStyle::DropShadow,
        "raised" => fidl_fuchsia_settings::EdgeStyle::Raised,
        "depressed" => fidl_fuchsia_settings::EdgeStyle::Depressed,
        "outline" => fidl_fuchsia_settings::EdgeStyle::Outline,
        _ => return Err("Couldn't parse edge style"),
    })
}

fn str_to_temperature_unit(src: &str) -> Result<fidl_fuchsia_intl::TemperatureUnit, &str> {
    match src.to_lowercase().as_str() {
        "c" | "celsius" => Ok(fidl_fuchsia_intl::TemperatureUnit::Celsius),
        "f" | "fahrenheit" => Ok(fidl_fuchsia_intl::TemperatureUnit::Fahrenheit),
        _ => Err("Couldn't parse temperature"),
    }
}

fn str_to_color_blindness_type(
    src: &str,
) -> Result<fidl_fuchsia_settings::ColorBlindnessType, &str> {
    match src.to_lowercase().as_str() {
        "none" | "n" => Ok(fidl_fuchsia_settings::ColorBlindnessType::None),
        "protanomaly" | "p" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Protanomaly),
        "deuteranomaly" | "d" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly),
        "tritanomaly" | "t" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly),
        _ => Err("Couldn't parse color blindness type"),
    }
}

fn str_to_audio_stream(src: &str) -> Result<fidl_fuchsia_media::AudioRenderUsage, &str> {
    match src.to_lowercase().as_str() {
        "background" | "b" => Ok(fidl_fuchsia_media::AudioRenderUsage::Background),
        "media" | "m" => Ok(fidl_fuchsia_media::AudioRenderUsage::Media),
        "interruption" | "i" => Ok(fidl_fuchsia_media::AudioRenderUsage::Interruption),
        "system_agent" | "systemagent" | "system agent" | "s" => {
            Ok(fidl_fuchsia_media::AudioRenderUsage::SystemAgent)
        }
        "communication" | "c" => Ok(fidl_fuchsia_media::AudioRenderUsage::Communication),
        _ => Err("Couldn't parse audio stream type"),
    }
}

fn str_to_audio_source(src: &str) -> Result<fidl_fuchsia_settings::AudioStreamSettingSource, &str> {
    match src.to_lowercase().as_str() {
        "user" | "u" => Ok(fidl_fuchsia_settings::AudioStreamSettingSource::User),
        "system" | "s" => Ok(fidl_fuchsia_settings::AudioStreamSettingSource::System),
        _ => Err("Couldn't parse audio source type"),
    }
}

// TODO(go/fxb/36262): Refactor tests out of lib
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
            Err("Couldn't parse audio stream type"),
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
            Err("Couldn't parse audio source type"),
        ];
        let mut results = vec![];
        for test_case in test_cases {
            results.push(str_to_audio_source(test_case));
        }
        for (expected, result) in expected.iter().zip(results.iter()) {
            assert_eq!(expected, result);
        }
    }

    /// Verifies that externally dependent values are not changed.
    #[test]
    fn test_describe_account_override() {
        println!("Running test_describe_account_override");
        verify_account_override(LoginOverride::AutologinGuest, "autologinguest");
        verify_account_override(LoginOverride::None, "none");
        verify_account_override(LoginOverride::AuthProvider, "auth");
    }

    fn verify_account_override(login_override: LoginOverride, expected: &str) {
        match describe_login_override(Some(login_override)) {
            Ok(description) => {
                assert_eq!(description, expected);
            }
            _ => {
                panic!("expected");
            }
        }
    }
}
