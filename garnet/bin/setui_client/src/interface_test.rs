// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_intl::{LocaleId, TemperatureUnit, TimeZoneId},
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    parking_lot::RwLock,
    setui_client_lib::accessibility,
    setui_client_lib::audio,
    setui_client_lib::device,
    setui_client_lib::display,
    setui_client_lib::do_not_disturb,
    setui_client_lib::input,
    setui_client_lib::intl,
    setui_client_lib::light,
    setui_client_lib::night_mode,
    setui_client_lib::privacy,
    setui_client_lib::setup,
    setui_client_lib::{AccessibilityOptions, CaptionCommands, CaptionFontStyle, CaptionOptions},
    std::sync::Arc,
};

enum Services {
    Accessibility(AccessibilityRequestStream),
    Audio(AudioRequestStream),
    Device(DeviceRequestStream),
    Display(DisplayRequestStream),
    DoNotDisturb(DoNotDisturbRequestStream),
    Input(InputRequestStream),
    Intl(IntlRequestStream),
    Light(LightRequestStream),
    NightMode(NightModeRequestStream),
    Privacy(PrivacyRequestStream),
    Setup(SetupRequestStream),
}

struct ExpectedStreamSettingsStruct {
    stream: Option<fidl_fuchsia_media::AudioRenderUsage>,
    source: Option<fidl_fuchsia_settings::AudioStreamSettingSource>,
    level: Option<f32>,
    volume_muted: Option<bool>,
    input_muted: Option<bool>,
}

const ENV_NAME: &str = "setui_client_test_environment";
const TEST_BUILD_TAG: &str = "0.20190909.1.0";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    println!("accessibility service tests");
    println!("  client calls set");
    validate_accessibility_set().await?;

    println!("  client calls watch");
    validate_accessibility_watch().await?;

    println!("audio service tests");
    println!("  client calls audio watch");
    validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: None,
        level: None,
        volume_muted: None,
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - stream");
    validate_audio(&ExpectedStreamSettingsStruct {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Background),
        source: None,
        level: None,
        volume_muted: None,
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - source");
    validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::System),
        level: None,
        volume_muted: None,
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - level");
    validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: None,
        level: Some(0.3),
        volume_muted: None,
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - volume_muted");
    validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: None,
        level: None,
        volume_muted: Some(true),
        input_muted: None,
    })
    .await?;

    println!("  client calls set audio input - input_muted");
    validate_audio(&ExpectedStreamSettingsStruct {
        stream: None,
        source: None,
        level: None,
        volume_muted: None,
        input_muted: Some(false),
    })
    .await?;

    println!("  client calls set audio input - multiple");
    validate_audio(&ExpectedStreamSettingsStruct {
        stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
        source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
        level: Some(0.6),
        volume_muted: Some(false),
        input_muted: Some(true),
    })
    .await?;

    println!("device service tests");
    println!("  client calls device watch");
    validate_device().await?;

    println!("display service tests");
    println!("  client calls display watch");
    validate_display(None, None, None, None).await?;

    println!("  client calls set brightness");
    validate_display(Some(0.5), None, None, None).await?;

    println!("  client calls set auto brightness");
    validate_display(None, Some(true), None, None).await?;

    println!("  client calls set user brightness offset");
    validate_display(None, None, Some(0.5), None).await?;

    println!("  client calls set low light mode");
    validate_display(None, None, None, Some(LowLightMode::Enable)).await?;

    println!("light tests");
    println!(" client calls light set");
    validate_light_set().await?;
    println!(" client calls watch light groups");
    validate_light_watch().await?;
    println!(" client calls watch individual light group");
    validate_light_watch_individual().await?;

    println!("  client calls watch light sensor");
    validate_light_sensor().await?;

    println!("input service tests");
    println!("  client calls input watch");
    validate_input(None).await?;

    println!("  client calls set input");
    validate_input(Some(false)).await?;

    println!("do not disturb service tests");
    println!("  client calls dnd watch");
    validate_dnd(Some(false), Some(false)).await?;

    println!("  client calls set user initiated do not disturb");
    validate_dnd(Some(true), Some(false)).await?;

    println!("  client calls set night mode initiated do not disturb");
    validate_dnd(Some(false), Some(true)).await?;

    println!("intl service tests");
    println!("  client calls intl set");
    validate_intl_set().await?;
    println!("  client calls intl watch");
    validate_intl_watch().await?;

    println!("night mode service tests");
    println!("  client calls night mode watch");
    validate_night_mode(None).await?;

    println!("  client calls set night_mode_enabled");
    validate_night_mode(Some(true)).await?;

    println!("  set() output");
    validate_night_mode_set_output(true).await?;
    validate_night_mode_set_output(false).await?;

    println!("  watch() output");
    validate_night_mode_watch_output(None).await?;
    validate_night_mode_watch_output(Some(true)).await?;
    validate_night_mode_watch_output(Some(false)).await?;

    println!("privacy service tests");
    println!("  client calls privacy watch");
    validate_privacy(None).await?;

    println!("  client calls set user_data_sharing_consent");
    validate_privacy(Some(true)).await?;

    println!("  set() output");
    validate_privacy_set_output(true).await?;
    validate_privacy_set_output(false).await?;

    println!("  watch() output");
    validate_privacy_watch_output(None).await?;
    validate_privacy_watch_output(Some(true)).await?;
    validate_privacy_watch_output(Some(false)).await?;

    println!("setup service tests");
    println!(" client calls set config interfaces");
    validate_setup().await?;

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

async fn validate_intl_set() -> Result<(), Error> {
    const TEST_TIME_ZONE: &str = "GMT";
    const TEST_TEMPERATURE_UNIT: TemperatureUnit = TemperatureUnit::Celsius;
    const TEST_LOCALE: &str = "blah";
    const TEST_HOUR_CYCLE: fidl_fuchsia_settings::HourCycle = fidl_fuchsia_settings::HourCycle::H12;

    let env = create_service!(Services::Intl,
        IntlRequest::Set { settings, responder } => {
            assert_eq!(Some(TimeZoneId { id: TEST_TIME_ZONE.to_string() }), settings.time_zone_id);
            assert_eq!(Some(TEST_TEMPERATURE_UNIT), settings.temperature_unit);
            assert_eq!(Some(vec![LocaleId { id: TEST_LOCALE.into() }]), settings.locales);
            assert_eq!(Some(TEST_HOUR_CYCLE), settings.hour_cycle);
            responder.send(&mut Ok(()))?;
    });

    let intl_service =
        env.connect_to_service::<IntlMarker>().context("Failed to connect to intl service")?;

    intl::command(
        intl_service,
        Some(TimeZoneId { id: TEST_TIME_ZONE.to_string() }),
        Some(TEST_TEMPERATURE_UNIT),
        vec![LocaleId { id: TEST_LOCALE.into() }],
        Some(TEST_HOUR_CYCLE),
        false,
    )
    .await?;

    Ok(())
}

async fn validate_intl_watch() -> Result<(), Error> {
    const TEST_TIME_ZONE: &str = "GMT";
    const TEST_TEMPERATURE_UNIT: TemperatureUnit = TemperatureUnit::Celsius;
    const TEST_LOCALE: &str = "blah";
    const TEST_HOUR_CYCLE: fidl_fuchsia_settings::HourCycle = fidl_fuchsia_settings::HourCycle::H12;

    let env = create_service!(Services::Intl,
        IntlRequest::Watch { responder } => {
            responder.send(IntlSettings {
                locales: Some(vec![LocaleId { id: TEST_LOCALE.into() }]),
                temperature_unit: Some(TEST_TEMPERATURE_UNIT),
                time_zone_id: Some(TimeZoneId { id: TEST_TIME_ZONE.to_string() }),
                hour_cycle: Some(TEST_HOUR_CYCLE),
            })?;
        }
    );

    let intl_service =
        env.connect_to_service::<IntlMarker>().context("Failed to connect to intl service")?;

    let output = intl::command(intl_service, None, None, vec![], None, false).await?;

    assert_eq!(
        output,
        format!(
            "{:#?}",
            IntlSettings {
                locales: Some(vec![LocaleId { id: TEST_LOCALE.into() }]),
                temperature_unit: Some(TEST_TEMPERATURE_UNIT),
                time_zone_id: Some(TimeZoneId { id: TEST_TIME_ZONE.to_string() }),
                hour_cycle: Some(TEST_HOUR_CYCLE),
            }
        )
    );

    Ok(())
}

async fn validate_device() -> Result<(), Error> {
    let env = create_service!(Services::Device,
        DeviceRequest::Watch { responder } => {
            responder.send(DeviceSettings {
                build_tag: Some(TEST_BUILD_TAG.to_string()),
            })?;
        }
    );

    let device_service =
        env.connect_to_service::<DeviceMarker>().context("Failed to connect to device service")?;

    device::command(device_service).await?;

    Ok(())
}

// Can only check one mutate option at once
async fn validate_display(
    expected_brightness: Option<f32>,
    expected_auto_brightness: Option<bool>,
    expected_user_brightness_offset: Option<f32>,
    expected_low_light_mode: Option<LowLightMode>,
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
            } else if let (Some(user_brightness_offset), Some(expected_user_brightness_offset_value)) =
              (settings.user_brightness_offset, expected_user_brightness_offset) {
                assert_eq!(user_brightness_offset, expected_user_brightness_offset_value);
                responder.send(&mut Ok(()))?;
            } else if let (Some(low_light_mode), Some(expected_low_light_mode_value)) =
              (settings.low_light_mode, expected_low_light_mode) {
                assert_eq!(low_light_mode, expected_low_light_mode_value);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        DisplayRequest::Watch { responder } => {
            responder.send(DisplaySettings {
                auto_brightness: Some(false),
                brightness_value: Some(0.5),
                user_brightness_offset: Some(0.5),
                low_light_mode: Some(LowLightMode::Disable),
                screen_enabled: Some(true),
            })?;
        }
    );

    let display_service = env
        .connect_to_service::<DisplayMarker>()
        .context("Failed to connect to display service")?;

    display::command(
        display_service,
        expected_brightness,
        expected_auto_brightness,
        false,
        expected_low_light_mode,
    )
    .await?;

    Ok(())
}

// Can only check one mutate option at once
async fn validate_light_sensor() -> Result<(), Error> {
    let watch_called = Arc::new(RwLock::new(false));

    let watch_called_clone = watch_called.clone();

    let (display_service, mut stream) =
        fidl::endpoints::create_proxy_and_stream::<DisplayMarker>().unwrap();

    fasync::Task::spawn(async move {
        while let Some(request) = stream.try_next().await.unwrap() {
            match request {
                DisplayRequest::WatchLightSensor2 { delta: _, responder } => {
                    *watch_called_clone.write() = true;
                    responder
                        .send(LightSensorData {
                            illuminance_lux: Some(100.0),
                            color: Some(fidl_fuchsia_ui_types::ColorRgb {
                                red: 25.0,
                                green: 16.0,
                                blue: 59.0,
                            }),
                        })
                        .unwrap();
                }
                _ => {}
            }
        }
    })
    .detach();

    display::command(display_service, None, None, true, None).await?;

    assert_eq!(*watch_called.read(), true);

    Ok(())
}

async fn validate_accessibility_set() -> Result<(), Error> {
    const TEST_COLOR: fidl_fuchsia_ui_types::ColorRgba =
        fidl_fuchsia_ui_types::ColorRgba { red: 238.0, green: 23.0, blue: 128.0, alpha: 255.0 };
    let expected_options: AccessibilityOptions = AccessibilityOptions {
        audio_description: Some(true),
        screen_reader: Some(true),
        color_inversion: Some(false),
        enable_magnification: Some(false),
        color_correction: Some(ColorBlindnessType::Protanomaly),
        caption_options: Some(CaptionCommands::CaptionOptions(CaptionOptions {
            for_media: Some(true),
            for_tts: Some(false),
            window_color: Some(TEST_COLOR),
            background_color: Some(TEST_COLOR),
            style: CaptionFontStyle {
                font_family: Some(CaptionFontFamily::Cursive),
                font_color: Some(TEST_COLOR),
                relative_size: Some(1.0),
                char_edge_style: Some(EdgeStyle::Raised),
            },
        })),
    };

    let env = create_service!(
        Services::Accessibility, AccessibilityRequest::Set { settings, responder } => {
            assert_eq!(expected_options.audio_description, settings.audio_description);
            assert_eq!(expected_options.screen_reader, settings.screen_reader);
            assert_eq!(expected_options.color_inversion, settings.color_inversion);
            assert_eq!(expected_options.enable_magnification, settings.enable_magnification);
            assert_eq!(expected_options.color_correction, settings.color_correction);

            // If no caption options are provided, then captions_settings field in service should
            // also be None. The inverse of this should also be true.
            assert_eq!(expected_options.caption_options.is_some(), settings.captions_settings.is_some());
            match (settings.captions_settings, expected_options.caption_options) {
                (Some(captions_settings), Some(caption_settings_enum)) => {
                    let CaptionCommands::CaptionOptions(input) = caption_settings_enum;

                    assert_eq!(input.for_media, captions_settings.for_media);
                    assert_eq!(input.for_tts, captions_settings.for_tts);
                    assert_eq!(input.window_color, captions_settings.window_color);
                    assert_eq!(input.background_color, captions_settings.background_color);

                    if let Some(font_style) = captions_settings.font_style {
                        let input_style = input.style;

                        assert_eq!(input_style.font_family, font_style.family);
                        assert_eq!(input_style.font_color, font_style.color);
                        assert_eq!(input_style.relative_size, font_style.relative_size);
                        assert_eq!(input_style.char_edge_style, font_style.char_edge_style);
                    }
                }
                _ => {}
            }

            responder.send(&mut Ok(()))?;
        }
    );

    let accessibility_service = env
        .connect_to_service::<AccessibilityMarker>()
        .context("Failed to connect to accessibility service")?;

    let output = accessibility::command(accessibility_service, expected_options).await?;

    assert_eq!(output, "Successfully set AccessibilitySettings");

    Ok(())
}

async fn validate_accessibility_watch() -> Result<(), Error> {
    let env = create_service!(
        Services::Accessibility,
        AccessibilityRequest::Watch { responder } => {
            responder.send(AccessibilitySettings::empty())?;
        }
    );

    let accessibility_service = env
        .connect_to_service::<AccessibilityMarker>()
        .context("Failed to connect to accessibility service")?;

    let output =
        accessibility::command(accessibility_service, AccessibilityOptions::default()).await?;

    assert_eq!(output, format!("{:#?}", AccessibilitySettings::empty()));

    Ok(())
}

async fn validate_audio(expected: &'static ExpectedStreamSettingsStruct) -> Result<(), Error> {
    let env = create_service!(Services::Audio,
        AudioRequest::Set { settings, responder } => {
            if let Some(streams) = settings.streams {
                verify_streams(streams, expected);
                responder.send(&mut (Ok(())))?;
            } else if let Some(input) = settings.input {
                if let (Some(input_muted), Some(expected_input_muted)) =
                    (input.muted, expected.input_muted) {
                    assert_eq!(input_muted, expected_input_muted);
                    responder.send(&mut (Ok(())))?;
                }
            }
        },
        AudioRequest::Watch { responder } => {
            responder.send(AudioSettings {
                streams: Some(vec![AudioStreamSettings {
                    stream: Some(fidl_fuchsia_media::AudioRenderUsage::Media),
                    source: Some(fidl_fuchsia_settings::AudioStreamSettingSource::User),
                    user_volume: Some(Volume {
                        level: Some(0.6),
                        muted: Some(false)
                    }),
                }]),
                input: Some(AudioInput {
                    muted: Some(true)
                }),
            })?;
        }
    );

    let audio_service =
        env.connect_to_service::<AudioMarker>().context("Failed to connect to audio service")?;

    audio::command(
        audio_service,
        expected.stream,
        expected.source,
        expected.level,
        expected.volume_muted,
        expected.input_muted,
    )
    .await?;
    Ok(())
}

async fn validate_input(expected_mic_muted: Option<bool>) -> Result<(), Error> {
    let env = create_service!(Services::Input,
        InputRequest::Set { settings, responder } => {
            if let Some(Microphone { muted }) = settings.microphone {
                assert_eq!(expected_mic_muted, muted);
                responder.send(&mut (Ok(())))?;
            }
        },
        InputRequest::Watch { responder } => {
            responder.send(InputDeviceSettings {
                microphone: Some(Microphone {
                    muted: expected_mic_muted,
                })
            })?;
        }
    );

    let input_service =
        env.connect_to_service::<InputMarker>().context("Failed to connect to input service")?;

    let output = input::command(input_service, expected_mic_muted).await?;
    if expected_mic_muted.is_none() {
        assert_eq!(
            output,
            format!(
                "{:#?}",
                InputDeviceSettings { microphone: Some(Microphone { muted: expected_mic_muted }) }
            )
        );
    } else {
        assert_eq!(
            output,
            format!("Successfully set mic mute to {}\n", expected_mic_muted.unwrap())
        );
    }

    Ok(())
}

fn verify_streams(
    streams: Vec<AudioStreamSettings>,
    expected: &'static ExpectedStreamSettingsStruct,
) {
    let extracted_stream_settings = streams.get(0).unwrap();
    if let (Some(stream), Some(expected_stream)) =
        (extracted_stream_settings.stream, expected.stream)
    {
        assert_eq!(stream, expected_stream);
    }
    if let (Some(source), Some(expected_source)) =
        (extracted_stream_settings.source, expected.source)
    {
        assert_eq!(source, expected_source);
    }
    if let Some(user_volume) = extracted_stream_settings.user_volume.as_ref() {
        if let (Some(level), Some(expected_level)) = (user_volume.level, expected.level) {
            assert_eq!(level, expected_level);
        }
        if let (Some(volume_muted), Some(expected_volume_muted)) =
            (user_volume.muted, expected.volume_muted)
        {
            assert_eq!(volume_muted, expected_volume_muted);
        }
    }
}

async fn validate_dnd(
    expected_user_dnd: Option<bool>,
    expected_night_mode_dnd: Option<bool>,
) -> Result<(), Error> {
    let env = create_service!(Services::DoNotDisturb,
        DoNotDisturbRequest::Set { settings, responder } => {
            if let(Some(user_dnd), Some(expected_user_dnd)) =
                (settings.user_initiated_do_not_disturb, expected_user_dnd) {
                assert_eq!(user_dnd, expected_user_dnd);
                responder.send(&mut Ok(()))?;
            } else if let (Some(night_mode_dnd), Some(expected_night_mode_dnd)) =
                (settings.night_mode_initiated_do_not_disturb, expected_night_mode_dnd) {
                assert_eq!(night_mode_dnd, expected_night_mode_dnd);
                responder.send(&mut (Ok(())))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        DoNotDisturbRequest::Watch { responder } => {
            responder.send(DoNotDisturbSettings {
                user_initiated_do_not_disturb: Some(false),
                night_mode_initiated_do_not_disturb: Some(false),
            })?;
        }
    );

    let do_not_disturb_service = env
        .connect_to_service::<DoNotDisturbMarker>()
        .context("Failed to connect to do not disturb service")?;

    do_not_disturb::command(do_not_disturb_service, expected_user_dnd, expected_night_mode_dnd)
        .await?;

    Ok(())
}

async fn validate_light_set() -> Result<(), Error> {
    const TEST_NAME: &str = "test_name";
    const LIGHT_VAL_1: f64 = 0.2;
    const LIGHT_VAL_2: f64 = 0.42;

    let env = create_service!(Services::Light,
        LightRequest::SetLightGroupValues { name, state, responder } => {
            assert_eq!(name, TEST_NAME);
            assert_eq!(state, vec![LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)) },
            LightState { value: Some(LightValue::Brightness(LIGHT_VAL_2)) }]);
            responder.send(&mut Ok(()))?;
        }
    );

    let light_service =
        env.connect_to_service::<LightMarker>().context("Failed to connect to light service")?;

    light::command(
        light_service,
        setui_client_lib::LightGroup {
            name: Some(TEST_NAME.to_string()),
            simple: vec![],
            brightness: vec![LIGHT_VAL_1, LIGHT_VAL_2],
            rgb: vec![],
        },
    )
    .await?;

    Ok(())
}

async fn validate_light_watch() -> Result<(), Error> {
    const TEST_NAME: &str = "test_name";
    const ENABLED: bool = false;
    const LIGHT_TYPE: LightType = LightType::Simple;
    const LIGHT_VAL_1: f64 = 0.2;
    const LIGHT_VAL_2: f64 = 0.42;

    let env = create_service!(Services::Light,
        LightRequest::WatchLightGroups { responder } => {
            responder.send(&mut vec![
                LightGroup {
                    name: Some(TEST_NAME.to_string()),
                    enabled: Some(ENABLED),
                    type_: Some(LIGHT_TYPE),
                    lights: Some(vec![
                        LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)) },
                        LightState { value: Some(LightValue::Brightness(LIGHT_VAL_2)) }
                    ])
                }
            ].into_iter())?;
        }
    );

    let light_service =
        env.connect_to_service::<LightMarker>().context("Failed to connect to light service")?;

    let output = light::command(
        light_service,
        setui_client_lib::LightGroup {
            name: None,
            simple: vec![],
            brightness: vec![],
            rgb: vec![],
        },
    )
    .await?;

    assert_eq!(
        output,
        format!(
            "{:#?}",
            vec![LightGroup {
                name: Some(TEST_NAME.to_string()),
                enabled: Some(ENABLED),
                type_: Some(LIGHT_TYPE),
                lights: Some(vec![
                    LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)) },
                    LightState { value: Some(LightValue::Brightness(LIGHT_VAL_2)) }
                ])
            }]
        )
    );

    Ok(())
}

async fn validate_light_watch_individual() -> Result<(), Error> {
    const TEST_NAME: &str = "test_name";
    const ENABLED: bool = false;
    const LIGHT_TYPE: LightType = LightType::Simple;
    const LIGHT_VAL_1: f64 = 0.2;
    const LIGHT_VAL_2: f64 = 0.42;

    let env = create_service!(Services::Light,
        LightRequest::WatchLightGroup { name, responder } => {
            responder.send(LightGroup {
                    name: Some(name),
                    enabled: Some(ENABLED),
                    type_: Some(LIGHT_TYPE),
                    lights: Some(vec![
                        LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)) },
                        LightState { value: Some(LightValue::Brightness(LIGHT_VAL_2)) }
                    ])
                })?;
        }
    );

    let light_service =
        env.connect_to_service::<LightMarker>().context("Failed to connect to light service")?;

    let output = light::command(
        light_service,
        setui_client_lib::LightGroup {
            name: Some(TEST_NAME.to_string()),
            simple: vec![],
            brightness: vec![],
            rgb: vec![],
        },
    )
    .await?;

    assert_eq!(
        output,
        format!(
            "{:#?}",
            LightGroup {
                name: Some(TEST_NAME.to_string()),
                enabled: Some(ENABLED),
                type_: Some(LIGHT_TYPE),
                lights: Some(vec![
                    LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)) },
                    LightState { value: Some(LightValue::Brightness(LIGHT_VAL_2)) }
                ])
            }
        )
    );

    Ok(())
}

async fn validate_night_mode(expected_night_mode_enabled: Option<bool>) -> Result<(), Error> {
    let env = create_service!(
        Services::NightMode, NightModeRequest::Set { settings, responder, } => {
            if let (Some(night_mode_enabled), Some(expected_night_mode_enabled_value)) =
                (settings.night_mode_enabled, expected_night_mode_enabled) {
                assert_eq!(night_mode_enabled, expected_night_mode_enabled_value);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        NightModeRequest::Watch { responder } => {
            responder.send(NightModeSettings {
                night_mode_enabled: Some(false),
            })?;
        }
    );

    let night_mode_service = env
        .connect_to_service::<NightModeMarker>()
        .context("Failed to connect to night mode service")?;

    night_mode::command(night_mode_service, expected_night_mode_enabled).await?;

    Ok(())
}

async fn validate_night_mode_set_output(expected_night_mode_enabled: bool) -> Result<(), Error> {
    let env = create_service!(
        Services::NightMode, NightModeRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        NightModeRequest::Watch { responder } => {
            responder.send(NightModeSettings {
                night_mode_enabled: Some(expected_night_mode_enabled),
            })?;
        }
    );

    let night_mode_service = env
        .connect_to_service::<NightModeMarker>()
        .context("Failed to connect to night mode service")?;

    let output = night_mode::command(night_mode_service, Some(expected_night_mode_enabled)).await?;

    assert_eq!(
        output,
        format!("Successfully set night_mode_enabled to {}", expected_night_mode_enabled)
    );

    Ok(())
}

async fn validate_night_mode_watch_output(
    expected_night_mode_enabled: Option<bool>,
) -> Result<(), Error> {
    let env = create_service!(
        Services::NightMode, NightModeRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        NightModeRequest::Watch { responder } => {
            responder.send(NightModeSettings {
                night_mode_enabled: expected_night_mode_enabled,
            })?;
        }
    );

    let night_mode_service = env
        .connect_to_service::<NightModeMarker>()
        .context("Failed to connect to night_mode service")?;

    // Pass in None to call Watch() on the service.
    let output = night_mode::command(night_mode_service, None).await?;

    assert_eq!(
        output,
        format!("{:#?}", NightModeSettings { night_mode_enabled: expected_night_mode_enabled })
    );

    Ok(())
}

async fn validate_privacy(expected_user_data_sharing_consent: Option<bool>) -> Result<(), Error> {
    let env = create_service!(
        Services::Privacy, PrivacyRequest::Set { settings, responder, } => {
            if let (Some(user_data_sharing_consent), Some(expected_user_data_sharing_consent_value)) =
                (settings.user_data_sharing_consent, expected_user_data_sharing_consent) {
                assert_eq!(user_data_sharing_consent, expected_user_data_sharing_consent_value);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        PrivacyRequest::Watch { responder } => {
            responder.send(PrivacySettings {
                user_data_sharing_consent: Some(false),
            })?;
        }
    );

    let privacy_service = env
        .connect_to_service::<PrivacyMarker>()
        .context("Failed to connect to privacy service")?;

    privacy::command(privacy_service, expected_user_data_sharing_consent).await?;

    Ok(())
}

async fn validate_privacy_set_output(
    expected_user_data_sharing_consent: bool,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Privacy, PrivacyRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        PrivacyRequest::Watch { responder } => {
            responder.send(PrivacySettings {
                user_data_sharing_consent: Some(expected_user_data_sharing_consent),
            })?;
        }
    );

    let privacy_service = env
        .connect_to_service::<PrivacyMarker>()
        .context("Failed to connect to privacy service")?;

    let output =
        privacy::command(privacy_service, Some(expected_user_data_sharing_consent)).await?;

    assert_eq!(
        output,
        format!(
            "Successfully set user_data_sharing_consent to {}",
            expected_user_data_sharing_consent
        )
    );

    Ok(())
}

async fn validate_privacy_watch_output(
    expected_user_data_sharing_consent: Option<bool>,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Privacy, PrivacyRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        PrivacyRequest::Watch { responder } => {
            responder.send(PrivacySettings {
                user_data_sharing_consent: expected_user_data_sharing_consent,
            })?;
        }
    );

    let privacy_service = env
        .connect_to_service::<PrivacyMarker>()
        .context("Failed to connect to privacy service")?;

    // Pass in None to call Watch() on the service.
    let output = privacy::command(privacy_service, None).await?;

    assert_eq!(
        output,
        format!(
            "{:?}",
            PrivacySettings { user_data_sharing_consent: expected_user_data_sharing_consent }
        )
    );

    Ok(())
}

fn create_setup_setting(interfaces: ConfigurationInterfaces) -> SetupSettings {
    let mut settings = SetupSettings::empty();
    settings.enabled_configuration_interfaces = Some(interfaces);

    settings
}

async fn validate_setup() -> Result<(), Error> {
    let expected_set_interfaces = ConfigurationInterfaces::Ethernet;
    let expected_watch_interfaces =
        ConfigurationInterfaces::Wifi | ConfigurationInterfaces::Ethernet;
    let env = create_service!(
        Services::Setup, SetupRequest::Set { settings, responder, } => {
            if let Some(interfaces) = settings.enabled_configuration_interfaces {
                assert_eq!(interfaces, expected_set_interfaces);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        SetupRequest::Watch { responder } => {
            responder.send(create_setup_setting(expected_watch_interfaces))?;
        }
    );

    let setup_service =
        env.connect_to_service::<SetupMarker>().context("Failed to connect to setup service")?;

    setup::command(setup_service.clone(), Some(expected_set_interfaces)).await?;

    let watch_result = setup::command(setup_service.clone(), None).await?;

    assert_eq!(
        watch_result,
        setup::describe_setup_setting(&create_setup_setting(expected_watch_interfaces))
    );

    Ok(())
}
