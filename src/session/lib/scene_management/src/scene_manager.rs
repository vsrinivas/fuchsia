// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::graphics_utils::ImageResource, anyhow::Error, async_trait::async_trait,
    fidl_fuchsia_ui_app as ui_app, fidl_fuchsia_ui_gfx as ui_gfx,
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

    /// Requests the scenic session from the scene_manager.
    ///
    /// # Returns
    /// The [`scenic::SessionPtr`] for the scene_manager.
    fn get_session(&self) -> scenic::SessionPtr;

    /// Sets the location of the cursor in the current scene. If no cursor has been created it will
    /// create one using default settings.
    ///
    /// # Parameters
    /// - `x`: The x position of the cursor in pips.
    /// - `y`: The y position of the cursor in pips.
    ///
    /// # Notes
    /// If a custom cursor has not been set using `set_cursor_image` or `set_cursor_shape` a default
    /// cursor will be created and added to the scene.
    fn set_cursor_location(&mut self, x: f32, y: f32);

    /// Sets the image to use for the scene's cursor.
    ///
    /// # Parameters
    /// - `image_path`: The path to the image to be used for the cursor.
    ///
    /// # Notes
    /// Due to a current limitation in the `Scenic` api this should only be called once and must be
    /// called *before* `set_cursor_location`.
    fn set_cursor_image(&mut self, image_path: &str) -> Result<(), Error> {
        let image = ImageResource::new(image_path, self.get_session())?;
        let cursor_rect = scenic::Rectangle::new(self.get_session(), image.width, image.height);
        let cursor_shape = scenic::ShapeNode::new(self.get_session());
        cursor_shape.set_shape(&cursor_rect);
        cursor_shape.set_material(&image.material);

        self.set_cursor_shape(cursor_shape);
        Ok(())
    }

    /// Allows the client to customize the look of the cursor by supplying their own ShapeNode
    ///
    /// # Parameters
    /// - `shape`: The [`scenic::ShapeNode`] to be used as the cursor.
    ///
    /// # Notes
    /// Due to a current limitation in the `Scenic` api this should only be called once and must be
    /// called *before* `set_cursor_location`.
    fn set_cursor_shape(&mut self, shape: scenic::ShapeNode);

    /// Creates a default cursor shape for use with the client hasn't created a custom cursor
    ///
    /// # Returns
    /// The [`scenic::ShapeNode`] to be used as the cursor.
    fn get_default_cursor(&self) -> scenic::ShapeNode {
        const CURSOR_DEFAULT_WIDTH: f32 = 20.0;
        const CURSOR_DEFAULT_HEIGHT: f32 = 20.0;

        let cursor_rect = scenic::RoundedRectangle::new(
            self.get_session(),
            CURSOR_DEFAULT_WIDTH,
            CURSOR_DEFAULT_HEIGHT,
            0.0,
            CURSOR_DEFAULT_WIDTH / 2.0,
            CURSOR_DEFAULT_WIDTH / 2.0,
            CURSOR_DEFAULT_WIDTH / 2.0,
        );
        let cursor_shape = scenic::ShapeNode::new(self.get_session());
        cursor_shape.set_shape(&cursor_rect);

        // Adjust position so that the upper left corner matches the pointer location
        cursor_shape.set_translation(CURSOR_DEFAULT_WIDTH / 2.0, CURSOR_DEFAULT_HEIGHT / 2.0, 0.0);

        let material = scenic::Material::new(self.get_session());
        material.set_color(ui_gfx::ColorRgba { red: 255, green: 0, blue: 255, alpha: 255 });
        cursor_shape.set_material(&material);

        cursor_shape
    }

    /// Presents the current scene and all the views which have been added to it.
    ///
    /// The presentation is done by spawning a future. The [`SessionPtr`] uses a
    /// [`parking_lot::Mutex`] to guard the session proxy. This means that
    /// `session.lock().present(...)` returns a future which can not be awaited.
    ///
    /// For example, `session.lock().present(0).await` cannot be done from an async function.
    fn present(&self);
}
