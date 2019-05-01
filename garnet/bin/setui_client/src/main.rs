#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_setui::*,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, fx_log_info},
    structopt::StructOpt,
};

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
    },
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-client"]).expect("Can't init logger");

    let setui =
        connect_to_service::<SetUiServiceMarker>().context("Failed to connect to setui service")?;

    match SettingClient::from_args() {
        SettingClient::Mutate { setting_type, value } => {
            let converted_type = extract_setting_type(&setting_type)?;
            let mut mutation = generate_string_mutation(&value)?;

            let res = await!(setui.mutate(converted_type, &mut mutation,))?;

            fx_log_info!("Response:{:?}", res);
        }
    }

    Ok(())
}

/// Converts argument string into a known SettingType. Will return an error if
/// no suitable type can be determined.
fn extract_setting_type(s: &str) -> Result<SettingType, Error> {
    match s {
        "unknown" => Ok(SettingType::Unknown),
        _ => Err(failure::format_err!("unknown type:{}", s)),
    }
}

/// Generates the appropriate Mutation for a string change.
fn generate_string_mutation(value: &str) -> Result<Mutation, Error> {
    Ok(fidl_fuchsia_setui::Mutation::StringMutationValue(StringMutation {
        operation: StringOperation::Update,
        value: value.to_string(),
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_setting_type() {
        assert_eq!(extract_setting_type("unknown").unwrap(), SettingType::Unknown);
        assert!(extract_setting_type("outside").is_err(), "outside isn't a valid type");
    }

    #[test]
    fn test_generate_string_mutation() {
        let test_string = "test_string";
        match generate_string_mutation(test_string).unwrap() {
            fidl_fuchsia_setui::Mutation::StringMutationValue(mutation) => {
                assert_eq!(mutation.value, test_string);
                assert_eq!(mutation.operation, StringOperation::Update);
            }
            _ => assert!(false, "generated unknown type"),
        }
    }
}
