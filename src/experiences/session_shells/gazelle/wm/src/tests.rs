// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use anyhow::Error;
use appkit::*;
use fidl::endpoints::{
    create_proxy, create_proxy_and_stream, create_request_stream, DiscoverableProtocolMarker,
    ServerEnd,
};
use fidl_fuchsia_element as felement;
use fidl_fuchsia_sysmem as sysmem;
use fidl_fuchsia_ui_app as ui_app;
use fidl_fuchsia_ui_composition as ui_comp;
use fidl_fuchsia_ui_input3 as ui_input3;
use fidl_fuchsia_ui_observation_geometry as ui_geometry;
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use fidl_fuchsia_ui_test_scene as ui_test_scene;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route};
use fuchsia_scenic::flatland::ViewCreationTokenPair;
use futures::{channel::mpsc::UnboundedReceiver, future, FutureExt, Stream, StreamExt};
use tracing::{error, info};

use crate::{
    shortcuts::ShortcutAction,
    wm::{get_first_view_creation_token, WMEvent, WindowManager},
};

async fn set_up(
    sender: EventSender<WMEvent>,
    receiver: &mut UnboundedReceiver<Event<WMEvent>>,
) -> Result<(WindowManager, TestProtocolConnector, ui_geometry::ViewTreeWatcherProxy), Error> {
    let test_protocol_connector = TestProtocolConnector::new(build_realm().await?);

    let (view_tree_watcher, view_tree_watcher_request) =
        create_proxy::<ui_geometry::ViewTreeWatcherMarker>()?;
    let _services_task = fasync::Task::spawn(start_view_provider(
        sender.clone(),
        test_protocol_connector.connect_to_test_scene_controller()?,
        view_tree_watcher_request,
    ));

    let (view_creation_token, view_spec_holders) = get_first_view_creation_token(receiver).await;
    let wm = WindowManager::new(
        sender.clone(),
        view_creation_token,
        view_spec_holders,
        Box::new(test_protocol_connector.clone()),
    )?;
    Ok((wm, test_protocol_connector, view_tree_watcher))
}

fn loggable_event_receiver(
    receiver: UnboundedReceiver<Event<WMEvent>>,
) -> impl Stream<Item = Event<WMEvent>> {
    receiver.inspect(|event| info!("Parent event: {:?}", event))
}

#[fuchsia::test]
async fn test_wm() -> Result<(), Error> {
    let (sender, mut receiver) = EventSender::<WMEvent>::new();
    let (mut wm, test_protocol_connector, _) = set_up(sender.clone(), &mut receiver).await?;
    let receiver = loggable_event_receiver(receiver);

    let loop_fut = wm.run(receiver);

    let cloned_test_protocol_connector = test_protocol_connector.clone();
    let test_fut = async move {
        let test_protocol_connector = cloned_test_protocol_connector;
        // Add child view 1. [ChildView1 (Focus)]
        info!("Add child_view1");
        let (child_window1, mut child_receiver1, _) =
            create_child_view(sender.clone(), Box::new(test_protocol_connector.clone())).await;
        let resize_event = wait_for_resize_event(&mut child_receiver1).await;
        assert!(window_has_size(resize_event));
        let focus_event = wait_for_focus_event(&mut child_receiver1).await;
        assert!(window_has_focus(focus_event));
        info!("Focused on child_view1: {:?}", child_window1.id());

        // Add child view 2. [ChildView1, ChildView2 (Focus)]
        info!("Add child_view2");
        let (child_window2, mut child_receiver2, _) =
            create_child_view(sender.clone(), Box::new(test_protocol_connector.clone())).await;
        let resize_event = wait_for_resize_event(&mut child_receiver2).await;
        assert!(window_has_size(resize_event));
        // ChildView2 should gain focus.
        let focus_event = wait_for_focus_event(&mut child_receiver2).await;
        assert!(window_has_focus(focus_event));
        // ChildView1 should lose focus.
        let focus_event = wait_for_focus_event(&mut child_receiver1).await;
        assert!(!window_has_focus(focus_event));
        info!("Focused on child_view2: {:?}", child_window2.id());

        // Add child view 3. [ChildView1, ChildView2, ChildView3 (Focus)]
        info!("Add child_view3");
        let (child_window3, mut child_receiver3, _) =
            create_child_view(sender.clone(), Box::new(test_protocol_connector.clone())).await;
        let resize_event = wait_for_resize_event(&mut child_receiver3).await;
        assert!(window_has_size(resize_event));
        // ChildView3 should gain focus.
        let focus_event = wait_for_focus_event(&mut child_receiver3).await;
        assert!(window_has_focus(focus_event));
        // ChildView2 should lose focus.
        let focus_event = wait_for_focus_event(&mut child_receiver2).await;
        assert!(!window_has_focus(focus_event));
        info!("Focused on child_view3: {:?}", child_window3.id());

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
        info!("Focused on child_view1: {:?}", child_window1.id());

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
        info!("Focused on child_view3: {:?}", child_window3.id());

        // Shortcut to close active, top-most (ChildView3) view.  [ChildView1, ChildView2 (Focus)].
        info!("Close child_view3");
        let handled = invoke_shortcut(ShortcutAction::Close, sender.clone()).await;
        assert!(matches!(handled, ui_shortcut2::Handled::Handled));
        let focus_event = wait_for_focus_event(&mut child_receiver2).await;
        assert!(window_has_focus(focus_event));
        info!("Focused on child_view2: {:?}", child_window2.id());

        sender.send(Event::Exit).expect("Failed to send");
    }
    .boxed();

    let _ = future::join(loop_fut, test_fut).await;
    std::mem::drop(wm);
    test_protocol_connector.release().await?;
    Ok(())
}

// TODO(https://fxbug.dev/114716): Enable after test scene controller stops reporting FATAL error.
// #[fuchsia::test]
// async fn test_no_windows() -> Result<(), Error> {
//     let realm = build_realm().await?;
//     let test_protocol_connector = TestProtocolConnector::new(realm);

//     let (sender, mut receiver) = EventSender::<WMEvent>::new();

//     let scene_provider = test_protocol_connector.connect_to_test_scene_controller()?;
//     let (_view_tree_watcher, view_tree_watcher_request) =
//         create_proxy::<ui_geometry::ViewTreeWatcherMarker>()?;
//     let view_provider_fut =
//         start_view_provider(sender.clone(), scene_provider, view_tree_watcher_request);
//     let _services_task = fasync::Task::spawn(view_provider_fut);

//     let (_view_creation_token, _view_spec_holders) =
//         get_first_view_creation_token(&mut receiver).await;

//     sender.send(Event::Exit).expect("Failed to send");

//     test_protocol_connector.release().await?;
//     Ok(())
// }

#[fuchsia::test]
async fn test_dismiss_window_in_background() -> Result<(), Error> {
    let (sender, mut receiver) = EventSender::<WMEvent>::new();
    let (mut wm, test_protocol_connector, _) = set_up(sender.clone(), &mut receiver).await?;
    let receiver = loggable_event_receiver(receiver);

    let loop_fut = wm.run(receiver);

    let cloned_test_protocol_connector = test_protocol_connector.clone();
    let test_fut = async move {
        let test_protocol_connector = cloned_test_protocol_connector;

        // Add child view 1. [ChildView1 (Focus)]
        info!("Add child_view1");
        let (child_window1, mut child_receiver1, child_view_controller1) =
            create_child_view(sender.clone(), Box::new(test_protocol_connector.clone())).await;
        let resize_event = wait_for_resize_event(&mut child_receiver1).await;
        assert!(window_has_size(resize_event));
        let focus_event = wait_for_focus_event(&mut child_receiver1).await;
        assert!(window_has_focus(focus_event));
        info!("Focused on child_view1: {:?}", child_window1.id());

        // Add child view 2. [ChildView1, ChildView2 (Focus)]
        info!("Add child_view2");
        let (child_window2, mut child_receiver2, _) =
            create_child_view(sender.clone(), Box::new(test_protocol_connector.clone())).await;
        let resize_event = wait_for_resize_event(&mut child_receiver2).await;
        assert!(window_has_size(resize_event));
        // ChildView2 should gain focus.
        let focus_event = wait_for_focus_event(&mut child_receiver2).await;
        assert!(window_has_focus(focus_event));
        // ChildView1 should lose focus.
        let focus_event = wait_for_focus_event(&mut child_receiver1).await;
        assert!(!window_has_focus(focus_event));
        info!("Focused on child_view2: {:?}", child_window2.id());

        // Add child view 3. [ChildView1, ChildView2, ChildView3 (Focus)]
        info!("Add child_view3");
        let (child_window3, mut child_receiver3, _) =
            create_child_view(sender.clone(), Box::new(test_protocol_connector.clone())).await;
        let resize_event = wait_for_resize_event(&mut child_receiver3).await;
        assert!(window_has_size(resize_event));
        // ChildView3 should gain focus.
        let focus_event = wait_for_focus_event(&mut child_receiver3).await;
        assert!(window_has_focus(focus_event));
        // ChildView2 should lose focus.
        let focus_event = wait_for_focus_event(&mut child_receiver2).await;
        assert!(!window_has_focus(focus_event));
        info!("Focused on child_view3: {:?}", child_window3.id());

        // Dismiss the background ChildView1. [ChildView2, ChildView3 (Focus)].
        info!("Dismissing ChildView1");
        child_view_controller1.dismiss().expect("Failed to dismiss ChildView1");

        // Nothing should crash.
        sender.send(Event::Exit).expect("Failed to send");
    }
    .boxed();

    let _ = future::join(loop_fut, test_fut).await;
    std::mem::drop(wm);
    test_protocol_connector.release().await?;
    Ok(())
}

// TODO(https://fxbug.dev/114716): Enable after test scene controller stops reporting FATAL error.
// #[fuchsia::test]
// async fn test_immediately_close() -> Result<(), Error> {
//     let (sender, mut receiver) = EventSender::<WMEvent>::new();
//     let (mut wm, test_protocol_connector, _) = set_up(sender.clone(), &mut receiver).await?;
//     let receiver = loggable_event_receiver(receiver);

//     let loop_fut = wm.run(receiver);

//     let cloned_test_protocol_connector = test_protocol_connector.clone();
//     let test_fut = async move {
//         let test_protocol_connector = cloned_test_protocol_connector;
//         info!("Add child_view1");
//         create_child_view(sender.clone(), Box::new(test_protocol_connector.clone())).await;

//         sender.send(Event::Exit).expect("Failed to send");
//     }
//     .boxed();

//     let _ = future::join(loop_fut, test_fut).await;
//     test_protocol_connector.release().await?;
//     Ok(())
// }

// TODO(https://fxbug.dev/114716): Enable after test scene controller stops reporting FATAL error.
// #[fuchsia::test]
// async fn test_dismiss_before_attach() -> Result<(), Error> {
//     let (sender, mut receiver) = EventSender::<WMEvent>::new();
//     let (mut wm, test_protocol_connector, _) = set_up(sender.clone(), &mut receiver).await?;
//     let receiver = loggable_event_receiver(receiver);

//     let loop_fut = wm.run(receiver);

//     let cloned_test_protocol_connector = test_protocol_connector.clone();
//     let test_fut = async move {
//         let test_protocol_connector = cloned_test_protocol_connector;
//         info!("Add child_view1");
//         let (_, _, child_view_controller1) =
//             create_child_view(sender.clone(), Box::new(test_protocol_connector.clone())).await;
//         child_view_controller1.dismiss().expect("Failed to dismiss childview1");

//         sender.send(Event::Exit).expect("Failed to send");
//     }
//     .boxed();

//     let _ = future::join(loop_fut, test_fut).await;
//     test_protocol_connector.release().await?;
//     Ok(())
// }

async fn start_view_provider(
    event_sender: EventSender<WMEvent>,
    scene_provider: ui_test_scene::ControllerProxy,
    _view_tree_watcher_request: ServerEnd<ui_geometry::ViewTreeWatcherMarker>,
) {
    let (view_provider, mut view_provider_request_stream) =
        create_request_stream::<ui_app::ViewProviderMarker>()
            .expect("failed to create ViewProvider request stream");

    let scene_provider_fut = async move {
        if let Err(e) = scene_provider
            .attach_client_view(ui_test_scene::ControllerAttachClientViewRequest {
                view_provider: Some(view_provider),
                ..ui_test_scene::ControllerAttachClientViewRequest::EMPTY
            })
            .await
        {
            error!("Failed to attach client view to test SceneProvider: {:?}", e);
        }
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
    protocol_connector: Box<dyn ProtocolConnector>,
) -> (
    Window<WMEvent>,
    futures::channel::mpsc::UnboundedReceiver<Event<WMEvent>>,
    felement::ViewControllerProxy,
) {
    let ViewCreationTokenPair { view_creation_token, viewport_creation_token } =
        ViewCreationTokenPair::new().expect("Fidl error");
    let (view_controller_proxy, view_controller_request) =
        create_proxy::<felement::ViewControllerMarker>().expect("Fidl error");
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
                    view_controller_request: Some(view_controller_request),
                    responder: None,
                },
            },
        })
        .expect("Failed to send SystemEvent::PresentViewSpec event");

    let (child_sender, child_receiver) = EventSender::<WMEvent>::new();
    let mut window = Window::new(child_sender)
        .with_view_creation_token(view_creation_token)
        .with_protocol_connector(protocol_connector);
    window.create_view().expect("Failed to create window for child view");
    (window, child_receiver, view_controller_proxy)
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

async fn _drain_events_until_closed(
    receiver: &mut futures::channel::mpsc::UnboundedReceiver<Event<WMEvent>>,
) {
    while let Some(_) = receiver.next().await {}
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

#[derive(Clone)]
pub struct TestProtocolConnector(Arc<RealmInstance>);

impl ProtocolConnector for TestProtocolConnector {
    fn connect_to_flatland(&self) -> Result<ui_comp::FlatlandProxy, Error> {
        self.connect_to_protocol::<ui_comp::FlatlandMarker>()
    }

    fn connect_to_graphical_presenter(&self) -> Result<felement::GraphicalPresenterProxy, Error> {
        self.connect_to_protocol::<felement::GraphicalPresenterMarker>()
    }

    fn connect_to_shortcuts_registry(&self) -> Result<ui_shortcut2::RegistryProxy, Error> {
        self.connect_to_protocol::<ui_shortcut2::RegistryMarker>()
    }

    fn connect_to_keyboard(&self) -> Result<ui_input3::KeyboardProxy, Error> {
        self.connect_to_protocol::<ui_input3::KeyboardMarker>()
    }

    fn connect_to_sysmem_allocator(&self) -> Result<sysmem::AllocatorProxy, Error> {
        connect_to_protocol::<sysmem::AllocatorMarker>()
    }

    fn connect_to_flatland_allocator(&self) -> Result<ui_comp::AllocatorProxy, Error> {
        self.connect_to_protocol::<ui_comp::AllocatorMarker>()
    }

    fn box_clone(&self) -> Box<dyn ProtocolConnector> {
        Box::new(TestProtocolConnector(self.0.clone()))
    }
}

impl TestProtocolConnector {
    fn new(realm: RealmInstance) -> Self {
        Self(Arc::new(realm))
    }

    async fn release(self) -> Result<(), Error> {
        let realm = if let Ok(realm) = Arc::try_unwrap(self.0) {
            realm
        } else {
            panic!("Failed to release realm instance. Someone still has a reference to it.")
        };
        realm.destroy().await?;
        Ok(())
    }

    fn connect_to_protocol<P: DiscoverableProtocolMarker>(&self) -> Result<P::Proxy, Error>
    where
        P: DiscoverableProtocolMarker,
    {
        self.0.root.connect_to_protocol_at_exposed_dir::<P>()
    }

    fn connect_to_test_scene_controller(&self) -> Result<ui_test_scene::ControllerProxy, Error> {
        self.connect_to_protocol::<ui_test_scene::ControllerMarker>()
    }
}

async fn build_realm() -> anyhow::Result<RealmInstance> {
    let builder = RealmBuilder::new().await?;

    let test_ui_stack =
        builder.add_child("test-ui-stack", "#meta/test-ui-stack.cm", ChildOptions::new()).await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.ui.composition.Allocator"))
                .capability(Capability::protocol_by_name("fuchsia.ui.composition.Flatland"))
                .capability(Capability::protocol_by_name("fuchsia.ui.composition.Screenshot"))
                .capability(Capability::protocol_by_name("fuchsia.ui.input3.Keyboard"))
                .capability(Capability::protocol_by_name("fuchsia.ui.shortcut2.Registry"))
                .capability(Capability::protocol_by_name("fuchsia.ui.test.input.Registry"))
                .capability(Capability::protocol_by_name("fuchsia.ui.test.scene.Controller"))
                .from(&test_ui_stack)
                .to(Ref::parent()),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.scheduler.ProfileProvider"))
                .capability(Capability::protocol_by_name("fuchsia.sysmem.Allocator"))
                .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                .capability(Capability::protocol_by_name("fuchsia.vulkan.loader.Loader"))
                .from(Ref::parent())
                .to(&test_ui_stack),
        )
        .await?;

    let realm = builder.build().await?;
    Ok(realm)
}
