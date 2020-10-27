// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input3 as ui_input3,
    fidl_fuchsia_ui_shortcut as ui_shortcut, fuchsia_async as fasync,
    fuchsia_async::TimeoutExt,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::{lock::Mutex, stream, StreamExt},
    std::collections::HashSet,
    std::sync::Arc,
};

use crate::registry::{ClientRegistry, RegistryStore, Shortcut, Subscriber};

/// Shortcut id to be used if a client doesn't provide one.
/// Arbitrary.
const DEFAULT_SHORTCUT_ID: u32 = 2;
const DEFAULT_LISTENER_TIMEOUT: zx::Duration = zx::Duration::from_seconds(3);

/// Handles requests to `fuchsia.ui.shortcut.Registry` interface.
pub struct RegistryService {}

impl RegistryService {
    pub fn new() -> Self {
        Self {}
    }

    pub fn normalize_shortcut(&self, shortcut: &mut ui_shortcut::Shortcut) {
        // Set default value for trigger.
        if shortcut.trigger.is_none() {
            shortcut.trigger = Some(ui_shortcut::Trigger::KeyPressed);
        }

        // Set default value for use_priority.
        if shortcut.use_priority.is_none() {
            shortcut.use_priority = Some(false);
        }
    }
}

#[derive(Debug)]
/// Abstraction wrapper for a key event.
pub struct KeyEvent {
    pub key: input::Key,
    pub pressed: Option<bool>,
    inner: Option<ui_input3::KeyEvent>,
}

impl KeyEvent {
    pub fn new(event: ui_input3::KeyEvent) -> Result<Self, Error> {
        let key = event.key.ok_or(format_err!("No key in the event"))?;
        let pressed = match event.type_ {
            Some(ui_input3::KeyEventType::Pressed) => Some(true),
            Some(ui_input3::KeyEventType::Released) => Some(false),
            _ => None,
        };

        Ok(Self { inner: Some(event), key, pressed })
    }
}

/// Handles requests to `fuchsia.ui.shortcut.Manager` interface.
pub struct ManagerService {
    store: RegistryStore,
    keys_pressed: HashSet<input::Key>,
}

impl ManagerService {
    pub fn new(store: RegistryStore) -> Self {
        Self { store, keys_pressed: HashSet::new() }
    }

    /// Handles a key event:
    /// - keeps state of pressed keys
    /// - detects shortcuts
    /// - notifies matching shortcut listeners
    /// Returns `true` if shortcut was handled.
    pub async fn handle_key(&mut self, event: KeyEvent) -> Result<bool, Error> {
        let type_ = match event.inner {
            Some(ui_input3::KeyEvent { type_: Some(type_), .. }) => type_,
            // The key event here can't be handled without a type.
            // This might or might not be an error case, and should be validated elsewhere.
            _ => return Ok(false),
        };
        match type_ {
            ui_input3::KeyEventType::Sync => {
                self.keys_pressed.insert(event.key);
                Ok(true)
            }
            ui_input3::KeyEventType::Cancel => {
                self.keys_pressed.remove(&event.key);
                Ok(true)
            }
            ui_input3::KeyEventType::Pressed => {
                let key = event.key;
                let was_handled = self.trigger_matching_shortcuts(event).await;
                self.keys_pressed.insert(key);
                was_handled
            }
            ui_input3::KeyEventType::Released => {
                let key = event.key;
                let was_handled = self.trigger_matching_shortcuts(event).await;
                self.keys_pressed.remove(&key);
                was_handled
            }
        }
    }

    async fn trigger_matching_shortcuts(&self, event: KeyEvent) -> Result<bool, Error> {
        // Clone, upgrade, and filter out stale Weak pointers.
        // TODO: remove when Weak pointers filtering done in router.
        let registries = self.store.get_focused_registries().await;
        let registries = registries.iter().cloned().filter_map(|r| r.upgrade()).into_iter();

        let (key, pressed) = (event.key, event.pressed);
        let handler = |use_priority| {
            move |registry| async move {
                let event = KeyEvent { key: key, pressed: pressed, inner: None };
                match self.process_client_registry(registry, event, use_priority).await {
                    Ok(true) => Some(()),
                    Ok(false) => None,
                    Err(e) => {
                        fx_log_err!("shortcut handle error: {:?}", e);
                        None
                    }
                }
            }
        };

        let priority_stream =
            stream::iter(registries.clone()).filter_map(handler(/* use_priority */ true));
        futures::pin_mut!(priority_stream);
        if priority_stream.next().await.is_some() {
            return Ok(true);
        }

        // Note that registries are processed in the reverse order.
        // The reason is that `get_focused_registries` returns "parent-first" order, following
        // FocusChain semantic, while non-priority shortcut disambiguation procedure calls for
        // child shortcuts to take precedence over parent ones.
        let non_priority_stream =
            stream::iter(registries.rev()).filter_map(handler(/* use_priority */ false));
        futures::pin_mut!(non_priority_stream);

        Ok(non_priority_stream.next().await.is_some())
    }

    /// Trigger all matching shortcuts for given `ClientRegistry` for given `event`.
    /// Reads currently pressed keys from `&self`.
    /// `use_priority` switches between priority and non-priority shortcuts.
    /// See FIDL documentation at //sdk/fidl/fuchsia.ui.shortcut/README.md.
    async fn process_client_registry(
        &self,
        registry: Arc<Mutex<ClientRegistry>>,
        event: KeyEvent,
        use_priority: bool,
    ) -> Result<bool, Error> {
        let registry = registry.lock().await;

        let shortcuts = self.get_matching_shortuts(&registry, event, use_priority)?;

        if let Some(ref subscriber) = registry.subscriber {
            self.trigger_shortcuts(&subscriber, shortcuts).await
        } else {
            Ok(false)
        }
    }

    async fn trigger_shortcuts<'a>(
        &self,
        subscriber: &'a Subscriber,
        shortcuts: Vec<&'a Shortcut>,
    ) -> Result<bool, Error> {
        for shortcut in shortcuts {
            let id = shortcut.id.unwrap_or(DEFAULT_SHORTCUT_ID);
            let was_handled = subscriber
                .listener
                .on_shortcut(id)
                .on_timeout(fasync::Time::after(DEFAULT_LISTENER_TIMEOUT), || Ok(false))
                .await;
            match was_handled {
                // Stop processing client registry on successful handling.
                Ok(true) => return Ok(true),
                // Keep processing on shortcut not being handled.
                Ok(false) => {}
                // Log an error and keep processing on shortcut listener error.
                Err(e) => {
                    fx_log_info!("shortcut listener error: {:?}", e);
                }
            }
        }
        Ok(false)
    }

    fn get_matching_shortuts<'a>(
        &self,
        registry: &'a ClientRegistry,
        event: KeyEvent,
        use_priority: bool,
    ) -> Result<Vec<&'a Shortcut>, Error> {
        let shortcuts = registry
            .shortcuts
            .iter()
            .filter(|shortcut| {
                match shortcut.use_priority {
                    Some(shortcut_use_priority) if use_priority != shortcut_use_priority => {
                        return false
                    }
                    None => panic!("normalize_shortcut() should not let this happen"),
                    // continue filtering
                    _ => {}
                }
                let shortcut_key = match shortcut.key3 {
                    Some(key) => key,
                    None => return false,
                };
                match (shortcut.trigger, event.pressed) {
                    (Some(ui_shortcut::Trigger::KeyPressed), Some(true))
                    | (Some(ui_shortcut::Trigger::KeyPressedAndReleased), Some(false)) => {
                        // continue filtering
                    }
                    _ => return false,
                }
                match &shortcut.keys_required_hash {
                    Some(keys_required) if &self.keys_pressed == keys_required => {
                        // continue filtering
                    }
                    None if self.keys_pressed.is_empty() => {
                        // continue filtering
                    }
                    _ => return false,
                }
                event.key == shortcut_key
            })
            .collect();

        Ok(shortcuts)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn normalize_shortcut() {
        let mut shortcut = ui_shortcut::Shortcut {
            keys_required: None,
            id: None,
            modifiers: None,
            key: None,
            use_priority: None,
            trigger: None,
            key3: None,
        };
        let normalized_shortcut = ui_shortcut::Shortcut {
            keys_required: None,
            id: None,
            modifiers: None,
            key: None,
            use_priority: Some(false),
            trigger: Some(ui_shortcut::Trigger::KeyPressed),
            key3: None,
        };
        let registry_service = RegistryService::new();
        registry_service.normalize_shortcut(&mut shortcut);
        assert_eq!(shortcut, normalized_shortcut);
    }

    #[fasync::run_singlethreaded(test)]
    async fn key_event_populates_key() {
        let event = KeyEvent::new(ui_input3::KeyEvent {
            timestamp: None,
            type_: None,
            key: Some(input::Key::A),
            modifiers: None,
        })
        .unwrap();
        assert_eq!(event.key, input::Key::A);
    }

    #[fasync::run_singlethreaded(test)]
    async fn key_event_key_required() {
        let event = KeyEvent::new(ui_input3::KeyEvent {
            timestamp: None,
            type_: None,
            key: None,
            modifiers: None,
        });
        assert!(event.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn key_event_pressed() {
        let event = KeyEvent::new(ui_input3::KeyEvent {
            timestamp: None,
            type_: Some(ui_input3::KeyEventType::Pressed),
            key: Some(input::Key::B),
            modifiers: None,
        })
        .unwrap();
        assert_eq!(event.pressed, Some(true));
    }

    #[fasync::run_singlethreaded(test)]
    async fn key_event_released() {
        let event = KeyEvent::new(ui_input3::KeyEvent {
            timestamp: None,
            type_: Some(ui_input3::KeyEventType::Released),
            key: Some(input::Key::B),
            modifiers: None,
        })
        .unwrap();
        assert_eq!(event.pressed, Some(false));
    }

    #[fasync::run_singlethreaded(test)]
    async fn key_event_pressed_unknown_on_unset_type() {
        let event = KeyEvent::new(ui_input3::KeyEvent {
            timestamp: None,
            type_: None,
            key: Some(input::Key::B),
            modifiers: None,
        })
        .unwrap();
        assert_eq!(event.pressed, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn key_event_pressed_unknown_on_sync() {
        let event = KeyEvent::new(ui_input3::KeyEvent {
            timestamp: None,
            type_: Some(ui_input3::KeyEventType::Sync),
            key: Some(input::Key::B),
            modifiers: None,
        })
        .unwrap();
        assert_eq!(event.pressed, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn key_event_pressed_unknown_on_cancel() {
        let event = KeyEvent::new(ui_input3::KeyEvent {
            timestamp: None,
            type_: Some(ui_input3::KeyEventType::Cancel),
            key: Some(input::Key::B),
            modifiers: None,
        })
        .unwrap();
        assert_eq!(event.pressed, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn manager_service_sync_cancel() -> Result<(), Error> {
        let store = RegistryStore::new();
        let mut manager_service = ManagerService::new(store);

        let was_handled = manager_service
            .handle_key(KeyEvent::new(ui_input3::KeyEvent {
                timestamp: None,
                type_: Some(ui_input3::KeyEventType::Sync),
                key: Some(input::Key::A),
                modifiers: None,
            })?)
            .await?;
        assert!(was_handled);
        assert_eq!(manager_service.keys_pressed, [input::Key::A].iter().cloned().collect());

        let was_handled = manager_service
            .handle_key(KeyEvent::new(ui_input3::KeyEvent {
                timestamp: None,
                type_: Some(ui_input3::KeyEventType::Cancel),
                key: Some(input::Key::A),
                modifiers: None,
            })?)
            .await?;
        assert!(was_handled);
        assert_eq!(manager_service.keys_pressed, HashSet::new());

        Ok(())
    }
}
