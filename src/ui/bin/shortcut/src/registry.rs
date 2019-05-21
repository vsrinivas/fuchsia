// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_ui_input as ui_input;
use fidl_fuchsia_ui_shortcut as ui_shortcut;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_syslog::fx_log_err;
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

#[derive(Default)]
pub struct RegistryStore {
    // Weak ref for ClientRegistry to allow shortcuts to be dropped out
    // of collection once client connection is removed.
    registries: Vec<Weak<Mutex<ClientRegistry>>>,
}

const DEFAULT_SHORTCUT_ID: u32 = 2;

async fn handle(
    registry: Arc<Mutex<ClientRegistry>>,
    key: Option<ui_input::Key>,
    modifiers: Option<ui_input::Modifiers>,
) -> Result<bool, Error> {
    let registry = await!(registry.lock());
    let shortcut = registry.shortcuts.iter().find(|s| s.key == key && s.modifiers == modifiers);
    match (&registry.subscriber, shortcut) {
        (Some(Subscriber { ref listener, .. }), Some(shortcut)) => {
            // TODO: timeout if the listener hangs
            let id = shortcut.id.unwrap_or(DEFAULT_SHORTCUT_ID);
            await!(listener.on_shortcut(id)).map_err(Into::into)
        }
        _ => Ok(false),
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
        if Some(ui_input::KeyEventPhase::Pressed) != event.phase {
            return Ok(false);
        }

        // Clone, upgrade, and filter out stale Weak pointers.
        let registries = self.registries.iter().cloned().filter_map(|r| r.upgrade()).into_iter();

        // TODO: sort according to ViewRef hierarchy
        // TODO: resolve accounting for use_priority

        let was_handled = futures::stream::iter(registries).fold(false, {
            // Capture variables by reference, since `async` non-`move` closures with
            // arguments are not currently supported
            let key = event.key;
            let modifiers = event.modifiers;
            async move |was_handled, registry| {
                let handled =
                    await!(handle(registry, key, modifiers)).unwrap_or_else(|e: failure::Error| {
                        fx_log_err!("shortcut handle error: {:?}", e);
                        false
                    });
                handled || was_handled
            }
        });

        Ok(await!(was_handled))
    }
}
