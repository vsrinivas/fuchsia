// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl::encoding::Decodable;
use fidl_fuchsia_ui_input2 as ui_input;
use futures::lock::Mutex;
use futures::TryStreamExt;
use serde::{Deserialize, Deserializer};
use serde_derive::Deserialize;
use serde_json::{self as json};
use std::collections::HashMap;
use std::fs;
use std::sync::Arc;

const DEFAULT_LAYOUT_PATH: &'static str = "/pkg/data/us.json";

#[derive(Clone)]
pub struct KeymapService {
    keymap: Arc<Mutex<Keymap>>,
}

impl KeymapService {
    pub fn new() -> Result<KeymapService, Error> {
        let data = fs::read_to_string(DEFAULT_LAYOUT_PATH)?;
        let keymap: Keymap = json::from_str(&data)?;
        Ok(KeymapService { keymap: Arc::new(Mutex::new(keymap)) })
    }
}

#[derive(Deserialize, Debug, Clone)]
enum Modifiers {
    Shift,
    LeftShift,
    RightShift,
    Control,
    LeftControl,
    RightControl,
    Alt,
    LeftAlt,
    RightAlt,
    Meta,
    LeftMeta,
    RightMeta,
    CapsLock,
    NumLock,
    ScrollLock,
}

impl Into<ui_input::Modifiers> for Modifiers {
    fn into(self) -> ui_input::Modifiers {
        match self {
            Modifiers::Shift => ui_input::Modifiers::Shift,
            Modifiers::LeftShift => ui_input::Modifiers::LeftShift,
            Modifiers::RightShift => ui_input::Modifiers::RightShift,
            Modifiers::Control => ui_input::Modifiers::Control,
            Modifiers::LeftControl => ui_input::Modifiers::LeftControl,
            Modifiers::RightControl => ui_input::Modifiers::RightControl,
            Modifiers::Alt => ui_input::Modifiers::Alt,
            Modifiers::LeftAlt => ui_input::Modifiers::LeftAlt,
            Modifiers::RightAlt => ui_input::Modifiers::RightAlt,
            Modifiers::Meta => ui_input::Modifiers::Meta,
            Modifiers::LeftMeta => ui_input::Modifiers::LeftMeta,
            Modifiers::RightMeta => ui_input::Modifiers::RightMeta,
            Modifiers::CapsLock => ui_input::Modifiers::CapsLock,
            Modifiers::NumLock => ui_input::Modifiers::NumLock,
            Modifiers::ScrollLock => ui_input::Modifiers::ScrollLock,
        }
    }
}

type Key = u32;

#[derive(Deserialize, Debug, Clone, PartialEq)]
#[serde(rename_all = "lowercase")]
enum Entry {
    Symbol(String),
    #[serde(with = "hex_serde")]
    Action(u32),
}

#[derive(Deserialize, Debug, Clone)]
struct Layout {
    modifiers: Vec<Modifiers>,
    optional_modifiers: Vec<Modifiers>,
    #[serde(deserialize_with = "deserialize_entry")]
    entries: HashMap<Key, Entry>,
}

#[derive(Deserialize, Debug)]
struct Keymap {
    name: String,
    layouts: Vec<Layout>,
}

fn deserialize_entry<'de, D>(deserializer: D) -> Result<HashMap<Key, Entry>, D::Error>
where
    D: Deserializer<'de>,
{
    fn key_from_hex<'de, D>(deserializer: D) -> Result<Key, D::Error>
    where
        D: Deserializer<'de>,
    {
        let s: String = Deserialize::deserialize(deserializer).map_err(serde::de::Error::custom)?;
        let s_lower = s.to_lowercase();
        let without_prefix = s_lower.trim_start_matches("0x");
        let index = Key::from_str_radix(&without_prefix, 16).map_err(serde::de::Error::custom);
        index.map(|i| {
            let key = ui_input::Key::from_primitive(i);
            if key.is_none() {
                panic!("Keymap uses unknown Key index: {:}", s);
            };
            i
        })
    }

    #[derive(Deserialize, Hash, Eq, PartialEq)]
    struct Wrapper(#[serde(deserialize_with = "key_from_hex")] Key);

    let v = HashMap::<Wrapper, Entry>::deserialize(deserializer)?;
    Ok(v.into_iter().map(|(Wrapper(k), v)| (k, v)).collect())
}

impl Keymap {
    fn get_layout(&self) -> Result<ui_input::KeyboardLayout, Error> {
        let semantic_key_map: Option<Vec<_>> = Some(
            self.layouts
                .iter()
                .cloned()
                .map(|layout| {
                    let modifiers = from_modifiers(&layout.modifiers);
                    let optional_modifiers = from_modifiers(&layout.optional_modifiers);
                    let entries = from_entries(&layout.entries);
                    ui_input::SemanticKeyMap { entries, modifiers, optional_modifiers }
                })
                .collect(),
        );

        Ok(ui_input::KeyboardLayout { key_map: None, semantic_key_map })
    }
}

fn from_modifiers(modifiers: &Vec<Modifiers>) -> Option<ui_input::Modifiers> {
    if modifiers.len() == 0 {
        return None;
    }
    let modifiers = modifiers
        .iter()
        .cloned()
        .fold(<ui_input::Modifiers as Decodable>::new_empty(), |acc, m| acc | m.into());
    Some(modifiers)
}

fn from_entries(entries: &HashMap<Key, Entry>) -> Option<Vec<ui_input::SemanticKeyMapEntry>> {
    if entries.len() == 0 {
        return None;
    };
    let entries = entries
        .iter()
        .map(|(&key, entry)| {
            let key = ui_input::Key::from_primitive(key).unwrap();
            let semantic_key = match entry {
                Entry::Symbol(s) => ui_input::SemanticKey::Symbol(s.to_string()),
                Entry::Action(i) => ui_input::SemanticKey::Action(
                    ui_input::SemanticKeyAction::from_primitive(*i).unwrap_or_else(|| {
                        panic!("Unable to parse semantic key action {:?}", entry)
                    }),
                ),
            };
            ui_input::SemanticKeyMapEntry { key, semantic_key }
        })
        .collect();
    Some(entries)
}

pub async fn handle_watch_keymap(
    mut stream: ui_input::KeyboardLayoutStateRequestStream,
    keymap_service: KeymapService,
) -> Result<(), Error> {
    // Since keymap never changes, respond to the first keymap request only.
    if let Some(ui_input::KeyboardLayoutStateRequest::Watch { responder }) =
        stream.try_next().await.context("error handling keymap requests")?
    {
        let keymap = keymap_service.keymap.lock().await;
        responder.send(keymap.get_layout()?).context("error sending keymap response")?;
    };
    while let Some(ui_input::KeyboardLayoutStateRequest::Watch { responder, .. }) =
        stream.try_next().await.context("failed handling no-op requests")?
    {
        // Dropping responed without shutdown to keep it hanging for an update.
        // Future versions of IME will dispatch updates to keymap using same interface.
        responder.drop_without_shutdown();
    }
    Ok(())
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

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    const TEST_LAYOUT_NAME: &str = "U.S.";
    const TEST_SYMBOL: &str = "a";
    const TEST_KEY: ui_input::Key = ui_input::Key::A;
    const TEST_KEY_CODE: u32 = 0x00000001;

    #[fasync::run_singlethreaded(test)]
    async fn populate_keymap_test() {
        let service = KeymapService::new().expect("new keymap service");
        let keymap = service.keymap.lock().await;
        assert_eq!(keymap.name, TEST_LAYOUT_NAME);
        assert_eq!(keymap.layouts.len(), 3);

        let layout = keymap.layouts.get(0).expect("layout page populated");
        assert_eq!(layout.entries.len(), 28);

        let entry = layout.entries.get(&TEST_KEY_CODE).expect("test key defined");
        assert_eq!(*entry, Entry::Symbol(TEST_SYMBOL.to_string()));

        let layout = keymap.get_layout().expect("layout defined");
        assert_eq!(layout.key_map, None);

        let semantic_key_map = layout.semantic_key_map.expect("has semantic key map");
        let semantic_key_page = semantic_key_map.get(0).expect("semantic key map is defined");
        assert_eq!(
            semantic_key_page.optional_modifiers,
            Some(ui_input::Modifiers::NumLock | ui_input::Modifiers::ScrollLock)
        );

        let semantic_key_page_entries = semantic_key_page.entries.as_ref().expect("has entries");
        let semantic_key = semantic_key_page_entries
            .iter()
            .find_map(
                |ui_input::SemanticKeyMapEntry { key, semantic_key }| {
                    if key == &TEST_KEY {
                        Some(semantic_key)
                    } else {
                        None
                    }
                },
            )
            .expect("contains TEST_KEY");
        assert_eq!(semantic_key, &ui_input::SemanticKey::Symbol(TEST_SYMBOL.to_string()));
    }
}
