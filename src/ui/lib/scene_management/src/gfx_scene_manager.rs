// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display_metrics::{DisplayMetrics, ViewingDistance},
    crate::graphics_utils::ScreenSize,
    crate::pointerinjector_config::{
        InjectorViewportHangingGet, InjectorViewportPublisher, InjectorViewportSpec,
        InjectorViewportSubscriber,
    },
    crate::scene_manager::{
        self, PresentationMessage, PresentationSender, SceneManager, ViewportToken,
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl,
    fidl_fuchsia_accessibility::{MagnificationHandlerRequest, MagnificationHandlerRequestStream},
    fidl_fuchsia_ui_app as ui_app, fidl_fuchsia_ui_gfx as ui_gfx,
    fidl_fuchsia_ui_scenic as ui_scenic, fidl_fuchsia_ui_views as ui_views,
    fuchsia_async as fasync, fuchsia_scenic as scenic, fuchsia_scenic,
    futures::channel::mpsc::unbounded,
    futures::channel::oneshot,
    futures::TryStreamExt,
    input_pipeline::Size,
    parking_lot::Mutex,
    std::sync::Arc,
    tracing::{error, warn},
};

pub type FocuserPtr = Arc<ui_views::FocuserProxy>;
pub type ViewRefInstalledPtr = Arc<ui_views::ViewRefInstalledProxy>;

/// The [`GfxSceneManager`] constructs an empty scene with a single white ambient light.
///
/// Each added view is positioned at (x, y, z) = 0, and sized to match the size of the display.
/// The display dimensions are computed at the time the [`GfxSceneManager`] is constructed.
pub struct GfxSceneManager {
    /// The ViewRefInstalled handle used to ensure that the root view is reattached to the scene
    /// after a11y view insertion.
    pub view_ref_installed: ViewRefInstalledPtr,

    /// The Scenic session associated with this [`GfxSceneManager`].
    pub session: scenic::SessionPtr,

    /// Presentation sender used to request presents for the root session.
    presentation_sender: PresentationSender,

    /// The view focuser associated with the [`session`].
    pub focuser: FocuserPtr,

    /// The id of the compositor used for the scene's layer stack.
    pub compositor_id: u32,

    /// The root node of the scene. Views are added as children of this node.
    pub root_node: scenic::EntityNode,

    /// The size of the display, as determined when [`GfxSceneManager::new()`] was called.
    pub display_size: ScreenSize,

    /// The metrics for the display presenting the scene.
    pub display_metrics: DisplayMetrics,

    /// Whether the display metrics dictate flipping dimensions of child viewports.
    pub should_flip_viewport_dimensions: bool,

    /// The camera of the scene.
    camera: scenic::Camera,

    // State of the camera clip space transform.
    clip_scale: f32,
    clip_offset_x: f32,
    clip_offset_y: f32,

    /// Scene topology:
    ///
    /// scene
    ///   |
    /// root_node
    ///   |
    /// global_root_view_holder
    ///   |
    /// global_root_view
    ///   |
    /// pointerinjector_view_holder
    ///   |
    /// pointerinjector_view
    ///   |
    /// a11y_proxy_view_holder*
    ///   |
    /// a11y_proxy_view
    ///   |
    /// client_root_view_holder_node
    ///
    ///
    /// *This represents the state when GfxSceneManager is first created, before
    /// the a11y view is inserted. After `insert_a11y_view` is called,
    /// a11y_proxy_view_holder will be removed, and the new topology will look
    /// like:
    ///  ...
    ///   |
    /// pointerinjector_view
    ///   |
    /// a11y_view_holder
    ///   |
    /// a11y_view (owned & created by a11y manager)
    ///   |
    /// new_a11y_proxy_view_holder (owned & created by a11y manager)
    ///   |
    /// a11y_proxy_view
    ///   |
    /// client_root_view_holder_node

    /// The root view of Scene Manager.
    /// This view is always static. It's used as the 'source' view when injecting events through
    /// fuchsia::ui::pointerinjector.
    _root_view_holder: scenic::ViewHolder,
    _root_view: scenic::View,

    /// The pointerinjector view is used as the 'target' view when injecting events through
    /// fuchsia::ui::pointerinjector.
    /// See also: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/a11y/docs/accessibility_view.md
    /// TODO(fxbug.dev/100980) This is where scale, rotation and translation for all child views
    /// should be set.
    _pointerinjector_view_holder: scenic::ViewHolder,
    pointerinjector_view: scenic::View,
    pointerinjector_session: scenic::SessionPtr,
    pointerinjector_presentation_sender: PresentationSender,

    /// The proxy View/ViewHolder pair exists so that the a11y manager can insert its view into the
    /// scene after SetRootView() has already been called.
    a11y_proxy_view_holder: scenic::ViewHolder,
    a11y_proxy_view: scenic::View,
    a11y_proxy_session: scenic::SessionPtr,
    a11y_proxy_presentation_sender: PresentationSender,

    /// Holds the view holder [`scenic::EntityNode`] (if `set_root_view` has been called).
    client_root_view_holder_node: Option<scenic::EntityNode>,

    /// Supports callers of fuchsia.ui.pointerinjector.configuration.setup.WatchViewport(), allowing
    /// each invocation to subscribe to changes in the viewport region.
    viewport_hanging_get: Arc<Mutex<InjectorViewportHangingGet>>,

    // Used to publish viewport changes to subscribers of |viewport_hanging_get|.
    // TODO(fxbug.dev/87517): use this to publish changes to screen resolution.
    viewport_publisher: Arc<Mutex<InjectorViewportPublisher>>,

    /// These are the ViewRefs returned by get_pointerinjection_view_refs().  They are used to
    /// configure input-pipeline handlers for pointer events.
    context_view_ref: ui_views::ViewRef,
    target_view_ref: ui_views::ViewRef,

    /// The resources used to construct the scene. If these are dropped, they will be removed
    /// from Scenic, so they must be kept alive for the lifetime of `GfxSceneManager`.
    _resources: ScenicResources,
}

/// A struct containing all the Scenic resources which are unused but still need to be kept alive.
/// If the resources are dropped, they are automatically removed from the Scenic session.
struct ScenicResources {
    _ambient_light: scenic::AmbientLight,
    _compositor: scenic::DisplayCompositor,
    _layer: scenic::Layer,
    _layer_stack: scenic::LayerStack,
    _renderer: scenic::Renderer,
    _scene: scenic::Scene,
}

#[async_trait]
impl SceneManager for GfxSceneManager {
    async fn set_root_view(
        &mut self,
        viewport_token: ViewportToken,
        view_ref: Option<ui_views::ViewRef>,
    ) -> Result<(), Error> {
        if self.client_root_view_holder_node.is_some() {
            warn!("GFX set_root_view is replacing the current root view");
        };

        let view_holder_token = match viewport_token {
            ViewportToken::Gfx(view_holder_token) => Ok(view_holder_token),
            ViewportToken::Flatland(_) => {
                Err(anyhow::anyhow!("Flatland token passed to GfxSceneManager.set_root_view"))
            }
        }?;

        match view_ref {
            None => Err(anyhow::anyhow!("GfxSceneManager.set_root_view requires ViewRef")),
            Some(client_root_view_ref) => {
                self.add_view(view_holder_token, Some("root".to_string()));
                GfxSceneManager::request_present(&self.a11y_proxy_presentation_sender);

                self.request_auto_focus(client_root_view_ref).await;

                Ok(())
            }
        }
    }

    async fn set_root_view_deprecated(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
    ) -> Result<ui_views::ViewRef, Error> {
        if self.client_root_view_holder_node.is_some() {
            warn!("GFX set_root_view is replacing the current root view");
        };

        let token_pair = scenic::ViewTokenPair::new()?;
        let mut viewref_pair = scenic::ViewRefPair::new()?;

        // Make two additional copies of the ViewRef.
        // - The original will be used to create the root view.
        // - The first copy will be returned to the caller.
        // - The second copy will be stored, and used to re-focus the root view if insertion
        //   of the a11y view breaks the focus chain.
        let viewref_to_return = fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;
        let view_ref_for_focuser = fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;

        view_provider.create_view_with_view_ref(
            token_pair.view_token.value,
            &mut viewref_pair.control_ref,
            &mut viewref_pair.view_ref,
        )?;

        self.add_view(token_pair.view_holder_token, Some("root".to_string()));
        GfxSceneManager::request_present(&self.a11y_proxy_presentation_sender);

        self.request_auto_focus(view_ref_for_focuser).await;

        Ok(viewref_to_return)
    }

    fn request_focus(
        &self,
        view_ref: &mut ui_views::ViewRef,
    ) -> fidl::client::QueryResponseFut<ui_views::FocuserRequestFocusResult> {
        self.focuser.request_focus(view_ref)
    }

    /// Creates an a11y view holder and attaches it to the scene. This method also deletes the
    /// existing proxy view/viewholder pair, and creates a new proxy view. It then returns the
    /// new proxy view holder token. The a11y manager is responsible for using this token to
    /// create the new proxy view holder.
    ///
    /// # Parameters
    /// - `a11y_view_ref`: The view ref of the a11y view.
    /// - `a11y_view_holder_token`: The token used to create the a11y view holder.
    async fn insert_a11y_view(
        &mut self,
        a11y_view_holder_token: ui_views::ViewHolderToken,
    ) -> Result<ui_views::ViewHolderToken, Error> {
        // Create the new a11y view holder, and attach it as a child of the pointerinjector view.
        let a11y_view_holder = GfxSceneManager::create_view_holder(
            &self.pointerinjector_session,
            a11y_view_holder_token,
            match self.should_flip_viewport_dimensions {
                false => {
                    (self.display_metrics.width_in_pips(), self.display_metrics.height_in_pips())
                }
                true => {
                    (self.display_metrics.height_in_pips(), self.display_metrics.width_in_pips())
                }
            },
            // View should not be focusable, to ensure that focus returns to the
            // root if insertion of the a11y view breaks the scene connectivity.
            false,
            None,
            None,
            None,
            Some(String::from("a11y view holder")),
        );
        self.pointerinjector_view.add_child(&a11y_view_holder);

        // Disconnect the old proxy view/viewholder from the scene graph.
        self.a11y_proxy_view_holder.detach();
        if let Some(ref view_holder_node) = self.client_root_view_holder_node {
            self.a11y_proxy_view.detach_child(&*view_holder_node);
        }

        // Generate a new proxy view/viewholder token pair, and create a new proxy view.
        // Save the proxy ViewRef so that we can observe when the view is attached to the scene.
        let proxy_token_pair = scenic::ViewTokenPair::new()?;
        let a11y_proxy_view_ref_pair = scenic::ViewRefPair::new()?;
        self.a11y_proxy_view = scenic::View::new3(
            self.a11y_proxy_session.clone(),
            proxy_token_pair.view_token,
            a11y_proxy_view_ref_pair.control_ref,
            a11y_proxy_view_ref_pair.view_ref,
            Some(String::from("a11y proxy view")),
        );

        // Reconnect existing view holders to the new a11y proxy view.
        if let Some(ref view_holder_node) = self.client_root_view_holder_node {
            self.a11y_proxy_view.add_child(&*view_holder_node);
        }

        GfxSceneManager::request_present(&self.pointerinjector_presentation_sender);
        GfxSceneManager::request_present(&self.a11y_proxy_presentation_sender);

        Ok(proxy_token_pair.view_holder_token)
    }

    fn insert_a11y_view2(
        &mut self,
        _a11y_viewport_creation_token: ui_views::ViewportCreationToken,
    ) -> Result<ui_views::ViewportCreationToken, Error> {
        Err(anyhow::anyhow!("A11y should be configured to use Gfx, not Flatland"))
    }

    async fn set_camera_clip_space_transform(&mut self, x: f32, y: f32, scale: f32) {
        self.camera.set_camera_clip_space_transform(x, y, scale);
        self.clip_offset_x = x;
        self.clip_offset_y = y;
        self.clip_scale = scale;

        GfxSceneManager::request_present_and_await_next_frame(&self.presentation_sender).await;
        self.update_viewport().await;
    }

    async fn reset_camera_clip_space_transform(&mut self) {
        self.set_camera_clip_space_transform(0.0, 0.0, 1.0).await;
    }

    fn get_pointerinjection_view_refs(&self) -> (ui_views::ViewRef, ui_views::ViewRef) {
        (
            scenic::duplicate_view_ref(&self.context_view_ref).expect("failed to copy ViewRef"),
            scenic::duplicate_view_ref(&self.target_view_ref).expect("failed to copy ViewRef"),
        )
    }

    fn set_cursor_position(&mut self, _position: input_pipeline::Position) {
        error!("Cursor is not supported on GFX");
    }

    fn set_cursor_visibility(&mut self, _visible: bool) {
        error!("Cursor is not supported on GFX");
    }

    fn get_pointerinjection_display_size(&self) -> Size {
        let (width_pixels, height_pixels) = self.display_size.pixels();
        Size { width: width_pixels, height: height_pixels }
    }

    fn get_pointerinjector_viewport_watcher_subscription(&self) -> InjectorViewportSubscriber {
        self.viewport_hanging_get.lock().new_subscriber()
    }

    fn get_display_metrics(&self) -> &DisplayMetrics {
        &self.display_metrics
    }
}

impl GfxSceneManager {
    /// The depth of the bounds of any added views. This can be used to compute where a view
    /// should be placed to render "in front of" another view.
    /// Note: -1000 is hardcoded in a lot of other locations, so don't change
    /// this unless you're sure it's safe. TODO(fxbug.dev/24474).
    const VIEW_BOUNDS_DEPTH: f32 = -1000.0;

    /// Creates a new SceneManager.
    ///
    /// # Parameters
    /// - `scenic`: The [`ScenicProxy`] which is used to create the session.
    /// - `view_ref_installed_proxy`: See `GfxSceneManager::view_ref_installed`.
    /// - `display_rotation`: How much the physical display is rotated, in
    /// degrees counter-clockwise. For example, 90 means that the physical
    /// display has been rotated so that its top edge is now its left edge.
    /// - `display_pixel_density`: The pixel density of the display, in pixels
    /// per mm. If this isn't provided, we'll try to guess one.
    /// - `viewing_distance`: The viewing distance. If this isn't provided,
    /// we'll try to guess one.
    ///
    /// # Errors
    /// Returns an error if a Scenic session could not be initialized, or the scene setup fails.
    pub async fn new(
        scenic: ui_scenic::ScenicProxy,
        view_ref_installed_proxy: ui_views::ViewRefInstalledProxy,
        display_rotation: i32,
        display_pixel_density: Option<f32>,
        viewing_distance: Option<ViewingDistance>,
    ) -> Result<Self, Error> {
        let view_ref_installed = Arc::new(view_ref_installed_proxy);

        let (session, focuser) = GfxSceneManager::create_session(&scenic)?;

        let ambient_light = GfxSceneManager::create_ambient_light(&session);
        let scene = GfxSceneManager::create_ambiently_lit_scene(&session, &ambient_light);

        let camera = scenic::Camera::new(session.clone(), &scene);
        let renderer = GfxSceneManager::create_renderer(&session, &camera);

        // Size the layer to fit the size of the display.
        let display_info = scenic.get_display_info().await?;
        let size_in_pixels = Size {
            width: display_info.width_in_px as f32,
            height: display_info.height_in_px as f32,
        };
        let (display_rotation_enum, pointerinjector_translation, should_flip_viewport_dimensions) =
            match display_rotation % 360 {
                0 => Ok((scenic::DisplayRotation::None, (0.0, 0.0), false)),
                90 => Ok((
                    scenic::DisplayRotation::By90Degrees,
                    (size_in_pixels.width as f32, 0.0),
                    true,
                )),
                180 => Ok((
                    scenic::DisplayRotation::By180Degrees,
                    (size_in_pixels.width as f32, size_in_pixels.height as f32),
                    false,
                )),
                270 => Ok((
                    scenic::DisplayRotation::By270Degrees,
                    (0.0, size_in_pixels.height as f32),
                    true,
                )),
                _ => Err(anyhow::anyhow!("Invalid display rotation; must be {{0,90,180,270}}")),
            }?;
        let display_metrics = DisplayMetrics::new(
            size_in_pixels,
            display_pixel_density,
            viewing_distance,
            Some(display_rotation_enum),
        );
        let layer = GfxSceneManager::create_layer(&session, &renderer, size_in_pixels);
        let layer_stack = GfxSceneManager::create_layer_stack(&session, &layer);
        let compositor =
            GfxSceneManager::create_compositor(&session, &layer_stack, display_metrics.rotation());

        // Add the root node to the scene immediately.
        let root_node = scenic::EntityNode::new(session.clone());

        scene.add_child(&root_node);

        // Create root view/viewholder and add to the scene.
        // Root view dimensions should not account for display rotation.
        let root_view_token_pair = scenic::ViewTokenPair::new()?;
        let root_viewref_pair = scenic::ViewRefPair::new()?;
        let root_view_holder = GfxSceneManager::create_view_holder(
            &session,
            root_view_token_pair.view_holder_token,
            (display_metrics.width_in_pixels() as f32, display_metrics.height_in_pixels() as f32),
            // View should not be focusable, to ensure that focus returns to the
            // root if insertion of the a11y view breaks the scene connectivity.
            false,
            None,
            None,
            None,
            Some(String::from("root view holder")),
        );

        let context_view_ref = scenic::duplicate_view_ref(&root_viewref_pair.view_ref)?;

        let root_view = scenic::View::new3(
            session.clone(),
            root_view_token_pair.view_token,
            root_viewref_pair.control_ref,
            root_viewref_pair.view_ref,
            Some(String::from("root_view view")),
        );

        root_node.add_child(&root_view_holder);

        // Create pointer injector view/viewholder and add to the scene.
        let pointerinjector_token_pair = scenic::ViewTokenPair::new()?;
        let pointerinjector_viewref_pair = scenic::ViewRefPair::new()?;
        let pointerinjector_view_holder = GfxSceneManager::create_view_holder(
            &session,
            pointerinjector_token_pair.view_holder_token,
            match should_flip_viewport_dimensions {
                false => (display_metrics.width_in_pips(), display_metrics.height_in_pips()),
                true => (display_metrics.height_in_pips(), display_metrics.width_in_pips()),
            },
            // View should not be focusable, to ensure that focus returns to the
            // root if insertion of the a11y view breaks the scene connectivity.
            false,
            Some(display_metrics.pixels_per_pip()),
            Some(display_rotation as f32),
            Some(pointerinjector_translation),
            Some(String::from("pointerinjector view holder")),
        );
        let (pointerinjector_session, _pointerinjector_focuser) =
            GfxSceneManager::create_session(&scenic)?;

        let target_view_ref = scenic::duplicate_view_ref(&pointerinjector_viewref_pair.view_ref)?;

        let pointerinjector_view = scenic::View::new3(
            pointerinjector_session.clone(),
            pointerinjector_token_pair.view_token,
            pointerinjector_viewref_pair.control_ref,
            pointerinjector_viewref_pair.view_ref,
            Some(String::from("pointerinjector view")),
        );
        root_view.add_child(&pointerinjector_view_holder);

        // Create a11y proxy view/viewholder and add to the scene.
        let a11y_proxy_token_pair = scenic::ViewTokenPair::new()?;
        let a11y_proxy_viewref_pair = scenic::ViewRefPair::new()?;
        let a11y_proxy_view_holder = GfxSceneManager::create_view_holder(
            &pointerinjector_session,
            a11y_proxy_token_pair.view_holder_token,
            match should_flip_viewport_dimensions {
                false => (display_metrics.width_in_pips(), display_metrics.height_in_pips()),
                true => (display_metrics.height_in_pips(), display_metrics.width_in_pips()),
            },
            // View should not be focusable, to ensure that focus returns to the
            // root if insertion of the a11y view breaks the scene connectivity.
            false,
            None,
            None,
            None,
            Some(String::from("a11y proxy view holder")),
        );
        let (a11y_proxy_session, _a11y_proxy_focuser) = GfxSceneManager::create_session(&scenic)?;

        let a11y_proxy_view = scenic::View::new3(
            a11y_proxy_session.clone(),
            a11y_proxy_token_pair.view_token,
            a11y_proxy_viewref_pair.control_ref,
            a11y_proxy_viewref_pair.view_ref,
            Some(String::from("a11y proxy view")),
        );
        pointerinjector_view.add_child(&a11y_proxy_view_holder);

        let viewport_hanging_get: Arc<Mutex<InjectorViewportHangingGet>> =
            scene_manager::create_viewport_hanging_get(InjectorViewportSpec {
                width: size_in_pixels.width,
                height: size_in_pixels.height,
                scale: 1.,
                x_offset: 0.,
                y_offset: 0.,
            });

        let viewport_publisher = Arc::new(Mutex::new(viewport_hanging_get.lock().new_publisher()));

        let compositor_id = compositor.id();

        let resources = ScenicResources {
            _ambient_light: ambient_light,
            _compositor: compositor,
            _layer: layer,
            _layer_stack: layer_stack,
            _renderer: renderer,
            _scene: scene,
        };

        let (sender, receiver) = unbounded();
        scene_manager::start_presentation_loop(sender.clone(), receiver, Arc::downgrade(&session));
        GfxSceneManager::request_present(&sender);

        let (pointerinjector_sender, pointerinjector_receiver) = unbounded();
        scene_manager::start_presentation_loop(
            pointerinjector_sender.clone(),
            pointerinjector_receiver,
            Arc::downgrade(&pointerinjector_session),
        );
        GfxSceneManager::request_present(&pointerinjector_sender);

        let (a11y_proxy_sender, a11y_proxy_receiver) = unbounded();
        scene_manager::start_presentation_loop(
            a11y_proxy_sender.clone(),
            a11y_proxy_receiver,
            Arc::downgrade(&a11y_proxy_session),
        );
        GfxSceneManager::request_present(&a11y_proxy_sender);

        Ok(GfxSceneManager {
            view_ref_installed,
            session,
            presentation_sender: sender,
            focuser,
            compositor_id,
            root_node,
            display_size: ScreenSize::from_size(&size_in_pixels, display_metrics),
            camera,
            clip_scale: 1.,
            clip_offset_x: 0.,
            clip_offset_y: 0.,
            _root_view_holder: root_view_holder,
            _root_view: root_view,
            _pointerinjector_view_holder: pointerinjector_view_holder,
            pointerinjector_view,
            pointerinjector_session,
            pointerinjector_presentation_sender: pointerinjector_sender,
            a11y_proxy_view_holder,
            a11y_proxy_view,
            a11y_proxy_session,
            a11y_proxy_presentation_sender: a11y_proxy_sender,
            client_root_view_holder_node: None,
            viewport_hanging_get,
            viewport_publisher: viewport_publisher,
            context_view_ref,
            target_view_ref,
            display_metrics,
            should_flip_viewport_dimensions,
            _resources: resources,
        })
    }

    pub fn handle_magnification_handler_request_stream(
        mut request_stream: MagnificationHandlerRequestStream,
        scene_manager: Arc<futures::lock::Mutex<dyn SceneManager>>,
    ) {
        fasync::Task::local(async move {
            loop {
                let request = request_stream.try_next().await;
                match request {
                    Ok(Some(MagnificationHandlerRequest::SetClipSpaceTransform {
                        x,
                        y,
                        scale,
                        responder,
                    })) => {
                        { scene_manager.lock().await.set_camera_clip_space_transform(x, y, scale) }
                            .await;
                        if let Err(e) = responder.send() {
                            warn!("Failed to send MagnificationHandlerRequest() response: {}", e);
                        }
                    }
                    Ok(None) => {
                        return;
                    }
                    Err(e) => {
                        { scene_manager.lock().await.reset_camera_clip_space_transform() }.await;
                        error!("Error obtaining MagnificationHandlerRequest: {}", e);
                        return;
                    }
                }
            }
        })
        .detach()
    }

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

    /// Creates a new ambient light for the [`GfxSceneManager`]'s scene.
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
        display_rotation: scenic::DisplayRotation,
    ) -> scenic::DisplayCompositor {
        let compositor = scenic::DisplayCompositor::new(session.clone());
        compositor.set_layer_stack(&layer_stack);
        compositor.set_display_rotation(display_rotation);

        compositor
    }

    /// Creates a view holder in the supplied session using the provided token and display metrics.
    ///
    /// # Parameters
    /// - `session`: The scenic session in which to create the view holder.
    /// - `dimensions`: The dimensions of the view's bounding box.
    /// - `is_focusable`: Indicates whether the child view should be able to receive focus.
    /// - `view_holder_token`: The view holder token used to create the view holder.
    /// - `scale`, `rotation`, `translation`: Used to adjust the view holder's transform.
    /// - `name`: The debug name of the view holder.
    fn create_view_holder(
        session: &scenic::SessionPtr,
        view_holder_token: ui_views::ViewHolderToken,
        dimensions: (f32, f32),
        is_focusable: bool,
        scale: Option<f32>,
        rotation: Option<f32>,
        translation: Option<(f32, f32)>,
        name: Option<String>,
    ) -> scenic::ViewHolder {
        let view_holder = scenic::ViewHolder::new(session.clone(), view_holder_token, name);

        let view_properties = ui_gfx::ViewProperties {
            bounding_box: ui_gfx::BoundingBox {
                min: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: GfxSceneManager::VIEW_BOUNDS_DEPTH },
                max: ui_gfx::Vec3 { x: dimensions.0, y: dimensions.1, z: 0.0 },
            },
            downward_input: true,
            focus_change: is_focusable,
            inset_from_min: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            inset_from_max: ui_gfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
        };
        view_holder.set_view_properties(view_properties);
        if let Some(s) = scale {
            view_holder.set_scale(s, s, 1.0);
        }
        if let Some(angle) = rotation {
            // This is a quarternion representing a rotation of `angle` degrees
            // around the z-axis.  See also:
            // https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles#Intuition
            let half_angle = 0.5 * angle.to_radians();
            view_holder.set_rotation(0.0, 0.0, half_angle.sin(), half_angle.cos());
        }
        if let Some((x, y)) = translation {
            view_holder.set_translation(x, y, 0.0);
        }

        view_holder
    }

    /// Creates a view holder, stores it in [`self.views`], then wraps it in a view holder node.
    ///
    /// # Parameters
    /// - `view_holder_token`: The view holder token used to create the view holder.
    /// - `name`: The debugging name for the created view.
    fn add_view(&mut self, view_holder_token: ui_views::ViewHolderToken, name: Option<String>) {
        let view_holder = GfxSceneManager::create_view_holder(
            &self.a11y_proxy_session,
            view_holder_token,
            match self.should_flip_viewport_dimensions {
                false => {
                    (self.display_metrics.width_in_pips(), self.display_metrics.height_in_pips())
                }
                true => {
                    (self.display_metrics.height_in_pips(), self.display_metrics.width_in_pips())
                }
            },
            true,
            None,
            None,
            None,
            name,
        );

        // This is necessary so that the geometry observer API will give the
        // correct `scale` value for this view. See fxbug.dev/106725.
        view_holder.set_event_mask(ui_gfx::METRICS_EVENT_MASK);

        let view_holder_node = scenic::EntityNode::new(self.a11y_proxy_session.clone());
        view_holder_node.attach(&view_holder);
        view_holder_node.set_translation(0.0, 0.0, 0.0);

        self.a11y_proxy_view.add_child(&view_holder_node);
        self.client_root_view_holder_node = Some(view_holder_node);
    }

    /// Ensures that the specified view receives focus if the focus ever returns to the root.
    ///
    /// # Parameters
    /// - `view_ref`: The ViewRef of the view to which the root view should automatically transfer
    /// focus.
    async fn request_auto_focus(&mut self, view_ref: ui_views::ViewRef) {
        let auto_focus_result = self
            .focuser
            .set_auto_focus(ui_views::FocuserSetAutoFocusRequest {
                view_ref: Some(view_ref),
                ..ui_views::FocuserSetAutoFocusRequest::EMPTY
            })
            .await;
        match auto_focus_result {
            Err(e) => warn!("Request focus failed with err: {}", e),
            Ok(Err(value)) => warn!("Request focus failed with err: {:?}", value),
            Ok(_) => {}
        }
    }

    /// Requests that all previously enqueued operations are presented.
    fn request_present(presentation_sender: &PresentationSender) {
        presentation_sender
            .unbounded_send(PresentationMessage::RequestPresent)
            .expect("failed to send RequestPresent message");
    }

    /// Requests that all previously enqueued operations are presented.
    ///
    /// # Notes
    /// Returns only once the next frame has been rendered.
    async fn request_present_and_await_next_frame(presentation_sender: &PresentationSender) {
        let (sender, receiver) = oneshot::channel::<()>();
        presentation_sender
            .unbounded_send(PresentationMessage::RequestPresentWithPingback(sender))
            .expect("failed to send RequestPresentWithPingback message");
        _ = receiver.await;
    }

    async fn update_viewport(&self) {
        let (width_pixels, height_pixels) = self.display_size.pixels();

        // Viewport should match the visible part of the display 1:1. To do this
        // we need to match the ClipSpaceTransform.
        //
        // Since the ClipSpaceTransform is defined in Vulkan NDC with scaling,
        // and the Viewport is defined in pixel coordinates, we need to be able
        // to transform offsets to pixel coordinates. This is done by
        // multiplying by half the display length and inverting the scale.
        //
        // Because the ClipSpaceTransform is defined with its origin in the
        // center, and the Viewport with its origin in the top left corner, we
        // need to add a center offset to compensate.  This turns out to be as
        // simple as half the scaled display length minus half the ClipSpace
        // length, which equals scale - 1 in NDC.
        //
        // Finally, because the ClipSpaceTransform and the Viewport transform
        // are defined in opposite directions (camera to scene vs context to
        // viewport), all the transforms should be inverted for the Viewport
        // transform. This means an inverted scale and negative clip offsets.
        //
        // (See the same logic in root presenter: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/bin/root_presenter/presentation.cc;drc=44c08193dbb4ed5d82804d0faf7bce76d95d4dab;l=423)
        self.viewport_publisher.lock().set({
            let inverted_scale = 1. / self.clip_scale;
            let center_offset_ndc = self.clip_scale - 1.;
            let ndc_to_pixel_x = inverted_scale * width_pixels * 0.5;
            let ndc_to_pixel_y = inverted_scale * height_pixels * 0.5;
            let x_offset = ndc_to_pixel_x * (center_offset_ndc - self.clip_offset_x);
            let y_offset = ndc_to_pixel_y * (center_offset_ndc - self.clip_offset_y);
            InjectorViewportSpec {
                width: width_pixels,
                height: height_pixels,
                scale: inverted_scale,
                x_offset,
                y_offset,
            }
        })
    }
}
