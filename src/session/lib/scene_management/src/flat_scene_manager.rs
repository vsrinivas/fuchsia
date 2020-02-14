// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display_metrics::{DisplayMetrics, ViewingDistance},
    crate::graphics_utils,
    crate::scene_manager::{self, SceneManager},
    anyhow::Error,
    async_trait::async_trait,
    fidl, fidl_fuchsia_ui_app as ui_app, fidl_fuchsia_ui_gfx as ui_gfx,
    fidl_fuchsia_ui_gfx::Vec3,
    fidl_fuchsia_ui_scenic as ui_scenic, fidl_fuchsia_ui_views as ui_views,
    fuchsia_scenic as scenic,
    fuchsia_scenic::DisplayRotation,
    std::collections::HashMap,
    std::f32::consts as f32const,
    std::sync::Arc,
};

/// The [`FlatSceneManager`] constructs an empty scene with a single white ambient light.
///
/// Each added view is positioned at (x, y, z) = 0, and sized to match the size of the display.
/// The display dimensions are computed at the time the [`FlatSceneManager`] is constructed.
pub struct FlatSceneManager {
    /// The Scenic session associated with this [`FlatSceneManager`].
    pub session: scenic::SessionPtr,

    /// The id of the compositor used for the scene's layer stack.
    pub compositor_id: u32,

    /// The width of the display, as determined when [`FlatSceneManager::new()`] was called.
    pub display_width: f32,

    /// The height of the display, as determined when [`FlatSceneManager::new()`] was called.
    pub display_height: f32,

    /// The root node of the scene. Views are added as children of this node.
    pub root_node: scenic::EntityNode,

    /// The metrics for the display presenting the scene.
    pub display_metrics: DisplayMetrics,

    /// The [`scenic::ViewHolder`], and their associated [`scenic::EntityNodes`], which have been
    /// added to the Scene.
    views: HashMap<u32, scenic::ViewHolder>,

    /// The node for the cursor. It is optional in case a scene doesn't render a cursor.
    cursor_node: Option<scenic::EntityNode>,

    /// The shapenode for the cursor. It is optional in case a scene doesn't render a cursor.
    cursor_shape: Option<scenic::ShapeNode>,

    /// The resources used to construct the scene. If these are dropped, they will be removed
    /// from Scenic, so they must be kept alive for the lifetime of `FlatSceneManager`.
    resources: ScenicResources,
}

/// A struct containing all the Scenic resources which are unused but still need to be kept alive.
/// If the resources are dropped, they are automatically removed from the Scenic session.
struct ScenicResources {
    _ambient_light: scenic::AmbientLight,
    _camera: scenic::Camera,
    _compositor: scenic::DisplayCompositor,
    _layer: scenic::Layer,
    _layer_stack: scenic::LayerStack,
    _renderer: scenic::Renderer,
    scene: scenic::Scene,
}

#[async_trait]
impl SceneManager for FlatSceneManager {
    async fn new(
        scenic_proxy: ui_scenic::ScenicProxy,
        display_pixel_density: Option<f32>,
        viewing_distance: Option<ViewingDistance>,
    ) -> Result<Self, Error> {
        Self::new_with_display_adjustments(
            scenic_proxy,
            display_pixel_density,
            viewing_distance,
            None,
        )
        .await
    }

    async fn new_with_display_adjustments(
        scenic_proxy: ui_scenic::ScenicProxy,
        display_pixel_density: Option<f32>,
        viewing_distance: Option<ViewingDistance>,
        display_rotation: Option<DisplayRotation>,
    ) -> Result<Self, Error> {
        // Size the layer to fit the size of the display.
        let display_info = scenic_proxy.get_display_info().await?;

        let (display_width, display_height) = (display_info.width_in_px, display_info.height_in_px);

        let display_metrics = DisplayMetrics::new(
            display_width,
            display_height,
            display_pixel_density,
            viewing_distance,
            display_rotation,
        );

        let session: scenic::SessionPtr = Self::create_session(&scenic_proxy)?;

        let ambient_light = Self::create_ambient_light(&session);
        let scene = Self::create_ambiently_lit_scene(&session, &ambient_light);

        let camera = scenic::Camera::new(session.clone(), &scene);
        let renderer = Self::create_renderer(&session, &camera);

        let layer =
            Self::create_layer(&session, &renderer, display_width as f32, display_height as f32);
        let layer_stack = Self::create_layer_stack(&session, &layer);
        let compositor = Self::create_compositor(&session, &layer_stack);
        if display_metrics.rotation() != DisplayRotation::None {
            compositor.set_display_rotation(display_metrics.rotation());
        }

        let root_node = scenic::EntityNode::new(session.clone());
        root_node.set_translation(0.0, 0.0, -0.1); // TODO(fxb/23608)
                                                   // Add the root node to the scene immediately.
        scene.add_child(&root_node);

        let compositor_id = compositor.id();

        let resources = ScenicResources {
            _ambient_light: ambient_light,
            _camera: camera,
            _compositor: compositor,
            _layer: layer,
            _layer_stack: layer_stack,
            _renderer: renderer,
            scene,
        };

        scene_manager::start_presentation_loop(Arc::downgrade(&session));

        Ok(FlatSceneManager {
            session,
            root_node,
            display_width: display_width as f32,
            display_height: display_height as f32,
            compositor_id,
            resources,
            views: HashMap::new(),
            display_metrics,
            cursor_node: None,
            cursor_shape: None,
        })
    }

    async fn remove_view_from_scene(&mut self, node: scenic::EntityNode) -> Result<(), Error> {
        let _view_holder = self.views.remove(&node.id());
        self.root_node.remove_child(&node);
        Ok(())
    }

    async fn add_view_to_scene(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
        name: Option<String>,
    ) -> Result<scenic::EntityNode, Error> {
        let token_pair = scenic::ViewTokenPair::new()?;
        view_provider.create_view(token_pair.view_token.value, None, None)?;

        let view_holder_node = self.create_view_holder_node(token_pair.view_holder_token, name);

        Ok(view_holder_node)
    }

    fn get_session(&self) -> scenic::SessionPtr {
        return self.session.clone();
    }

    fn set_cursor_location(&mut self, x: f32, y: f32) {
        if self.cursor_node.is_none() {
            // We don't already have a cursor node so let's make one with the default cursor
            self.set_cursor_shape(self.get_default_cursor());
        }

        self.cursor_node().set_translation(x, y, FlatSceneManager::CURSOR_DEPTH);
    }

    fn set_cursor_shape(&mut self, shape: scenic::ShapeNode) {
        if !self.cursor_shape.is_none() {
            let current_shape = self.cursor_shape.as_ref().unwrap();
            let node = self.cursor_node.as_ref().unwrap();
            node.remove_child(current_shape);
        }

        self.cursor_node().add_child(&shape);
        self.cursor_shape = Some(shape);
    }
}

impl FlatSceneManager {
    /// The depth of the bounds of any added views. This can be used to compute where a view
    /// should be placed to render "in front of" another view.
    const VIEW_BOUNDS_DEPTH: f32 = -800.0;
    /// The depth at which to draw the cursor in order to ensure it's on top of everything else
    const CURSOR_DEPTH: f32 = Self::VIEW_BOUNDS_DEPTH - 1.0;

    /// Creates a new Scenic session.
    ///
    /// # Parameters
    /// - `scenic`: The [`ScenicProxy`] which is used to create the session.
    ///
    /// # Errors
    /// If the [`scenic::SessionPtr`] could not be created.
    fn create_session(scenic_proxy: &ui_scenic::ScenicProxy) -> Result<scenic::SessionPtr, Error> {
        let (session_proxy, session_request_stream) = fidl::endpoints::create_proxy()?;
        scenic_proxy.create_session(session_request_stream, None)?;

        Ok(scenic::Session::new(session_proxy))
    }

    /// Creates a scene with the given ambient light.
    ///
    /// # Parameters
    /// - `session`: The Scenic session to create the scene in.
    /// - `light`: The [`scenic::AmbientLight`] which is added to the created [`scenic::Scene`].
    fn create_ambiently_lit_scene(
        session: &scenic::SessionPtr,
        light: &scenic::AmbientLight,
    ) -> scenic::Scene {
        let scene = scenic::Scene::new(session.clone());
        scene.add_ambient_light(&light);

        scene
    }

    /// Creates a new ambient light for the [`FlatSceneManager`]'s scene.
    ///
    /// # Parameters
    /// - `session`: The Scenic session to create the light in.
    fn create_ambient_light(session: &scenic::SessionPtr) -> scenic::AmbientLight {
        let ambient_light = scenic::AmbientLight::new(session.clone());
        ambient_light.set_color(ui_gfx::ColorRgb { red: 1.0, green: 1.0, blue: 1.0 });

        ambient_light
    }

    /// Creates a renderer in the given session.
    ///
    /// # Parameters
    /// - `session`: The Scenic session to create the renderer in.
    /// - `camera`: The camera to use for the renderer.
    fn create_renderer(session: &scenic::SessionPtr, camera: &scenic::Camera) -> scenic::Renderer {
        let renderer = scenic::Renderer::new(session.clone());
        renderer.set_camera(camera);

        renderer
    }

    /// Creates a new layer.
    ///
    /// # Parameters
    /// - `session`: The Scenic session to create the layer in.
    /// - `renderer`: The renderer for the layer.
    /// - `display_width`: The width of the display.
    /// - `display_height`: The height of the display.
    fn create_layer(
        session: &scenic::SessionPtr,
        renderer: &scenic::Renderer,
        display_width: f32,
        display_height: f32,
    ) -> scenic::Layer {
        let layer = scenic::Layer::new(session.clone());
        layer.set_size(display_width, display_height);
        layer.set_renderer(&renderer);

        layer
    }

    /// Creates a new layer stack with one layer.
    ///
    /// # Parameters
    /// - `session`: The Scenic session to create the layer stack in.
    /// - `layer`: The layer to add to the layer stack.
    fn create_layer_stack(
        session: &scenic::SessionPtr,
        layer: &scenic::Layer,
    ) -> scenic::LayerStack {
        let layer_stack = scenic::LayerStack::new(session.clone());
        layer_stack.add_layer(&layer);

        layer_stack
    }

    /// Creates a new compositor.
    ///
    /// # Parameters
    /// - `session`: The Scenic session to create the compositor in.
    /// - `layer_stack`: The layer stack to composite.
    fn create_compositor(
        session: &scenic::SessionPtr,
        layer_stack: &scenic::LayerStack,
    ) -> scenic::DisplayCompositor {
        let compositor = scenic::DisplayCompositor::new(session.clone());
        compositor.set_layer_stack(&layer_stack);
        compositor
    }

    /// Creates a view holder, stores it in [`self.views`], then wraps it in a view holder node.
    ///
    /// # Parameters
    /// - `view_holder_token`: The view holder token used to create the view holder.
    /// - `name`: The debugging name for the created view.
    fn create_view_holder_node(
        &mut self,
        view_holder_token: ui_views::ViewHolderToken,
        name: Option<String>,
    ) -> scenic::EntityNode {
        let view_holder = scenic::ViewHolder::new(self.session.clone(), view_holder_token, name);
        let view_holder_node = scenic::EntityNode::new(self.session.clone());
        self.root_node.add_child(&view_holder_node);
        view_holder_node.attach(&view_holder);

        self.apply_display_model(&view_holder, &view_holder_node);

        self.views.insert(view_holder_node.id(), view_holder);

        view_holder_node
    }

    fn apply_display_model(
        &mut self,
        view_holder: &scenic::ViewHolder,
        view_holder_node: &scenic::EntityNode,
    ) {
        let mut rotated_width_in_pips = self.display_metrics.width_in_pips();
        let mut rotated_height_in_pips = self.display_metrics.height_in_pips();
        if self.display_metrics.rotation() as u32 % 180 != 0 {
            std::mem::swap(&mut rotated_width_in_pips, &mut rotated_height_in_pips);
        }

        let view_properties = ui_gfx::ViewProperties {
            bounding_box: ui_gfx::BoundingBox {
                min: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: Self::VIEW_BOUNDS_DEPTH },
                max: ui_gfx::Vec3 { x: rotated_width_in_pips, y: rotated_height_in_pips, z: 0.0 },
            },
            downward_input: true,
            focus_change: true,
            inset_from_min: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            inset_from_max: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
        };
        view_holder.set_view_properties(view_properties);

        // NOTE: SceneManager currently supprts square pixels only. If this changes in the future,
        // there would be two different values for scale: horizontal and vertical; and these two
        // values will also need to be swapped, if the screen is rotated 90 or 270 degrees.
        self.resources.scene.set_scale(
            self.display_metrics.pixels_per_pip(),
            self.display_metrics.pixels_per_pip(),
            1.0,
        );

        view_holder_node.set_anchor(rotated_width_in_pips / 2.0, rotated_height_in_pips / 2.0, 0.0);

        if self.display_metrics.rotation() != DisplayRotation::None {
            let angle_radians: f32 = self.display_metrics.rotation_in_degrees() as u32 as f32
                * (f32const::PI / 180.0) as f32;
            let quat = graphics_utils::quaternion_from_axis_angle(
                Vec3 { x: 0.0, y: 0.0, z: 1.0 },
                angle_radians,
            );
            view_holder_node.set_rotation(quat.x, quat.y, quat.z, quat.w);
        }

        let width_in_pixels: f32 = self.display_metrics.width_in_pixels() as f32;
        let height_in_pixels: f32 = self.display_metrics.height_in_pixels() as f32;
        let density = self.display_metrics.pixels_per_pip();

        let mut rotated_width_in_pixels = width_in_pixels;
        let mut rotated_height_in_pixels = height_in_pixels;
        if self.display_metrics.rotation() as u32 % 180 != 0 {
            std::mem::swap(&mut rotated_width_in_pixels, &mut rotated_height_in_pixels);
        }

        // Center the view in the display. This is particularly important if the view is rotated
        // because the axis of rotation is not the center of the screen.
        let left_offset = (width_in_pixels - rotated_width_in_pixels) / density / 2.0;
        let top_offset = (height_in_pixels - rotated_height_in_pixels) / density / 2.0;

        view_holder_node.set_translation(left_offset, top_offset, 0.0);
    }

    /// Gets the `EntityNode` for the cursor or creates one if it doesn't exist yet.
    ///
    /// # Returns
    /// The [`scenic::EntityNode`] that contains the cursor.
    fn cursor_node(&mut self) -> &scenic::EntityNode {
        if self.cursor_node.is_none() {
            self.cursor_node = Some(scenic::EntityNode::new(self.session.clone()));
            self.root_node.add_child(self.cursor_node.as_ref().unwrap());
        }

        self.cursor_node.as_ref().unwrap()
    }
}
