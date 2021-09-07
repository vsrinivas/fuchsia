// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Services;
use crate::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{
    LightGroup, LightMarker, LightRequest, LightState, LightType, LightValue,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use setui_client_lib::light;

pub(crate) async fn validate_light_set() -> Result<(), Error> {
    const TEST_NAME: &str = "test_name";
    const LIGHT_VAL_1: f64 = 0.2;
    const LIGHT_VAL_2: f64 = 0.42;

    let env = create_service!(Services::Light,
        LightRequest::SetLightGroupValues { name, state, responder } => {
            assert_eq!(name, TEST_NAME);
            assert_eq!(state, vec![LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)), ..LightState::EMPTY },
            LightState { value: Some(LightValue::Brightness(LIGHT_VAL_2)), ..LightState::EMPTY }]);
            responder.send(&mut Ok(()))?;
        }
    );

    let light_service =
        env.connect_to_protocol::<LightMarker>().context("Failed to connect to light service")?;

    assert_set!(light::command(
        light_service,
        setui_client_lib::LightGroup {
            name: Some(TEST_NAME.to_string()),
            simple: vec![],
            brightness: vec![LIGHT_VAL_1, LIGHT_VAL_2],
            rgb: vec![],
        },
    ));
    Ok(())
}

pub(crate) async fn validate_light_watch() -> Result<(), Error> {
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
                        LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)), ..LightState::EMPTY },
                        LightState { value: Some(LightValue::Brightness(LIGHT_VAL_2)), ..LightState::EMPTY }
                    ]),
                    ..LightGroup::EMPTY
                }
            ].into_iter())?;
        }
    );

    let light_service =
        env.connect_to_protocol::<LightMarker>().context("Failed to connect to light service")?;

    let output = assert_watch!(light::command(
        light_service,
        setui_client_lib::LightGroup {
            name: None,
            simple: vec![],
            brightness: vec![],
            rgb: vec![],
        },
    ));
    assert_eq!(
        output,
        format!(
            "{:#?}",
            vec![LightGroup {
                name: Some(TEST_NAME.to_string()),
                enabled: Some(ENABLED),
                type_: Some(LIGHT_TYPE),
                lights: Some(vec![
                    LightState {
                        value: Some(LightValue::Brightness(LIGHT_VAL_1)),
                        ..LightState::EMPTY
                    },
                    LightState {
                        value: Some(LightValue::Brightness(LIGHT_VAL_2)),
                        ..LightState::EMPTY
                    }
                ]),
                ..LightGroup::EMPTY
            }]
        )
    );
    Ok(())
}

pub(crate) async fn validate_light_watch_individual() -> Result<(), Error> {
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
                        LightState { value: Some(LightValue::Brightness(LIGHT_VAL_1)), ..LightState::EMPTY },
                        LightState { value: Some(LightValue::Brightness(LIGHT_VAL_2)), ..LightState::EMPTY }
                    ]),
                    ..LightGroup::EMPTY
                })?;
        }
    );

    let light_service =
        env.connect_to_protocol::<LightMarker>().context("Failed to connect to light service")?;

    let output = assert_watch!(light::command(
        light_service,
        setui_client_lib::LightGroup {
            name: Some(TEST_NAME.to_string()),
            simple: vec![],
            brightness: vec![],
            rgb: vec![],
        },
    ));
    assert_eq!(
        output,
        format!(
            "{:#?}",
            LightGroup {
                name: Some(TEST_NAME.to_string()),
                enabled: Some(ENABLED),
                type_: Some(LIGHT_TYPE),
                lights: Some(vec![
                    LightState {
                        value: Some(LightValue::Brightness(LIGHT_VAL_1)),
                        ..LightState::EMPTY
                    },
                    LightState {
                        value: Some(LightValue::Brightness(LIGHT_VAL_2)),
                        ..LightState::EMPTY
                    }
                ]),
                ..LightGroup::EMPTY
            }
        )
    );
    Ok(())
}
