// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait, failure::Error, fidl_fuchsia_ui_app as ui_app,
    fidl_fuchsia_ui_scenic as ui_scenic, fuchsia_scenic as scenic, fuchsia_scenic,
};

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
pub trait SceneManager: Sized {
    /// Creates a new SceneManager.
    ///
    /// # Errors
    /// Returns an error if a Scenic session could not be initialized, or the scene setup fails.
    async fn new(
        scenic: ui_scenic::ScenicProxy,
        display_pixel_density: Option<f32>,
        viewing_distance: Option<f32>,
    ) -> Result<Self, Error>;

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
    ) -> Result<scenic::EntityNode, Error>;

    /// Presents the current scene and all the views which have been added to it.
    ///
    /// The presentation is done by spawning a future. The [`SessionPtr`] uses a
    /// [`parking_lot::Mutex`] to guard the session proxy. This means that
    /// `session.lock().present(...)` returns a future which can not be awaited.
    ///
    /// For example, `session.lock().present(0).await` cannot be done from an async function.
    fn present(&self);
}
