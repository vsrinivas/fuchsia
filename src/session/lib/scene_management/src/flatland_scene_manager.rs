// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::scene_manager::{self, PresentationMessage, PresentationSender, SceneManager},
    anyhow::Error,
    async_trait::async_trait,
    fidl,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_ui_app as ui_app,
    fidl_fuchsia_ui_composition::{self as ui_comp, ContentId, TransformId},
    fidl_fuchsia_ui_views as ui_views, fuchsia_scenic as scenic, fuchsia_scenic,
    fuchsia_syslog::fx_log_warn,
    futures::channel::mpsc::unbounded,
    input_pipeline::input_pipeline::InputPipelineAssembly,
    parking_lot::Mutex,
    std::sync::Arc,
};

pub type FlatlandPtr = Arc<Mutex<ui_comp::FlatlandProxy>>;

pub struct TransformContentIdPair {
    transform_id: TransformId,
    content_id: ContentId,
}

pub struct FlatlandSceneManager {
    // Flatland connection between the physical display and the rest of the scene graph.
    pub display: ui_comp::FlatlandDisplayProxy,

    // Layout info received from the display.
    layout_info: ui_comp::LayoutInfo,

    // Flatland instance that connects to |display|.  Hosts a viewport which connects it either to a
    // view in |scene_flatland| by default, or a view provided by the accessibility manager via
    // |insert_a11y_view2()|.
    pub root_flatland: FlatlandPtr,

    // Flatland instance that embeds the system shell (i.e. via the SetRootView() FIDL API).  Its
    // root view is attached to a viewport in |root_flatland| by default, or a viewport owned by the
    // accessibility manager
    pub scene_flatland: FlatlandPtr,

    // Used to sent presentation requests for |root_flatand| and |scene_flatland|, respectively.
    _root_flatland_presentation_sender: PresentationSender,
    scene_flatland_presentation_sender: PresentationSender,

    scene_root_transform_id: TransformId,

    // Holds a pair of IDs that are used to embed the system shell inside |scene_flatland|, a
    // TransformId identifying a transform in the scene graph, and a ContentId which identifies a
    // a viewport that is set as the content of that transform.
    scene_root_viewport_ids: Option<TransformContentIdPair>,

    // ContentIds and TransformIds are generated sequentially by incrementing this field.  This is
    // used for IDs for both the |root_flatland| and |scene_flatland| Flatland instances; the
    // uniqueness across instances potentially makes debugging a little easier.
    next_id: u64,

    _root_parent_viewport_watcher: ui_comp::ParentViewportWatcherProxy,
    _scene_parent_viewport_watcher: ui_comp::ParentViewportWatcherProxy,

    focuser: ui_views::FocuserProxy,
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
            let locked = self.scene_flatland.lock();
            locked.set_content(&mut ids.transform_id.clone(), &mut ContentId { value: 0 })?;
            locked.remove_child(
                &mut self.scene_root_transform_id.clone(),
                &mut ids.transform_id.clone(),
            )?;
            locked.release_transform(&mut ids.transform_id.clone())?;
            let _ = locked.release_viewport(&mut ids.content_id.clone());
            self.scene_root_viewport_ids = None;
        }

        // Create new viewport.
        let ids = TransformContentIdPair {
            transform_id: self.next_transform_id(),
            content_id: self.next_content_id(),
        };
        let viewport_properties = ui_comp::ViewportProperties {
            logical_size: Some(self.layout_info.logical_size.unwrap()),
            ..ui_comp::ViewportProperties::EMPTY
        };
        let (view_ref, _child_view_watcher_proxy) = FlatlandSceneManager::create_viewport(
            self.scene_flatland.clone(),
            &ids.content_id,
            &mut link_token_pair.viewport_creation_token,
            viewport_properties,
        )
        .await?;
        {
            let locked = self.scene_flatland.lock();

            locked.create_transform(&mut ids.transform_id.clone())?;
            locked.add_child(
                &mut self.scene_root_transform_id.clone(),
                &mut ids.transform_id.clone(),
            )?;
            locked.set_content(&mut ids.transform_id.clone(), &mut ids.content_id.clone())?;
        }
        self.scene_root_viewport_ids = Some(ids);

        self.scene_flatland_presentation_sender.unbounded_send(PresentationMessage::Present)?;

        Ok(view_ref)
    }

    fn request_focus(
        &self,
        view_ref: &mut ui_views::ViewRef,
    ) -> fidl::client::QueryResponseFut<ui_views::FocuserRequestFocusResult> {
        self.focuser.request_focus(view_ref)
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

    // TODO(fxbug.dev/86554)
    fn set_cursor_position(&mut self, _position: input_pipeline::Position) {
        fx_log_warn!("fxbug.dev/86554: set_cursor_position() not implemented");
        // let screen_coordinates = ScreenCoordinates::from_pixels(position.x, position.y, self.display_metrics);
    }

    async fn add_touch_handler(&self, assembly: InputPipelineAssembly) -> InputPipelineAssembly {
        fx_log_warn!("fxbug.dev/86554: add_touch_handler() not implemented");

        // TODO(fxbug.dev/86554): need to implement a "FlatlandTouchHandler" type that uses the
        // injector APIs.
        /*
        let logical_size = self.layout_info.logical_size.unwrap();
        let pixel_scale = self.layout_info.pixel_scale.unwrap();

        let width_pixels : f32 = (logical_size.width * pixel_scale.width) as f32;
        let height_pixels : f32 = (logical_size.height * pixel_scale.height) as f32;
        */
        assembly
    }

    async fn add_mouse_handler(
        &self,
        _position_sender: futures::channel::mpsc::Sender<input_pipeline::Position>,
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
}

impl FlatlandSceneManager {
    pub async fn new(
        display: ui_comp::FlatlandDisplayProxy,
        root_flatland: ui_comp::FlatlandProxy,
        scene_flatland: ui_comp::FlatlandProxy,
    ) -> Result<Self, Error> {
        let mut next_id: u64 = 1;
        let display_root_transform_id = next_transform_id(&mut next_id);
        let cursor_transform_id = next_transform_id(&mut next_id);
        let scene_root_transform_id = next_transform_id(&mut next_id);
        let a11y_viewport_transform_id = next_transform_id(&mut next_id);
        let a11y_viewport_content_id = next_content_id(&mut next_id);

        root_flatland.set_debug_name("SceneManager Root")?;
        scene_flatland.set_debug_name("SceneManager Scene")?;

        let root_parent_viewport_watcher;
        let scene_parent_viewport_watcher;

        let mut view_bound_protocols = ui_comp::ViewBoundProtocols::EMPTY;
        let (focuser, focuser_request) = create_proxy::<ui_views::FocuserMarker>()?;
        view_bound_protocols.view_focuser = Some(focuser_request);

        // Create the FlatlandDisplay and the root Flatland instance, and connect them.
        let layout_info = {
            let mut link_token_pair = scenic::flatland::LinkTokenPair::new()?;

            let (_, child_view_watcher_request) =
                create_proxy::<ui_comp::ChildViewWatcherMarker>()?;

            let (parent_viewport_watcher_proxy, parent_viewport_watcher_request) =
                create_proxy::<ui_comp::ParentViewportWatcherMarker>()?;

            display.set_content(
                &mut link_token_pair.viewport_creation_token,
                child_view_watcher_request,
            )?;

            let mut view_identity =
                ui_views::ViewIdentityOnCreation::from(scenic::ViewRefPair::new()?);
            root_flatland.create_view2(
                &mut link_token_pair.view_creation_token,
                &mut view_identity,
                view_bound_protocols,
                parent_viewport_watcher_request,
            )?;

            let layout = parent_viewport_watcher_proxy.get_layout().await?;

            root_parent_viewport_watcher = parent_viewport_watcher_proxy;
            layout
        };

        // Create the root transform and two permanent children, one for the cursor, and one
        // for the a11y proxy.
        {
            root_flatland.create_transform(&mut display_root_transform_id.clone())?;
            root_flatland.set_root_transform(&mut display_root_transform_id.clone())?;

            root_flatland.create_transform(&mut a11y_viewport_transform_id.clone())?;
            root_flatland.add_child(
                &mut display_root_transform_id.clone(),
                &mut a11y_viewport_transform_id.clone(),
            )?;

            root_flatland.create_transform(&mut cursor_transform_id.clone())?;
            root_flatland.add_child(
                &mut display_root_transform_id.clone(),
                &mut cursor_transform_id.clone(),
            )?;
        }

        // Bridge the root and scene Flatland instances.  This direct connection may later be replaced
        // by interposing a Flatland sub-scene owned by the a11y manager.
        {
            let mut link_token_pair = scenic::flatland::LinkTokenPair::new()?;

            let link_properties = ui_comp::ViewportProperties {
                logical_size: Some(layout_info.logical_size.unwrap()),
                ..ui_comp::ViewportProperties::EMPTY
            };

            let (_, child_view_watcher_request) =
                create_proxy::<ui_comp::ChildViewWatcherMarker>()?;

            root_flatland.create_viewport(
                &mut a11y_viewport_content_id.clone(),
                &mut link_token_pair.viewport_creation_token,
                link_properties,
                child_view_watcher_request,
            )?;
            root_flatland.set_content(
                &mut a11y_viewport_transform_id.clone(),
                &mut a11y_viewport_content_id.clone(),
            )?;

            // TODO(fxbug.dev/86379): we probably want to listen for viewport events, maybe not at
            // first, but when a11y manager connects and inserts its own scene content.
            let (parent_viewport_watcher_proxy, parent_viewport_watcher_request) =
                create_proxy::<ui_comp::ParentViewportWatcherMarker>()?;

            // TODO(fxbug.dev/86554): use create_view2() to hook up to input
            scene_flatland.create_view(
                &mut link_token_pair.view_creation_token,
                parent_viewport_watcher_request,
            )?;

            scene_parent_viewport_watcher = parent_viewport_watcher_proxy;
        }

        // Set root transform
        {
            scene_flatland.create_transform(&mut scene_root_transform_id.clone())?;
            scene_flatland.set_root_transform(&mut scene_root_transform_id.clone())?;
        }

        let root_flatland: FlatlandPtr = Arc::new(Mutex::new(root_flatland));
        let scene_flatland: FlatlandPtr = Arc::new(Mutex::new(scene_flatland));

        // Start Present() loops for both Flatland instances, and request that both be presented.
        let (root_flatland_presentation_sender, root_receiver) = unbounded();
        scene_manager::start_flatland_presentation_loop(
            root_receiver,
            Arc::downgrade(&root_flatland),
        );
        let (scene_flatland_presentation_sender, scene_receiver) = unbounded();
        scene_manager::start_flatland_presentation_loop(
            scene_receiver,
            Arc::downgrade(&scene_flatland),
        );
        root_flatland_presentation_sender.unbounded_send(PresentationMessage::Present)?;
        scene_flatland_presentation_sender.unbounded_send(PresentationMessage::Present)?;

        Ok(FlatlandSceneManager {
            display,
            layout_info,
            root_flatland,
            scene_flatland,
            _root_flatland_presentation_sender: root_flatland_presentation_sender,
            scene_flatland_presentation_sender,
            scene_root_transform_id,
            scene_root_viewport_ids: None,
            next_id,
            _root_parent_viewport_watcher: root_parent_viewport_watcher,
            _scene_parent_viewport_watcher: scene_parent_viewport_watcher,
            focuser,
        })
    }

    async fn create_viewport(
        flatland: FlatlandPtr,
        viewport_id: &ContentId,
        viewport_creation_token: &mut ui_views::ViewportCreationToken,
        properties: ui_comp::ViewportProperties,
    ) -> Result<(ui_views::ViewRef, ui_comp::ChildViewWatcherProxy), Error> {
        let (child_view_watcher_proxy, child_view_watcher_request) =
            create_proxy::<ui_comp::ChildViewWatcherMarker>()?;

        flatland.lock().create_viewport(
            &mut viewport_id.clone(),
            viewport_creation_token,
            properties,
            child_view_watcher_request,
        )?;

        let view_ref = child_view_watcher_proxy.get_view_ref().await?;
        Ok((view_ref, child_view_watcher_proxy))
    }

    fn next_content_id(&mut self) -> ContentId {
        next_content_id(&mut self.next_id)
    }

    fn next_transform_id(&mut self) -> TransformId {
        next_transform_id(&mut self.next_id)
    }
}

fn next_content_id(next_id: &mut u64) -> ContentId {
    let id = *next_id;
    *next_id += 1;
    ContentId { value: id }
}

fn next_transform_id(next_id: &mut u64) -> TransformId {
    let id = *next_id;
    *next_id += 1;
    TransformId { value: id }
}
