// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use failure::{Error, ResultExt};
use fidl::client::QueryResponseFut;
use fidl_fuchsia_ui_input as ui_input;
use fidl_fuchsia_ui_input2 as ui_input2;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use futures::stream::StreamExt;
use serde::{Deserialize, Deserializer};
use serde_derive::Deserialize;
use serde_json::{self as json};
use std::fs;
use std::sync::{Arc, Once};

static START: Once = Once::new();
const DEFAULT_GOLDEN_PATH: &'static str = "/pkg/data/goldens/en-us.json";

fn event_to_semantic_key(event: ui_input2::KeyEvent) -> ui_input2::SemanticKey {
    match &event.semantic_key {
        Some(ui_input2::SemanticKey::Symbol(symbol)) => {
            ui_input2::SemanticKey::Symbol(symbol.to_string())
        }
        Some(ui_input2::SemanticKey::Action(action)) => ui_input2::SemanticKey::Action(*action),
        None => panic!("Semantic key empty {:?}", event),
        _ => panic!("UnknownVariant"),
    }
}

#[derive(Deserialize, Debug)]
struct GoldenTestSuite {
    test_cases: Vec<TestCase>,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "lowercase")]
enum SemanticKey {
    Symbol(String),
    #[serde(with = "hex_serde")]
    Action(u32),
}

impl From<SemanticKey> for ui_input2::SemanticKey {
    fn from(def: SemanticKey) -> ui_input2::SemanticKey {
        match def {
            SemanticKey::Symbol(symbol) => ui_input2::SemanticKey::Symbol(symbol.to_string()),
            SemanticKey::Action(action) => ui_input2::SemanticKey::Action(
                ui_input2::SemanticKeyAction::from_primitive(action)
                    .unwrap_or_else(|| panic!("Unable to parse semantic key action {:?}", def)),
            ),
        }
    }
}

#[derive(Deserialize, Debug, Copy, Clone)]
enum Modifiers {
    Shift = 0x00000001,
    LeftShift = 0x00000002,
    RightShift = 0x00000004,
    Control = 0x00000008,
    LeftControl = 0x00000010,
    RightControl = 0x00000020,
    Alt = 0x00000040,
    LeftAlt = 0x00000080,
    RightAlt = 0x00000100,
    Meta = 0x00000200,
    LeftMeta = 0x00000400,
    RightMeta = 0x00000800,
    CapsLock = 0x00001000,
    NumLock = 0x00002000,
    ScrollLock = 0x00004000,
}

impl From<Modifiers> for ui_input2::Modifiers {
    fn from(def: Modifiers) -> ui_input2::Modifiers {
        ui_input2::Modifiers::from_bits(def as u32)
            .unwrap_or_else(|| panic!("Unable to parse modifiers {:?}", def))
    }
}

#[derive(Deserialize, Debug)]
struct TestCase {
    #[serde(deserialize_with = "deserialize_key")]
    key: ui_input2::Key,
    #[serde(deserialize_with = "deserialize_modifiers")]
    modifiers: Option<ui_input2::Modifiers>,
    #[serde(deserialize_with = "deserialize_semantic_key")]
    semantic_key: ui_input2::SemanticKey,
}

fn deserialize_key<'de, D>(deserializer: D) -> Result<ui_input2::Key, D::Error>
where
    D: Deserializer<'de>,
{
    let s: String = Deserialize::deserialize(deserializer).map_err(serde::de::Error::custom)?;
    let s_lower = s.to_lowercase();
    let without_prefix = s_lower.trim_start_matches("0x");
    let index = u32::from_str_radix(&without_prefix, 16).map_err(serde::de::Error::custom);
    index.map(|i| {
        let key = ui_input2::Key::from_primitive(i);
        key.unwrap()
    })
}

fn deserialize_modifiers<'de, D>(deserializer: D) -> Result<Option<ui_input2::Modifiers>, D::Error>
where
    D: Deserializer<'de>,
{
    let s: Vec<Modifiers> =
        Deserialize::deserialize(deserializer).map_err(serde::de::Error::custom)?;
    if s.len() == 0 {
        Ok(None)
    } else {
        Ok(Some(s.iter().fold(ui_input2::Modifiers::empty(), |acc, &m| acc | m.into())))
    }
}

fn deserialize_semantic_key<'de, D>(deserializer: D) -> Result<ui_input2::SemanticKey, D::Error>
where
    D: Deserializer<'de>,
{
    let s: SemanticKey =
        Deserialize::deserialize(deserializer).map_err(serde::de::Error::custom)?;
    Ok(s.into())
}

mod hex_serde {
    use serde::Deserialize;

    pub fn deserialize<'de, D>(deserializer: D) -> Result<u32, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let s: String = Deserialize::deserialize(deserializer).map_err(serde::de::Error::custom)?;
        let s_lower = s.to_lowercase();
        let without_prefix = s_lower.trim_start_matches("0x");
        u32::from_str_radix(&without_prefix, 16).map_err(serde::de::Error::custom)
    }
}

struct KeyboardService {
    _keyboard: ui_input2::KeyboardProxy,
    ime_service: ui_input::ImeServiceProxy,
    listener: ui_input2::KeyListenerRequestStream,
}

impl KeyboardService {
    async fn new() -> Result<KeyboardService, Error> {
        START.call_once(|| {
            fuchsia_syslog::init_with_tags(&["keyboard_test"])
                .expect("keyboard test syslog init should not fail");
        });

        let keyboard = connect_to_service::<ui_input2::KeyboardMarker>()
            .context("Failed to connect to Keyboard service")?;

        let ime_service = connect_to_service::<ui_input::ImeServiceMarker>()
            .context("Failed to connect to IME service")?;

        let (listener_client_end, listener) =
            fidl::endpoints::create_request_stream::<ui_input2::KeyListenerMarker>()?;

        // Set listener and view ref.
        let (raw_event_pair, _) = zx::EventPair::create()?;
        let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };
        keyboard.set_listener(view_ref, listener_client_end).await.expect("set_listener");

        Ok(KeyboardService { ime_service, listener, _keyboard: keyboard })
    }

    fn press_key_low_level(
        &self,
        key: ui_input2::Key,
        modifiers: Option<ui_input2::Modifiers>,
    ) -> QueryResponseFut<bool> {
        // Process key event that triggers a shortcut.
        let event = ui_input2::KeyEvent {
            key: Some(key),
            modifiers: modifiers,
            phase: Some(ui_input2::KeyEventPhase::Pressed),
            physical_key: None,
            semantic_key: None,
        };

        self.ime_service.dispatch_key(event)
    }

    async fn press_key(
        &mut self,
        key: ui_input2::Key,
        modifiers: Option<ui_input2::Modifiers>,
    ) -> ui_input2::KeyEvent {
        let was_handled_fut = self.press_key_low_level(key, modifiers);
        let event = match self.listener.next().await {
            Some(Ok(ui_input2::KeyListenerRequest::OnKeyEvent { event, responder, .. })) => {
                responder.send(ui_input2::Status::Handled).expect("responding from key listener");
                Some(event)
            }
            None => None,
            _ => panic!("Error from listener.next() while pressing {:?}", (key, modifiers)),
        };
        let was_handled = was_handled_fut.await.expect("press_key");
        assert_eq!(true, was_handled);
        event.unwrap()
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_goldens() -> Result<(), Error> {
    let data = fs::read_to_string(DEFAULT_GOLDEN_PATH)?;
    let goldens: GoldenTestSuite = json::from_str(&data)?;

    let service = Arc::new(Mutex::new(KeyboardService::new().await?));

    futures::stream::iter(goldens.test_cases.into_iter())
        .then(|TestCase { key, modifiers, semantic_key }| {
            let service = service.clone();
            async move {
                let mut service = service.lock().await;
                let event = service.press_key(key, modifiers).await;
                assert_eq!(
                    semantic_key,
                    event_to_semantic_key(event),
                    "Pressed key {:?} expected semantic key {:?}",
                    (key, modifiers),
                    semantic_key
                );
            }
        })
        .collect::<Vec<_>>()
        .await;

    Ok(())
}
