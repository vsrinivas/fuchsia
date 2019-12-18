// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_ui_input2 as ui_input;
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use futures::StreamExt;
use std::sync::{Arc, Weak};
use std::vec::Vec;

pub struct Subscriber {
    pub view_ref: ui_views::ViewRef,
    pub listener: fidl_fuchsia_ui_shortcut::ListenerProxy,
}

#[derive(Default)]
pub struct ClientRegistry {
    pub subscriber: Option<Subscriber>,
    pub shortcuts: Vec<ui_shortcut::Shortcut>,
}

pub struct RegistryStore {
    // Weak ref for ClientRegistry to allow shortcuts to be dropped out
    // of collection once client connection is removed.
    registries: Vec<Weak<Mutex<ClientRegistry>>>,

    // Held down modifiers that were matched by activated shortcuts.
    // Used to match Trigger.KeyPressedAndReleased shortcuts.
    matched_modifiers: ui_input::Modifiers,
}

const DEFAULT_SHORTCUT_ID: u32 = 2;
const DEFAULT_LISTENER_TIMEOUT_SECONDS: i64 = 3;

// Validates event modifiers applicability for shortcut modifiers.
// Examples:
//  CapsLock shouldn't affect matching of Ctrl+C.
//  Modifier with Shift should match both LeftShift and RightShift
fn are_modifiers_applicable(
    shortcut_modifiers: Option<ui_input::Modifiers>,
    event_modifiers: Option<ui_input::Modifiers>,
) -> bool {
    match (shortcut_modifiers, event_modifiers) {
        (Some(shortcut_modifiers), Some(event_modifiers)) => {
            let masks = [
                vec![
                    ui_input::Modifiers::Shift,
                    ui_input::Modifiers::LeftShift | ui_input::Modifiers::RightShift,
                ],
                vec![
                    ui_input::Modifiers::Alt,
                    ui_input::Modifiers::LeftAlt | ui_input::Modifiers::RightAlt,
                ],
                vec![
                    ui_input::Modifiers::Meta,
                    ui_input::Modifiers::LeftMeta | ui_input::Modifiers::RightMeta,
                ],
                vec![
                    ui_input::Modifiers::Control,
                    ui_input::Modifiers::LeftControl | ui_input::Modifiers::RightControl,
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

async fn handle(
    registry: Arc<Mutex<ClientRegistry>>,
    event: ui_input::KeyEvent,
    matched_modifiers: ui_input::Modifiers,
) -> Result<bool, Error> {
    let registry = registry.lock().await;
    let shortcuts = registry.get_matching_shortuts(event, matched_modifiers)?;

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

impl ClientRegistry {
    fn get_matching_shortuts(
        &self,
        event: ui_input::KeyEvent,
        matched_modifiers: ui_input::Modifiers,
    ) -> Result<Vec<&ui_shortcut::Shortcut>, Error> {
        let shortcuts = self
            .shortcuts
            .iter()
            .filter(|s| {
                match (s.trigger, event.phase) {
                    (
                        Some(ui_shortcut::Trigger::KeyPressed),
                        Some(ui_input::KeyEventPhase::Pressed),
                    )
                    | (
                        Some(ui_shortcut::Trigger::KeyPressedAndReleased),
                        Some(ui_input::KeyEventPhase::Released),
                    ) => {}
                    _ => return false,
                };
                // Exclude modifier keys from key event, because
                // modifier shortcuts are matched based on event.modifiers field.
                let event_key = match event.key {
                    Some(ui_input::Key::LeftShift)
                    | Some(ui_input::Key::RightShift)
                    | Some(ui_input::Key::LeftCtrl)
                    | Some(ui_input::Key::RightCtrl)
                    | Some(ui_input::Key::LeftAlt)
                    | Some(ui_input::Key::RightAlt)
                    | Some(ui_input::Key::LeftMeta)
                    | Some(ui_input::Key::RightMeta)
                    | Some(ui_input::Key::CapsLock)
                    | Some(ui_input::Key::NumLock)
                    | Some(ui_input::Key::ScrollLock) => None,
                    _ => event.key,
                };
                if let Some(ui_input::KeyEventPhase::Pressed) = event.phase {
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
}

impl Default for RegistryStore {
    fn default() -> Self {
        let matched_modifiers = ui_input::Modifiers::empty();
        let registries = Default::default();
        RegistryStore { matched_modifiers, registries }
    }
}

impl RegistryStore {
    /// Create and add new client registry of shortcuts to the store.
    pub fn add_new_registry(&mut self) -> Arc<Mutex<ClientRegistry>> {
        let registry = Arc::new(Mutex::new(ClientRegistry::default()));
        self.registries.push(Arc::downgrade(&registry));
        registry
    }

    /// Detect shortcut and route it to the client.
    /// Returns true if key event triggers a shortcut and was handled.
    pub async fn handle_key_event(&mut self, event: ui_input::KeyEvent) -> Result<bool, Error> {
        if event.modifiers.is_none() {
            self.matched_modifiers = ui_input::Modifiers::empty();
        }

        // Clone, upgrade, and filter out stale Weak pointers.
        let registries = self.registries.iter().cloned().filter_map(|r| r.upgrade()).into_iter();

        // TODO: sort according to ViewRef hierarchy
        // TODO: resolve accounting for use_priority

        let was_handled = futures::stream::iter(registries)
            .fold(false, {
                // Capture variables by reference, since `async` non-`move` closures with
                // arguments are not currently supported
                let (key, phase, modifiers) = (event.key, event.phase, event.modifiers);
                let matched_modifiers = self.matched_modifiers;
                move |was_handled, registry| async move {
                    let event = ui_input::KeyEvent {
                        key,
                        phase,
                        modifiers,
                        semantic_key: None,
                        physical_key: None,
                    };
                    let handled = handle(registry, event, matched_modifiers).await.unwrap_or_else(
                        |e: failure::Error| {
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shortcut_matching() {
        let shortcut_modifier = Some(ui_input::Modifiers::Shift);
        let event_modifier = Some(ui_input::Modifiers::Shift | ui_input::Modifiers::LeftShift);
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input::Modifiers::Shift);
        let event_modifier = Some(ui_input::Modifiers::Shift | ui_input::Modifiers::RightShift);
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        // Locks should not affect modifier matching.
        let shortcut_modifier = Some(ui_input::Modifiers::Shift);
        let event_modifier = Some(
            ui_input::Modifiers::Shift
                | ui_input::Modifiers::LeftShift
                | ui_input::Modifiers::CapsLock,
        );
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input::Modifiers::Shift);
        let event_modifier = Some(
            ui_input::Modifiers::Shift
                | ui_input::Modifiers::LeftShift
                | ui_input::Modifiers::NumLock,
        );
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input::Modifiers::Shift);
        let event_modifier = Some(
            ui_input::Modifiers::Shift
                | ui_input::Modifiers::LeftShift
                | ui_input::Modifiers::ScrollLock,
        );
        assert_eq!(true, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input::Modifiers::Shift);
        let event_modifier = Some(ui_input::Modifiers::ScrollLock);
        assert_eq!(false, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier = Some(ui_input::Modifiers::LeftShift);
        let event_modifier = Some(ui_input::Modifiers::ScrollLock);
        assert_eq!(false, are_modifiers_applicable(shortcut_modifier, event_modifier));

        let shortcut_modifier =
            Some(ui_input::Modifiers::LeftShift | ui_input::Modifiers::RightShift);
        let event_modifier = Some(ui_input::Modifiers::LeftShift);
        assert_eq!(false, are_modifiers_applicable(shortcut_modifier, event_modifier));
    }
}
