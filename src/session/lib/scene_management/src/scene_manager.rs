// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_ui_app as ui_app, fidl_fuchsia_ui_scenic as ui_scenic,
    fidl_fuchsia_ui_views as ui_views, fuchsia_async as fasync, fuchsia_scenic as scenic,
    fuchsia_scenic, fuchsia_syslog as syslog,
    futures::channel::mpsc::{UnboundedReceiver, UnboundedSender},
    futures::future::TryFutureExt,
    futures::prelude::*,
    input_pipeline::input_pipeline::InputPipelineAssembly,
    parking_lot::Mutex,
    std::sync::Weak,
};

/// Presentation messages.
pub enum PresentationMessage {
    /// Request a present call.
    RequestPresent,
    /// Submit a present call.
    Present,
}

/// Unbounded sender used for presentation messages.
pub type PresentationSender = UnboundedSender<PresentationMessage>;

/// Unbounded receiver used for presentation messages.
pub type PresentationReceiver = UnboundedReceiver<PresentationMessage>;

/// A [`SceneManager`] sets up and manages a Scenic scene graph.
///
/// A [`SceneManager`] allows clients to add views to the scene graph.
///
/// Each [`SceneManager`] can choose how to configure the scene, including lighting, setting the
/// frames of added views, etc.
///
/// # Example
///
/// ```
/// let view_provider = some_apips.connect_to_service::<ViewProviderMarker>()?;
///
/// let scenic = connect_to_service::<ScenicMarker>()?;
/// let mut scene_manager = scene_management::FlatSceneManager::new(scenic).await?;
/// let node = scene_manager.add_view_to_scene(view_provider, Some("Demo View".to_string())).await?;
/// node.set_translation(20.0, 30.0, 0.0);
/// ```
#[async_trait]
pub trait SceneManager: Send {
    /// Requests a view from the view provider and adds it to the scene.
    ///
    /// # Parameters
    /// - `view_provider`: The [`ViewProviderProxy`] to fetch the view from.
    /// - `name`: The optional name for the view.
    ///
    /// # Returns
    /// The [`scenic::Node`] for the added view. This can be used to move the view around in the
    /// scene.
    ///
    /// # Errors
    /// Returns an error if a view could not be created or added to the scene.
    async fn add_view_to_scene(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
        name: Option<String>,
    ) -> Result<ui_views::ViewRef, Error>;

    /// Sets the root view for the scene.
    ///
    /// Removes any previous root view, as well as all of its descendants.
    async fn set_root_view(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
    ) -> Result<ui_views::ViewRef, Error>;

    fn request_focus(
        &self,
        view_ref: &mut ui_views::ViewRef,
    ) -> fidl::client::QueryResponseFut<ui_views::FocuserRequestFocusResult>;

    /// Inserts an a11y view into the scene.
    ///
    /// Removes the existing proxy view/viewholder pair, adds the a11y view/viewholder under the
    /// root, and creates a new proxy view. Then, attaches any existing views to the new proxy
    /// view.
    ///
    /// # Parameters
    /// - `a11y_view_holder_token`: The [`ViewHolderToken`] used to create the a11y view holder.
    ///
    /// # Returns
    /// The [`ViewHolderToken`] used to create the new a11y proxy view holder.
    ///
    /// # Errors
    /// Returns an error if the a11y view holder or proxy view could not be created or added to the
    /// scene.
    fn insert_a11y_view(
        &mut self,
        a11y_view_holder_token: ui_views::ViewHolderToken,
    ) -> Result<ui_views::ViewHolderToken, Error>;

    fn insert_a11y_view2(
        &mut self,
        a11y_viewport_creation_token: ui_views::ViewportCreationToken,
    ) -> Result<ui_views::ViewportCreationToken, Error>;

    /// Sets the position of the cursor in the current scene. If no cursor has been created it will
    /// create one using default settings.
    ///
    /// # Parameters
    /// - `position`: A [`Position`] struct representing the cursor position.
    ///
    /// # Notes
    /// If a custom cursor has not been set using `set_cursor_image` or `set_cursor_shape` a default
    /// cursor will be created and added to the scene.  The implementation of the `SceneManager` trait
    /// is responsible for translating the raw input position into "pips".
    fn set_cursor_position(&mut self, position: input_pipeline::Position);

    /// Annotates `assembly` with an additional pipeline stage.
    ///
    /// # Parameters
    /// - `assembly`: An [`InputPipelineAssembly`] which represents a partially-constructed input pipeline.
    async fn add_touch_handler(&self, mut assembly: InputPipelineAssembly)
        -> InputPipelineAssembly;

    /// Annotates `assembly` with an additional pipeline stage.
    ///
    /// # Parameters
    /// - `assembly`: An [`InputPipelineAssembly`] which represents a partially-constructed input pipeline.
    async fn add_mouse_handler(
        &self,
        position_sender: futures::channel::mpsc::Sender<input_pipeline::Position>,
        mut assembly: InputPipelineAssembly,
    ) -> InputPipelineAssembly;
}

/// Listens for presentation requests and schedules presents.
/// Connects to the Scenic event stream to listen for OnFramePresented messages and calls present
/// when Scenic is ready for an update.
pub fn start_presentation_loop(
    sender: PresentationSender,
    mut receiver: PresentationReceiver,
    weak_session: Weak<Mutex<scenic::Session>>,
) {
    fasync::Task::local(async move {
        let mut event_stream = {
            let session = weak_session.upgrade().expect("Failed to acquire session");
            let event_stream = session.lock().take_event_stream();
            event_stream
        };
        let mut present_requested = false;

        while let Some(message) = receiver.next().await {
            match message {
                PresentationMessage::RequestPresent => {
                    // Queue a present if not already queued.
                    if !present_requested {
                        sender
                            .unbounded_send(PresentationMessage::Present)
                            .expect("failed to send Present message");
                        present_requested = true;
                    }
                }
                PresentationMessage::Present => {
                    present_requested = false;
                    if let Some(session) = weak_session.upgrade() {
                        present(&session);

                        // Wait for frame to be presented before we queue another present.
                        while let Some(event) =
                            event_stream.try_next().await.expect("Failed to get next event")
                        {
                            match event {
                                ui_scenic::SessionEvent::OnFramePresented {
                                    frame_presented_info: _,
                                } => break,
                            }
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    })
    .detach();
}

/// Inform Scenic that is should render any pending changes
fn present(session: &scenic::SessionPtr) {
    fasync::Task::local(
        session
            .lock()
            // Passing 0 for requested_presentation_time tells scenic that that it should process
            // enqueued operation as soon as possible.
            // Passing 0 for requested_prediction_span guarantees that scenic will provide at least
            // one future time.
            .present2(0, 0)
            .map_ok(|_| ())
            .unwrap_or_else(|error| syslog::fx_log_err!("Present error: {:?}", error)),
    )
    .detach();
}
