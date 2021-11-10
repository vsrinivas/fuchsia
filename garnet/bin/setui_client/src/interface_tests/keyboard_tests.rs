// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::keyboard;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{KeyboardMarker, KeyboardRequest, KeyboardSettings};

use crate::interface_tests::Services;
use crate::interface_tests::ENV_NAME;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

async fn validate_keyboard(
    expected_keymap: Option<fidl_fuchsia_input::KeymapId>,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Keyboard, KeyboardRequest::Set { settings, responder, } => {
            if let (Some(keymap), Some(expected_keymap_value)) =
                (settings.keymap, expected_keymap) {
                assert_eq!(keymap, expected_keymap_value);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        KeyboardRequest::Watch { responder } => {
            responder.send(KeyboardSettings {
                keymap: Some(fidl_fuchsia_input::KeymapId::UsQwerty),
                ..KeyboardSettings::EMPTY
            })?;
        }
    );

    let keyboard_service = env
        .connect_to_protocol::<KeyboardMarker>()
        .context("Failed to connect to keyboard service")?;

    assert_successful!(keyboard::command(keyboard_service, expected_keymap));
    Ok(())
}

async fn validate_keyboard_set_output(
    expected_keymap_id: fidl_fuchsia_input::KeymapId,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Keyboard, KeyboardRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        KeyboardRequest::Watch { responder } => {
            responder.send(KeyboardSettings {
                keymap: Some(expected_keymap_id),
                ..KeyboardSettings::EMPTY
            })?;
        }
    );

    let keyboard_service = env
        .connect_to_protocol::<KeyboardMarker>()
        .context("Failed to connect to keyboard service")?;

    let output = assert_set!(keyboard::command(keyboard_service, Some(expected_keymap_id)));
    assert_eq!(output, format!("Successfully set keymap ID to {:?}", expected_keymap_id));
    Ok(())
}

async fn validate_privacy_watch_output(
    expected_keymap: Option<fidl_fuchsia_input::KeymapId>,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Keyboard, KeyboardRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        KeyboardRequest::Watch { responder } => {
            responder.send(KeyboardSettings {
                keymap: expected_keymap,
                ..KeyboardSettings::EMPTY
            })?;
        }
    );

    let keyboard_service = env
        .connect_to_protocol::<KeyboardMarker>()
        .context("Failed to connect to keyboard service")?;

    let output = assert_watch!(keyboard::command(keyboard_service, None));
    assert_eq!(
        output,
        format!("{:#?}", KeyboardSettings { keymap: expected_keymap, ..KeyboardSettings::EMPTY })
    );
    Ok(())
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_keyboard() -> Result<(), Error> {
    println!("keyboard service tests");
    println!("  client calls keyboard watch");
    validate_keyboard(None).await?;

    println!("  client calls set keymap");
    validate_keyboard(Some(fidl_fuchsia_input::KeymapId::FrAzerty)).await?;

    println!("  set() output");
    validate_keyboard_set_output(fidl_fuchsia_input::KeymapId::UsQwerty).await?;
    validate_keyboard_set_output(fidl_fuchsia_input::KeymapId::UsDvorak).await?;

    println!("  watch() output");
    validate_privacy_watch_output(None).await?;
    validate_privacy_watch_output(Some(fidl_fuchsia_input::KeymapId::UsQwerty)).await?;
    validate_privacy_watch_output(Some(fidl_fuchsia_input::KeymapId::UsDvorak)).await?;

    Ok(())
}
