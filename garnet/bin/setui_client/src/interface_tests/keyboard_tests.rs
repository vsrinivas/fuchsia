// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{keyboard, Keyboard};
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{KeyboardMarker, KeyboardRequest, KeyboardSettings};

use crate::interface_tests::Services;
use crate::interface_tests::ENV_NAME;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use test_case::test_case;

#[test_case(
    Keyboard { keymap: None, autorepeat_delay: Some(0), autorepeat_period: Some(0) };
    "Test keyboard client calls keyboard watch."
)]
#[test_case(
    Keyboard {
        keymap: Some(fidl_fuchsia_input::KeymapId::FrAzerty),
        autorepeat_delay: Some(1),
        autorepeat_period: Some(2),
    }; "Test keyboard client calls set Keyboard."
)]
#[fuchsia_async::run_until_stalled(test)]
async fn validate_keyboard(expected_keyboard: Keyboard) -> Result<(), Error> {
    let env = create_service!(
        Services::Keyboard, KeyboardRequest::Set { settings, responder, } => {
            assert_eq!(Keyboard::from(settings), expected_keyboard);
            responder.send(&mut Ok(()))?;
        },
        KeyboardRequest::Watch { responder } => {
            responder.send(KeyboardSettings {
                keymap: Some(fidl_fuchsia_input::KeymapId::UsQwerty),
                autorepeat: None,
                ..KeyboardSettings::EMPTY
            })?;
        }
    );

    let keyboard_service = env
        .connect_to_protocol::<KeyboardMarker>()
        .context("Failed to connect to keyboard service")?;

    assert_successful!(keyboard::command(keyboard_service, expected_keyboard,));
    Ok(())
}

#[test_case(
    Keyboard {
        keymap: Some(fidl_fuchsia_input::KeymapId::FrAzerty),
        autorepeat_delay: Some(-1),
        autorepeat_period: Some(-2),
    }; "Test keyboard invalid autorepeat inputs."
)]
#[fuchsia_async::run_until_stalled(test)]
async fn validate_keyboard_failure(expected_keyboard: Keyboard) -> Result<(), Error> {
    let env = create_service!(
        Services::Keyboard, KeyboardRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        KeyboardRequest::Watch { responder } => {
            responder.send(KeyboardSettings::from(expected_keyboard))?;
        }
    );

    let keyboard_service = env
        .connect_to_protocol::<KeyboardMarker>()
        .context("Failed to connect to keyboard service")?;

    let result = keyboard::command(keyboard_service, expected_keyboard).await;
    match result {
        Err(e) => assert!(
            format!("{:?}", e).contains("Negative values are invalid for autorepeat values.")
        ),
        _ => panic!("Should return errors."),
    }
    Ok(())
}

#[test_case(
    Keyboard {
        keymap: Some(fidl_fuchsia_input::KeymapId::UsQwerty),
        autorepeat_delay: Some(2),
        autorepeat_period: Some(3),
    }; "Test keyboard set() output."
)]
#[test_case(
    Keyboard {
        keymap: Some(fidl_fuchsia_input::KeymapId::UsDvorak),
        autorepeat_delay: Some(3),
        autorepeat_period: Some(4),
    }; "Test keyboard set() output with different values."
)]
#[fuchsia_async::run_until_stalled(test)]
async fn validate_keyboard_set_output(expected_keyboard: Keyboard) -> Result<(), Error> {
    let env = create_service!(
        Services::Keyboard, KeyboardRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        KeyboardRequest::Watch { responder } => {
            responder.send(KeyboardSettings::from(expected_keyboard))?;
        }
    );

    let keyboard_service = env
        .connect_to_protocol::<KeyboardMarker>()
        .context("Failed to connect to keyboard service")?;

    let output = assert_set!(keyboard::command(keyboard_service, expected_keyboard));
    assert_eq!(output, format!("Successfully set Keyboard to {:#?}", expected_keyboard));
    Ok(())
}

#[test_case(
    Keyboard {
        keymap: None,
        autorepeat_delay: Some(0),
        autorepeat_period: Some(0),
    }; "Test keyboard watch() output with empty Keyboard."
)]
#[test_case(
    Keyboard {
        keymap: Some(fidl_fuchsia_input::KeymapId::UsDvorak),
        autorepeat_delay: Some(7),
        autorepeat_period: Some(8),
    }; "Test keyboard watch() output with non-empty Keyboard."
)]
#[fuchsia_async::run_until_stalled(test)]
async fn validate_keyboard_watch_output(expected_keyboard: Keyboard) -> Result<(), Error> {
    let env = create_service!(
        Services::Keyboard, KeyboardRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        KeyboardRequest::Watch { responder } => {
            responder.send(KeyboardSettings::from(expected_keyboard))?;
        }
    );

    let keyboard_service = env
        .connect_to_protocol::<KeyboardMarker>()
        .context("Failed to connect to keyboard service")?;

    let output = assert_watch!(keyboard::command(
        keyboard_service,
        Keyboard { keymap: None, autorepeat_delay: None, autorepeat_period: None }
    ));
    assert_eq!(output, format!("{:#?}", KeyboardSettings::from(expected_keyboard)));
    Ok(())
}
