// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `deprecated_service` handles all deprecated input2-related shortcuts.

use {
    anyhow::Error,
    fidl_fuchsia_ui_input2 as ui_input2, fidl_fuchsia_ui_shortcut as ui_shortcut,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, StreamExt},
    std::sync::Arc,
};

use crate::registry::{ClientRegistry, RegistryStore, Shortcut, Subscriber};

const DEFAULT_SHORTCUT_ID: u32 = 2;
const DEFAULT_LISTENER_TIMEOUT_SECONDS: i64 = 3;

pub struct Input2Service {
    // Held down modifiers that were matched by activated shortcuts.
    // Used to match Trigger.KeyPressedAndReleased shortcuts.
    matched_modifiers: ui_input2::Modifiers,

    store: RegistryStore,
}

impl Input2Service {
    pub fn new(store: RegistryStore) -> Self {
        let matched_modifiers = ui_input2::Modifiers::empty();
        Self { matched_modifiers, store }
    }

    pub fn normalize_shortcut(&self, mut shortcut: &mut ui_shortcut::Shortcut) {
        fn key_to_modifiers(
            mut shortcut: &mut ui_shortcut::Shortcut,
            modifiers: ui_input2::Modifiers,
        ) {
            match shortcut.modifiers {
                Some(mut shortcut_modifiers) => shortcut_modifiers.insert(modifiers),
                None => shortcut.modifiers = Some(modifiers),
            }
            shortcut.key = None;
        }

        // Normalize modifiers for simpler matching.
        match shortcut.key {
            Some(ui_input2::Key::LeftShift) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::LeftShift)
            }
            Some(ui_input2::Key::RightShift) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::RightShift)
            }
            Some(ui_input2::Key::LeftAlt) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::LeftAlt)
            }
            Some(ui_input2::Key::RightAlt) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::RightAlt)
            }
            Some(ui_input2::Key::LeftMeta) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::LeftMeta)
            }
            Some(ui_input2::Key::RightMeta) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::RightMeta)
            }
            Some(ui_input2::Key::LeftCtrl) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::LeftControl)
            }
            Some(ui_input2::Key::RightCtrl) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::RightControl)
            }
            Some(ui_input2::Key::NumLock) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::NumLock)
            }
            Some(ui_input2::Key::CapsLock) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::CapsLock)
            }
            Some(ui_input2::Key::ScrollLock) => {
                key_to_modifiers(&mut shortcut, ui_input2::Modifiers::ScrollLock)
            }
            _ => {}
        };

        if let Some(mut modifiers) = shortcut.modifiers {
            if modifiers.contains(ui_input2::Modifiers::Shift) {
                modifiers.remove(ui_input2::Modifiers::LeftShift);
                modifiers.remove(ui_input2::Modifiers::RightShift);
            }
            if modifiers.contains(ui_input2::Modifiers::Control) {
                modifiers.remove(ui_input2::Modifiers::LeftControl);
                modifiers.remove(ui_input2::Modifiers::RightControl);
            }
            if modifiers.contains(ui_input2::Modifiers::Alt) {
                modifiers.remove(ui_input2::Modifiers::LeftAlt);
                modifiers.remove(ui_input2::Modifiers::RightAlt);
            }
            if modifiers.contains(ui_input2::Modifiers::Meta) {
                modifiers.remove(ui_input2::Modifiers::LeftMeta);
                modifiers.remove(ui_input2::Modifiers::RightMeta);
            }
        }
    }

    /// Detect shortcut and route it to the client.
    /// Returns true if key event triggers a shortcut and was handled.
    pub async fn handle_key_event(&mut self, event: ui_input2::KeyEvent) -> Result<bool, Error> {
        if event.modifiers.is_none() {
            self.matched_modifiers = ui_input2::Modifiers::empty();
        }

        // Clone, upgrade, and filter out stale Weak pointers.
        let registries = self.store.get_registries().await;
        let registries = registries.iter().cloned().filter_map(|r| r.upgrade()).into_iter();

        let was_handled = futures::stream::iter(registries)
            .fold(false, {
                // Capture variables by reference, since `async` non-`move` closures with
                // arguments are not currently supported
                let (key, phase, modifiers) = (event.key, event.phase, event.modifiers);
                let matched_modifiers = self.matched_modifiers;
                move |was_handled, registry| async move {
                    let event = ui_input2::KeyEvent {
                        key,
                        phase,
                        modifiers,
                        semantic_key: None,
                        physical_key: None,
                    };
                    let handled = handle(registry, event, matched_modifiers).await.unwrap_or_else(
                        |e: anyhow::Error| {
                            fx_log_err!("shortcut handle error: {:?}", e);
                            false
                        },
                    );
                    handled || was_handled
                }
            })
            .await;

        if was_handled {
            if let Some(modifiers) = event.modifiers {
                self.matched_modifiers.insert(modifiers);
            }
        }
        Ok(was_handled)
    }
}

async fn handle(
    registry: Arc<Mutex<ClientRegistry>>,
    event: ui_input2::KeyEvent,
    matched_modifiers: ui_input2::Modifiers,
) -> Result<bool, Error> {
    let registry = registry.lock().await;
    let shortcuts = get_matching_shortcuts(&registry, event, matched_modifiers)?;

    for shortcut in shortcuts {
        if let Some(Subscriber { ref listener, .. }) = &registry.subscriber {
            let id = shortcut.id.unwrap_or(DEFAULT_SHORTCUT_ID);
            let was_handled = fasync::TimeoutExt::on_timeout(
                listener.on_shortcut(id),
                fasync::Time::after(zx::Duration::from_seconds(DEFAULT_LISTENER_TIMEOUT_SECONDS)),
                || Ok(false),
            );
            if was_handled.await? {
                return Ok(true);
            }
        }
    }
    Ok(false)
}

fn get_matching_shortcuts(
    registry: &ClientRegistry,
    event: ui_input2::KeyEvent,
    matched_modifiers: ui_input2::Modifiers,
) -> Result<Vec<&Shortcut>, Error> {
    let shortcuts = registry
        .shortcuts
        .iter()
        .filter(|s| {
            match (s.trigger, event.phase) {
                (
                    Some(ui_shortcut::Trigger::KeyPressed),
                    Some(ui_input2::KeyEventPhase::Pressed),
                )
                | (
                    Some(ui_shortcut::Trigger::KeyPressedAndReleased),
                    Some(ui_input2::KeyEventPhase::Released),
                ) => {}
                _ => return false,
            };
            // Exclude modifier keys from key event, because
            // modifier shortcuts are matched based on event.modifiers field.
            let event_key = match event.key {
                Some(ui_input2::Key::LeftShift)
                | Some(ui_input2::Key::RightShift)
                | Some(ui_input2::Key::LeftCtrl)
                | Some(ui_input2::Key::RightCtrl)
                | Some(ui_input2::Key::LeftAlt)
                | Some(ui_input2::Key::RightAlt)
                | Some(ui_input2::Key::LeftMeta)
                | Some(ui_input2::Key::RightMeta)
                | Some(ui_input2::Key::CapsLock)
                | Some(ui_input2::Key::NumLock)
                | Some(ui_input2::Key::ScrollLock) => None,
                _ => event.key,
            };
            if let Some(ui_input2::KeyEventPhase::Pressed) = event.phase {
                event_key == s.key && are_modifiers_applicable(s.modifiers, event.modifiers)
            } else {
                // For Released shortcuts, exclude modifiers matched during Pressed phase.
                event_key == s.key
                    && are_modifiers_applicable(
                        s.modifiers,
                        event.modifiers.map(|m| m & !matched_modifiers),
                    )
            }
        })
        .collect();
    Ok(shortcuts)
}

// Validates event modifiers applicability for shortcut modifiers.
// Examples:
//  CapsLock shouldn't affect matching of Ctrl+C.
//  Modifier with Shift should match both LeftShift and RightShift
fn are_modifiers_applicable(
    shortcut_modifiers: Option<ui_input2::Modifiers>,
    event_modifiers: Option<ui_input2::Modifiers>,
) -> bool {
    match (shortcut_modifiers, event_modifiers) {
        (Some(shortcut_modifiers), Some(event_modifiers)) => {
            let masks = [
                vec![
                    ui_input2::Modifiers::Shift,
                    ui_input2::Modifiers::LeftShift | ui_input2::Modifiers::RightShift,
                ],
                vec![
                    ui_input2::Modifiers::Alt,
                    ui_input2::Modifiers::LeftAlt | ui_input2::Modifiers::RightAlt,
                ],
                vec![
                    ui_input2::Modifiers::Meta,
                    ui_input2::Modifiers::LeftMeta | ui_input2::Modifiers::RightMeta,
                ],
                vec![
                    ui_input2::Modifiers::Control,
                    ui_input2::Modifiers::LeftControl | ui_input2::Modifiers::RightControl,
                ],
                // TODO: locks affecting shortcuts?
            ];

            masks.iter().all(|variations| {
                // if shortcut has modifiers from the variation, event should have the same.
                variations.iter().all(|&mask| {
                    !shortcut_modifiers.intersects(mask)
                        || shortcut_modifiers & mask == event_modifiers & mask
                })
            })
        }
        (None, None) => true,
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shortcut_matching() {
        let shortcut_modifier = Some(ui_input2::Modifiers::Shift);
        let event_modifier = Some(ui_input2::Modifiers::Shift | ui_input2::Modifiers::LeftShift);
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input2::Modifiers::Shift);
        let event_modifier = Some(ui_input2::Modifiers::Shift | ui_input2::Modifiers::RightShift);
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        // Locks should not affect modifier matching.
        let shortcut_modifier = Some(ui_input2::Modifiers::Shift);
        let event_modifier = Some(
            ui_input2::Modifiers::Shift
                | ui_input2::Modifiers::LeftShift
                | ui_input2::Modifiers::CapsLock,
        );
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input2::Modifiers::Shift);
        let event_modifier = Some(
            ui_input2::Modifiers::Shift
                | ui_input2::Modifiers::LeftShift
                | ui_input2::Modifiers::NumLock,
        );
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input2::Modifiers::Shift);
        let event_modifier = Some(
            ui_input2::Modifiers::Shift
                | ui_input2::Modifiers::LeftShift
                | ui_input2::Modifiers::ScrollLock,
        );
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input2::Modifiers::Shift);
        let event_modifier = Some(ui_input2::Modifiers::ScrollLock);
        assert_eq!(false, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input2::Modifiers::LeftShift);
        let event_modifier = Some(ui_input2::Modifiers::ScrollLock);
        assert_eq!(false, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier =
            Some(ui_input2::Modifiers::LeftShift | ui_input2::Modifiers::RightShift);
        let event_modifier = Some(ui_input2::Modifiers::LeftShift);
        assert_eq!(false, are_modifiers_applicable(shortcut_modifier, event_modifier));
    }
}
