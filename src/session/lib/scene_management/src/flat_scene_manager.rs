// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display_metrics::{DisplayMetrics, ViewingDistance},
    crate::graphics_utils::{ScreenCoordinates, ScreenSize},
    crate::scene_manager::{self, PresentationMessage, PresentationSender, SceneManager},
    anyhow::Error,
    async_trait::async_trait,
    fidl, fidl_fuchsia_ui_app as ui_app, fidl_fuchsia_ui_gfx as ui_gfx,
    fidl_fuchsia_ui_scenic as ui_scenic, fidl_fuchsia_ui_views as ui_views,
    fuchsia_scenic as scenic, fuchsia_scenic,
    futures::channel::mpsc::unbounded,
    input_pipeline::Size,
    std::sync::Arc,
};

pub type FocuserPtr = Arc<ui_views::FocuserProxy>;

/// The [`FlatSceneManager`] constructs an empty scene with a single white ambient light.
///
/// Each added view is positioned at (x, y, z) = 0, and sized to match the size of the display.
/// The display dimensions are computed at the time the [`FlatSceneManager`] is constructed.
pub struct FlatSceneManager {
    /// The Scenic session associated with this [`FlatSceneManager`].
    pub session: scenic::SessionPtr,

    /// The view focuser associated with the [`session`].
    pub focuser: FocuserPtr,

    /// The id of the compositor used for the scene's layer stack.
    pub compositor_id: u32,

    /// The size of the display, as determined when [`FlatSceneManager::new()`] was called.
    pub display_size: ScreenSize,

    /// The root node of the scene. Views are added as children of this node.
    pub root_node: scenic::EntityNode,

    /// The metrics for the display presenting the scene.
    pub display_metrics: DisplayMetrics,

    /// The view holder [`scenic::EntityNodes`], which have been added to the Scene.
    views: Vec<scenic::EntityNode>,

    /// The proxy View/ViewHolder pair exists so that the a11y manager can insert its view into the
    /// scene after SetRootView() has already been called.
    a11y_proxy_view_holder: scenic::ViewHolder,

    /// See comment for a11y_proxy_view_holder.
    a11y_proxy_view: scenic::View,

    /// Proxy session. The proxy view must exist in a separate session from the root view since
    /// its parent is in a different session.
    a11y_proxy_session: scenic::SessionPtr,

    /// The node for the cursor. It is optional in case a scene doesn't render a cursor.
    cursor_node: Option<scenic::EntityNode>,

    /// The shapenode for the cursor. It is optional in case a scene doesn't render a cursor.
    cursor_shape: Option<scenic::ShapeNode>,

    /// Presentation sender used to request presents for the root session.
    presentation_sender: PresentationSender,

    /// Presentation sender used to request presents for the a11y proxy session.
    a11y_proxy_presentation_sender: PresentationSender,

    /// The resources used to construct the scene. If these are dropped, they will be removed
    /// from Scenic, so they must be kept alive for the lifetime of `FlatSceneManager`.
    _resources: ScenicResources,
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
    _scene: scenic::Scene,
}

#[async_trait]
impl SceneManager for FlatSceneManager {
    async fn new(
        scenic: ui_scenic::ScenicProxy,
        display_pixel_density: Option<f32>,
        viewing_distance: Option<ViewingDistance>,
    ) -> Result<Self, Error> {
        let (session, focuser) = FlatSceneManager::create_session(&scenic)?;
        let (a11y_proxy_session, _a11y_proxy_focuser) = FlatSceneManager::create_session(&scenic)?;

        let ambient_light = FlatSceneManager::create_ambient_light(&session);
        let scene = FlatSceneManager::create_ambiently_lit_scene(&session, &ambient_light);

        let camera = scenic::Camera::new(session.clone(), &scene);
        let renderer = FlatSceneManager::create_renderer(&session, &camera);

        // Size the layer to fit the size of the display.
        let display_info = scenic.get_display_info().await?;

        let size_in_pixels = Size {
            width: display_info.width_in_px as f32,
            height: display_info.height_in_px as f32,
        };

        let display_metrics =
            DisplayMetrics::new(size_in_pixels, display_pixel_density, viewing_distance, None);

        scene.set_scale(display_metrics.pixels_per_pip(), display_metrics.pixels_per_pip(), 1.0);

        let layer = FlatSceneManager::create_layer(&session, &renderer, size_in_pixels);
        let layer_stack = FlatSceneManager::create_layer_stack(&session, &layer);
        let compositor = FlatSceneManager::create_compositor(&session, &layer_stack);

        // Add the root node to the scene immediately.
        let root_node = scenic::EntityNode::new(session.clone());
        scene.add_child(&root_node);

        // Create proxy view/viewholder and add to the scene.
        let proxy_token_pair = scenic::ViewTokenPair::new()?;
        let a11y_proxy_viewref_pair = scenic::ViewRefPair::new()?;
        let a11y_proxy_view_holder = FlatSceneManager::create_view_holder(
            &session,
            proxy_token_pair.view_holder_token,
            display_metrics,
            Some(String::from("a11y proxy view holder")),
        );
        let a11y_proxy_view = scenic::View::new3(
            a11y_proxy_session.clone(),
            proxy_token_pair.view_token,
            a11y_proxy_viewref_pair.control_ref,
            a11y_proxy_viewref_pair.view_ref,
            Some(String::from("a11y proxy view")),
        );
        root_node.add_child(&a11y_proxy_view_holder);

        let compositor_id = compositor.id();

        let resources = ScenicResources {
            _ambient_light: ambient_light,
            _camera: camera,
            _compositor: compositor,
            _layer: layer,
            _layer_stack: layer_stack,
            _renderer: renderer,
            _scene: scene,
        };

        let (sender, receiver) = unbounded();
        scene_manager::start_presentation_loop(sender.clone(), receiver, Arc::downgrade(&session));
        FlatSceneManager::request_present(&sender);

        let (a11y_proxy_sender, a11y_proxy_receiver) = unbounded();
        scene_manager::start_presentation_loop(
            a11y_proxy_sender.clone(),
            a11y_proxy_receiver,
            Arc::downgrade(&a11y_proxy_session),
        );
        FlatSceneManager::request_present(&a11y_proxy_sender);

        Ok(FlatSceneManager {
            session,
            focuser,
            root_node,
            display_size: ScreenSize::from_size(&size_in_pixels, display_metrics),
            compositor_id,
            _resources: resources,
            views: vec![],
            a11y_proxy_view_holder,
            a11y_proxy_view,
            a11y_proxy_session,
            display_metrics,
            cursor_node: None,
            cursor_shape: None,
            presentation_sender: sender,
            a11y_proxy_presentation_sender: a11y_proxy_sender,
        })
    }

    async fn add_view_to_scene(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
        name: Option<String>,
    ) -> Result<ui_views::ViewRef, Error> {
        let token_pair = scenic::ViewTokenPair::new()?;
        let mut viewref_pair = scenic::ViewRefPair::new()?;
        let viewref_dup = fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;
        view_provider.create_view_with_view_ref(
            token_pair.view_token.value,
            &mut viewref_pair.control_ref,
            &mut viewref_pair.view_ref,
        )?;
        self.add_view(token_pair.view_holder_token, name);
        FlatSceneManager::request_present(&self.a11y_proxy_presentation_sender);

        Ok(viewref_dup)
    }

    async fn set_root_view(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
    ) -> Result<ui_views::ViewRef, Error> {
        let token_pair = scenic::ViewTokenPair::new()?;
        let mut viewref_pair = scenic::ViewRefPair::new()?;
        let viewref_dup = fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;
        view_provider.create_view_with_view_ref(
            token_pair.view_token.value,
            &mut viewref_pair.control_ref,
            &mut viewref_pair.view_ref,
        )?;
        self.add_view(token_pair.view_holder_token, Some("root".to_string()));
        FlatSceneManager::request_present(&self.a11y_proxy_presentation_sender);

        Ok(viewref_dup)
    }

    /// Creates an a11y view holder and attaches it to the scene. This method also deletes the
    /// existing proxy view/viewholder pair, and creates a new proxy view. It then returns the
    /// new proxy view holder token. The a11y manager is responsible for using this token to
    /// create the new proxy view holder.
    ///
    /// # Parameters
    /// - `a11y_view_ref`: The view ref of the a11y view.
    /// - `a11y_view_holder_token`: The token used to create the a11y view holder.
    fn insert_a11y_view(
        &mut self,
        a11y_view_holder_token: ui_views::ViewHolderToken,
    ) -> Result<ui_views::ViewHolderToken, Error> {
        // Create the new a11y view holder, and attach it as a child of the root node.
        let a11y_view_holder = FlatSceneManager::create_view_holder(
            &self.session,
            a11y_view_holder_token,
            self.display_metrics,
            Some(String::from("a11y view holder")),
        );
        self.root_node.add_child(&a11y_view_holder);

        // Disconnect the old proxy view/viewholder from the scene graph.
        self.a11y_proxy_view_holder.detach();
        for view_holder_node in self.views.iter_mut() {
            self.a11y_proxy_view.detach_child(&*view_holder_node);
        }

        // Generate a new proxy view/viewholder token pair, and create a new proxy view.
        let proxy_token_pair = scenic::ViewTokenPair::new()?;
        let a11y_proxy_viewref_pair = scenic::ViewRefPair::new()?;
        self.a11y_proxy_view = scenic::View::new3(
            self.a11y_proxy_session.clone(),
            proxy_token_pair.view_token,
            a11y_proxy_viewref_pair.control_ref,
            a11y_proxy_viewref_pair.view_ref,
            Some(String::from("a11y proxy view")),
        );

        // Reconnect existing view holders to the new a11y proxy view.
        for view_holder_node in self.views.iter_mut() {
            self.a11y_proxy_view.add_child(&*view_holder_node);
        }

        FlatSceneManager::request_present(&self.presentation_sender);
        FlatSceneManager::request_present(&self.a11y_proxy_presentation_sender);

        Ok(proxy_token_pair.view_holder_token)
    }

    fn session(&self) -> scenic::SessionPtr {
        return self.session.clone();
    }

    fn display_metrics(&self) -> DisplayMetrics {
        self.display_metrics
    }

    fn set_cursor_location(&mut self, location: ScreenCoordinates) {
        if self.cursor_node.is_none() {
            // We don't already have a cursor node so let's make one with the default cursor
            self.set_cursor_shape(self.get_default_cursor());
        }

        let (x, y) = location.pips();
        self.cursor_node().set_translation(x, y, FlatSceneManager::CURSOR_DEPTH);
        FlatSceneManager::request_present(&self.presentation_sender);
    }

    fn set_cursor_shape(&mut self, shape: scenic::ShapeNode) {
        if !self.cursor_shape.is_none() {
            let current_shape = self.cursor_shape.as_ref().unwrap();
            let node = self.cursor_node.as_ref().unwrap();
            node.remove_child(current_shape);
        }

        self.cursor_node().add_child(&shape);
        self.cursor_shape = Some(shape);
        FlatSceneManager::request_present(&self.presentation_sender);
    }
}

impl FlatSceneManager {
    /// The depth of the bounds of any added views. This can be used to compute where a view
    /// should be placed to render "in front of" another view.
    const VIEW_BOUNDS_DEPTH: f32 = -800.0;
    /// The depth at which to draw the cursor in order to ensure it's on top of everything else
    const CURSOR_DEPTH: f32 = FlatSceneManager::VIEW_BOUNDS_DEPTH - 1.0;

    /// Creates a new Scenic session.
    ///
    /// # Parameters
    /// - `scenic`: The [`ScenicProxy`] which is used to create the session.
    ///
    /// # Errors
    /// If the [`scenic::SessionPtr`] could not be created.
    fn create_session(
        scenic: &ui_scenic::ScenicProxy,
    ) -> Result<(scenic::SessionPtr, FocuserPtr), Error> {
        let (session_proxy, session_request_stream) = fidl::endpoints::create_proxy()?;
        let (focuser_proxy, focuser_request_stream) = fidl::endpoints::create_proxy()?;
        scenic.create_session2(session_request_stream, None, Some(focuser_request_stream))?;

        Ok((scenic::Session::new(session_proxy), Arc::new(focuser_proxy)))
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
    /// - `display_size`: The size of the display in pixels.
    fn create_layer(
        session: &scenic::SessionPtr,
        renderer: &scenic::Renderer,
        display_size: Size,
    ) -> scenic::Layer {
        let layer = scenic::Layer::new(session.clone());
        layer.set_size(display_size.width, display_size.height);
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

    /// Creates a view holder in the supplied session using the provided token and display metrics.
    ///
    /// # Parameters
    /// - `session`: The scenic session in which to create the view holder.
    /// - `view_holder_token`: The view holder token used to create the view holder.
    /// - `display_metrics`: The metrics for the display presenting the scene.
    /// - `name`: The debug name of the view holder.
    fn create_view_holder(
        session: &scenic::SessionPtr,
        view_holder_token: ui_views::ViewHolderToken,
        display_metrics: DisplayMetrics,
        name: Option<String>,
    ) -> scenic::ViewHolder {
        let view_holder = scenic::ViewHolder::new(session.clone(), view_holder_token, name);

        let view_properties = ui_gfx::ViewProperties {
            bounding_box: ui_gfx::BoundingBox {
                min: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: FlatSceneManager::VIEW_BOUNDS_DEPTH },
                max: ui_gfx::Vec3 {
                    x: display_metrics.width_in_pips(),
                    y: display_metrics.height_in_pips(),
                    z: 0.0,
                },
            },
            downward_input: true,
            focus_change: true,
            inset_from_min: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            inset_from_max: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
        };
        view_holder.set_view_properties(view_properties);

        view_holder
    }

    /// Creates a view holder, stores it in [`self.views`], then wraps it in a view holder node.
    ///
    /// # Parameters
    /// - `view_holder_token`: The view holder token used to create the view holder.
    /// - `name`: The debugging name for the created view.
    fn add_view(&mut self, view_holder_token: ui_views::ViewHolderToken, name: Option<String>) {
        let view_holder = FlatSceneManager::create_view_holder(
            &self.a11y_proxy_session,
            view_holder_token,
            self.display_metrics,
            name,
        );

        let view_holder_node = scenic::EntityNode::new(self.a11y_proxy_session.clone());
        view_holder_node.attach(&view_holder);
        view_holder_node.set_translation(0.0, 0.0, 0.0);

        self.a11y_proxy_view.add_child(&view_holder_node);
        self.views.push(view_holder_node);
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

    /// Requests that all previously enqueued operations are presented.
    fn request_present(presentation_sender: &PresentationSender) {
        presentation_sender
            .unbounded_send(PresentationMessage::RequestPresent)
            .expect("failed to send RequestPresent message");
    }
}
