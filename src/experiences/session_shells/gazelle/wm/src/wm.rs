// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    fidl::endpoints,
    fidl_fuchsia_element as felement, fidl_fuchsia_math as fmath,
    fidl_fuchsia_ui_composition as ui_comp, fidl_fuchsia_ui_views as ui_views,
    fuchsia_scenic::flatland,
    fuchsia_scenic::ViewRefPair,
    futures::{stream, FutureExt, StreamExt},
    ui_views::{FocuserProxy, ViewCreationToken, ViewRef},
};

/// Width of the border around the screen.
const BORDER_WIDTH: i32 = 10;

/// Color of the background, and the border.
const BG_COLOR: ui_comp::ColorRgba =
    ui_comp::ColorRgba { red: 0.41, green: 0.24, blue: 0.21, alpha: 1.0 };

/// Main server for the gazelle window manager.
pub struct WindowManager {
    flatland: flatland::FlatlandProxy,
    id_generator: flatland::IdGenerator,
    root_focuser: FocuserProxy,
    viewport_size: fmath::SizeU,
    frame_transform_id: flatland::TransformId,
    window: Option<Window>,
}

impl WindowManager {
    /// Create a new WindowManager, build the UI, and attach to the given
    /// `view_creation_token`.
    pub async fn new(
        flatland: flatland::FlatlandProxy,
        mut view_creation_token: ViewCreationToken,
    ) -> anyhow::Result<Self> {
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            endpoints::create_proxy::<flatland::ParentViewportWatcherMarker>()
                .context("creating parent_viewport_watcher")?;
        let (view_focuser, view_focuser_request) =
            endpoints::create_proxy::<ui_views::FocuserMarker>()
                .context("creating view_focuser")?;
        flatland
            .create_view2(
                &mut view_creation_token,
                &mut ui_views::ViewIdentityOnCreation::from(ViewRefPair::new()?),
                flatland::ViewBoundProtocols {
                    view_focuser: Some(view_focuser_request),
                    ..flatland::ViewBoundProtocols::EMPTY
                },
                parent_viewport_watcher_request,
            )
            .context("creating root view")?;

        let viewport_size = parent_viewport_watcher
            .get_layout()
            .await
            .context("calling get_layout")?
            .logical_size
            .ok_or(anyhow!("get_layout didn't return logical_size"))?;

        // Create the "layout", such as it is.
        let mut id_generator = flatland::IdGenerator::new();

        let root_transform_id = id_generator.next_transform_id();
        flatland
            .create_transform(&mut root_transform_id.clone())
            .context("creating root transform")?;

        let frame_transform_id = id_generator.next_transform_id();
        flatland
            .create_transform(&mut frame_transform_id.clone())
            .context("creating frame transform")?;
        flatland
            .add_child(&mut root_transform_id.clone(), &mut frame_transform_id.clone())
            .context("connecting frame to root")?;
        flatland
            .set_translation(
                &mut frame_transform_id.clone(),
                &mut fmath::Vec_ { x: BORDER_WIDTH, y: BORDER_WIDTH },
            )
            .context("positioning frame")?;

        flatland
            .set_root_transform(&mut root_transform_id.clone())
            .context("connecting root to scene")?;

        let root_content_id = id_generator.next_content_id();
        flatland.create_filled_rect(&mut root_content_id.clone()).context("creating desktop")?;
        flatland
            .set_content(&mut root_transform_id.clone(), &mut root_content_id.clone())
            .context("installing desktop")?;
        flatland
            .set_solid_fill(
                &mut root_content_id.clone(),
                &mut BG_COLOR.clone(),
                &mut viewport_size.clone(),
            )
            .context("filling desktop")?;

        Ok(WindowManager {
            flatland,
            id_generator,
            root_focuser: view_focuser,
            viewport_size,
            frame_transform_id,
            window: None,
        })
    }

    /// Create a window for an application.
    //
    // TODO(hjfreyer@google.com): Consider supporting applications that don't
    // supply a `view_controller_server`.
    pub fn create_window(
        &mut self,
        mut viewport_creation_token: ui_views::ViewportCreationToken,
        _annotation_controller: Option<endpoints::ClientEnd<felement::AnnotationControllerMarker>>,
        view_controller_server: endpoints::ServerEnd<felement::ViewControllerMarker>,
    ) -> anyhow::Result<EventStream> {
        let content_id = self.id_generator.next_content_id();

        let (child_view_watcher, child_view_watcher_server) =
            endpoints::create_proxy::<flatland::ChildViewWatcherMarker>()
                .context("creating ChildViewWatcher proxy")?;

        self.flatland
            .create_viewport(
                &mut content_id.clone(),
                &mut viewport_creation_token,
                flatland::ViewportProperties {
                    logical_size: Some(fidl_fuchsia_math::SizeU {
                        width: self.viewport_size.width - 2 * (BORDER_WIDTH as u32),
                        height: self.viewport_size.height - 2 * (BORDER_WIDTH as u32),
                    }),
                    ..flatland::ViewportProperties::EMPTY
                },
                child_view_watcher_server,
            )
            .context("creating window viewport")?;
        self.flatland
            .set_content(&mut self.frame_transform_id.clone(), &mut content_id.clone())
            .context("attaching window viewport to frame")?;

        self.window = Some(Window { _annotation_controller, content_id });

        // Stream that notifies us when the child actually attaches to the
        // viewport.
        //
        // TODO(hjfreyer@google.com): Handle the case where `child_view_watcher`
        // closes. That's the last opportunity for the shell to cleanup any
        // state related to that child view.
        //
        // TODO(hjfreyer@google.com): Use `child_view_watcher.get_status()` to
        // delay showing the window until the child is "ready" (i.e., it has
        // rendered its first frame).
        let attached_event = child_view_watcher
            .get_view_ref()
            .map(move |res| {
                let child_view_ref = res.context("waiting for application's view_ref")?;
                Ok(Event::ChildAttached { window_content_id: content_id, child_view_ref })
            })
            .map(Event::from)
            .into_stream()
            .boxed();

        // Stream that notifies us when the child requests that the window be
        // dismissed OR when the view_controller closes.
        let dismiss_event = view_controller_server
            .into_stream()?
            // Read the stream, forwarding any errors along, but end the stream
            // as soon as we get a call to Dismiss.
            .scan((), |_, request| async move {
                match request.context("while reading ViewController request") {
                    Ok(felement::ViewControllerRequest::Dismiss { .. }) => None,
                    Err(err) => Some(Event::Err(err)),
                }
            })
            // Tack a DismissRequested event onto the end of the stream.
            .chain(stream::iter([Event::DismissRequested { window_content_id: content_id }]))
            .boxed();

        Ok(stream::select_all([attached_event, dismiss_event]).boxed())
    }

    /// Dismiss the active window, if any. If there isn't one, do nothing.
    fn dismiss_window(&mut self) -> EventStream {
        let maybe_window = std::mem::replace(&mut self.window, None);
        if let Some(mut window) = maybe_window {
            let release_viewport_result =
                self.flatland.release_viewport(&mut window.content_id).map(|result| {
                    Event::from(result.map(|_| Event::Ok).context("while releasing viewport"))
                });

            release_viewport_result.into_stream().boxed()
        } else {
            stream::empty().boxed()
        }
    }

    /// Handle a call to GraphicalPresenter/PresentView by closing the active
    /// window (if any), and creating a new window.
    pub fn present_view(
        &mut self,
        request: felement::GraphicalPresenterRequest,
    ) -> anyhow::Result<EventStream> {
        let felement::GraphicalPresenterRequest::PresentView {
            view_spec,
            annotation_controller,
            view_controller_request,
            responder,
        } = request;

        let viewport_creation_token = view_spec
            .viewport_creation_token
            .ok_or(anyhow!("view_spec didn't include viewport_creation_token"))?;

        let view_controller_server = view_controller_request
            .ok_or(anyhow!("request didn't include view_controller_request"))?;

        let dismiss_window_events = self.dismiss_window();

        let create_window_events = self.create_window(
            viewport_creation_token,
            annotation_controller,
            view_controller_server,
        )?;

        responder.send(&mut Ok(())).context("while replying to PresentView")?;
        Ok(stream::select_all([dismiss_window_events, create_window_events]).boxed())
    }

    /// Handle an asynchronous event returned by another method.
    pub fn handle_event(&mut self, event: Event) -> EventStream {
        match event {
            Event::Ok => stream::empty().boxed(),
            Event::Err(err) => {
                tracing::error!("async error: {}", err);
                stream::empty().boxed()
            }
            Event::ChildAttached { window_content_id, child_view_ref } => match &mut self.window {
                Some(window) if window.content_id == window_content_id => self
                    .root_focuser
                    .set_auto_focus(ui_views::FocuserSetAutoFocusRequest {
                        view_ref: Some(child_view_ref),
                        ..ui_views::FocuserSetAutoFocusRequest::EMPTY
                    })
                    .map(|res| {
                        Event::from(res.context("setting auto_focus").map(|inner_res| {
                            match inner_res {
                                Ok(()) => Event::Ok,
                                Err(err) => Event::Err(anyhow!("auto focus error: {:?}", err)),
                            }
                        }))
                    })
                    .into_stream()
                    .boxed(),
                _ => stream::empty().boxed(),
            },
            Event::DismissRequested { window_content_id } => match &mut self.window {
                Some(window) if window.content_id == window_content_id => self.dismiss_window(),
                _ => stream::empty().boxed(),
            },
        }
    }
}

/// An asynchronous event, generally returned by Flatland or another dependency.
///
/// Most methods in WindowManager are feed-forward and synchronous. When work
/// needs to happen in response to something else, a method will return a stream
/// of Events. That Stream should be polled by the main loop, and any events it
/// emits should be passed back into WindowManager::handle_event.
#[derive(Debug)]
pub enum Event {
    /// No-op. Indicates that some background work has completed successfully.
    Ok,

    /// Indicates that some background work encountered an error that we don't
    /// know how to act on.
    Err(anyhow::Error),

    /// Indicates that a child view has attached to the viewport.
    ChildAttached { window_content_id: flatland::ContentId, child_view_ref: ViewRef },

    /// Indicates that the child has requested that the window be dismissed, or
    /// has dropped its end of the ViewController for whatever other reason.
    DismissRequested { window_content_id: flatland::ContentId },
}

impl From<Result<Event, anyhow::Error>> for Event {
    fn from(result: Result<Event, anyhow::Error>) -> Self {
        match result {
            Ok(event) => event,
            Err(err) => Event::Err(err),
        }
    }
}

/// A boxed stream of Events with a static lifetime.
pub type EventStream = stream::BoxStream<'static, Event>;

/// A helper structure for storing state associated with a (full screen) Window.
struct Window {
    _annotation_controller: Option<endpoints::ClientEnd<felement::AnnotationControllerMarker>>,
    content_id: ui_comp::ContentId,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        assert_matches::assert_matches,
        fidl::endpoints::{create_proxy_and_stream, create_request_stream},
        fidl_fuchsia_element as felement, fidl_fuchsia_ui_app as ui_app,
        fidl_fuchsia_ui_composition as ui_comp, fidl_fuchsia_ui_test_scene as ui_test_scene,
        fidl_fuchsia_ui_views as ui_views, fuchsia_async as fasync,
        fuchsia_component_test::{
            Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
        },
        fuchsia_scenic::flatland,
        fuchsia_scenic::flatland::ViewCreationTokenPair,
        futures::future::{AbortHandle, Abortable},
        futures::{StreamExt, TryStreamExt},
        std::collections::HashMap,
    };

    // A stream that emits an empty value for each allowed call to present.
    // Panics on any errors encountered.
    fn present_budget_stream(
        events: flatland::FlatlandEventStream,
    ) -> impl stream::Stream<Item = ()> {
        // You get one token for free.
        stream::iter([()]).chain(events.filter_map(|event| {
            futures::future::ready({
                match event.unwrap() {
                    ui_comp::FlatlandEvent::OnNextFrameBegin { .. } => Some(()),
                    ui_comp::FlatlandEvent::OnFramePresented { .. } => None,
                    ui_comp::FlatlandEvent::OnError { error } => {
                        panic!("flatland error: {:?}", error)
                    }
                }
            })
        }))
    }

    #[fuchsia::test]
    async fn test_wm_create_and_dismiss() -> Result<(), Error> {
        let builder = RealmBuilder::new().await?;

        let test_ui_stack = builder
            .add_child("test-ui-stack", "#meta/test-ui-stack.cm", ChildOptions::new())
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
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.ui.test.scene.Controller"))
                    .capability(Capability::protocol_by_name("fuchsia.ui.composition.Flatland"))
                    .from(&test_ui_stack)
                    .to(Ref::parent()),
            )
            .await?;

        let realm = builder.build().await?;
        let flatland =
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?;
        let mut budget = present_budget_stream(flatland.take_event_stream());

        let (view_provider, mut view_provider_request_stream) =
            create_request_stream::<ui_app::ViewProviderMarker>()
                .expect("failed to create ViewProvider request stream");

        let scene_controller =
            realm.root.connect_to_protocol_at_exposed_dir::<ui_test_scene::ControllerMarker>()?;
        fasync::Task::spawn(async move {
            let _view_ref_koid = scene_controller
                .attach_client_view(ui_test_scene::ControllerAttachClientViewRequest {
                    view_provider: Some(view_provider),
                    ..ui_test_scene::ControllerAttachClientViewRequest::EMPTY
                })
                .await
                .expect("failed to attach root client view");
        })
        .detach();

        // Set up the window manager.
        let view_creation_token = view_provider_request_stream
            .map(|request| {
                if let Ok(ui_app::ViewProviderRequest::CreateView2 { args, .. }) = request {
                    args.view_creation_token.unwrap()
                } else {
                    panic!("Unexpected request: {:?}", request)
                }
            })
            .into_future()
            .await
            .0
            .unwrap();

        let mut server = WindowManager::new(flatland.clone(), view_creation_token).await?;

        budget.next().await;
        flatland.present(flatland::PresentArgs::EMPTY)?;

        // Create a window.
        let ViewCreationTokenPair { mut view_creation_token, viewport_creation_token } =
            ViewCreationTokenPair::new()?;

        let flatland2 =
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?;
        let mut budget2 = present_budget_stream(flatland2.take_event_stream());

        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            endpoints::create_proxy::<flatland::ParentViewportWatcherMarker>().unwrap();

        flatland2
            .create_view2(
                &mut view_creation_token,
                &mut ui_views::ViewIdentityOnCreation::from(ViewRefPair::new().unwrap()),
                flatland::ViewBoundProtocols::EMPTY,
                parent_viewport_watcher_request,
            )
            .unwrap();

        let (view_controller_client, view_controller_server) =
            endpoints::create_proxy::<felement::ViewControllerMarker>().unwrap();
        let mut creation_events =
            server.create_window(viewport_creation_token, None, view_controller_server)?;

        budget.next().await;
        flatland.present(flatland::PresentArgs::EMPTY)?;
        budget2.next().await;
        flatland2.present(flatland::PresentArgs::EMPTY)?;

        // Observe that the child was attached.
        let event = creation_events.next().await.unwrap();
        assert_matches!(
            &event,
            Event::ChildAttached { window_content_id: flatland::ContentId { value: 4 }, .. }
        );

        let attached_events: Vec<_> = server.handle_event(event).collect().await;
        assert_eq!(attached_events.len(), 1);
        assert_matches!(attached_events[0], Event::Ok);

        assert_matches!(
            parent_viewport_watcher.get_status().await,
            Ok(ui_comp::ParentViewportStatus::ConnectedToDisplay)
        );

        // Close the window.
        view_controller_client.dismiss()?;

        budget.next().await;
        flatland.present(flatland::PresentArgs::EMPTY)?;
        budget2.next().await;
        flatland2.present(flatland::PresentArgs::EMPTY)?;

        let event = creation_events.next().await;
        assert_matches!(
            &event,
            Some(Event::DismissRequested { window_content_id: flatland::ContentId { value: 4 } })
        );

        let dismiss_events = server.handle_event(event.unwrap());

        budget.next().await;
        flatland.present(flatland::PresentArgs::EMPTY)?;
        budget2.next().await;
        flatland2.present(flatland::PresentArgs::EMPTY)?;

        let dismiss_events: Vec<_> = dismiss_events.collect().await;
        assert_eq!(dismiss_events.len(), 1);
        assert_matches!(dismiss_events[0], Event::Ok);

        // Check that the parent_viewport_watcher stream closes.
        assert_matches!(parent_viewport_watcher.take_event_stream().into_future().await.0, None);

        realm.destroy().await?;
        Ok(())
    }
}
