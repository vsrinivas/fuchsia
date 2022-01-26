// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        pointerinjector_config::{
            InjectorViewportChangeFn, InjectorViewportHangingGet, InjectorViewportPublisher,
            InjectorViewportSpec, InjectorViewportSubscriber,
        },
        scene_manager::{self, PresentationMessage, PresentationSender, SceneManager},
        DisplayMetrics,
    },
    anyhow::Error,
    async_trait::async_trait,
    async_utils::hanging_get::server as hanging_get,
    fidl,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_math as fmath, fidl_fuchsia_ui_app as ui_app,
    fidl_fuchsia_ui_composition::{self as ui_comp, ContentId, TransformId},
    fidl_fuchsia_ui_scenic as ui_scenic, fidl_fuchsia_ui_views as ui_views,
    fuchsia_scenic as scenic, fuchsia_scenic,
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    futures::channel::mpsc::unbounded,
    input_pipeline::{input_pipeline::InputPipelineAssembly, Size},
    parking_lot::Mutex,
    std::{convert::TryFrom, sync::Arc},
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
//      Pa             Pa:  transform with viewport linking either to |scene_flatland|, or to
//      |                        an external Flatland instance owned by the A11y Manager.
//      |
//      =              =:   indicates possible insertion of an intermediary view/viewport between
//      |                   |pointerinjector_flatland| and |scene_flatland|, via insert_a11y_view2()
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
    // root view is attached to a viewport in |pointerinjector_flatland| by default, or a viewport
    // owned by the accessibility manager (via fuchsia.ui.accessibility.view.Registry API, see
    // insert_a11y_view2() ).
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
    cursor_transform_id: TransformId,

    // Used to track cursor visibility.
    cursor_visibility: bool,

    display_metrics: DisplayMetrics,
}

#[async_trait]
impl SceneManager for FlatlandSceneManager {
    async fn add_view_to_scene(
        &mut self,
        _view_provider: ui_app::ViewProviderProxy,
        _name: Option<String>,
    ) -> Result<ui_views::ViewRef, Error> {
        panic!("unimplemented");
    }

    async fn set_root_view(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
    ) -> Result<ui_views::ViewRef, Error> {
        let mut link_token_pair = scenic::flatland::LinkTokenPair::new()?;

        // Use view provider to initiate creation of the view which will be connected to the
        // viewport that we create below.
        view_provider.create_view2(ui_app::CreateView2Args {
            view_creation_token: Some(link_token_pair.view_creation_token),
            ..ui_app::CreateView2Args::EMPTY
        })?;

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
                logical_size: Some(self.layout_info.logical_size.unwrap()),
                ..ui_comp::ViewportProperties::EMPTY
            };
            locked.create_viewport(
                &mut ids.content_id.clone(),
                &mut link_token_pair.viewport_creation_token,
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
        self.scene_flatland_presentation_sender.unbounded_send(PresentationMessage::Present)?;

        let mut view_ref = child_view_watcher.get_view_ref().await?;
        let view_ref_copy = fuchsia_scenic::duplicate_view_ref(&view_ref)?;

        let child_status = child_view_watcher.get_status().await?;
        match child_status {
            ui_comp::ChildViewStatus::ContentHasPresented => {}
        }
        let request_focus_result = self.root_flatland.focuser.request_focus(&mut view_ref).await;
        match request_focus_result {
            Err(e) => fx_log_warn!("Request focus failed with err: {}", e),
            Ok(Err(value)) => fx_log_warn!("Request focus failed with err: {:?}", value),
            Ok(_) => {}
        }
        self.root_flatland_presentation_sender.unbounded_send(PresentationMessage::Present)?;

        Ok(view_ref_copy)
    }

    fn request_focus(
        &self,
        view_ref: &mut ui_views::ViewRef,
    ) -> fidl::client::QueryResponseFut<ui_views::FocuserRequestFocusResult> {
        self.root_flatland.focuser.request_focus(view_ref)
    }

    fn insert_a11y_view(
        &mut self,
        _a11y_view_holder_token: ui_views::ViewHolderToken,
    ) -> Result<ui_views::ViewHolderToken, Error> {
        Err(anyhow::anyhow!("A11y should be configured to use Flatland, not Gfx"))
    }

    // TODO(fxbug.dev/592501): implement.
    fn insert_a11y_view2(
        &mut self,
        _a11y_viewport_creation_token: ui_views::ViewportCreationToken,
    ) -> Result<ui_views::ViewportCreationToken, Error> {
        panic!("unimplemented")
    }

    fn get_pointerinjection_view_refs(&self) -> (ui_views::ViewRef, ui_views::ViewRef) {
        (
            scenic::duplicate_view_ref(&self.context_view_ref).expect("failed to copy ViewRef"),
            scenic::duplicate_view_ref(&self.target_view_ref).expect("failed to copy ViewRef"),
        )
    }

    fn set_cursor_position(&mut self, position: input_pipeline::Position) {
        let x = position.x.round() as i32 - physical_cursor_size(CURSOR_HOTSPOT.0) as i32;
        let y = position.y.round() as i32 - physical_cursor_size(CURSOR_HOTSPOT.1) as i32;
        let flatland = self.root_flatland.flatland.lock();
        flatland
            .set_translation(&mut self.cursor_transform_id, &mut fmath::Vec_ { x, y })
            .expect("fidl error");
        self.root_flatland_presentation_sender
            .unbounded_send(PresentationMessage::Present)
            .expect("send failed");
    }

    fn set_cursor_visibility(&mut self, visible: bool) {
        if self.cursor_visibility != visible {
            self.cursor_visibility = visible;
            let flatland = self.root_flatland.flatland.lock();
            if visible {
                flatland
                    .add_child(
                        &mut self.root_flatland.root_transform_id.clone(),
                        &mut self.cursor_transform_id.clone(),
                    )
                    .expect("failed to add cursor to scene");
            } else {
                flatland
                    .remove_child(
                        &mut self.root_flatland.root_transform_id.clone(),
                        &mut self.cursor_transform_id.clone(),
                    )
                    .expect("failed to remove cursor from scene");
            }
            self.root_flatland_presentation_sender
                .unbounded_send(PresentationMessage::Present)
                .expect("send failed");
        }
    }

    fn get_pointerinjection_display_size(&self) -> Size {
        let logical_size = self.layout_info.logical_size.unwrap();
        let pixel_scale = self.layout_info.pixel_scale.unwrap();
        let width: f32 = (logical_size.width * pixel_scale.width) as f32;
        let height: f32 = (logical_size.height * pixel_scale.height) as f32;
        Size { width, height }
    }

    // TODO(fxbug.dev/87519): delete
    async fn add_touch_handler(
        &self,
        mut _assembly: InputPipelineAssembly,
    ) -> InputPipelineAssembly {
        panic!("add_touch_handler() not implemented for Flatland.  See build_input_pipeline() in input_pipeline.rs");
    }

    async fn add_mouse_handler(
        &self,
        _cursor_sender: futures::channel::mpsc::Sender<input_pipeline::CursorMessage>,
        assembly: InputPipelineAssembly,
    ) -> InputPipelineAssembly {
        fx_log_warn!("fxbug.dev/86554: add_mouse_handler() not implemented");

        // TODO(fxbug.dev/86554): need to implement a "FlatlandMouseHandler" type that uses the
        // injector APIs.
        /*
        let logical_size = self.layout_info.logical_size.unwrap();
        let pixel_scale = self.layout_info.pixel_scale.unwrap();

        let width_pixels : f32 = (logical_size.width * pixel_scale.width) as f32;
        let height_pixels : f32 = (logical_size.height * pixel_scale.height) as f32;
        */
        assembly
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
        scenic: ui_scenic::ScenicProxy,
        display: ui_comp::FlatlandDisplayProxy,
        root_flatland: ui_comp::FlatlandProxy,
        pointerinjector_flatland: ui_comp::FlatlandProxy,
        scene_flatland: ui_comp::FlatlandProxy,
        cursor_view_provider: ui_app::ViewProviderProxy,
    ) -> Result<Self, Error> {
        let mut id_generator = scenic::flatland::IdGenerator::new();

        // Generate unique transform/content IDs that will be used to create the sub-scenegraphs
        // in the Flatland instances managed by SceneManager.
        let cursor_transform_id = id_generator.next_transform_id();
        let cursor_viewport_content_id = id_generator.next_content_id();
        let pointerinjector_viewport_transform_id = id_generator.next_transform_id();
        let pointerinjector_viewport_content_id = id_generator.next_content_id();
        let a11y_viewport_transform_id = id_generator.next_transform_id();
        let a11y_viewport_content_id = id_generator.next_content_id();

        root_flatland.set_debug_name("SceneManager Display")?;
        pointerinjector_flatland.set_debug_name("SceneManager PointerInjector")?;
        scene_flatland.set_debug_name("SceneManager Scene")?;

        let mut root_view_creation_pair = scenic::flatland::LinkTokenPair::new()?;
        let root_flatland = FlatlandInstance::new(
            root_flatland,
            root_view_creation_pair.view_creation_token,
            &mut id_generator,
        )?;

        let mut cursor_view_creation_pair = scenic::flatland::LinkTokenPair::new()?;
        cursor_view_provider
            .create_view2(ui_app::CreateView2Args {
                view_creation_token: Some(cursor_view_creation_pair.view_creation_token),
                ..ui_app::CreateView2Args::EMPTY
            })
            .expect("fidl error");

        let mut pointerinjector_view_creation_pair = scenic::flatland::LinkTokenPair::new()?;
        let pointerinjector_flatland = FlatlandInstance::new(
            pointerinjector_flatland,
            pointerinjector_view_creation_pair.view_creation_token,
            &mut id_generator,
        )?;

        let mut scene_view_creation_pair = scenic::flatland::LinkTokenPair::new()?;
        let scene_flatland = FlatlandInstance::new(
            scene_flatland,
            scene_view_creation_pair.view_creation_token,
            &mut id_generator,
        )?;

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

        // Create the root transform and two permanent children, one for the cursor, and one
        // to hold the viewport to the pointerinjector view.  Create the viewport, and
        // set it on the transform.
        {
            let flatland = root_flatland.flatland.lock();
            flatland.create_transform(&mut pointerinjector_viewport_transform_id.clone())?;
            flatland.add_child(
                &mut root_flatland.root_transform_id.clone(),
                &mut pointerinjector_viewport_transform_id.clone(),
            )?;

            let link_properties = ui_comp::ViewportProperties {
                logical_size: Some(layout_info.logical_size.unwrap()),
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
        }

        // Bridge the pointerinjector and scene Flatland instances.
        //
        // This direct connection may later be replaced by interposing a Flatland sub-scene owned by
        // the a11y manager.
        {
            let flatland = pointerinjector_flatland.flatland.lock();
            flatland.create_transform(&mut &mut a11y_viewport_transform_id.clone())?;
            flatland.add_child(
                &mut pointerinjector_flatland.root_transform_id.clone(),
                &mut a11y_viewport_transform_id.clone(),
            )?;

            let link_properties = ui_comp::ViewportProperties {
                logical_size: Some(layout_info.logical_size.unwrap()),
                ..ui_comp::ViewportProperties::EMPTY
            };

            let (_, child_view_watcher_request) =
                create_proxy::<ui_comp::ChildViewWatcherMarker>()?;

            flatland.create_viewport(
                &mut a11y_viewport_content_id.clone(),
                &mut scene_view_creation_pair.viewport_creation_token,
                link_properties,
                child_view_watcher_request,
            )?;
            flatland.set_content(
                &mut a11y_viewport_transform_id.clone(),
                &mut a11y_viewport_content_id.clone(),
            )?;
        }

        // Start Present() loops for both Flatland instances, and request that both be presented.
        let (root_flatland_presentation_sender, root_receiver) = unbounded();
        scene_manager::start_flatland_presentation_loop(
            root_receiver,
            Arc::downgrade(&root_flatland.flatland),
        );
        let (pointerinjector_flatland_presentation_sender, pointerinjector_receiver) = unbounded();
        scene_manager::start_flatland_presentation_loop(
            pointerinjector_receiver,
            Arc::downgrade(&pointerinjector_flatland.flatland),
        );
        let (scene_flatland_presentation_sender, scene_receiver) = unbounded();
        scene_manager::start_flatland_presentation_loop(
            scene_receiver,
            Arc::downgrade(&scene_flatland.flatland),
        );
        root_flatland_presentation_sender.unbounded_send(PresentationMessage::Present)?;
        pointerinjector_flatland_presentation_sender
            .unbounded_send(PresentationMessage::Present)?;
        scene_flatland_presentation_sender.unbounded_send(PresentationMessage::Present)?;

        let viewport_hanging_get: Arc<Mutex<InjectorViewportHangingGet>> = {
            let notify_fn: InjectorViewportChangeFn = Box::new(|viewport_spec, responder| {
                if let Err(fidl_error) = responder.send((*viewport_spec).into()) {
                    fx_log_info!("Viewport hanging get notification, FIDL error: {}", fidl_error);
                }
                // TODO(fxbug.dev/87670): the HangingGet docs don't explain what value to return.
                true
            });

            Arc::new(Mutex::new(hanging_get::HangingGet::new(
                InjectorViewportSpec::try_from(layout_info.clone())?,
                notify_fn,
            )))
        };
        let viewport_publisher = Arc::new(Mutex::new(viewport_hanging_get.lock().new_publisher()));

        let context_view_ref = scenic::duplicate_view_ref(&root_flatland.view_ref)?;
        let target_view_ref = scenic::duplicate_view_ref(&pointerinjector_flatland.view_ref)?;

        // Query display info from Scenic, and compute `DisplayMetrics`.
        let display_metrics = {
            let display_info = scenic.get_display_info().await?;
            let size_in_pixels = Size {
                width: display_info.width_in_px as f32,
                height: display_info.height_in_px as f32,
            };
            DisplayMetrics::new(
                size_in_pixels,
                /* density_in_pixels_per_mm */ None,
                /* viewing_distance */ None,
                /* display_rotation */ None,
            )
        };

        Ok(FlatlandSceneManager {
            _display: display,
            layout_info,
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
            cursor_transform_id,
            cursor_visibility: true,
            display_metrics,
        })
    }
}
