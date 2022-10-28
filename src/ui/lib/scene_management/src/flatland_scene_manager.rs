// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        display_metrics::{DisplayMetrics, ViewingDistance},
        pointerinjector_config::{
            InjectorViewportHangingGet, InjectorViewportPublisher, InjectorViewportSpec,
            InjectorViewportSubscriber,
        },
        scene_manager::{
            self, PresentationMessage, PresentationSender, SceneManager, ViewportToken,
        },
    },
    anyhow::anyhow,
    anyhow::Error,
    async_trait::async_trait,
    fidl,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_accessibility_scene as a11y_scene, fidl_fuchsia_math as math,
    fidl_fuchsia_ui_app as ui_app,
    fidl_fuchsia_ui_composition::{self as ui_comp, ContentId, TransformId},
    fidl_fuchsia_ui_display_singleton as singleton_display, fidl_fuchsia_ui_views as ui_views,
    fuchsia_scenic as scenic,
    futures::channel::{mpsc::unbounded, oneshot},
    input_pipeline::Size,
    math as fmath,
    parking_lot::Mutex,
    std::sync::Arc,
    tracing::warn,
};

// TODO(fxbug.dev/91061): Make this larger when we have a protocol to
// determine the hotspot and the cursor component knows how to produce
// output that doesn't occupy the full viewport.
const CURSOR_SIZE: (u32, u32) = (18, 29);
const CURSOR_HOTSPOT: (u32, u32) = (2, 4);

// TODO(fxbug.dev/78201): Remove hardcoded scale when Flatland provides
// what is needed to determine the cursor scale factor.
const CURSOR_SCALE_MULTIPLIER: u32 = 5;
const CURSOR_SCALE_DIVIDER: u32 = 4;

// Converts a cursor size to physical pixels.
fn physical_cursor_size(value: u32) -> u32 {
    (CURSOR_SCALE_MULTIPLIER * value) / CURSOR_SCALE_DIVIDER
}

pub type FlatlandPtr = Arc<Mutex<ui_comp::FlatlandProxy>>;

#[derive(Clone)]
pub struct TransformContentIdPair {
    transform_id: TransformId,
    content_id: ContentId,
}

/// FlatlandInstance encapsulates a FIDL connection to a Flatland instance, along with some other
/// state resulting from initializing the instance in a standard way; see FlatlandInstance::new().
/// For example, a view is created during initialization, and so FlatlandInstance stores the
/// corresponding ViewRef and a ParentViewportWatcher FIDL connection.
struct FlatlandInstance {
    // TODO(fxbug.dev/87156): Arc<Mutex<>>, yuck.
    flatland: FlatlandPtr,
    view_ref: ui_views::ViewRef,
    root_transform_id: TransformId,
    parent_viewport_watcher: ui_comp::ParentViewportWatcherProxy,
    focuser: ui_views::FocuserProxy,
}

impl FlatlandInstance {
    fn new(
        flatland: ui_comp::FlatlandProxy,
        mut view_creation_token: ui_views::ViewCreationToken,
        id_generator: &mut scenic::flatland::IdGenerator,
    ) -> Result<FlatlandInstance, Error> {
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            create_proxy::<ui_comp::ParentViewportWatcherMarker>()?;

        let (focuser, focuser_request) = create_proxy::<ui_views::FocuserMarker>()?;

        let view_bound_protocols = ui_comp::ViewBoundProtocols {
            view_focuser: Some(focuser_request),
            ..ui_comp::ViewBoundProtocols::EMPTY
        };

        let mut view_identity = ui_views::ViewIdentityOnCreation::from(scenic::ViewRefPair::new()?);
        let view_ref = scenic::duplicate_view_ref(&view_identity.view_ref)?;
        flatland.create_view2(
            &mut view_creation_token,
            &mut view_identity,
            view_bound_protocols,
            parent_viewport_watcher_request,
        )?;

        let root_transform_id = id_generator.next_transform_id();
        flatland.create_transform(&mut root_transform_id.clone())?;
        flatland.set_root_transform(&mut root_transform_id.clone())?;

        Ok(FlatlandInstance {
            flatland: Arc::new(Mutex::new(flatland)),
            view_ref,
            root_transform_id,
            parent_viewport_watcher,
            focuser,
        })
    }
}

fn request_present_with_pingback(
    presentation_sender: &PresentationSender,
) -> Result<oneshot::Receiver<()>, Error> {
    let (sender, receiver) = oneshot::channel::<()>();
    presentation_sender.unbounded_send(PresentationMessage::RequestPresentWithPingback(sender))?;
    Ok(receiver)
}

/// FlatlandSceneManager manages the platform/framework-controlled part of the global Scenic scene
/// graph, with the fundamental goal of connecting the physical display to the product-defined user
/// shell.  The part of the scene graph managed by the scene manager is split between three Flatland
/// instances, which are linked by view/viewport pairs.
//
// The scene graph looks like this:
//
//         FD          FD:  FlatlandDisplay
//         |
//         R*          R*:  root transform of |root_flatland|,
//         |                and also the corresponding view/view-ref (see below)
//        / \
//       /   \         Rc:  transform holding whatever is necessary to render the cursor
//     Rpi    Rc
//      |      \       Rpi: transform with viewport linking to |pointerinjector_flatland|
//      |       (etc.)      (see docs on struct field for rationale)
//      |
//      P*             P*:  root transform of |pointerinjector_flatland|,
//      |                   and also the corresponding view/view-ref (see below)
//      |
//      Pa             Pa:  transform with viewport linking to an external Flatland instance
//      |                   owned by a11y manager.
//      |
//      A*             A*:  root transform of |a11y_flatland| (owned by a11y manager),
//      |                   and also the corresponding view/view-ref (see below).
//      |
//      As             As:  transform with viewport linking to |scene_flatland|.
//      |
//      |
//      S*             S*:  root transform of |scene_flatland|,
//      |                   and also the corresponding view/view-ref (see below)
//      |
//      (user shell)   The session uses the SceneManager.SetRootView() FIDL API to attach the user
//                     shell to the scene graph depicted above.
//
// There is a reason why the "corresponding view/view-refs" are called out in the diagram above.
// When registering an input device with the fuchsia.ui.pointerinjector.Registry API, the Config
// must specify two ViewRefs, the "context" and the "target"; the former must be a strict ancestor
// or the former (the target denotes the first eligible view to receive input; it will always be
// the root of the focus chain).  The context ViewRef is R* and the target ViewRef is P*.  Note that
// the possibly-inserted accessiblity view is the direct descendant of |pointerinjector_flatland|.
// This gives the accessiblity manager the ability to give itself focus, and therefore receive all
// input.
pub struct FlatlandSceneManager {
    // Flatland connection between the physical display and the rest of the scene graph.
    _display: ui_comp::FlatlandDisplayProxy,

    // Layout info received from the display.
    layout_info: ui_comp::LayoutInfo,

    // The size that will ultimately be assigned to the View created with the
    // `fuchsia.session.scene.Manager` protocol.
    client_viewport_size: math::SizeU,

    // Flatland instance that connects to |display|.  Hosts a viewport which connects it to
    // to a view in |pointerinjector_flatland|.
    //
    // See the above diagram of FlatlandSceneManager's scene graph topology.
    root_flatland: FlatlandInstance,

    // Flatland instance that sits beneath |root_flatland| in the scene graph.  The reason that this
    // exists is that two different ViewRefs must be provided when configuring the input pipeline to
    // inject pointer events into Scenic via fuchsia.ui.pointerinjector.Registry; since a Flatland
    // instance can have only a single view, we add an additional Flatland instance into the scene
    // graph to obtain the second view (the "target" view; the "context" view is obtained from
    // |root_flatland|).
    //
    // See the above diagram of FlatlandSceneManager's scene graph topology.
    _pointerinjector_flatland: FlatlandInstance,

    // Flatland instance that embeds the system shell (i.e. via the SetRootView() FIDL API).  Its
    // root view is attached to a viewport owned by the accessibility manager (via
    // fuchsia.ui.accessibility.scene.Provider, create_view()).
    scene_flatland: FlatlandInstance,

    // These are the ViewRefs returned by get_pointerinjection_view_refs().  They are used to
    // configure input-pipeline handlers for pointer events.
    context_view_ref: ui_views::ViewRef,
    target_view_ref: ui_views::ViewRef,

    // Used to sent presentation requests for |root_flatand| and |scene_flatland|, respectively.
    root_flatland_presentation_sender: PresentationSender,
    _pointerinjector_flatland_presentation_sender: PresentationSender,
    scene_flatland_presentation_sender: PresentationSender,

    // Holds a pair of IDs that are used to embed the system shell inside |scene_flatland|, a
    // TransformId identifying a transform in the scene graph, and a ContentId which identifies a
    // a viewport that is set as the content of that transform.
    scene_root_viewport_ids: Option<TransformContentIdPair>,

    // Generates a sequential stream of ContentIds and TransformIds.  By guaranteeing
    // uniqueness across all Flatland instances, we avoid potential confusion during debugging.
    id_generator: scenic::flatland::IdGenerator,

    // Supports callers of fuchsia.ui.pointerinjector.configuration.setup.WatchViewport(), allowing
    // each invocation to subscribe to changes in the viewport region.
    viewport_hanging_get: Arc<Mutex<InjectorViewportHangingGet>>,

    // Used to publish viewport changes to subscribers of |viewport_hanging_get|.
    // TODO(fxbug.dev/87517): use this to publish changes to screen resolution.
    _viewport_publisher: Arc<Mutex<InjectorViewportPublisher>>,

    // Used to position the cursor.
    cursor_transform_id: Option<TransformId>,

    // Used to track cursor visibility.
    cursor_visibility: bool,

    // Used to track the display metrics for the root scene.
    display_metrics: DisplayMetrics,
}

#[async_trait]
impl SceneManager for FlatlandSceneManager {
    async fn set_root_view(
        &mut self,
        viewport_token: ViewportToken,
        _view_ref: Option<ui_views::ViewRef>,
    ) -> Result<(), Error> {
        let viewport_creation_token = match viewport_token {
            ViewportToken::Gfx(_) => Err(anyhow::anyhow!(
                "Gfx ViewHolderToken passed to FlatlandSceneManager.set_root_view!"
            )),
            ViewportToken::Flatland(viewport_creation_token) => Ok(viewport_creation_token),
        }?;

        self.set_root_view_internal(viewport_creation_token).await.map(|_view_ref| {})
    }

    async fn set_root_view_deprecated(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
    ) -> Result<ui_views::ViewRef, Error> {
        let link_token_pair = scenic::flatland::ViewCreationTokenPair::new()?;

        // Use view provider to initiate creation of the view which will be connected to the
        // viewport that we create below.
        view_provider.create_view2(ui_app::CreateView2Args {
            view_creation_token: Some(link_token_pair.view_creation_token),
            ..ui_app::CreateView2Args::EMPTY
        })?;

        self.set_root_view_internal(link_token_pair.viewport_creation_token).await
    }

    fn request_focus(
        &self,
        view_ref: &mut ui_views::ViewRef,
    ) -> fidl::client::QueryResponseFut<ui_views::FocuserRequestFocusResult> {
        self.root_flatland.focuser.request_focus(view_ref)
    }

    async fn insert_a11y_view(
        &mut self,
        _a11y_view_holder_token: ui_views::ViewHolderToken,
    ) -> Result<ui_views::ViewHolderToken, Error> {
        Err(anyhow::anyhow!("A11y should be configured to use Flatland, not Gfx"))
    }

    fn insert_a11y_view2(
        &mut self,
        _a11y_viewport_creation_token: ui_views::ViewportCreationToken,
    ) -> Result<ui_views::ViewportCreationToken, Error> {
        Err(anyhow::anyhow!(
            "Scene manager should use fuchsia.accessibility.scene.Provider to insert a11y view."
        ))
    }

    async fn set_camera_clip_space_transform(&mut self, _x: f32, _y: f32, _scale: f32) {
        panic!("unimplemented")
    }

    async fn reset_camera_clip_space_transform(&mut self) {
        panic!("unimplemented")
    }

    fn get_pointerinjection_view_refs(&self) -> (ui_views::ViewRef, ui_views::ViewRef) {
        (
            scenic::duplicate_view_ref(&self.context_view_ref).expect("failed to copy ViewRef"),
            scenic::duplicate_view_ref(&self.target_view_ref).expect("failed to copy ViewRef"),
        )
    }

    fn set_cursor_position(&mut self, position: input_pipeline::Position) {
        if let Some(cursor_transform_id) = self.cursor_transform_id {
            let x = position.x.round() as i32 - physical_cursor_size(CURSOR_HOTSPOT.0) as i32;
            let y = position.y.round() as i32 - physical_cursor_size(CURSOR_HOTSPOT.1) as i32;
            let flatland = self.root_flatland.flatland.lock();
            flatland
                .set_translation(&mut cursor_transform_id.clone(), &mut fmath::Vec_ { x, y })
                .expect("fidl error");
            self.root_flatland_presentation_sender
                .unbounded_send(PresentationMessage::RequestPresent)
                .expect("send failed");
        }
    }

    fn set_cursor_visibility(&mut self, visible: bool) {
        if let Some(cursor_transform_id) = self.cursor_transform_id {
            if self.cursor_visibility != visible {
                self.cursor_visibility = visible;
                let flatland = self.root_flatland.flatland.lock();
                if visible {
                    flatland
                        .add_child(
                            &mut self.root_flatland.root_transform_id.clone(),
                            &mut cursor_transform_id.clone(),
                        )
                        .expect("failed to add cursor to scene");
                } else {
                    flatland
                        .remove_child(
                            &mut self.root_flatland.root_transform_id.clone(),
                            &mut cursor_transform_id.clone(),
                        )
                        .expect("failed to remove cursor from scene");
                }
                self.root_flatland_presentation_sender
                    .unbounded_send(PresentationMessage::RequestPresent)
                    .expect("send failed");
            }
        }
    }

    fn get_pointerinjection_display_size(&self) -> Size {
        let logical_size = self.layout_info.logical_size.unwrap();
        let width: f32 = logical_size.width as f32;
        let height: f32 = logical_size.height as f32;
        Size { width, height }
    }

    fn get_pointerinjector_viewport_watcher_subscription(&self) -> InjectorViewportSubscriber {
        self.viewport_hanging_get.lock().new_subscriber()
    }

    fn get_display_metrics(&self) -> &DisplayMetrics {
        &self.display_metrics
    }
}

impl FlatlandSceneManager {
    pub async fn new(
        display: ui_comp::FlatlandDisplayProxy,
        singleton_display_info: singleton_display::InfoProxy,
        root_flatland: ui_comp::FlatlandProxy,
        pointerinjector_flatland: ui_comp::FlatlandProxy,
        scene_flatland: ui_comp::FlatlandProxy,
        cursor_view_provider: ui_app::ViewProviderProxy,
        a11y_view_provider: a11y_scene::ProviderProxy,
        display_rotation: i32,
        display_pixel_density: Option<f32>,
        viewing_distance: Option<ViewingDistance>,
    ) -> Result<Self, Error> {
        let mut id_generator = scenic::flatland::IdGenerator::new();

        // Generate unique transform/content IDs that will be used to create the sub-scenegraphs
        // in the Flatland instances managed by SceneManager.
        let pointerinjector_viewport_transform_id = id_generator.next_transform_id();
        let pointerinjector_viewport_content_id = id_generator.next_content_id();
        let a11y_viewport_transform_id = id_generator.next_transform_id();
        let a11y_viewport_content_id = id_generator.next_content_id();

        root_flatland.set_debug_name("SceneManager Display")?;
        pointerinjector_flatland.set_debug_name("SceneManager PointerInjector")?;
        scene_flatland.set_debug_name("SceneManager Scene")?;

        let mut root_view_creation_pair = scenic::flatland::ViewCreationTokenPair::new()?;
        let root_flatland = FlatlandInstance::new(
            root_flatland,
            root_view_creation_pair.view_creation_token,
            &mut id_generator,
        )?;

        let mut pointerinjector_view_creation_pair =
            scenic::flatland::ViewCreationTokenPair::new()?;
        let pointerinjector_flatland = FlatlandInstance::new(
            pointerinjector_flatland,
            pointerinjector_view_creation_pair.view_creation_token,
            &mut id_generator,
        )?;

        let mut scene_view_creation_pair = scenic::flatland::ViewCreationTokenPair::new()?;
        let scene_flatland = FlatlandInstance::new(
            scene_flatland,
            scene_view_creation_pair.view_creation_token,
            &mut id_generator,
        )?;

        // Set the device pixel ratio of FlatlandDisplay.
        {
            let info = singleton_display_info.get_metrics().await?;
            let extent_in_px =
                info.extent_in_px.ok_or(anyhow::anyhow!("Did not receive display size"))?;
            let display_metrics = DisplayMetrics::new(
                Size { width: extent_in_px.width as f32, height: extent_in_px.height as f32 },
                display_pixel_density,
                viewing_distance,
                None,
            );
            display.set_device_pixel_ratio(&mut fmath::VecF {
                x: display_metrics.pixels_per_pip(),
                y: display_metrics.pixels_per_pip(),
            })?;
        }

        // Connect the FlatlandDisplay to |root_flatland|'s view.
        {
            // We don't need to watch the child view, since we also own it. So, we discard the
            // client end of the the channel pair.
            let (_, child_view_watcher_request) =
                create_proxy::<ui_comp::ChildViewWatcherMarker>()?;

            display.set_content(
                &mut root_view_creation_pair.viewport_creation_token,
                child_view_watcher_request,
            )?;
        }

        // Obtain layout info from the display.
        let layout_info = root_flatland.parent_viewport_watcher.get_layout().await?;
        let root_viewport_size = layout_info
            .logical_size
            .ok_or(anyhow::anyhow!("Did not receive layout info from the display"))?;

        // Combine the layout info with passed-in display properties.
        let display_metrics = DisplayMetrics::new(
            Size {
                width: root_viewport_size.width as f32,
                height: root_viewport_size.height as f32,
            },
            display_pixel_density,
            viewing_distance,
            None,
        );
        let (
            display_rotation_enum,
            injector_viewport_translation,
            flip_injector_viewport_dimensions,
        ) = match display_rotation % 360 {
            0 => Ok((ui_comp::Orientation::Ccw0Degrees, math::Vec_ { x: 0, y: 0 }, false)),
            90 => Ok((
                // Rotation is specified in the opposite winding direction to gfx (and the
                // specified |display_rotation| value). Winding in the opposite direction is equal
                // to -90 degrees, which is equivalent to 270.
                ui_comp::Orientation::Ccw270Degrees,
                math::Vec_ { x: display_metrics.width_in_pixels() as i32, y: 0 },
                true,
            )),
            180 => Ok((
                ui_comp::Orientation::Ccw180Degrees,
                math::Vec_ {
                    x: display_metrics.width_in_pixels() as i32,
                    y: display_metrics.height_in_pixels() as i32,
                },
                false,
            )),
            270 => Ok((
                // Rotation is specified in the opposite winding direction to gfx (and the
                // specified |display_rotation| value). Winding in the opposite direction is equal
                // to -270 degrees, which is equivalent to 90.
                ui_comp::Orientation::Ccw90Degrees,
                math::Vec_ { x: 0, y: display_metrics.height_in_pixels() as i32 },
                true,
            )),
            _ => Err(anyhow::anyhow!("Invalid display rotation; must be {{0,90,180,270}}")),
        }?;
        let client_viewport_size = match flip_injector_viewport_dimensions {
            true => math::SizeU {
                width: display_metrics.height_in_pixels(),
                height: display_metrics.width_in_pixels(),
            },
            false => math::SizeU {
                width: display_metrics.width_in_pixels(),
                height: display_metrics.height_in_pixels(),
            },
        };

        // Create the pointerinjector view and embed it as a child of the root view.
        {
            let flatland = root_flatland.flatland.lock();
            flatland.create_transform(&mut pointerinjector_viewport_transform_id.clone())?;
            flatland.add_child(
                &mut root_flatland.root_transform_id.clone(),
                &mut pointerinjector_viewport_transform_id.clone(),
            )?;
            flatland.set_orientation(
                &mut pointerinjector_viewport_transform_id.clone(),
                display_rotation_enum,
            )?;
            flatland.set_translation(
                &mut pointerinjector_viewport_transform_id.clone(),
                &mut injector_viewport_translation.clone(),
            )?;

            let link_properties = ui_comp::ViewportProperties {
                logical_size: Some(client_viewport_size),
                ..ui_comp::ViewportProperties::EMPTY
            };

            let (_, child_view_watcher_request) =
                create_proxy::<ui_comp::ChildViewWatcherMarker>()?;

            flatland.create_viewport(
                &mut pointerinjector_viewport_content_id.clone(),
                &mut pointerinjector_view_creation_pair.viewport_creation_token,
                link_properties,
                child_view_watcher_request,
            )?;
            flatland.set_content(
                &mut pointerinjector_viewport_transform_id.clone(),
                &mut pointerinjector_viewport_content_id.clone(),
            )?;
        }

        // If the cursor exists, create the cursor view and embed it as a child of the root view.
        let mut maybe_cursor_transform_id = None;
        let mut cursor_view_creation_pair = scenic::flatland::ViewCreationTokenPair::new()?;
        match cursor_view_provider.create_view2(ui_app::CreateView2Args {
            view_creation_token: Some(cursor_view_creation_pair.view_creation_token),
            ..ui_app::CreateView2Args::EMPTY
        }) {
            Ok(_) => {
                let flatland = root_flatland.flatland.lock();

                let cursor_transform_id = id_generator.next_transform_id();
                let cursor_viewport_content_id = id_generator.next_content_id();

                // We create/add the cursor transform second, so that it is above everything else in
                // the scene graph.
                flatland.create_transform(&mut cursor_transform_id.clone())?;
                flatland.add_child(
                    &mut root_flatland.root_transform_id.clone(),
                    &mut cursor_transform_id.clone(),
                )?;
                // Visible but offscreen until we get a first position event.
                flatland.set_translation(
                    &mut cursor_transform_id.clone(),
                    &mut fmath::Vec_ { x: -100, y: -100 },
                )?;

                let cursor_size = fmath::SizeU {
                    width: physical_cursor_size(CURSOR_SIZE.0),
                    height: physical_cursor_size(CURSOR_SIZE.1),
                };
                let link_properties = ui_comp::ViewportProperties {
                    logical_size: Some(cursor_size),
                    ..ui_comp::ViewportProperties::EMPTY
                };

                let (_, child_view_watcher_request) =
                    create_proxy::<ui_comp::ChildViewWatcherMarker>()?;

                flatland.create_viewport(
                    &mut cursor_viewport_content_id.clone(),
                    &mut cursor_view_creation_pair.viewport_creation_token,
                    link_properties,
                    child_view_watcher_request,
                )?;
                flatland.set_content(
                    &mut cursor_transform_id.clone(),
                    &mut cursor_viewport_content_id.clone(),
                )?;

                maybe_cursor_transform_id = Some(cursor_transform_id);
            }
            Err(e) => {
                warn!("Failed to create cursor View: {:?}. Cursor disabled.", e);
            }
        };

        let mut a11y_view_creation_pair = scenic::flatland::ViewCreationTokenPair::new()?;

        // Bridge the pointerinjector and a11y Flatland instances.
        let (a11y_view_watcher, a11y_view_watcher_request) =
            create_proxy::<ui_comp::ChildViewWatcherMarker>()?;
        {
            let flatland = pointerinjector_flatland.flatland.lock();
            flatland.create_transform(&mut &mut a11y_viewport_transform_id.clone())?;
            flatland.add_child(
                &mut pointerinjector_flatland.root_transform_id.clone(),
                &mut a11y_viewport_transform_id.clone(),
            )?;

            let link_properties = ui_comp::ViewportProperties {
                logical_size: Some(client_viewport_size),
                ..ui_comp::ViewportProperties::EMPTY
            };

            flatland.create_viewport(
                &mut a11y_viewport_content_id.clone(),
                &mut a11y_view_creation_pair.viewport_creation_token,
                link_properties,
                a11y_view_watcher_request,
            )?;
            flatland.set_content(
                &mut a11y_viewport_transform_id.clone(),
                &mut a11y_viewport_content_id.clone(),
            )?;
        }

        // Request for the a11y manager to create its view.
        a11y_view_provider.create_view(
            &mut a11y_view_creation_pair.view_creation_token,
            &mut scene_view_creation_pair.viewport_creation_token,
        )?;

        // Start Present() loops for both Flatland instances, and request that both be presented.
        let (root_flatland_presentation_sender, root_receiver) = unbounded();
        scene_manager::start_flatland_presentation_loop(
            root_receiver,
            Arc::downgrade(&root_flatland.flatland),
            "root_view".to_string(),
        );
        let (pointerinjector_flatland_presentation_sender, pointerinjector_receiver) = unbounded();
        scene_manager::start_flatland_presentation_loop(
            pointerinjector_receiver,
            Arc::downgrade(&pointerinjector_flatland.flatland),
            "pointerinjector_view".to_string(),
        );
        let (scene_flatland_presentation_sender, scene_receiver) = unbounded();
        scene_manager::start_flatland_presentation_loop(
            scene_receiver,
            Arc::downgrade(&scene_flatland.flatland),
            "scene_view".to_string(),
        );

        let mut pingback_channels = Vec::new();
        pingback_channels.push(request_present_with_pingback(&root_flatland_presentation_sender)?);
        pingback_channels
            .push(request_present_with_pingback(&pointerinjector_flatland_presentation_sender)?);
        pingback_channels.push(request_present_with_pingback(&scene_flatland_presentation_sender)?);

        // Wait for a11y view to attach before proceeding.
        let a11y_view_status = a11y_view_watcher.get_status().await?;
        match a11y_view_status {
            ui_comp::ChildViewStatus::ContentHasPresented => {}
        }

        let viewport_hanging_get: Arc<Mutex<InjectorViewportHangingGet>> =
            scene_manager::create_viewport_hanging_get({
                let logical_size =
                    layout_info.logical_size.ok_or(anyhow!("LayoutInfo must have logical_size"))?;

                InjectorViewportSpec {
                    width: logical_size.width as f32,
                    height: logical_size.height as f32,
                    scale: 1.,
                    x_offset: 0.,
                    y_offset: 0.,
                }
            });
        let viewport_publisher = Arc::new(Mutex::new(viewport_hanging_get.lock().new_publisher()));

        let context_view_ref = scenic::duplicate_view_ref(&root_flatland.view_ref)?;
        let target_view_ref = scenic::duplicate_view_ref(&pointerinjector_flatland.view_ref)?;

        // Wait for all pingbacks to ensure the scene is fully set up before returning.
        for receiver in pingback_channels {
            _ = receiver.await;
        }

        Ok(FlatlandSceneManager {
            _display: display,
            layout_info,
            client_viewport_size,
            root_flatland,
            _pointerinjector_flatland: pointerinjector_flatland,
            scene_flatland,
            context_view_ref,
            target_view_ref,
            root_flatland_presentation_sender,
            _pointerinjector_flatland_presentation_sender:
                pointerinjector_flatland_presentation_sender,
            scene_flatland_presentation_sender,
            scene_root_viewport_ids: None,
            id_generator,
            viewport_hanging_get,
            _viewport_publisher: viewport_publisher,
            cursor_transform_id: maybe_cursor_transform_id,
            cursor_visibility: true,
            display_metrics,
        })
    }

    async fn set_root_view_internal(
        &mut self,
        mut viewport_creation_token: ui_views::ViewportCreationToken,
    ) -> Result<ui_views::ViewRef, Error> {
        // Remove any existing viewport.
        if let Some(ids) = &self.scene_root_viewport_ids {
            let locked = self.scene_flatland.flatland.lock();
            locked.set_content(&mut ids.transform_id.clone(), &mut ContentId { value: 0 })?;
            locked.remove_child(
                &mut self.scene_flatland.root_transform_id.clone(),
                &mut ids.transform_id.clone(),
            )?;
            locked.release_transform(&mut ids.transform_id.clone())?;
            let _ = locked.release_viewport(&mut ids.content_id.clone());
            self.scene_root_viewport_ids = None;
        }

        // Create new viewport.
        let ids = TransformContentIdPair {
            transform_id: self.id_generator.next_transform_id(),
            content_id: self.id_generator.next_content_id(),
        };
        let (child_view_watcher, child_view_watcher_request) =
            create_proxy::<ui_comp::ChildViewWatcherMarker>()?;
        {
            let locked = self.scene_flatland.flatland.lock();
            let viewport_properties = ui_comp::ViewportProperties {
                logical_size: Some(self.client_viewport_size),
                ..ui_comp::ViewportProperties::EMPTY
            };
            locked.create_viewport(
                &mut ids.content_id.clone(),
                &mut viewport_creation_token,
                viewport_properties,
                child_view_watcher_request,
            )?;
            locked.create_transform(&mut ids.transform_id.clone())?;
            locked.add_child(
                &mut self.scene_flatland.root_transform_id.clone(),
                &mut ids.transform_id.clone(),
            )?;
            locked.set_content(&mut ids.transform_id.clone(), &mut ids.content_id.clone())?;
        }
        self.scene_root_viewport_ids = Some(ids);

        // Present the previous scene graph mutations.  This MUST be done before awaiting the result
        // of get_view_ref() below, because otherwise the view won't become attached to the global
        // scene graph topology, and the awaited ViewRef will never come.
        let mut pingback_channels = Vec::new();
        pingback_channels
            .push(request_present_with_pingback(&self.scene_flatland_presentation_sender)?);

        let _child_status = child_view_watcher.get_status().await?;
        let child_view_ref = child_view_watcher.get_view_ref().await?;
        let child_view_ref_copy = scenic::duplicate_view_ref(&child_view_ref)?;

        let auto_focus_result = self
            .root_flatland
            .focuser
            .set_auto_focus(ui_views::FocuserSetAutoFocusRequest {
                view_ref: Some(child_view_ref),
                ..ui_views::FocuserSetAutoFocusRequest::EMPTY
            })
            .await;
        match auto_focus_result {
            Err(e) => warn!("Request focus failed with err: {}", e),
            Ok(Err(value)) => warn!("Request focus failed with err: {:?}", value),
            Ok(_) => {}
        }
        pingback_channels
            .push(request_present_with_pingback(&self.root_flatland_presentation_sender)?);

        // Wait for all pingbacks to ensure the scene is fully set up before returning.
        for receiver in pingback_channels {
            _ = receiver.await;
        }

        Ok(child_view_ref_copy)
    }
}
