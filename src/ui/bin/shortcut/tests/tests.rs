// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]
use {
    anyhow::{format_err, Result},
    fidl::prelude::*,
    fidl_fuchsia_input as input, fidl_fuchsia_ui_input3 as ui_input3,
    fidl_fuchsia_ui_shortcut as ui_shortcut,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_syslog::fx_log_debug,
    fuchsia_zircon as zx,
    futures::{
        self, future,
        stream::{self, StreamExt},
        FutureExt,
    },
};

use crate::test_helpers::{Latch, ManagerService, RegistryService, ShortcutBuilder};

mod test_helpers;

static TEST_SHORTCUT_ID: u32 = 123;
static TEST_SHORTCUT_2_ID: u32 = 321;
static TEST_SHORTCUT_3_ID: u32 = 777;

const LISTENER_ACTIVATION_TIMEOUT: zx::Duration = zx::Duration::from_seconds(10);
const WAS_HANDLED_TIMEOUT: zx::Duration = zx::Duration::from_seconds(10);

/// Helper wrapper for a typical test case:
///  - One of: `keys` are pressed sequentially; or `events` happen sequentially.
///    Only one of the two may be used in any one `TestCase`.
///  - if `shortcut_hook` is provided:
///    - `shortcut_hook` is invoked for each shortcut activation
///    - may be called multiple times
///  - if `shortcut_hook` is not provided:
///    - default shortcut handler responds `false` to any shortcut activation
///  - if `handled_hook` is provided:
///    - `handled_hook` is invoked with `true` if any of the keys were handled, or
///      `false` if they were not.
///  - `keys` are released sequentially
struct TestCase {
    keys: Option<Vec<input::Key>>,
    events: Option<Vec<(input::Key, ui_input3::KeyEventType)>>,
    shortcut_hook: Box<dyn Fn(u32) -> bool>,
    handled_hook: Box<dyn Fn(bool) -> ()>,
    // If set, `handled_hook` above will be called when key releases are processed.
    // If not set, the hook will not be called.  This allows us to ensure that
    // keys are handled on presses.
    call_handled_hook_on_release: bool,
}

/// A type that multiplexes two different event streams into one.
#[derive(Debug)]
enum EventType {
    /// A subtype for shortcut activation.
    ShortcutActivation,
    /// A subtype for handled keys.
    KeyHandled(Result<bool>),
}

impl TestCase {
    fn new() -> Self {
        Self {
            keys: None,
            events: None,
            shortcut_hook: Box::new(|_| false),
            handled_hook: Box::new(|_| ()),
            call_handled_hook_on_release: true,
        }
    }

    // Set the sequence of keys that will be sent when `run` is called.  The test fixture
    // first sends key presses of the respective keys in the given sequence, followed by
    // key releases of the respective keys.  Currently the keys are released in the same
    // order they are pressed, which we may decide to change.
    // Returns `self` for chaining.
    fn set_keys(mut self, keys: Vec<input::Key>) -> Self {
        if let Some(_) = self.events {
            panic!("you may use only one of set_keys or set_events, not both");
        }
        self.keys = Some(keys);
        self
    }

    // Sets the sequence of expected events that will be sent when `run` is called.
    // Returns `self` for chaining.
    fn set_events(mut self, events: Vec<(input::Key, ui_input3::KeyEventType)>) -> Self {
        if let Some(_) = self.keys {
            panic!("you may use only one of set_keys or set_events, not both");
        }
        self.events = Some(events);
        self
    }

    fn set_shortcut_hook(mut self, shortcut_hook: Box<dyn Fn(u32) -> bool>) -> Self {
        self.shortcut_hook = shortcut_hook;
        self
    }

    fn set_handled_hook(mut self, handled_hook: Box<dyn Fn(bool) -> ()>) -> Self {
        self.handled_hook = handled_hook;
        self
    }

    // Set whether the hook set by `set_handled_hook` will be called on key release. It is
    // called by default, so passing `false` here will turn that off.
    // Returns `self` for chaining.
    fn set_call_handled_hook_on_release(mut self, call_handled_hook_on_release: bool) -> Self {
        self.call_handled_hook_on_release = call_handled_hook_on_release;
        self
    }

    fn respond_to_activation_request(
        &self,
        req: Result<ui_shortcut::ListenerRequest, fidl::Error>,
    ) -> EventType {
        if let Ok(ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. }) = req {
            responder.send((self.shortcut_hook)(id)).expect("responding from shortcut listener");
            EventType::ShortcutActivation
        } else {
            panic!("Error from listener.next() on shortcut activation");
        }
    }

    async fn handle_event_stream<S: futures::StreamExt<Item = EventType> + std::marker::Unpin>(
        &self,
        mut events: S,
    ) -> bool {
        loop {
            if let Some(EventType::KeyHandled(Ok(was_handled))) = events
                .next()
                .on_timeout(fasync::Time::after(WAS_HANDLED_TIMEOUT), || {
                    panic!("was_handled timeout")
                })
                .await
            {
                fx_log_debug!(
                    "TestCase::run/pressed: got from shortcut server: {:?}",
                    &was_handled
                );
                break was_handled;
            }
        }
    }

    /// Runs the test case, injecting key press events for `keys` into the input pipeline, in
    /// the order specified in the vector.
    async fn run_with_keys_pressed(
        &self,
        registry_service: &mut RegistryService,
        manager_service: &ManagerService,
        keys: &Vec<input::Key>,
    ) -> Result<()> {
        let listener = &mut registry_service.listener;

        // Stream of shortcut activation events.
        // For each shortcut activation, self.shortcut_hook is executed with shortcut id.
        let shortcut_activation_stream =
            listener.map(|req| self.respond_to_activation_request(req));

        let handle_key_fut = manager_service.press_multiple_key3(keys.to_vec());
        futures::pin_mut!(handle_key_fut);

        // Create a single-item event stream for pressing and handling all events.
        let key_stream =
            stream::once(handle_key_fut).map(|was_handled| EventType::KeyHandled(was_handled));

        let events = stream::select(key_stream, shortcut_activation_stream);

        // Advance combined stream of events and stop when the key was handled.
        let was_handled = self.handle_event_stream(events).await;
        (self.handled_hook)(was_handled);
        Ok(())
    }

    /// Runs the test case, injecting key release events for `keys` into the input pipeline, in the
    /// order specified in the vector.
    // TODO(fmil): If we could pass a closure into this method that calls either
    // `manager_service.press_multiple_key3`, or `manager_service.release_multiple_key3`,
    // we would not need this method, which mostly repeats `run_with_keys_pressed`.
    async fn run_with_keys_released(
        &self,
        registry_service: &mut RegistryService,
        manager_service: &ManagerService,
        keys: &Vec<input::Key>,
    ) -> Result<()> {
        let listener = &mut registry_service.listener;
        let shortcut_activation_stream =
            listener.map(|req| self.respond_to_activation_request(req));
        let handle_key_fut = manager_service.release_multiple_key3(keys.clone());
        futures::pin_mut!(handle_key_fut);
        let key_release_stream =
            stream::once(handle_key_fut).map(|was_handled| EventType::KeyHandled(was_handled));
        let events = stream::select(key_release_stream, shortcut_activation_stream);
        let was_handled = self.handle_event_stream(events).await;
        // Some tests may want to call this hook only on presses, so this one is
        // made conditional.
        if self.call_handled_hook_on_release {
            (self.handled_hook)(was_handled);
        }
        Ok(())
    }

    /// Run the test case, passing it the key events as specified in `events`, in the specified
    /// order.
    async fn run_with_events(
        self,
        registry_service: &mut RegistryService,
        manager_service: &ManagerService,
        events: &Vec<(input::Key, ui_input3::KeyEventType)>,
    ) -> Result<()> {
        let listener = &mut registry_service.listener;
        let shortcut_activation_stream =
            listener.map(|req| self.respond_to_activation_request(req));
        let handle_key_fut = manager_service.send_multiple_key3_event(events.clone());
        futures::pin_mut!(handle_key_fut);
        let handle_key_stream =
            stream::once(handle_key_fut).map(|was_handled| EventType::KeyHandled(was_handled));
        let events = stream::select(handle_key_stream, shortcut_activation_stream);
        let was_handled = self.handle_event_stream(events).await;
        (self.handled_hook)(was_handled);
        Ok(())
    }

    /// Run the shortcut test, pressing the chord set in either from [TestCase::set_keys],
    /// or [TestCase::set_events] to activate the shortcuts.
    async fn run(
        self,
        registry_service: &mut RegistryService,
        manager_service: &ManagerService,
    ) -> Result<()> {
        if let Some(ref keys) = self.keys {
            // Keys are pressed and released to trigger the test.
            self.run_with_keys_pressed(registry_service, manager_service, keys).await?;
            self.run_with_keys_released(registry_service, manager_service, keys).await?;
        } else if let Some(ref events) = self.events {
            // Decouple events from self so self can be moved.
            let events = events.clone();
            self.run_with_events(registry_service, manager_service, &events).await?;
        } else {
            panic!("no events have been scheduled either with set_keys or set_events");
        }
        Ok(())
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_keys3() -> Result<()> {
    let mut registry_service = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;
    manager_service.set_focus_chain(vec![&registry_service.view_ref]).await?;

    // Set shortcut for either LEFT_SHIFT or RIGHT_SHIFT + E.
    let shortcut = ShortcutBuilder::new()
        .set_id(TEST_SHORTCUT_ID)
        .set_key3(input::Key::E)
        .set_keys_required(vec![input::Key::LeftShift])
        .build();
    registry_service.register_shortcut(shortcut).await?;

    let shortcut = ShortcutBuilder::new()
        .set_id(TEST_SHORTCUT_ID)
        .set_key3(input::Key::E)
        .set_keys_required(vec![input::Key::RightShift])
        .build();
    registry_service.register_shortcut(shortcut).await?;

    // Set shortcut for RIGHT_CONTROL + R.
    let shortcut = ShortcutBuilder::new()
        .set_id(TEST_SHORTCUT_2_ID)
        .set_key3(input::Key::R)
        .set_keys_required(vec![input::Key::RightCtrl, input::Key::RightShift])
        .build();
    registry_service.register_shortcut(shortcut).await?;

    // Process key event that *does not* trigger a shortcut.
    TestCase::new()
        .set_keys(vec![input::Key::E])
        .set_handled_hook(Box::new(|was_handled| assert_eq!(false, was_handled)))
        .set_call_handled_hook_on_release(false)
        .run(&mut registry_service, &manager_service)
        .await?;

    // LeftShift + E triggers a shortcut.
    let left_shift_e_latch = Latch::new();
    {
        let left_shift_e = left_shift_e_latch.clone();
        TestCase::new()
            .set_keys(vec![input::Key::LeftShift, input::Key::E])
            .set_shortcut_hook(Box::new(|id| {
                assert_eq!(id, TEST_SHORTCUT_ID);
                true
            }))
            .set_call_handled_hook_on_release(false)
            .set_handled_hook(Box::new(move |was_handled| left_shift_e.latch(was_handled)))
            .run(&mut registry_service, &manager_service)
            .await?;
    }
    assert_eq!(true, left_shift_e_latch.get_value());

    // RightShift + E triggers a shortcut.
    let right_shift_e_latch = Latch::new();
    {
        let right_shift_e = right_shift_e_latch.clone();
        TestCase::new()
            .set_keys(vec![input::Key::RightShift, input::Key::E])
            .set_shortcut_hook(Box::new(|id| {
                assert_eq!(id, TEST_SHORTCUT_ID);
                true
            }))
            .set_call_handled_hook_on_release(false)
            .set_handled_hook(Box::new(move |was_handled| right_shift_e.latch(was_handled)))
            .run(&mut registry_service, &manager_service)
            .await?;
    }
    assert_eq!(true, right_shift_e_latch.get_value());

    // RightCtrl + RightShift + R triggers a shortcut.
    let ctrl_shift_r_latch = Latch::new();
    {
        let ctrl_shift_r = ctrl_shift_r_latch.clone();
        TestCase::new()
            .set_keys(vec![input::Key::RightCtrl, input::Key::RightShift, input::Key::R])
            .set_shortcut_hook(Box::new(|id| {
                assert_eq!(id, TEST_SHORTCUT_2_ID);
                true
            }))
            .set_handled_hook(Box::new(move |was_handled| ctrl_shift_r.latch(was_handled)))
            .set_call_handled_hook_on_release(false)
            .run(&mut registry_service, &manager_service)
            .await?;
    }
    assert_eq!(true, ctrl_shift_r_latch.get_value());

    // RightShift + RightCtrl + R triggers a shortcut.
    let ctrl_shift_r_latch = Latch::new();
    {
        let ctrl_shift_r = ctrl_shift_r_latch.clone();
        TestCase::new()
            .set_keys(vec![input::Key::RightShift, input::Key::RightCtrl, input::Key::R])
            .set_shortcut_hook(Box::new(|id| {
                assert_eq!(id, TEST_SHORTCUT_2_ID);
                true
            }))
            .set_handled_hook(Box::new(move |was_handled| ctrl_shift_r.latch(was_handled)))
            .set_call_handled_hook_on_release(false)
            .run(&mut registry_service, &manager_service)
            .await?;
    }
    assert_eq!(true, ctrl_shift_r_latch.get_value());

    // LeftCtrl + R does not trigger a shortcut.
    TestCase::new()
        .set_keys(vec![input::Key::LeftCtrl, input::Key::R])
        .set_handled_hook(Box::new(move |was_handled| assert_eq!(false, was_handled)))
        .set_call_handled_hook_on_release(false)
        .run(&mut registry_service, &manager_service)
        .await?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_client_timeout() -> Result<()> {
    let mut registry_service = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    let shortcut =
        ShortcutBuilder::new().set_id(TEST_SHORTCUT_3_ID).set_key3(input::Key::J).build();
    registry_service.register_shortcut(shortcut).await?;

    let listener = &mut registry_service.listener;

    manager_service.set_focus_chain(vec![&registry_service.view_ref]).await?;

    let (was_handled, listener_activated) = future::join(
        manager_service.press_key3(input::Key::J),
        listener
            .next()
            .map(|req| {
                if let Some(Ok(ui_shortcut::ListenerRequest::OnShortcut { responder, .. })) = req {
                    // Shutdown the channel instead of responding, adding an epitaph
                    // to simplify debugging the test in case of a flake.
                    responder.control_handle().shutdown_with_epitaph(zx::Status::OK);
                }
                Ok(())
            })
            .on_timeout(fasync::Time::after(LISTENER_ACTIVATION_TIMEOUT), || {
                Err(format_err!("Shortcut not activated."))
            }),
    )
    .await;

    assert!(listener_activated.is_ok());

    assert_eq!(false, was_handled?);

    // Release the pressed key.
    manager_service.release_key3(input::Key::J).await?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_focus_change() -> Result<()> {
    let mut client1 = RegistryService::new().await?;
    let mut client2 = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    client1
        .register_shortcut(
            ShortcutBuilder::new().set_key3(input::Key::F).set_id(TEST_SHORTCUT_ID).build(),
        )
        .await?;
    client2
        .register_shortcut(
            ShortcutBuilder::new().set_key3(input::Key::F).set_id(TEST_SHORTCUT_2_ID).build(),
        )
        .await?;

    manager_service.set_focus_chain(vec![&client1.view_ref]).await?;

    // Scope part of the test case to release listeners and borrows once done.
    {
        let handled = Latch::new();
        let client1 = {
            let handled = handled.clone();
            TestCase::new()
                .set_keys(vec![input::Key::F])
                .set_shortcut_hook(Box::new(move |id| {
                    assert_eq!(id, TEST_SHORTCUT_ID);
                    true
                }))
                .set_handled_hook(Box::new(move |was_handled| handled.latch(was_handled)))
                .run(&mut client1, &manager_service)
        };
        futures::pin_mut!(client1);

        let client2 = client2.listener.next();
        futures::pin_mut!(client2);

        let activated_listener = future::select(client1, client2).await;

        assert_eq!(true, handled.get_value());
        assert!(matches!(activated_listener, future::Either::Left { .. }));
    }

    // Change focus to another client.
    manager_service.set_focus_chain(vec![&client2.view_ref]).await?;

    // Scope part of the test case to release listeners and borrows once done.
    {
        let client1 = client1.listener.next();
        futures::pin_mut!(client1);

        let handled = Latch::new();
        let client2 = {
            let handled = handled.clone();
            TestCase::new()
                .set_keys(vec![input::Key::F])
                .set_shortcut_hook(Box::new(move |id| {
                    assert_eq!(id, TEST_SHORTCUT_2_ID);
                    true
                }))
                .set_handled_hook(Box::new(move |was_handled| handled.latch(was_handled)))
                .run(&mut client2, &manager_service)
        };
        futures::pin_mut!(client2);
        let activated_listener = future::select(client2, client1).await;
        assert_eq!(true, handled.get_value());
        assert!(matches!(activated_listener, future::Either::Left { .. }));
    }

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_multiple_matches() -> Result<()> {
    let mut parent = RegistryService::new().await?;
    let mut child = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&parent.view_ref, &child.view_ref]).await?;

    // Set parent shortcut for LEFT_SHIFT + G.
    let shortcut = ShortcutBuilder::new()
        .set_keys_required(vec![input::Key::LeftShift])
        .set_key3(input::Key::G)
        .set_id(TEST_SHORTCUT_ID)
        .build();
    parent.register_shortcut(shortcut).await?;

    // Set same shortcut for a child view with a different shortcut ID.
    let shortcut = ShortcutBuilder::new()
        .set_keys_required(vec![input::Key::LeftShift])
        .set_key3(input::Key::G)
        .set_id(TEST_SHORTCUT_2_ID)
        .build();
    child.register_shortcut(shortcut).await?;

    // Add another child shortcut with a different ID.
    let shortcut = ShortcutBuilder::new()
        .set_keys_required(vec![input::Key::LeftShift])
        .set_key3(input::Key::G)
        .set_id(TEST_SHORTCUT_3_ID)
        .build();
    child.register_shortcut(shortcut).await?;

    let parent_fut = parent.listener.next();
    futures::pin_mut!(parent_fut);

    let handled = Latch::new();
    let child_fut = {
        let handled = handled.clone();
        TestCase::new()
            .set_keys(vec![input::Key::LeftShift, input::Key::G])
            .set_shortcut_hook(Box::new(move |id| {
                assert_eq!(id, TEST_SHORTCUT_2_ID);
                true
            }))
            .set_handled_hook(Box::new(move |was_handled| handled.latch(was_handled)))
            .run(&mut child, &manager_service)
    };
    futures::pin_mut!(child_fut);
    let activated_listener = future::select(child_fut, parent_fut).await;
    assert_eq!(true, handled.get_value());
    assert!(matches!(activated_listener, future::Either::Left { .. }));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_priority_matches() -> Result<()> {
    let mut parent = RegistryService::new().await?;
    let mut child = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&parent.view_ref, &child.view_ref]).await?;

    // Register parent shortcut, with priority.
    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::H)
        .set_id(TEST_SHORTCUT_ID)
        .set_use_priority(true)
        .build();
    parent.register_shortcut(shortcut).await?;

    // Register child shortcut, without priority.
    let shortcut =
        ShortcutBuilder::new().set_key3(input::Key::H).set_id(TEST_SHORTCUT_2_ID).build();
    child.register_shortcut(shortcut).await?;

    let handled = Latch::new();
    let parent_fut = {
        let handled = handled.clone();
        TestCase::new()
            .set_keys(vec![input::Key::H])
            .set_shortcut_hook(Box::new(|id| {
                assert_eq!(id, TEST_SHORTCUT_ID);
                true
            }))
            .set_handled_hook(Box::new(move |was_handled| handled.latch(was_handled)))
            .run(&mut parent, &manager_service)
    };
    futures::pin_mut!(parent_fut);

    let child_fut = child.listener.next();
    futures::pin_mut!(child_fut);

    let activated_listener = future::select(parent_fut, child_fut).await;

    assert_eq!(true, handled.get_value());
    assert!(matches!(activated_listener, future::Either::Left { .. }));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_multiple_priority_matches() -> Result<()> {
    let mut parent = RegistryService::new().await?;
    let mut child = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&parent.view_ref, &child.view_ref]).await?;

    // Register parent shortcut, with priority.
    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::I)
        .set_id(TEST_SHORTCUT_ID)
        .set_use_priority(true)
        .build();
    parent.register_shortcut(shortcut).await?;

    // Register child shortcut, with priority.
    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::I)
        .set_use_priority(true)
        .set_id(TEST_SHORTCUT_2_ID)
        .build();
    child.register_shortcut(shortcut).await?;

    let handled = Latch::new();
    let parent_fut = {
        let handled = handled.clone();
        TestCase::new()
            .set_keys(vec![input::Key::I])
            .set_shortcut_hook(Box::new(move |id| {
                assert_eq!(id, TEST_SHORTCUT_ID);
                true
            }))
            .set_handled_hook(Box::new(move |was_handled| handled.latch(was_handled)))
            .run(&mut parent, &manager_service)
    };
    futures::pin_mut!(parent_fut);

    let child_fut = child.listener.next();
    futures::pin_mut!(child_fut);

    let activated_listener = future::select(parent_fut, child_fut).await;

    assert_eq!(true, handled.get_value());
    assert!(matches!(activated_listener, future::Either::Left { .. }));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_priority_same_client() -> Result<()> {
    let mut client = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&client.view_ref]).await?;

    // Register a shortcut, without priority.
    let shortcut =
        ShortcutBuilder::new().set_key3(input::Key::K).set_id(TEST_SHORTCUT_2_ID).build();
    client.register_shortcut(shortcut).await?;

    // Register a shortcut, with priority.
    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::K)
        .set_id(TEST_SHORTCUT_ID)
        .set_use_priority(true)
        .build();
    client.register_shortcut(shortcut).await?;

    let handled = Latch::new();
    let handled_clone = handled.clone();
    TestCase::new()
        .set_keys(vec![input::Key::K])
        .set_shortcut_hook(Box::new(move |id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        }))
        .set_handled_hook(Box::new(move |was_handled| handled_clone.latch(was_handled)))
        .run(&mut client, &manager_service)
        .await?;

    assert_eq!(true, handled.get_value());

    Ok(())
}

// A shortcut activated by pressing and releasing the left Meta key and nothing
// else.
#[fasync::run_singlethreaded(test)]
async fn handle_meta_press_release_shortcut() -> Result<()> {
    let mut client = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&client.view_ref]).await?;

    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::LeftMeta)
        .set_trigger(ui_shortcut::Trigger::KeyPressedAndReleased)
        .set_id(TEST_SHORTCUT_ID)
        .build();
    client.register_shortcut(shortcut).await?;

    let handled = Latch::new();
    let handled_clone = handled.clone();
    TestCase::new()
        .set_keys(vec![input::Key::LeftMeta])
        .set_shortcut_hook(Box::new(move |id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        }))
        .set_handled_hook(Box::new(move |handled| handled_clone.latch(handled)))
        .run(&mut client, &manager_service)
        .await?;
    assert_eq!(true, handled.get_value(), "shortcut was not handled");

    Ok(())
}

// When we watch for LeftMeta/press+release and LeftMeta+K/press, only the
// latter will be triggered.
#[fasync::run_singlethreaded(test)]
async fn handle_overlapping_shortcuts_with_meta_key() -> Result<()> {
    let mut client = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&client.view_ref]).await?;

    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::LeftMeta)
        .set_trigger(ui_shortcut::Trigger::KeyPressedAndReleased)
        .set_id(TEST_SHORTCUT_ID)
        .build();
    client.register_shortcut(shortcut).await?;
    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::K)
        .set_keys_required(vec![input::Key::LeftMeta])
        .set_trigger(ui_shortcut::Trigger::KeyPressed)
        .set_id(TEST_SHORTCUT_2_ID)
        .build();
    client.register_shortcut(shortcut).await?;

    // When LeftMeta+K is pressed, only the LeftMeta+K shortcut is actuated,
    // not the LeftMeta-press-release.
    let handled = Latch::new();
    let handled_clone = handled.clone();
    TestCase::new()
        .set_keys(vec![input::Key::LeftMeta, input::Key::K])
        .set_shortcut_hook(Box::new(move |id| {
            assert_eq!(id, TEST_SHORTCUT_2_ID);
            true
        }))
        .set_handled_hook(Box::new(move |handled| handled_clone.latch(handled)))
        .run(&mut client, &manager_service)
        .await?;
    assert_eq!(true, handled.get_value(), "event was not handled");

    Ok(())
}

// Shows that a press+release shortcut is not triggered if a key intervenes.
#[fasync::run_singlethreaded(test)]
async fn do_not_handle_shortcut_with_intervening_keys() -> Result<()> {
    let mut client = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&client.view_ref]).await?;

    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::LeftMeta)
        .set_trigger(ui_shortcut::Trigger::KeyPressedAndReleased)
        .set_id(TEST_SHORTCUT_ID)
        .build();
    client.register_shortcut(shortcut).await?;

    let handled = Latch::new();
    let shortcut_activated = Latch::new();

    // Clones here and below are moved into the hooks supplied to TestCase.
    let shortcut_activated_clone = shortcut_activated.clone();
    let handled_clone = handled.clone();

    use ui_input3::KeyEventType::{Pressed, Released};
    TestCase::new()
        // LeftMeta  _____/"""""""""""""""""\___________
        // K         ________/""""""""\_________________
        //
        // """" - actuated
        // ____ - not actuated
        .set_events(vec![
            (input::Key::LeftMeta, Pressed),
            (input::Key::K, Pressed),
            (input::Key::K, Released),
            (input::Key::LeftMeta, Released),
        ])
        .set_shortcut_hook(Box::new(move |id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            shortcut_activated_clone.latch(true);
            true
        }))
        .set_handled_hook(Box::new(move |handled| handled_clone.latch(handled)))
        .run(&mut client, &manager_service)
        .await?;
    assert_eq!(false, handled.get_value(), "event was handled but should not have been");
    assert_eq!(
        0,
        shortcut_activated.get_num_valid_sets(),
        "event was not handled the expected number of times"
    );

    Ok(())
}

// Shows that when the user tries to cycle through open windows, the cycle shortcut
// is invoked as many times as the user requested it to.
//
// This shortcut is typically LeftMeta+Tab, and is typically done by holding LeftMeta
// and pressing Tab as many times as is needed to reach the desired window.
#[fasync::run_singlethreaded(test)]
async fn handle_meta_tab_tab() -> Result<()> {
    let mut client = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&client.view_ref]).await?;

    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::Tab)
        .set_trigger(ui_shortcut::Trigger::KeyPressedAndReleased)
        .set_keys_required(vec![input::Key::LeftMeta])
        .set_id(TEST_SHORTCUT_ID)
        .build();
    client.register_shortcut(shortcut).await?;

    let handled = Latch::new();
    // Clones here and below are moved into the hooks supplied to TestCase.
    let handled_clone = handled.clone();

    let shortcut_activated = Latch::new();
    let shortcut_activated_clone = shortcut_activated.clone();

    use ui_input3::KeyEventType::{Pressed, Released};
    TestCase::new()
        // LeftMeta  _____/"""""""""""""""""""""""""""""""""\___________
        // Tab       ________/""""""""\________/"""""""""\______________
        //
        // """" - actuated
        // ____ - not actuated
        .set_events(vec![
            (input::Key::LeftMeta, Pressed),
            (input::Key::Tab, Pressed),
            (input::Key::Tab, Released),
            (input::Key::Tab, Pressed),
            (input::Key::Tab, Released),
            (input::Key::LeftMeta, Released),
        ])
        .set_shortcut_hook(Box::new(move |id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            shortcut_activated_clone.latch(true);
            true
        }))
        .set_handled_hook(Box::new(move |handled| handled_clone.latch(handled)))
        .run(&mut client, &manager_service)
        .await?;
    assert_eq!(true, handled.get_value(), "event was not handled");
    assert_eq!(
        2,
        shortcut_activated.get_num_valid_sets(),
        "event was not handled the expected number of times"
    );

    Ok(())
}

// Verifies that registering a shortcut handler on a view_ref *after* that
// view_ref is hooked up into the focus chain still triggers a shortcut.
// See fxbug.dev/76758 for details.
#[fasync::run_singlethreaded(test)]
async fn shortcut_registration_after_focus_chain_set() -> Result<()> {
    let view_ref = RegistryService::new_view_ref();

    // Timing: a valid view ref is hooked into the focus chain before its
    // handler is registered.
    let manager_service = ManagerService::new().await?;
    // Set focus chain here.
    manager_service.set_focus_chain(vec![&view_ref]).await?;

    // Register a shortcut handler here.
    // Creation of `client` registers a shortcut with the given view_ref.
    let mut client = RegistryService::new_with_view_ref(view_ref).await?;

    let shortcut = ShortcutBuilder::new().set_key3(input::Key::K).set_id(TEST_SHORTCUT_ID).build();
    client.register_shortcut(shortcut).await?;

    let handled = Latch::new();
    let handled_clone = handled.clone();
    TestCase::new()
        .set_keys(vec![input::Key::K])
        .set_shortcut_hook(Box::new(move |id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        }))
        .set_handled_hook(Box::new(move |was_handled| handled_clone.latch(was_handled)))
        .run(&mut client, &manager_service)
        .await?;

    assert_eq!(true, handled.get_value(), "shortcut was supposed to be handled but wasn't");

    Ok(())
}
