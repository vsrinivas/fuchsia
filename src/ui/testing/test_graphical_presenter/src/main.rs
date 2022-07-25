// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::{Proxy, RequestStream},
    fidl_fuchsia_element::{
        GraphicalPresenterRequest, GraphicalPresenterRequestStream, PresentViewError,
        ViewControllerRequestStream,
    },
    fidl_fuchsia_ui_app::{ViewProviderRequest, ViewProviderRequestStream},
    fidl_fuchsia_ui_composition::{
        ChildViewWatcherMarker, ContentId, FlatlandEvent, FlatlandEventStream, FlatlandMarker,
        FlatlandProxy, LayoutInfo, ParentViewportWatcherMarker, ParentViewportWatcherProxy,
        PresentArgs, TransformId, ViewBoundProtocols, ViewportProperties,
    },
    fidl_fuchsia_ui_views::ViewportCreationToken,
    fuchsia_async as fasync,
    fuchsia_component::{client, server},
    fuchsia_zircon as zx,
    futures::{
        stream::{FusedStream, SelectAll},
        {select, FutureExt, StreamExt},
    },
    std::pin::Pin,
};

const NULL_CONTENT_ID: ContentId = ContentId { value: 0 };
const ROOT_TRANSFORM_ID: TransformId = TransformId { value: 1 };
const VIEWPORT_CONTENT_ID: ContentId = ContentId { value: 2 };

struct ChildView(fasync::Task<()>);

struct TestGraphicalPresenter {
    flatland: FlatlandProxy,
    flatland_events: FlatlandEventStream,
    service_fs: server::ServiceFs<server::ServiceObj<'static, IncomingService>>,
    view_provider: Pin<Box<dyn FusedStream<Item = Result<ViewProviderRequest, fidl::Error>>>>,
    graphical_presenter_streams: SelectAll<GraphicalPresenterRequestStream>,
    parent_viewport_watcher: HangingGetStream<ParentViewportWatcherProxy, LayoutInfo>,
    parent_viewport_request: Option<fidl::endpoints::ServerEnd<ParentViewportWatcherMarker>>,
    root_layout_info: LayoutInfo,
    child_view: Option<ChildView>,
    present_credits: u32,
    need_present: bool,
}

impl TestGraphicalPresenter {
    pub fn new() -> Result<Self, Error> {
        let mut fs = server::ServiceFs::new();
        fs.dir("svc")
            .add_fidl_service(IncomingService::GraphicalPresenter)
            .add_fidl_service(IncomingService::ViewProvider);
        fs.take_and_serve_directory_handle().context("Error starting server")?;
        let flatland = client::connect_to_protocol::<FlatlandMarker>()
            .context("error connecting to Flatland")?;
        let (parent_viewport_watcher, parent_viewport_request) =
            fidl::endpoints::create_proxy::<ParentViewportWatcherMarker>()
                .context("error creating viewport watcher")?;
        let flatland_events = flatland.take_event_stream();

        Ok(Self {
            flatland,
            flatland_events,
            service_fs: fs,
            view_provider: Box::pin(futures::stream::pending().fuse()),
            graphical_presenter_streams: SelectAll::new(),
            parent_viewport_watcher: HangingGetStream::new_with_fn_ptr(
                parent_viewport_watcher,
                ParentViewportWatcherProxy::get_layout,
            ),
            parent_viewport_request: Some(parent_viewport_request),
            root_layout_info: LayoutInfo::EMPTY,
            child_view: None,
            present_credits: 1,
            need_present: false,
        })
    }

    async fn create_view(&mut self) -> Result<(), Error> {
        loop {
            select! {
                service = self.service_fs.next().fuse() => self.handle_incoming_service_connection(service).await,
                request = self.view_provider.next() => match request {
                    Some(Ok(ViewProviderRequest::CreateView2 { args, .. })) => {
                        if let Some(mut token) = args.view_creation_token {
                            if let Some(viewport_watcher_request) = self.parent_viewport_request.take() {
                                let viewref_pair = fuchsia_scenic::ViewRefPair::new()?;
                                let mut view_identity =
                                                    fidl_fuchsia_ui_views::ViewIdentityOnCreation::from(viewref_pair);
                                self.flatland.create_view2(
                                    &mut token,
                                    &mut view_identity,
                                    ViewBoundProtocols::EMPTY,
                                    viewport_watcher_request)?;

                                // Get our layout size
                                self.root_layout_info = match self.parent_viewport_watcher.next().await {
                                    Some(Ok(layout_info)) => layout_info,
                                    _ => {
                                        return Err(anyhow!("Failed to get initial layout"));
                                    }
                                };

                                // Create the root transform.
                                self.flatland.create_transform(&mut ROOT_TRANSFORM_ID.clone())
                                    .context("Failed to create root transform")?;
                                self.flatland.set_root_transform(&mut ROOT_TRANSFORM_ID.clone())
                                    .context("Failed to set root transform")?;
                            }
                        }
                        break;
                    }
                    Some(Ok(other)) => {
                        tracing::error!("Got unexpected ViewProvider request {:?}", other);
                    }
                    Some(Err(e)) => {
                        tracing::error!("ViewProvider FIDL error {:?}", e);
                    }
                    None => {
                        tracing::error!("ViewProvider End of Stream");
                    }
                }
            }
        }
        Ok(())
    }

    pub async fn run(&mut self) -> Result<(), Error> {
        // Create the root view before we process any graphical presenter requests.
        self.create_view().await?;

        // Now we poll for incoming GraphcicalPresenter connections and requests, as well as the
        // flatland event stream.
        loop {
            select! {
                service = self.service_fs.next().fuse() => self.handle_incoming_service_connection(service).await,
                graphical_presenter_request = self.graphical_presenter_streams.next() => {
                    if let Err(e) = self.handle_graphical_presenter_request(graphical_presenter_request).await {
                        tracing::error!("Error handling GraphicalPresenter request: {}", e);
                    }
                }
                // Read the event stream to manage our present credit budget.
                event = self.flatland_events.next().fuse() => match event {
                    Some(Ok(FlatlandEvent::OnNextFrameBegin { values })) => {
                        if let Some(credits) = values.additional_present_credits {
                            self.present_credits += credits;
                            if self.need_present {
                                self.try_present()?;
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
    }

    async fn handle_graphical_presenter_request(
        &mut self,
        request: Option<Result<GraphicalPresenterRequest, fidl::Error>>,
    ) -> Result<(), Error> {
        match request {
            Some(Ok(GraphicalPresenterRequest::PresentView {
                view_spec,
                view_controller_request,
                responder,
                ..
            })) => {
                let viewport_creation_token = match view_spec.viewport_creation_token {
                    Some(token) => token,
                    None => {
                        responder.send(&mut Err(PresentViewError::InvalidArgs))?;
                        return Err(anyhow!("No viewport creation token was provided"));
                    }
                };
                // The request is OK, so complete the request.
                responder.send(&mut Ok(()))?;

                let view_controller_request_stream =
                    view_controller_request.map(|s| s.into_stream().ok()).flatten();

                // If we have a view already, first detach and destroy the viewport.
                if let Some(_) = self.child_view.take() {
                    self.flatland
                        .set_content(&mut ROOT_TRANSFORM_ID.clone(), &mut NULL_CONTENT_ID.clone())
                        .context("Failed to remove root content")?;
                    // The returns the viewport creation token, but that response will not come
                    // until we present so we can't await here.
                    //
                    // Since we don't need the token we just ignore the future.
                    let _ = self.flatland.release_viewport(&mut VIEWPORT_CONTENT_ID.clone());
                }

                // Attach the new view.
                self.child_view = Some(
                    self.present_view(viewport_creation_token, view_controller_request_stream)?,
                );
            }
            Some(Err(e)) => {
                tracing::error!("GraphicalPresenter FIDL error {:?}", e);
            }
            None => {
                tracing::error!("GraphicalPresenter End of Stream");
            }
        }
        Ok(())
    }

    async fn handle_incoming_service_connection(&mut self, service: Option<IncomingService>) {
        match service {
            Some(IncomingService::GraphicalPresenter(stream)) => {
                self.graphical_presenter_streams.push(stream);
            }
            Some(IncomingService::ViewProvider(stream)) => {
                self.view_provider = Box::pin(stream);
            }
            None => {}
        }
    }

    fn try_present(&mut self) -> Result<(), Error> {
        if self.present_credits > 0 {
            self.present_credits -= 1;
            self.need_present = false;
            self.flatland.present(PresentArgs::EMPTY).context("Failed to present")?;
        } else {
            self.need_present = true;
        }
        Ok(())
    }

    fn present_view(
        &mut self,
        mut viewport_creation_token: ViewportCreationToken,
        view_controller_request_stream: Option<ViewControllerRequestStream>,
    ) -> Result<ChildView, Error> {
        // The child view will take up the entire size of our root view.
        let viewport_properties = ViewportProperties {
            logical_size: self.root_layout_info.logical_size,
            ..ViewportProperties::EMPTY
        };

        // Now set the content and present the new viewport.
        let (child_view_watcher, child_view_watcher_request) =
            fidl::endpoints::create_proxy::<ChildViewWatcherMarker>()
                .context("error creating viewport watcher")?;
        self.flatland
            .create_viewport(
                &mut VIEWPORT_CONTENT_ID.clone(),
                &mut viewport_creation_token,
                viewport_properties,
                child_view_watcher_request,
            )
            .context("Failed to create viewport")?;
        self.flatland
            .set_content(&mut ROOT_TRANSFORM_ID.clone(), &mut VIEWPORT_CONTENT_ID.clone())
            .context("Failed to set content viewport")?;
        self.try_present()?;

        // Notify the client that the view has been presented.
        if let Some(stream) = view_controller_request_stream.as_ref() {
            stream.control_handle().send_on_presented()?;
        }

        Ok(ChildView(fasync::Task::local(async move {
            // TODO: Poll the request stream and handle Dismiss. For now we just hold onto the
            // channels so they don't close.
            let _view_controller_request_stream = view_controller_request_stream;
            fasync::OnSignals::new(
                child_view_watcher.as_channel(),
                zx::Signals::CHANNEL_PEER_CLOSED,
            )
            .await
            .unwrap();
        })))
    }
}

enum IncomingService {
    GraphicalPresenter(GraphicalPresenterRequestStream),
    ViewProvider(ViewProviderRequestStream),
}

#[fuchsia::main(logging = true, threads = 1)]
async fn main() -> Result<(), Error> {
    if let Err(e) = TestGraphicalPresenter::new()?.run().await {
        tracing::error!("Failure running test_graphical_presenter {}", e);
    }
    Ok(())
}
