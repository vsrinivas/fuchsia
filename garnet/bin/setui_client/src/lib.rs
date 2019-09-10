#![feature(async_await)]
#![allow(dead_code)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_device_manager::*,
    fidl_fuchsia_devicesettings::*,
    fidl_fuchsia_settings::ConfigurationInterfaces,
    fidl_fuchsia_setui::LoginOverride,
    fidl_fuchsia_setui::*,
    fuchsia_component::client::connect_to_service,
    structopt::StructOpt,
};

mod accessibility;
mod audio;
mod client;
mod display;
mod do_not_disturb;
mod intl;
mod privacy;
mod setup;
mod system;

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
    Accessibility {
        // Debug option that attempts to set some value for all fields in AccessibilitySettings.
        #[structopt(long)]
        debug_set_all: bool,

        #[structopt(short = "a", long)]
        audio_description: Option<bool>,

        #[structopt(short = "s", long)]
        screen_reader: Option<bool>,

        #[structopt(short = "i", long)]
        color_inversion: Option<bool>,

        #[structopt(short = "m", long)]
        enable_magnification: Option<bool>,

        #[structopt(short = "c", long, parse(try_from_str = "str_to_color_blindness_type"))]
        color_correction: Option<fidl_fuchsia_settings::ColorBlindnessType>,
    },

    #[structopt(name = "audio")]
    Audio {
        #[structopt(flatten)]
        streams: AudioStreams,

        #[structopt(flatten)]
        input: AudioInput,
    },

    // Operations that use the new interfaces.
    #[structopt(name = "display")]
    Display {
        #[structopt(short = "b", long = "brightness")]
        brightness: Option<f32>,

        #[structopt(short = "a", long = "auto_brightness")]
        auto_brightness: Option<bool>,
    },

    // Operations that use the new interfaces.
    #[structopt(name = "do_not_disturb")]
    DoNotDisturb {
        #[structopt(short = "u", long = "user_dnd")]
        user_dnd: Option<bool>,

        #[structopt(short = "n", long = "night_mode_dnd")]
        night_mode_dnd: Option<bool>,
    },

    // Operations that use the new interfaces.
    #[structopt(name = "intl")]
    Intl {
        #[structopt(short = "z", long = "time_zone", parse(from_str = "str_to_time_zone"))]
        time_zone: Option<fidl_fuchsia_intl::TimeZoneId>,

        #[structopt(
            short = "u",
            long = "temperature_unit",
            parse(try_from_str = "str_to_temperature_unit")
        )]
        temperature_unit: Option<fidl_fuchsia_intl::TemperatureUnit>,

        #[structopt(short = "l", long = "locales", parse(from_str = "str_to_locale"))]
        locales: Vec<fidl_fuchsia_intl::LocaleId>,
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
        SettingClient::Display { brightness, auto_brightness } => {
            let display_service = connect_to_service::<fidl_fuchsia_settings::DisplayMarker>()
                .context("Failed to connect to display service")?;
            let output = display::command(display_service, brightness, auto_brightness).await?;
            println!("Display: {}", output);
        }
        SettingClient::DoNotDisturb { user_dnd, night_mode_dnd } => {
            let dnd_service = connect_to_service::<fidl_fuchsia_settings::DoNotDisturbMarker>()
                .context("Failed to connect to do_not_disturb service")?;
            let output = do_not_disturb::command(dnd_service, user_dnd, night_mode_dnd).await?;
            println!("Display: {}", output);
        }
        SettingClient::Intl { time_zone, temperature_unit, locales } => {
            let intl_service = connect_to_service::<fidl_fuchsia_settings::IntlMarker>()
                .context("Failed to connect to intl service")?;
            let output = intl::command(intl_service, time_zone, temperature_unit, locales).await?;
            println!("Intl: {}", output);
        }
        SettingClient::Accessibility {
            debug_set_all,
            audio_description,
            screen_reader,
            color_inversion,
            enable_magnification,
            color_correction,
        } => {
            let accessibility_service =
                connect_to_service::<fidl_fuchsia_settings::AccessibilityMarker>()
                    .context("Failed to connect to accessibility service")?;
            let output = accessibility::command(
                accessibility_service,
                debug_set_all,
                audio_description,
                screen_reader,
                color_inversion,
                enable_magnification,
                color_correction,
            )
            .await?;
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
                Err(failure::err_msg("malformed data for SettingType::Unknown"))
            }
        }
        SettingType::Account => {
            if let SettingData::Account(data) = setting.data {
                Ok(describe_login_override(data.mode)?)
            } else {
                Err(failure::err_msg("malformed data for SettingType::Account"))
            }
        }
        _ => Err(failure::err_msg("unhandled type")),
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

fn str_to_temperature_unit(src: &str) -> Result<fidl_fuchsia_intl::TemperatureUnit, &str> {
    match src {
        "C" | "c" | "celsius" | "Celsius" => Ok(fidl_fuchsia_intl::TemperatureUnit::Celsius),
        "F" | "f" | "fahrenheit" | "Fahrenheit" => {
            Ok(fidl_fuchsia_intl::TemperatureUnit::Fahrenheit)
        }
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
        "default" | "d" => Ok(fidl_fuchsia_settings::AudioStreamSettingSource::Default),
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
        let test_cases = vec!["Default", "USER", "system", "unexpected_source_type"];
        let expected = vec![
            Ok(fidl_fuchsia_settings::AudioStreamSettingSource::Default),
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
