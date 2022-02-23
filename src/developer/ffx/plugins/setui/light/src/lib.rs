// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_light_args::LightGroup;
use fidl_fuchsia_settings::{LightProxy, LightState};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin("setui", LightProxy = "core/setui_service:expose:fuchsia.settings.Light")]
pub async fn run_command(light_proxy: LightProxy, light_group: LightGroup) -> Result<()> {
    handle_mixed_result("Light", command(light_proxy, light_group).await).await
}

async fn command(proxy: LightProxy, light_group: LightGroup) -> WatchOrSetResult {
    let has_name = light_group.name.is_some();
    let has_values =
        light_group.simple.len() + light_group.brightness.len() + light_group.rgb.len() > 0;

    if !has_name && !has_values {
        // No values set, perform a watch instead.
        return Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch_light_groups())));
    }

    if !has_values {
        // Only name specified, perform watch on individual light group.
        return Ok(Either::Watch(utils::watch_to_stream(proxy, move |p: &LightProxy| {
            p.watch_light_group(light_group.name.clone().unwrap().as_str())
        })));
    }

    if !has_name {
        return Err(format_err!("light group name required"));
    }

    let light_states: Vec<LightState> = light_group.clone().into();
    let result = proxy
        .set_light_group_values(
            light_group.name.clone().unwrap().as_str(),
            &mut light_states.clone().into_iter(),
        )
        .await?;

    Ok(Either::Set(match result {
        Ok(_) => format!(
            "Successfully set light group {} with values {:?}",
            light_group.name.unwrap(),
            light_states
        ),
        Err(err) => format!("{:#?}", err),
    }))
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_settings::{
        LightGroup as LightGroupSettings, LightRequest, LightType, LightValue,
    };
    use futures::prelude::*;
    use test_case::test_case;

    const TEST_NAME: &str = "test_name";
    const LIGHT_VAL_1: f64 = 0.2;
    const LIGHT_VAL_2: f64 = 0.42;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
        let proxy = setup_fake_light_proxy(move |req| match req {
            LightRequest::SetLightGroupValues { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            LightRequest::WatchLightGroups { .. } => {
                panic!("Unexpected call to watch light groups");
            }
            LightRequest::WatchLightGroup { .. } => {
                panic!("Unexpected call to watch a light group");
            }
        });

        let light = LightGroup {
            name: Some(TEST_NAME.to_string()),
            simple: vec![],
            brightness: vec![LIGHT_VAL_1, LIGHT_VAL_2],
            rgb: vec![],
        };
        let response = run_command(proxy, light).await;
        assert!(response.is_ok());
    }

    #[test_case(
        LightGroup {
            name: Some(TEST_NAME.to_string()),
            simple: vec![],
            brightness: vec![0.2, 0.42],
            rgb: vec![],
        };
        "Test light set() output with non-empty input."
    )]
    #[test_case(
        LightGroup {
            name: Some(TEST_NAME.to_string()),
            simple: vec![],
            brightness: vec![0.2, 0.42, 0.5],
            rgb: vec![],
        };
        "Test light set() output with a different non-empty input."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_light_set_output(expected_light: LightGroup) -> Result<()> {
        let proxy = setup_fake_light_proxy(move |req| match req {
            LightRequest::SetLightGroupValues { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            LightRequest::WatchLightGroups { .. } => {
                panic!("Unexpected call to watch light groups");
            }
            LightRequest::WatchLightGroup { .. } => {
                panic!("Unexpected call to watch a light group");
            }
        });

        let light_states: Vec<LightState> = expected_light.clone().into();
        let output = utils::assert_set!(command(proxy, expected_light.clone()));
        assert_eq!(
            output,
            format!(
                "Successfully set light group {} with values {:?}",
                expected_light.name.unwrap(),
                light_states
            )
        );
        Ok(())
    }

    #[test_case(
        LightGroupSettings {
            name: Some(TEST_NAME.to_string()),
            enabled: Some(false),
            type_: Some(LightType::Simple),
            lights: Some(vec![
                LightState { value: Some(LightValue::Brightness(0.2)), ..LightState::EMPTY },
                LightState { value: Some(LightValue::Brightness(0.42)), ..LightState::EMPTY }
            ]),
            ..LightGroupSettings::EMPTY
        };
        "Test light watch() output."
    )]
    #[test_case(
        LightGroupSettings {
            name: Some(TEST_NAME.to_string()),
            enabled: Some(true),
            type_: Some(LightType::Simple),
            lights: Some(vec![
                LightState { value: Some(LightValue::Brightness(0.2)), ..LightState::EMPTY },
                LightState { value: Some(LightValue::Brightness(0.42)), ..LightState::EMPTY },
                LightState { value: Some(LightValue::Brightness(0.66)), ..LightState::EMPTY }
            ]),
            ..LightGroupSettings::EMPTY
        };
        "Test light watch() output with different values."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_light_watch_output(
        expected_light_settings: LightGroupSettings,
    ) -> Result<()> {
        let val_clone = expected_light_settings.clone();
        let proxy = setup_fake_light_proxy(move |req| match req {
            LightRequest::SetLightGroupValues { .. } => {
                panic!("Unexpected call to set");
            }
            LightRequest::WatchLightGroups { responder } => {
                let _ = responder.send(&mut vec![expected_light_settings.clone()].into_iter());
            }
            LightRequest::WatchLightGroup { .. } => {
                panic!("Unexpected call to watch a light group");
            }
        });

        let output = utils::assert_watch!(command(
            proxy,
            LightGroup { name: None, simple: vec![], brightness: vec![], rgb: vec![] }
        ));
        assert_eq!(output, format!("{:#?}", vec![val_clone]));
        Ok(())
    }

    #[test_case(
        LightGroupSettings {
            name: Some(TEST_NAME.to_string()),
            enabled: Some(false),
            type_: Some(LightType::Simple),
            lights: Some(vec![
                LightState { value: Some(LightValue::Brightness(0.2)), ..LightState::EMPTY },
                LightState { value: Some(LightValue::Brightness(0.42)), ..LightState::EMPTY }
            ]),
            ..LightGroupSettings::EMPTY
        };
        "Test individual light watch() output."
    )]
    #[test_case(
        LightGroupSettings {
            name: Some(TEST_NAME.to_string()),
            enabled: Some(true),
            type_: Some(LightType::Simple),
            lights: Some(vec![
                LightState { value: Some(LightValue::Brightness(0.2)), ..LightState::EMPTY }
            ]),
            ..LightGroupSettings::EMPTY
        };
        "Test individual light watch() output with different values."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_light_watch_individual_output(
        expected_light_settings: LightGroupSettings,
    ) -> Result<()> {
        let val_clone = expected_light_settings.clone();
        let proxy = setup_fake_light_proxy(move |req| match req {
            LightRequest::SetLightGroupValues { .. } => {
                panic!("Unexpected call to set");
            }
            LightRequest::WatchLightGroups { .. } => {
                panic!("Unexpected call to watch light groups");
            }
            LightRequest::WatchLightGroup { responder, .. } => {
                let _ = responder.send(expected_light_settings.clone());
            }
        });

        let output = utils::assert_watch!(command(
            proxy,
            LightGroup {
                name: Some(TEST_NAME.to_string()),
                simple: vec![],
                brightness: vec![],
                rgb: vec![]
            }
        ));
        assert_eq!(output, format!("{:#?}", val_clone));
        Ok(())
    }
}
