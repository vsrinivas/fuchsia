// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_media::AudioRenderUsage;
use fidl_fuchsia_settings::{
    AccessibilityRequestStream, AudioRequestStream, DeviceType, DisplayRequestStream,
    DoNotDisturbRequestStream, FactoryResetRequestStream, InputRequestStream, IntlRequestStream,
    LightRequestStream, LowLightMode, NightModeRequestStream, PrivacyRequestStream,
    SetupRequestStream, ThemeType,
};
use fidl_fuchsia_settings_policy::VolumePolicyControllerRequestStream;
use fuchsia_async as fasync;

/// Validate that the results of the call are successful, and in the case of watch,
/// that the first item can be retrieved, but do not analyze the result.
#[macro_export]
macro_rules! assert_successful {
    ($expr:expr) => {
        // We only need an extra check on the watch so we can exercise it at least once.
        // The sets already return a result.
        if let ::setui_client_lib::utils::Either::Watch(mut stream) = $expr.await? {
            stream.try_next().await?;
        }
    };
}

/// Validate that the results of the call are a successful set and return the result.
#[macro_export]
macro_rules! assert_set {
    ($expr:expr) => {
        match $expr.await? {
            ::setui_client_lib::utils::Either::Set(output) => output,
            ::setui_client_lib::utils::Either::Watch(_) => {
                panic!("Did not expect a watch result for a set call")
            }
            ::setui_client_lib::utils::Either::Get(_) => {
                panic!("Did not expect a get result for a set call")
            }
        }
    };
}

/// Validate that the results of the call are a successful watch and return the
/// first result.
#[macro_export]
macro_rules! assert_watch {
    ($expr:expr) => {
        match $expr.await? {
            ::setui_client_lib::utils::Either::Watch(mut stream) => {
                stream.try_next().await?.expect("Watch should have a result")
            }
            ::setui_client_lib::utils::Either::Set(_) => {
                panic!("Did not expect a set result for a watch call")
            }
            ::setui_client_lib::utils::Either::Get(_) => {
                panic!("Did not expect a get result for a watch call")
            }
        }
    };
}

/// Validate that the results of the call are a successful get and return the result.
#[macro_export]
macro_rules! assert_get {
    ($expr:expr) => {
        match $expr.await? {
            ::setui_client_lib::utils::Either::Get(output) => output,
            ::setui_client_lib::utils::Either::Watch(_) => {
                panic!("Did not expect a watch result for a get call")
            }
            ::setui_client_lib::utils::Either::Set(_) => {
                panic!("Did not expect a set result for a get call")
            }
        }
    };
}

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

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    println!("accessibility service tests");
    println!("  client calls set");
    accessibility_tests::validate_accessibility_set().await?;

    println!("  client calls watch");
    accessibility_tests::validate_accessibility_watch().await?;

    println!("audio service tests");
    println!("  client calls audio watch");
    audio_tests::validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: None,
        level: None,
        volume_muted: None,
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - stream");
    audio_tests::validate_audio(&ExpectedStreamSettingsStruct {
        stream: Some(AudioRenderUsage::Background),
        source: None,
        level: None,
        volume_muted: None,
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - source");
    audio_tests::validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::System),
        level: None,
        volume_muted: None,
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - level");
    audio_tests::validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: None,
        level: Some(0.3),
        volume_muted: None,
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - volume_muted");
    audio_tests::validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: None,
        level: None,
        volume_muted: Some(true),
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - input_muted");
    audio_tests::validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: None,
        level: None,
        volume_muted: None,
        input_muted: Some(false),
    })
    .await?;

    println!("  client calls set audio input - multiple");
    audio_tests::validate_audio(&ExpectedStreamSettingsStruct {
        stream: Some(AudioRenderUsage::Media),
        source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
        level: Some(0.6),
        volume_muted: Some(false),
        input_muted: Some(true),
    })
    .await?;

    println!("display service tests");
    println!("  client calls display watch");
    display_tests::validate_display(None, None, None, None, None, None).await?;

    println!("  client calls set brightness");
    display_tests::validate_display(Some(0.5), None, None, None, None, None).await?;

    println!("  client calls set auto brightness");
    display_tests::validate_display(None, Some(true), None, None, None, None).await?;

    println!("  client calls set auto brightness value");
    display_tests::validate_display(None, None, Some(0.5), None, None, None).await?;

    println!("  client calls set low light mode");
    display_tests::validate_display(None, None, None, Some(LowLightMode::Enable), None, None)
        .await?;

    println!("  client calls set theme");
    display_tests::validate_display(None, None, None, None, Some(ThemeType::Dark), None).await?;

    println!("  client calls set screen enabled");
    display_tests::validate_display(None, None, None, None, Some(ThemeType::Dark), Some(false))
        .await?;

    println!("  client can modify multiple settings");
    display_tests::validate_display(
        Some(0.3),
        Some(false),
        Some(0.8),
        None,
        Some(ThemeType::Light),
        Some(true),
    )
    .await?;

    println!("factory reset tests");
    println!("  client calls set local reset allowed");
    factory_reset_tests::validate_factory_reset(true).await?;

    println!("light tests");
    println!(" client calls light set");
    light_tests::validate_light_set().await?;
    println!(" client calls watch light groups");
    light_tests::validate_light_watch().await?;
    println!(" client calls watch individual light group");
    light_tests::validate_light_watch_individual().await?;

    println!("  client calls watch light sensor");
    display_tests::validate_light_sensor().await?;

    println!("input service tests");
    println!("  client calls input watch");
    input_tests::validate_input(None).await?;

    println!("  client calls set input");
    input_tests::validate_input(Some(false)).await?;

    println!("input2 service tests");
    println!("  client calls input watch2");
    input_tests::validate_input2_watch().await?;

    println!("  client calls set input with microphone");
    input_tests::validate_input2_set(DeviceType::Microphone, "microphone", 3, "Available | Active")
        .await?;
    println!("  client calls set input with camera");
    input_tests::validate_input2_set(DeviceType::Camera, "camera", 3, "Available | Active").await?;

    println!("do not disturb service tests");
    println!("  client calls dnd watch");
    do_not_disturb_tests::validate_dnd(Some(false), Some(false)).await?;

    println!("  client calls set user initiated do not disturb");
    do_not_disturb_tests::validate_dnd(Some(true), Some(false)).await?;

    println!("  client calls set night mode initiated do not disturb");
    do_not_disturb_tests::validate_dnd(Some(false), Some(true)).await?;

    println!("intl service tests");
    println!("  client calls intl set");
    intl_tests::validate_intl_set().await?;
    println!("  client calls intl watch");
    intl_tests::validate_intl_watch().await?;

    println!("night mode service tests");
    println!("  client calls night mode watch");
    night_mode_tests::validate_night_mode(None).await?;

    println!("  client calls set night_mode_enabled");
    night_mode_tests::validate_night_mode(Some(true)).await?;

    println!("  set() output");
    night_mode_tests::validate_night_mode_set_output(true).await?;
    night_mode_tests::validate_night_mode_set_output(false).await?;

    println!("  watch() output");
    night_mode_tests::validate_night_mode_watch_output(None).await?;
    night_mode_tests::validate_night_mode_watch_output(Some(true)).await?;
    night_mode_tests::validate_night_mode_watch_output(Some(false)).await?;

    println!("privacy service tests");
    println!("  client calls privacy watch");
    privacy_tests::validate_privacy(None).await?;

    println!("  client calls set user_data_sharing_consent");
    privacy_tests::validate_privacy(Some(true)).await?;

    println!("  set() output");
    privacy_tests::validate_privacy_set_output(true).await?;
    privacy_tests::validate_privacy_set_output(false).await?;

    println!("  watch() output");
    privacy_tests::validate_privacy_watch_output(None).await?;
    privacy_tests::validate_privacy_watch_output(Some(true)).await?;
    privacy_tests::validate_privacy_watch_output(Some(false)).await?;

    println!("setup service tests");
    println!(" client calls set config interfaces");
    setup_tests::validate_setup().await?;

    println!("volume policy tests");
    println!("  client calls get");
    volume_policy_tests::validate_volume_policy_get().await?;
    println!("  client calls add");
    volume_policy_tests::validate_volume_policy_add().await?;
    println!("  client calls remove");
    volume_policy_tests::validate_volume_policy_remove().await?;

    Ok(())
}

// Creates a service in an environment for a given setting type.
// Usage: create_service!(service_enum_name,
//          request_name => {code block},
//          request2_name => {code_block}
//          ... );
#[macro_export]
macro_rules! create_service  {
    ($setting_type:path, $( $request:pat => $callback:block ),*) => {{

        let mut fs = ServiceFs::new();
        fs.add_fidl_service($setting_type);
        let env = fs.create_nested_environment(ENV_NAME)?;

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
