// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![allow(dead_code)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_setui::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
};

mod client;

fn serve_check_login_override_mutate(
    stream: SetUiServiceRequestStream,
    expected_override: LoginOverride,
) -> impl Future<Output = ()> {
    stream
        .err_into::<failure::Error>()
        .try_for_each(async move |req| {
            match req {
                SetUiServiceRequest::Mutate { setting_type, mutation, responder } => {
                    assert_eq!(setting_type, SettingType::Account);

                    match mutation {
                        fidl_fuchsia_setui::Mutation::AccountMutationValue(account_mutation) => {
                            if let (Some(login_override), Some(operation)) =
                                (account_mutation.login_override, account_mutation.operation)
                            {
                                assert_eq!(login_override, expected_override);
                                assert_eq!(operation, AccountOperation::SetLoginOverride);
                            }
                        }
                        _ => {
                            panic!("unexpected data for account mutation");
                        }
                    }
                    responder
                        .send(&mut MutationResponse { return_code: ReturnCode::Ok })
                        .context("sending response")?;
                }
                _ => {}
            };
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error| panic!("error running setui server: {:?}", e))
}

enum Services {
    SetUi(SetUiServiceRequestStream),
}

const ENV_NAME: &str = "setui_client_test_environment";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    await!(validate_account_mutate("autologinguest".to_string(), LoginOverride::AutologinGuest))?;
    await!(validate_account_mutate("auth".to_string(), LoginOverride::AuthProvider))?;
    await!(validate_account_mutate("none".to_string(), LoginOverride::None))?;
    Ok(())
}

async fn validate_account_mutate(
    specified_type: String,
    expected_override: LoginOverride,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.add_fidl_service(Services::SetUi);
    let env = fs.create_nested_environment(ENV_NAME)?;

    fasync::spawn(fs.for_each_concurrent(None, move |req| {
        async move {
            match req {
                Services::SetUi(stream) => {
                    await!(serve_check_login_override_mutate(stream, expected_override))
                }
            }
        }
    }));

    let setui = env
        .connect_to_service::<SetUiServiceMarker>()
        .context("Failed to connect to setui service")?;

    await!(client::mutate(setui, "login".to_string(), specified_type))?;
    Ok(())
}
