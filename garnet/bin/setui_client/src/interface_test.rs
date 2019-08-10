// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![allow(dead_code)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
};

mod client;
mod display;
mod intl;
mod system;

enum Services {
    SetUi(fidl_fuchsia_setui::SetUiServiceRequestStream),
    Display(DisplayRequestStream),
    System(SystemRequestStream),
    Intl(IntlRequestStream),
}

const ENV_NAME: &str = "setui_client_test_environment";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    println!("account mutation tests");
    validate_account_mutate(
        "autologinguest".to_string(),
        fidl_fuchsia_setui::LoginOverride::AutologinGuest
    ).await?;
    validate_account_mutate(
        "auth".to_string(),
        fidl_fuchsia_setui::LoginOverride::AuthProvider
    ).await?;
    validate_account_mutate("none".to_string(), fidl_fuchsia_setui::LoginOverride::None).await?;

    println!("display service tests");
    println!("  client calls display watch");
    validate_display(None, None).await?;

    println!("  client calls set brightness");
    validate_display(Some(0.5), None).await?;

    println!("  client calls set auto brightness");
    validate_display(None, Some(true)).await?;

    println!("intl service tests");
    println!("  client calls set temperature unit");
    validate_temperature_unit().await?;

    println!("system service tests");
    println!("  client calls set login mode");
    validate_system_override().await?;

    Ok(())
}

// Creates a service in an environment for a given setting type.
// Usage: create_service!(service_enum_name,
//          request_name => {code block},
//          request2_name => {code_block}
//          ... );
macro_rules! create_service  {
    ($setting_type:path, $( $request:pat => $callback:block ),*) => {{

        let mut fs = ServiceFs::new();
        fs.add_fidl_service($setting_type);
        let env = fs.create_nested_environment(ENV_NAME)?;

        fasync::spawn(fs.for_each_concurrent(None, move |connection| {
            async move {
                #![allow(unreachable_patterns)]
                match connection {
                    $setting_type(stream) => {
                        stream
                            .err_into::<failure::Error>()
                            .try_for_each(|req| async move {
                                match req {
                                    $($request => $callback)*
                                    _ => panic!("Incorrect command to service"),
                                }
                                Ok(())
                            })
                            .unwrap_or_else(|e: failure::Error| panic!(
                                "error running setui server: {:?}",
                                e
                            )).await;
                    }
                    _ => {
                        panic!("Unexpected service");
                    }
                }
            }
        }));
        env
    }};
}

async fn validate_system_override() -> Result<(), Error> {
    let env = create_service!(Services::System,
        SystemRequest::Set { settings, responder } => {
            if let Some(login_override) = settings.mode {
                assert_eq!(login_override, LoginOverride::AuthProvider);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Wrong call to set");
            }
    });

    let system_service =
        env.connect_to_service::<SystemMarker>().context("Failed to connect to intl service")?;

    system::command(system_service, Some("auth".to_string())).await?;

    Ok(())
}

async fn validate_temperature_unit() -> Result<(), Error> {
    let env = create_service!(Services::Intl,
        IntlRequest::Set { settings, responder } => {
            if let Some(temperature_unit) = settings.temperature_unit {
            assert_eq!(
                temperature_unit,
                fidl_fuchsia_intl::TemperatureUnit::Celsius
            );
            responder.send(&mut Ok(()))?;
            } else {
                panic!("Wrong call to set");
            }
    });

    let intl_service =
        env.connect_to_service::<IntlMarker>().context("Failed to connect to intl service")?;

    intl::command(
        intl_service,
        None,
        Some(fidl_fuchsia_intl::TemperatureUnit::Celsius),
        vec![]
    ).await?;

    Ok(())
}

// Can only check one mutate option at once
async fn validate_display(
    expected_brightness: Option<f32>,
    expected_auto_brightness: Option<bool>,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Display, DisplayRequest::Set { settings, responder, } => {
            if let (Some(brightness_value), Some(expected_brightness_value)) =
              (settings.brightness_value, expected_brightness) {
                assert_eq!(brightness_value, expected_brightness_value);
                responder.send(&mut Ok(()))?;
            } else if let (Some(auto_brightness), Some(expected_auto_brightness_value)) =
              (settings.auto_brightness, expected_auto_brightness) {
                assert_eq!(auto_brightness, expected_auto_brightness_value);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        DisplayRequest::Watch { responder } => {
            responder.send(&mut Ok(DisplaySettings {
                auto_brightness: Some(false),
                brightness_value: Some(0.5),
            }))?;
        }
    );

    let display_service = env
        .connect_to_service::<DisplayMarker>()
        .context("Failed to connect to display service")?;

    display::command(display_service, expected_brightness, expected_auto_brightness).await?;

    Ok(())
}

async fn validate_account_mutate(
    specified_type: String,
    expected_override: fidl_fuchsia_setui::LoginOverride,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.add_fidl_service(Services::SetUi);
    let env = fs.create_nested_environment(ENV_NAME)?;

    fasync::spawn(fs.for_each_concurrent(None, move |req| {
        async move {
            match req {
                Services::SetUi(stream) => {
                    serve_check_login_override_mutate(stream, expected_override).await
                }
                _ => {}
            }
        }
    }));

    let setui = env
        .connect_to_service::<fidl_fuchsia_setui::SetUiServiceMarker>()
        .context("Failed to connect to setui service")?;

    client::mutate(setui, "login".to_string(), specified_type).await?;
    Ok(())
}

fn serve_check_login_override_mutate(
    stream: fidl_fuchsia_setui::SetUiServiceRequestStream,
    expected_override: fidl_fuchsia_setui::LoginOverride,
) -> impl Future<Output = ()> {
    stream
        .err_into::<failure::Error>()
        .try_for_each(move |req| async move {
            match req {
                fidl_fuchsia_setui::SetUiServiceRequest::Mutate {
                    setting_type,
                    mutation,
                    responder,
                } => {
                    assert_eq!(setting_type, fidl_fuchsia_setui::SettingType::Account);

                    match mutation {
                        fidl_fuchsia_setui::Mutation::AccountMutationValue(account_mutation) => {
                            if let (Some(login_override), Some(operation)) =
                                (account_mutation.login_override, account_mutation.operation)
                            {
                                assert_eq!(login_override, expected_override);
                                assert_eq!(
                                    operation,
                                    fidl_fuchsia_setui::AccountOperation::SetLoginOverride
                                );
                            }
                        }
                        _ => {
                            panic!("unexpected data for account mutation");
                        }
                    }
                    responder
                        .send(&mut fidl_fuchsia_setui::MutationResponse {
                            return_code: fidl_fuchsia_setui::ReturnCode::Ok,
                        })
                        .context("sending response")?;
                }
                _ => {}
            };
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error| panic!("error running setui server: {:?}", e))
}
