#![feature(async_await)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_device_manager::*,
    fidl_fuchsia_devicesettings::*,
    fidl_fuchsia_setui::*,
    fuchsia_component::client::connect_to_service,
    structopt::StructOpt,
};

mod client;
mod display;
mod intl;
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

    // Operations that use the new interfaces.
    #[structopt(name = "display")]
    Display {
        #[structopt(short = "b", long = "brightness")]
        brightness: Option<f32>,

        #[structopt(short = "a", long = "auto_brightness")]
        auto_brightness: Option<bool>,
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
        SettingClient::Intl { time_zone, temperature_unit, locales } => {
            let intl_service = connect_to_service::<fidl_fuchsia_settings::IntlMarker>()
                .context("Failed to connect to intl service")?;
            let output = intl::command(intl_service, time_zone, temperature_unit, locales).await?;
            println!("Intl: {}", output);
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

fn str_to_temperature_unit(src: &str) -> Result<fidl_fuchsia_intl::TemperatureUnit, &str> {
    match src {
        "C" | "c" | "celsius" | "Celsius" => Ok(fidl_fuchsia_intl::TemperatureUnit::Celsius),
        "F" | "f" | "fahrenheit" | "Fahrenheit" => {
            Ok(fidl_fuchsia_intl::TemperatureUnit::Fahrenheit)
        }
        _ => Err("Couldn't parse temperature"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Verifies that externally dependent values are not changed
    #[test]
    fn test_describe_account_override() {
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
