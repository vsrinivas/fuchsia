#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_device_manager::*,
    fidl_fuchsia_devicesettings::*,
    fidl_fuchsia_setui::*,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog as syslog,
    structopt::StructOpt,
};

mod client;
mod system;

/// SettingClient exercises the functionality found in SetUI service. Currently,
/// action parameters are specified at as individual arguments, but the goal is
/// to eventually parse details from a JSON file input.
#[derive(StructOpt, Debug)]
#[structopt(name = "setui_client", about = "set setting values")]
enum SettingClient {
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
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-client"]).expect("Can't init logger");

    match SettingClient::from_args() {
        SettingClient::Mutate { setting_type, value, remove_users } => {
            let setui = connect_to_service::<SetUiServiceMarker>()
                .context("Failed to connect to setui service")?;

            await!(client::mutate(setui, setting_type, value))?;

            if remove_users {
                let device_settings = connect_to_service::<DeviceSettingsManagerMarker>()
                    .context("Failed to connect to devicesettings service")?;
                await!(device_settings.set_integer("FactoryReset", 1))?;
                let device_admin = connect_to_service::<AdministratorMarker>()
                    .context("Failed to connect to deviceadmin service")?;
                await!(device_admin.suspend(SUSPEND_FLAG_REBOOT))?;
            }
        }
        SettingClient::Get { setting_type } => {
            let setui = connect_to_service::<SetUiServiceMarker>()
                .context("Failed to connect to setui service")?;
            let description = describe_setting(await!(client::get(setui, setting_type.clone()))?)?;
            println!("value for setting[{}]:{}", setting_type, description);
        }
        SettingClient::System { login_mode } => {
            let system_service = connect_to_service::<fidl_fuchsia_settings::SystemMarker>()
                .context("Failed to connect to system service")?;
            let output = await!(system::command(system_service, login_mode))?;
            println!("System: {}", output);
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
