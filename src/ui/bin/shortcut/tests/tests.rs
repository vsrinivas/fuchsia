// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]
use {
    anyhow::{format_err, Error},
    fidl_fuchsia_input as input, fidl_fuchsia_ui_shortcut as ui_shortcut,
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
///  - `keys` are pressed sequentially
///  - if `shortcut_hook` is provided:
///    - `shortcut_hook` is invoked for each shortcut activation
///    - may be called multiple times
///  - if `shortcut_hook` is not provided:
///    - default shortcut handler responds `false` to any shortcut activation
///  - if `handled_hook` is provided:
///    - `handled_hook` is invoked with `true` if any of the keys were handled
///  - `keys` are released sequentially
struct TestCase<F: Fn(bool) -> ()> {
    keys: Option<Vec<input::Key>>,
    shortcut_hook: Option<fn(u32) -> bool>,
    handled_hook: Option<F>,
}

#[derive(Debug)]
enum EventType {
    ShortcutActivation,
    KeyHandled(Result<bool, Error>),
}

impl<F: Fn(bool) -> ()> TestCase<F> {
    fn new() -> Self {
        Self { keys: None, shortcut_hook: None, handled_hook: None }
    }

    fn set_keys(mut self, keys: Vec<input::Key>) -> Self {
        self.keys = Some(keys);
        self
    }

    fn set_shortcut_hook(mut self, shortcut_hook: fn(u32) -> bool) -> Self {
        self.shortcut_hook = Some(shortcut_hook);
        self
    }

    fn set_handled_hook(mut self, handled_hook: F) -> Self {
        self.handled_hook = Some(handled_hook);
        self
    }

    fn respond_to_activation_stream(
        &self,
        req: Result<ui_shortcut::ListenerRequest, fidl::Error>,
    ) -> EventType {
        if let Ok(ui_shortcut::ListenerRequest::OnShortcut { id, responder, .. }) = req {
            let shortcut_hook = self.shortcut_hook.unwrap_or(|_| false);
            responder.send(shortcut_hook(id)).expect("responding from shortcut listener");
            EventType::ShortcutActivation
        } else {
            panic!("Error from listener.next() on shortcut activation");
        }
    }

    async fn run(
        self,
        registry_service: &mut RegistryService,
        manager_service: &ManagerService,
    ) -> Result<(), Error> {
        // Keys are pressed and released to trigger the test.
        let keys = self.keys.as_ref().ok_or(format_err!("No keys specified!"))?;

        let listener = &mut registry_service.listener;

        // Stream of shortcut activation events.
        // For each shortcut activation, shortcut hook is executed with shortcut id.
        let shortcut_activation_stream = listener.map(|req| self.respond_to_activation_stream(req));

        let handle_key_fut = manager_service.press_multiple_key3(keys.to_vec());
        futures::pin_mut!(handle_key_fut);

        // Create a single-item event stream for pressing and handling all events.
        let handle_key_stream =
            stream::once(handle_key_fut).map(|was_handled| EventType::KeyHandled(was_handled));

        let mut events = stream::select(handle_key_stream, shortcut_activation_stream);

        // Advance combined stream of events and stop when the key was handled.
        let was_handled = loop {
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
        };
        if let Some(ref hook) = self.handled_hook {
            hook(was_handled);
        }

        // Release all the pressed keys, and repeat the handling exercise.
        let shortcut_activation_stream = listener.map(|req| self.respond_to_activation_stream(req));
        let handle_release_fut = manager_service.release_multiple_key3(keys.to_vec());
        futures::pin_mut!(handle_release_fut);
        let handle_key_stream =
            stream::once(handle_release_fut).map(|was_handled| EventType::KeyHandled(was_handled));
        let mut events = stream::select(handle_key_stream, shortcut_activation_stream);
        // TODO(fmil): This should be deduped with above, but I can't for the life of me figure out
        // the correct incantation to make the types work.  I hope to revisit and refactor.
        let was_handled = loop {
            if let Some(EventType::KeyHandled(Ok(was_handled))) = events
                .next()
                .on_timeout(fasync::Time::after(WAS_HANDLED_TIMEOUT), || {
                    panic!("was_handled timeout")
                })
                .await
            {
                fx_log_debug!(
                    "TestCase::run/released: got from shortcut server: {:?}",
                    &was_handled
                );
                break was_handled;
            }
        };
        if let Some(ref hook) = self.handled_hook {
            hook(was_handled);
        }
        Ok(())
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_keys3() -> Result<(), Error> {
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
        .set_handled_hook(|was_handled| assert_eq!(false, was_handled))
        .run(&mut registry_service, &manager_service)
        .await?;

    // LeftShift + E triggers a shortcut.
    let left_shift_e = Latch::new();
    TestCase::new()
        .set_keys(vec![input::Key::LeftShift, input::Key::E])
        .set_shortcut_hook(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        })
        .set_handled_hook(|was_handled| left_shift_e.latch_if_set(was_handled))
        .run(&mut registry_service, &manager_service)
        .await?;
    assert_eq!(true, left_shift_e.value());

    // RightShift + E triggers a shortcut.
    let right_shift_e = Latch::new();
    TestCase::new()
        .set_keys(vec![input::Key::RightShift, input::Key::E])
        .set_shortcut_hook(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        })
        .set_handled_hook(|was_handled| right_shift_e.latch_if_set(was_handled))
        .run(&mut registry_service, &manager_service)
        .await?;
    assert_eq!(true, right_shift_e.value());

    // RightCtrl + RightShift + R triggers a shortcut.
    let ctrl_shift_r = Latch::new();
    TestCase::new()
        .set_keys(vec![input::Key::RightCtrl, input::Key::RightShift, input::Key::R])
        .set_shortcut_hook(|id| {
            assert_eq!(id, TEST_SHORTCUT_2_ID);
            true
        })
        .set_handled_hook(|was_handled| ctrl_shift_r.latch_if_set(was_handled))
        .run(&mut registry_service, &manager_service)
        .await?;
    assert_eq!(true, ctrl_shift_r.value());

    // RightShift + RightCtrl + R triggers a shortcut.
    let ctrl_shift_r = Latch::new();
    TestCase::new()
        .set_keys(vec![input::Key::RightShift, input::Key::RightCtrl, input::Key::R])
        .set_shortcut_hook(|id| {
            assert_eq!(id, TEST_SHORTCUT_2_ID);
            true
        })
        .set_handled_hook(|was_handled| ctrl_shift_r.latch_if_set(was_handled))
        .run(&mut registry_service, &manager_service)
        .await?;
    assert_eq!(true, ctrl_shift_r.value());

    // LeftCtrl + R does not trigger a shortcut.
    TestCase::new()
        .set_keys(vec![input::Key::LeftCtrl, input::Key::R])
        .set_handled_hook(|was_handled| assert_eq!(false, was_handled))
        .run(&mut registry_service, &manager_service)
        .await?;

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_client_timeout() -> Result<(), Error> {
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
async fn test_focus_change() -> Result<(), Error> {
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
        let client1 = TestCase::new()
            .set_keys(vec![input::Key::F])
            .set_shortcut_hook(|id| {
                assert_eq!(id, TEST_SHORTCUT_ID);
                true
            })
            .set_handled_hook(|was_handled| handled.latch_if_set(was_handled))
            .run(&mut client1, &manager_service);
        futures::pin_mut!(client1);

        let client2 = client2.listener.next();
        futures::pin_mut!(client2);

        let activated_listener = future::select(client1, client2).await;

        assert_eq!(true, handled.value());
        assert!(matches!(activated_listener, future::Either::Left { .. }));
    }

    // Change focus to another client.
    manager_service.set_focus_chain(vec![&client2.view_ref]).await?;

    // Scope part of the test case to release listeners and borrows once done.
    {
        let client1 = client1.listener.next();
        futures::pin_mut!(client1);

        let handled = Latch::new();
        let client2 = TestCase::new()
            .set_keys(vec![input::Key::F])
            .set_shortcut_hook(|id| {
                assert_eq!(id, TEST_SHORTCUT_2_ID);
                true
            })
            .set_handled_hook(|was_handled| handled.latch_if_set(was_handled))
            .run(&mut client2, &manager_service);
        futures::pin_mut!(client2);

        let activated_listener = future::select(client2, client1).await;

        assert_eq!(true, handled.value());
        assert!(matches!(activated_listener, future::Either::Left { .. }));
    }

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_multiple_matches() -> Result<(), Error> {
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
    let child_fut = TestCase::new()
        .set_keys(vec![input::Key::LeftShift, input::Key::G])
        .set_shortcut_hook(|id| {
            assert_eq!(id, TEST_SHORTCUT_2_ID);
            true
        })
        .set_handled_hook(|was_handled| handled.latch_if_set(was_handled))
        .run(&mut child, &manager_service);
    futures::pin_mut!(child_fut);

    let activated_listener = future::select(child_fut, parent_fut).await;

    assert_eq!(true, handled.value());
    assert!(matches!(activated_listener, future::Either::Left { .. }));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_priority_matches() -> Result<(), Error> {
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
    let parent_fut = TestCase::new()
        .set_keys(vec![input::Key::H])
        .set_shortcut_hook(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        })
        .set_handled_hook(|was_handled| handled.latch_if_set(was_handled))
        .run(&mut parent, &manager_service);
    futures::pin_mut!(parent_fut);

    let child_fut = child.listener.next();
    futures::pin_mut!(child_fut);

    let activated_listener = future::select(parent_fut, child_fut).await;

    assert_eq!(true, handled.value());
    assert!(matches!(activated_listener, future::Either::Left { .. }));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_multiple_priority_matches() -> Result<(), Error> {
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
    let parent_fut = TestCase::new()
        .set_keys(vec![input::Key::I])
        .set_shortcut_hook(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        })
        .set_handled_hook(|was_handled| handled.latch_if_set(was_handled))
        .run(&mut parent, &manager_service);
    futures::pin_mut!(parent_fut);

    let child_fut = child.listener.next();
    futures::pin_mut!(child_fut);

    let activated_listener = future::select(parent_fut, child_fut).await;

    assert_eq!(true, handled.value());
    assert!(matches!(activated_listener, future::Either::Left { .. }));

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_priority_same_client() -> Result<(), Error> {
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
    TestCase::new()
        .set_keys(vec![input::Key::K])
        .set_shortcut_hook(|id| {
            assert_eq!(id, TEST_SHORTCUT_ID);
            true
        })
        .set_handled_hook(|was_handled| handled.latch_if_set(was_handled))
        .run(&mut client, &manager_service)
        .await?;

    assert_eq!(true, handled.value());

    Ok(())
}

// A shortcut activated by pressing and releasing the left Meta key and nothing
// else.
#[fasync::run_singlethreaded(test)]
async fn handle_meta_press_release_shortcut() -> Result<(), Error> {
    let mut client = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&client.view_ref]).await?;

    let shortcut = ShortcutBuilder::new()
        .set_key3(input::Key::LeftMeta)
        .set_trigger(ui_shortcut::Trigger::KeyPressedAndReleased)
        .set_id(TEST_SHORTCUT_ID)
        .build();
    client.register_shortcut(shortcut).await?;

    let was_handled = Latch::new();
    {
        let was_handled = was_handled.clone();
        TestCase::new()
            .set_keys(vec![input::Key::LeftMeta])
            .set_shortcut_hook(|id| {
                assert_eq!(id, TEST_SHORTCUT_ID);
                true
            })
            .set_handled_hook(|handled| was_handled.latch_if_set(handled))
            .run(&mut client, &manager_service)
            .await?;
    }
    assert_eq!(true, was_handled.value(), "shortcut was not handled");

    Ok(())
}

// When we watch for LeftMeta/press+release and LeftMeta+K/press, only the
// latter will be triggered.
#[fasync::run_singlethreaded(test)]
async fn handle_overlapping_shortcuts_with_meta_key() -> Result<(), Error> {
    let mut client = RegistryService::new().await?;
    let manager_service = ManagerService::new().await?;

    manager_service.set_focus_chain(vec![&client.view_ref]).await?;

    {
        let shortcut = ShortcutBuilder::new()
            .set_key3(input::Key::LeftMeta)
            .set_trigger(ui_shortcut::Trigger::KeyPressedAndReleased)
            .set_id(TEST_SHORTCUT_ID)
            .build();
        client.register_shortcut(shortcut).await?;
    }
    {
        let shortcut = ShortcutBuilder::new()
            .set_key3(input::Key::K)
            .set_keys_required(vec![input::Key::LeftMeta])
            .set_trigger(ui_shortcut::Trigger::KeyPressed)
            .set_id(TEST_SHORTCUT_2_ID)
            .build();
        client.register_shortcut(shortcut).await?;
    }

    // When LeftMeta+K is pressed, only the LeftMeta+K shortcut is actuated,
    // not the LeftMeta-press-release.
    let was_handled = Latch::new();
    {
        let was_handled = was_handled.clone();
        TestCase::new()
            .set_keys(vec![input::Key::LeftMeta, input::Key::K])
            .set_shortcut_hook(|id| {
                assert_eq!(id, TEST_SHORTCUT_2_ID);
                true
            })
            .set_handled_hook(|handled| was_handled.latch_if_set(handled))
            .run(&mut client, &manager_service)
            .await?;
    }
    assert_eq!(true, was_handled.value(), "event was not handled");

    Ok(())
}
