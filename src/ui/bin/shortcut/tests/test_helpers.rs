// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::client::QueryResponseFut,
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input2 as ui_input2,
    fidl_fuchsia_ui_input3 as ui_input3, fidl_fuchsia_ui_shortcut as ui_shortcut,
    fidl_fuchsia_ui_views as ui_views,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
    futures::StreamExt,
    std::sync::Once,
};

static START: Once = Once::new();

/// Creates instances of FIDL fuchsia.ui.shortcut.Shortcut.
pub struct ShortcutBuilder {
    shortcut: ui_shortcut::Shortcut,
}

#[allow(dead_code)]
impl ShortcutBuilder {
    pub fn new() -> Self {
        Self {
            shortcut: ui_shortcut::Shortcut {
                id: None,
                modifiers: None,
                key: None,
                use_priority: None,
                trigger: None,
                key3: None,
                keys_required: None,
            },
        }
    }

    /// Creates a new instance of fuchsia.ui.shortcut.Shortcut.
    pub fn build(&self) -> ui_shortcut::Shortcut {
        ui_shortcut::Shortcut {
            id: self.shortcut.id,
            modifiers: self.shortcut.modifiers,
            key: self.shortcut.key,
            use_priority: self.shortcut.use_priority,
            trigger: self.shortcut.trigger,
            key3: self.shortcut.key3,
            keys_required: self.shortcut.keys_required.clone(),
        }
    }

    pub fn set_id(mut self, id: u32) -> Self {
        self.shortcut.id = Some(id);
        self
    }

    pub fn set_modifiers(mut self, modifiers: ui_input2::Modifiers) -> Self {
        self.shortcut.modifiers = Some(modifiers);
        self
    }

    pub fn set_key(mut self, key: ui_input2::Key) -> Self {
        self.shortcut.key = Some(key);
        self
    }

    pub fn set_use_priority(mut self, use_priority: bool) -> Self {
        self.shortcut.use_priority = Some(use_priority);
        self
    }

    pub fn set_trigger(mut self, trigger: ui_shortcut::Trigger) -> Self {
        self.shortcut.trigger = Some(trigger);
        self
    }

    pub fn set_key3(mut self, key3: input::Key) -> Self {
        self.shortcut.key3 = Some(key3);
        self
    }

    pub fn set_keys_required(mut self, keys_required: Vec<input::Key>) -> Self {
        self.shortcut.keys_required = Some(keys_required);
        self
    }
}

/// Test helper for FIDL fuchsia.ui.shortcut.Registry service.
pub struct RegistryService {
    pub registry: ui_shortcut::RegistryProxy,
    pub listener: ui_shortcut::ListenerRequestStream,
}

impl RegistryService {
    /// Creates the instance of the test helper and connects to shortcut registry service.
    pub async fn new() -> Result<Self, Error> {
        START.call_once(|| {
            fuchsia_syslog::init_with_tags(&["shortcut"])
                .expect("shortcut syslog init should not fail");
        });

        let registry = connect_to_service::<ui_shortcut::RegistryMarker>()
            .context("Failed to connect to Shortcut registry service")?;

        let (listener_client_end, listener) =
            fidl::endpoints::create_request_stream::<ui_shortcut::ListenerMarker>()?;

        // Set listener and view ref.
        let (raw_event_pair, _) = zx::EventPair::create()?;
        let view_ref = &mut ui_views::ViewRef { reference: raw_event_pair };
        registry.set_view(view_ref, listener_client_end).expect("set_view");

        Ok(Self { registry, listener })
    }

    /// Registers a new shortcut with the shortcut registry service.
    /// Returns a future that resolves to the FIDL response.
    pub async fn register_shortcut(&self, shortcut: ui_shortcut::Shortcut) -> QueryResponseFut<()> {
        match self.registry.register_shortcut(shortcut).check() {
            Err(e) => panic!("Error registering shortcut: {:?}", e),
            Ok(fut) => fut,
        }
    }

    /// Expects next FIDL request from Registry service to be a shortcut activation.
    /// `handler` is called to handle the shortcut activation.
    /// Return value from handler is routed to the shortcut registry service.
    pub async fn handle_shortcut_activation<HandleFunc>(&mut self, mut handler: HandleFunc)
    where
        HandleFunc: FnMut(u32) -> bool,
    {
        if let Some(Ok(ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. })) =
            self.listener.next().await
        {
            responder.send(handler(id)).expect("responding from shortcut listener for shift")
        } else {
            panic!("Error from listener.next() on shortcut activation");
        };
    }
}

/// Test helper for FIDL fuchsia.ui.shortcut.Manager service.
pub struct ManagerService {
    manager: ui_shortcut::ManagerProxy,
}

#[allow(dead_code)]
impl ManagerService {
    /// Creates the instance of the test helper and connects to shortcut manager service.
    pub async fn new() -> Result<Self, Error> {
        START.call_once(|| {
            fuchsia_syslog::init_with_tags(&["shortcut"])
                .expect("shortcut syslog init should not fail");
        });

        let manager = connect_to_service::<ui_shortcut::ManagerMarker>()
            .context("Failed to connect to Shortcut manager service")?;

        Ok(Self { manager })
    }

    /// Emulates a key press event using input2 interface.
    /// Returns a future that resolves to a FIDL response from manager service.
    pub fn press_key2(
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

        self.manager.handle_key_event(event)
    }

    /// Emulates a key release event using input2 interface.
    /// Returns a future that resolves to a FIDL response from manager service.
    pub fn release_key2(
        &self,
        key: ui_input2::Key,
        modifiers: Option<ui_input2::Modifiers>,
    ) -> QueryResponseFut<bool> {
        // Process key event that triggers a shortcut.
        let event = ui_input2::KeyEvent {
            key: Some(key),
            modifiers: modifiers,
            phase: Some(ui_input2::KeyEventPhase::Released),
            physical_key: None,
            semantic_key: None,
        };

        self.manager.handle_key_event(event)
    }

    /// Emulates a key press event using input3 interface.
    /// Returns a future that resolves to a FIDL response from manager service.
    pub fn press_key3(&self, key: input::Key) -> QueryResponseFut<bool> {
        // Process key event that triggers a shortcut.
        let event = ui_input3::KeyEvent {
            timestamp: None,
            type_: Some(ui_input3::KeyEventType::Pressed),
            key: Some(key),
            modifiers: None,
        };

        self.manager.handle_key3_event(event)
    }

    /// Emulates a key release event using input3 interface.
    /// Returns a future that resolves to a FIDL response from manager service.
    pub fn release_key3(&self, key: input::Key) -> QueryResponseFut<bool> {
        // Process key event that triggers a shortcut.
        let event = ui_input3::KeyEvent {
            timestamp: None,
            type_: Some(ui_input3::KeyEventType::Released),
            key: Some(key),
            modifiers: None,
        };

        self.manager.handle_key3_event(event)
    }

    /// Emulates multiple key press events sequentially using input3 interface.
    /// Returns `Ok(true)` if any of the keys were handled.
    pub async fn press_multiple_key3(&self, keys: Vec<input::Key>) -> Result<bool, Error> {
        let mut was_handled = false;
        let mut iter = keys.into_iter().peekable();
        while let Some(key) = iter.next() {
            let event = ui_input3::KeyEvent {
                timestamp: None,
                type_: Some(ui_input3::KeyEventType::Pressed),
                key: Some(key),
                modifiers: None,
            };
            let key_handled = self.manager.handle_key3_event(event).await?;
            if key_handled && iter.peek().is_some() {
                panic!("Shortcuts activated, but unused keys remained in the sequence!");
            }
            was_handled = was_handled || key_handled;
        }
        Ok(was_handled)
    }

    /// Emulates multiple key release events sequentially using input3 interface.
    /// Returns `Ok(true)` if any of the keys were handled.
    pub async fn release_multiple_key3(&self, keys: Vec<input::Key>) -> Result<bool, Error> {
        let mut was_handled = false;
        for key in keys.into_iter() {
            let event = ui_input3::KeyEvent {
                timestamp: None,
                type_: Some(ui_input3::KeyEventType::Released),
                key: Some(key),
                modifiers: None,
            };
            was_handled = was_handled || self.manager.handle_key3_event(event).await?;
        }
        Ok(was_handled)
    }
}
