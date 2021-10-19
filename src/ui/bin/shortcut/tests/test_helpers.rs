// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error, Result},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_focus as ui_focus,
    fidl_fuchsia_ui_input3 as ui_input3, fidl_fuchsia_ui_shortcut as ui_shortcut,
    fidl_fuchsia_ui_views as ui_views,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_scenic as scenic,
    fuchsia_syslog::fx_log_debug,
    std::sync::{self, Once},
};

static START: Once = Once::new();

/// A reference-counted value that can be latched to the value of "true" once.  Subsequent
/// invocations do not change the latch value, but the latch can be queried as to how many times
/// a valid "set" happened, using `get_num_valid_sets`.
#[derive(Clone, Debug)]
pub struct Latch {
    inner: sync::Arc<std::cell::Cell<i32>>,
}

impl Latch {
    pub fn new() -> Self {
        Latch { inner: sync::Arc::new(std::cell::Cell::new(0)) }
    }

    /// Returns the current value stored in the [Latch].
    pub fn get_value(&self) -> bool {
        self.get_num_valid_sets() > 0
    }

    /// Returns the number of calls that the latch was set.
    pub fn get_num_valid_sets(&self) -> i32 {
        self.inner.get()
    }

    /// Latches the `value` if it is "true".  Any subsequent calls to [Latch::get_value] will
    /// return `true`.
    pub fn latch(&self, value: bool) {
        if value {
            let val = self.get_num_valid_sets();
            self.inner.replace(val + 1);
        }
    }
}

/// Creates instances of FIDL fuchsia.ui.shortcut.Shortcut.
pub struct ShortcutBuilder {
    shortcut: ui_shortcut::Shortcut,
}

#[allow(dead_code)]
impl ShortcutBuilder {
    pub fn new() -> Self {
        Self { shortcut: ui_shortcut::Shortcut { ..ui_shortcut::Shortcut::EMPTY } }
    }

    /// Creates a new instance of fuchsia.ui.shortcut.Shortcut.
    pub fn build(&self) -> ui_shortcut::Shortcut {
        ui_shortcut::Shortcut {
            id: self.shortcut.id,
            use_priority: self.shortcut.use_priority,
            trigger: self.shortcut.trigger,
            key3: self.shortcut.key3,
            keys_required: self.shortcut.keys_required.clone(),
            ..ui_shortcut::Shortcut::EMPTY
        }
    }

    pub fn set_id(mut self, id: u32) -> Self {
        self.shortcut.id = Some(id);
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
    pub view_ref: ui_views::ViewRef,
    pub registry: ui_shortcut::RegistryProxy,
    pub listener: ui_shortcut::ListenerRequestStream,
}

impl RegistryService {
    /// Creates the instance of the test helper and connects to shortcut registry service.
    pub async fn new() -> Result<Self> {
        RegistryService::new_with_view_ref(RegistryService::new_view_ref()).await
    }

    /// Registers a new shortcut with the shortcut registry service.
    /// Returns a future that resolves to the FIDL response.
    pub async fn register_shortcut(&self, shortcut: ui_shortcut::Shortcut) -> Result<()> {
        self.registry.register_shortcut(shortcut).await.map_err(Into::into)
    }

    /// Creates a new dummy `ViewRef` for testing.
    pub fn new_view_ref() -> fidl_fuchsia_ui_views::ViewRef {
        scenic::ViewRefPair::new().expect("could not create ViewRef").view_ref
    }

    /// Creates a new client with a custom view ref.  Use with, for example
    /// [RegistryService::new_view_ref].
    pub async fn new_with_view_ref(view_ref: fidl_fuchsia_ui_views::ViewRef) -> Result<Self> {
        START.call_once(|| {
            fuchsia_syslog::init_with_tags(&["shortcut"])
                .expect("shortcut syslog init should not fail");
        });

        let registry = connect_to_protocol::<ui_shortcut::RegistryMarker>()
            .context("Failed to connect to Shortcut registry service")?;

        let (listener_client_end, listener) =
            fidl::endpoints::create_request_stream::<ui_shortcut::ListenerMarker>()?;

        // Set listener for the supplied view_ref.
        registry
            .set_view(&mut fuchsia_scenic::duplicate_view_ref(&view_ref)?, listener_client_end)?;

        Ok(Self { registry, listener, view_ref })
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

        let manager = connect_to_protocol::<ui_shortcut::ManagerMarker>()
            .context("Failed to connect to Shortcut manager service")?;

        Ok(Self { manager })
    }

    /// Emulates a key3 event.  Returns `true` if the key was handled, or error.
    pub async fn send_key3_event(
        &self,
        key: input::Key,
        event: ui_input3::KeyEventType,
    ) -> Result<bool> {
        // Process key event that triggers a shortcut.
        let event = ui_input3::KeyEvent {
            timestamp: None,
            type_: Some(event),
            key: Some(key),
            modifiers: None,
            ..ui_input3::KeyEvent::EMPTY
        };

        self.manager.handle_key3_event(event).await.map_err(Into::into)
    }

    /// Emulates sending the specified key events verbatim: can be used to send
    /// asymmetric events, which allows testing for specific key chords.
    /// Returns `Ok(true)` if at least one of the events in the stream was
    /// handled.
    pub async fn send_multiple_key3_event(
        &self,
        events: Vec<(input::Key, ui_input3::KeyEventType)>,
    ) -> Result<bool> {
        let mut was_handled = false;
        for (key, event_type) in events.into_iter() {
            let event_handled = self.send_key3_event(key, event_type).await?;
            was_handled = was_handled || event_handled;
        }
        Ok(was_handled)
    }

    /// Emulates a key press event using input3 interface.  Returns a a FIDL response from manager
    /// service.
    pub async fn press_key3(&self, key: input::Key) -> Result<bool> {
        self.send_key3_event(key, ui_input3::KeyEventType::Pressed).await
    }

    /// Emulates a key release event using input3 interface.  Returns a FIDL response from manager
    /// service.
    pub async fn release_key3(&self, key: input::Key) -> Result<bool> {
        self.send_key3_event(key, ui_input3::KeyEventType::Released).await
    }

    /// Emulates multiple key press events sequentially using input3 interface.
    /// Returns `Ok(true)` if any of the keys were handled.
    pub async fn press_multiple_key3(&self, keys: Vec<input::Key>) -> Result<bool> {
        let mut was_handled = false;
        let mut iter = keys.into_iter().peekable();
        while let Some(key) = iter.next() {
            fx_log_debug!("TestCase::press_multiple_keys: processing key: {:?}", &key);
            let key_handled = self.press_key3(key).await?;

            if key_handled && iter.peek().is_some() {
                panic!("Shortcuts activated, but unused keys remained in the sequence!");
            }
            was_handled = was_handled || key_handled;
            fx_log_debug!("TestCase::press_multiple_keys: was handled: {:?}", &was_handled);
        }
        Ok(was_handled)
    }

    /// Emulates multiple key release events sequentially using input3 interface.
    /// Returns `Ok(true)` if any of the keys were handled.
    pub async fn release_multiple_key3(&self, keys: Vec<input::Key>) -> Result<bool> {
        let mut was_handled = false;
        for key in keys.into_iter() {
            let key_handled = self.release_key3(key).await?;
            was_handled = was_handled || key_handled;
        }
        Ok(was_handled)
    }

    pub async fn set_focus_chain(&self, focus_chain: Vec<&ui_views::ViewRef>) -> Result<()> {
        let focus_chain = ui_focus::FocusChain {
            focus_chain: Some(
                focus_chain
                    .into_iter()
                    .map(scenic::duplicate_view_ref)
                    .collect::<Result<Vec<_>, _>>()?,
            ),
            ..ui_focus::FocusChain::EMPTY
        };
        self.manager.handle_focus_change(focus_chain).await.map_err(Into::into)
    }
}
