// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use appkit::*;
use fidl::endpoints::{create_proxy_and_stream, create_request_stream};
use fidl_fuchsia_element as felement;
use fidl_fuchsia_ui_app as ui_app;
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use fidl_fuchsia_ui_test_scene as ui_test_scene;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_scenic::flatland::ViewCreationTokenPair;
use futures::{future, FutureExt, StreamExt};
use tracing::{error, info};

use crate::{
    shortcuts::ShortcutAction,
    wm::{WMEvent, WindowManager},
};

#[fuchsia::test]
async fn test_wm() -> Result<(), Error> {
    let (sender, receiver) = EventSender::<WMEvent>::new();

    // Start the WM event loop and the ViewProvider that attaches it to the test scene provider.
    let wm_run_fut = WindowManager::run(sender.clone(), receiver).boxed();
    let view_provider_fut = start_view_provider(sender.clone()).boxed();
    let loop_fut = match future::select(wm_run_fut, view_provider_fut).await {
        future::Either::Right((_, wm_run_fut)) => wm_run_fut,
        _ => panic!("WM loop finished earlier than view_provider_future"),
    };

    let test_fut = async move {
        // Add child view 1. [ChildView1 (Focus)]
        info!("Add child_view1");
        let (_child_window1, mut child_receiver1) = create_child_view(sender.clone()).await;
        let resize_event = wait_for_resize_event(&mut child_receiver1).await;
        assert!(window_has_size(resize_event));
        let focus_event = wait_for_focus_event(&mut child_receiver1).await;
        assert!(window_has_focus(focus_event));
        info!("Focused on child_view1: {:?}", _child_window1.id());

        // Add child view 2. [ChildView1, ChildView2 (Focus)]
        info!("Add child_view2");
        let (_child_window2, mut child_receiver2) = create_child_view(sender.clone()).await;
        let resize_event = wait_for_resize_event(&mut child_receiver2).await;
        assert!(window_has_size(resize_event));
        // ChildView2 should gain focus.
        let focus_event = wait_for_focus_event(&mut child_receiver2).await;
        assert!(window_has_focus(focus_event));
        // ChildView1 should lose focus.
        let focus_event = wait_for_focus_event(&mut child_receiver1).await;
        assert!(!window_has_focus(focus_event));
        info!("Focused on child_view2: {:?}", _child_window2.id());

        // Add child view 3. [ChildView1, ChildView2, ChildView3 (Focus)]
        info!("Add child_view3");
        let (_child_window3, mut child_receiver3) = create_child_view(sender.clone()).await;
        let resize_event = wait_for_resize_event(&mut child_receiver3).await;
        assert!(window_has_size(resize_event));
        // ChildView3 should gain focus.
        let focus_event = wait_for_focus_event(&mut child_receiver3).await;
        assert!(window_has_focus(focus_event));
        // ChildView2 should lose focus.
        let focus_event = wait_for_focus_event(&mut child_receiver2).await;
        assert!(!window_has_focus(focus_event));
        info!("Focused on child_view3: {:?}", _child_window3.id());

        // Shortcut to focus on next view. Child view 1 should receive focus.
        // [ChildView2, ChildView3, ChildView1 (Focus)]
        info!("Switch focus next to  child_view1");
        let handled = invoke_shortcut(ShortcutAction::FocusNext, sender.clone()).await;
        assert!(matches!(handled, ui_shortcut2::Handled::Handled));
        // ChildView1 should gain focus.
        let focus_event = wait_for_focus_event(&mut child_receiver1).await;
        assert!(window_has_focus(focus_event));
        // ChildView3 should lose focus.
        let focus_event = wait_for_focus_event(&mut child_receiver3).await;
        assert!(!window_has_focus(focus_event));
        info!("Focused on child_view1: {:?}", _child_window1.id());

        // Shortcut to focus on previous view. Child view 2 should receive focus.
        // [ChildView1, ChildView2, ChildView3 (Focus)]
        info!("Switch focus previous to  child_view2");
        let handled = invoke_shortcut(ShortcutAction::FocusPrev, sender.clone()).await;
        assert!(matches!(handled, ui_shortcut2::Handled::Handled));
        // ChildView3 should gain focus.
        let focus_event = wait_for_focus_event(&mut child_receiver3).await;
        assert!(window_has_focus(focus_event));
        // ChildView1 should lose focus.
        let focus_event = wait_for_focus_event(&mut child_receiver1).await;
        assert!(!window_has_focus(focus_event));
        info!("Focused on child_view3: {:?}", _child_window3.id());

        // Shortcut to close active, top-most (ChildView3) view.  [ChildView1, ChildView2 (Focus)].
        info!("Close child_view3");
        let handled = invoke_shortcut(ShortcutAction::Close, sender.clone()).await;
        assert!(matches!(handled, ui_shortcut2::Handled::Handled));
        let focus_event = wait_for_focus_event(&mut child_receiver2).await;
        assert!(window_has_focus(focus_event));
        info!("Focused on child_view2: {:?}", _child_window2.id());

        sender.send(Event::Exit).expect("Failed to send");
    }
    .boxed();

    future::select(loop_fut, test_fut).await;
    Ok(())
}

async fn start_view_provider(event_sender: EventSender<WMEvent>) {
    let (view_provider, mut view_provider_request_stream) =
        create_request_stream::<ui_app::ViewProviderMarker>()
            .expect("failed to create ViewProvider request stream");

    let scene_provider_fut = async move {
        let scene_provider = connect_to_protocol::<ui_test_scene::ControllerMarker>()
            .expect("failed to connect to fuchsia.ui.test.scene.Controller");
        let _view_ref_koid = scene_provider
            .attach_client_view(ui_test_scene::ControllerAttachClientViewRequest {
                view_provider: Some(view_provider),
                ..ui_test_scene::ControllerAttachClientViewRequest::EMPTY
            })
            .await
            .expect("failed to attach root client view");
    };

    let view_provider_fut = async move {
        match view_provider_request_stream
            .next()
            .await
            .expect("Failed to read ViewProvider request stream")
        {
            Ok(ui_app::ViewProviderRequest::CreateView2 { args, .. }) => {
                event_sender
                    .send(Event::SystemEvent {
                        event: SystemEvent::ViewCreationToken {
                            token: args.view_creation_token.expect(
                                "ViewCreationToken missing in ViewProvider.CreateView2 request",
                            ),
                        },
                    })
                    .expect("Failed to send SystemEvent::ViewCreationToken event");
            }
            // Panic for all other CreateView requests and errors to fail the test.
            _ => panic!("ViewProvider impl only handles CreateView2()"),
        }
    };

    futures::join!(scene_provider_fut, view_provider_fut);
}

async fn create_child_view(
    parent_sender: EventSender<WMEvent>,
) -> (Window<WMEvent>, futures::channel::mpsc::UnboundedReceiver<Event<WMEvent>>) {
    let ViewCreationTokenPair { view_creation_token, viewport_creation_token } =
        ViewCreationTokenPair::new().expect("Fidl error");
    let view_spec = felement::ViewSpec {
        viewport_creation_token: Some(viewport_creation_token),
        ..felement::ViewSpec::EMPTY
    };
    parent_sender
        .send(Event::SystemEvent {
            event: SystemEvent::PresentViewSpec {
                view_spec_holder: ViewSpecHolder {
                    view_spec,
                    annotation_controller: None,
                    view_controller_request: None,
                    responder: None,
                },
            },
        })
        .expect("Failed to send SystemEvent::PresentViewSpec event");

    let (child_sender, child_receiver) = EventSender::<WMEvent>::new();
    let mut window = Window::new(child_sender).with_view_creation_token(view_creation_token);
    window.create_view().expect("Failed to create window for child view");
    (window, child_receiver)
}

async fn wait_for_resize_event(
    receiver: &mut futures::channel::mpsc::UnboundedReceiver<Event<WMEvent>>,
) -> Event<WMEvent> {
    wait_for_event(
        move |event| match event {
            Event::WindowEvent { event: WindowEvent::Resized { .. }, .. } => true,
            _ => false,
        },
        receiver,
    )
    .await
}

async fn wait_for_focus_event(
    receiver: &mut futures::channel::mpsc::UnboundedReceiver<Event<WMEvent>>,
) -> Event<WMEvent> {
    wait_for_event(
        move |event| match event {
            Event::WindowEvent { event: WindowEvent::Focused { .. }, .. } => true,
            _ => false,
        },
        receiver,
    )
    .await
}

async fn wait_for_event(
    mut callback: impl FnMut(&Event<WMEvent>) -> bool,
    receiver: &mut futures::channel::mpsc::UnboundedReceiver<Event<WMEvent>>,
) -> Event<WMEvent> {
    while let Some(child_event) = receiver.next().await {
        info!("Child event: {:?}", child_event);
        if callback(&child_event) {
            return child_event;
        }
    }
    return Event::Exit;
}

/// Removes events that may have accumulated since last checked and test does not care about.
fn _drain_events(receiver: &mut futures::channel::mpsc::UnboundedReceiver<Event<WMEvent>>) {
    while let Ok(Some(_)) = receiver.try_next() {}
}

fn window_has_size(event: Event<WMEvent>) -> bool {
    match event {
        Event::WindowEvent { event: WindowEvent::Resized { width, height, .. }, .. } => {
            width > 0 && height > 0
        }
        _ => false,
    }
}

fn window_has_focus(event: Event<WMEvent>) -> bool {
    match event {
        Event::WindowEvent { event: WindowEvent::Focused { focused }, .. } => focused,
        _ => false,
    }
}

async fn invoke_shortcut(
    action: ShortcutAction,
    sender: EventSender<WMEvent>,
) -> ui_shortcut2::Handled {
    let (listener_request, mut listener_stream) =
        create_proxy_and_stream::<ui_shortcut2::ListenerMarker>()
            .expect("Failed to create proxy and stream");

    fasync::Task::local(async move {
        if let Some(request) = listener_stream.next().await {
            match request {
                Ok(ui_shortcut2::ListenerRequest::OnShortcut { id, responder }) => {
                    sender
                        .send(Event::WindowEvent {
                            window_id: WindowId(0),
                            event: WindowEvent::Shortcut { id, responder },
                        })
                        .expect("Failed to send WindowEvent::Shortcut event");
                }
                Err(fidl::Error::ClientChannelClosed { .. }) => {
                    error!("Shortcut listener connection closed.");
                }
                Err(fidl_error) => {
                    error!("Shortcut listener error: {:?}", fidl_error);
                }
            }
        }
    })
    .detach();

    listener_request.on_shortcut(action as u32).await.expect("Failed to call on_shortcut")
}
