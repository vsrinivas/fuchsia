// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{
    AccessibilityRequestStream, AudioRequestStream, DisplayRequestStream,
    DoNotDisturbRequestStream, FactoryResetRequestStream, InputRequestStream, IntlRequestStream,
    LightRequestStream, NightModeRequestStream, PrivacyRequestStream, SetupRequestStream,
};
use fidl_fuchsia_settings_policy::VolumePolicyControllerRequestStream;

enum Services {
    Accessibility(AccessibilityRequestStream),
    Audio(AudioRequestStream),
    Display(DisplayRequestStream),
    DoNotDisturb(DoNotDisturbRequestStream),
    FactoryReset(FactoryResetRequestStream),
    Input(InputRequestStream),
    Intl(IntlRequestStream),
    Light(LightRequestStream),
    NightMode(NightModeRequestStream),
    Privacy(PrivacyRequestStream),
    Setup(SetupRequestStream),
    VolumePolicy(VolumePolicyControllerRequestStream),
}

struct ExpectedStreamSettingsStruct {
    stream: Option<AudioRenderUsage>,
    source: Option<fidl_fuchsia_settings::AudioStreamSettingSource>,
    level: Option<f32>,
    volume_muted: Option<bool>,
    input_muted: Option<bool>,
}

const ENV_NAME: &str = "setui_client_test_environment";

// Creates a service in an environment for a given setting type.
// Usage: create_service!(service_enum_name,
//          request_name => {code block},
//          request2_name => {code_block}
//          ... );
#[macro_export]
macro_rules! create_service  {
    ($setting_type:path, $( $request:pat => $callback:block ),*) => {{
        use uuid::Uuid;

        let mut fs = ServiceFs::new();
        fs.add_fidl_service($setting_type);

        let mut uuid = Uuid::new_v4().to_string();
        uuid.push_str(ENV_NAME);
        let env = fs.create_nested_environment(&uuid)?;

        fasync::Task::spawn(fs.for_each_concurrent(None, move |connection| {
            async move {
                #![allow(unreachable_patterns)]
                match connection {
                    $setting_type(stream) => {
                        stream
                            .err_into::<anyhow::Error>()
                            .try_for_each(|req| async move {
                                match req {
                                    $($request => $callback)*
                                    _ => panic!("Incorrect command to service"),
                                }
                                Ok(())
                            })
                            .unwrap_or_else(|e: anyhow::Error| panic!(
                                "error running setui server: {:?}",
                                e
                            )).await;
                    }
                    _ => {
                        panic!("Unexpected service");
                    }
                }
            }
        })).detach();
        env
    }};
}

/// Validate that the results of the call are successful, and in the case of watch,
/// that the first item can be retrieved, but do not analyze the result.
#[macro_export]
macro_rules! assert_successful {
    ($expr:expr) => {
        // We only need an extra check on the watch so we can exercise it at least once.
        // The sets already return a result.
        if let crate::utils::Either::Watch(mut stream) = $expr.await? {
            stream.try_next().await?;
        }
    };
}

/// Validate that the results of the call are a successful watch and return the
/// first result.
#[macro_export]
macro_rules! assert_watch {
    ($expr:expr) => {
        match $expr.await? {
            crate::utils::Either::Watch(mut stream) => {
                stream.try_next().await?.expect("Watch should have a result")
            }
            crate::utils::Either::Set(_) => {
                panic!("Did not expect a set result for a watch call")
            }
            crate::utils::Either::Get(_) => {
                panic!("Did not expect a get result for a watch call")
            }
        }
    };
}

/// Validate that the results of the call are a successful set and return the result.
#[macro_export]
macro_rules! assert_set {
    ($expr:expr) => {
        match $expr.await? {
            crate::utils::Either::Set(output) => output,
            crate::utils::Either::Watch(_) => {
                panic!("Did not expect a watch result for a set call")
            }
            crate::utils::Either::Get(_) => {
                panic!("Did not expect a get result for a set call")
            }
        }
    };
}

/// Validate that the results of the call are a successful get and return the result.
#[macro_export]
macro_rules! assert_get {
    ($expr:expr) => {
        match $expr.await? {
            crate::utils::Either::Get(output) => output,
            crate::utils::Either::Watch(_) => {
                panic!("Did not expect a watch result for a get call")
            }
            crate::utils::Either::Set(_) => {
                panic!("Did not expect a set result for a get call")
            }
        }
    };
}

mod accessibility_tests;
mod audio_tests;
mod display_tests;
mod do_not_disturb_tests;
mod factory_reset_tests;
mod input_tests;
mod intl_tests;
mod light_tests;
mod night_mode_tests;
mod privacy_tests;
mod setup_tests;
mod volume_policy_tests;
