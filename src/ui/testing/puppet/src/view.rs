// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::presentation_loop,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_math as fmath, fidl_fuchsia_ui_composition as ui_comp,
    fidl_fuchsia_ui_views as ui_views, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_scenic as scenic,
    futures::channel::{mpsc, oneshot},
    once_cell::unsync::OnceCell,
    parking_lot::Mutex,
    std::rc::Rc,
    tracing::info,
};

pub type FlatlandPtr = Rc<ui_comp::FlatlandProxy>;

/// Helper function to open a connection to flatland and associate a presentation loop with it.
fn connect_to_flatland_and_presenter() -> (FlatlandPtr, presentation_loop::PresentationSender) {
    let flatland_proxy =
        connect_to_protocol::<ui_comp::FlatlandMarker>().expect("failed to connect to flatland");
    let flatland = Rc::new(flatland_proxy);
    let (presentation_sender, presentation_receiver) = mpsc::unbounded();
    presentation_loop::start_flatland_presentation_loop(
        presentation_receiver,
        Rc::downgrade(&flatland),
    );

    (flatland, presentation_sender)
}

/// Helper function to request to present a set of changes to flatland.
async fn request_present(presentation_sender: &presentation_loop::PresentationSender) {
    let (sender, receiver) = oneshot::channel::<()>();
    presentation_sender.unbounded_send(sender).expect("failed to request present");
    _ = receiver.await;
}

/// Encapsulates capabilities and resources associated with a puppet's view.
pub(super) struct View {
    /// Flatland connection scoped to our view.
    #[allow(dead_code)]
    flatland: FlatlandPtr,

    /// Task to poll continuously for view events, and respond as necessary.
    _event_listener: OnceCell<fasync::Task<()>>,

    /// Used to present changes to flatland.
    #[allow(dead_code)]
    presentation_sender: presentation_loop::PresentationSender,

    /// Used to generate flatland transform and content IDs.
    #[allow(dead_code)]
    id_generator: scenic::flatland::IdGenerator,

    /// Flatland `TransformId` that corresponds to our view's root transform.
    #[allow(dead_code)]
    root_transform_id: ui_comp::TransformId,

    /// View dimensions, in its own logical coordinate space.
    logical_size: fmath::SizeU,

    /// DPR used to convert between logical and physical coordinates.
    device_pixel_ratio: f32,

    /// Indicates whether our view is connected to the display.
    connected_to_display: bool,
}

impl View {
    pub async fn new(mut view_creation_token: ui_views::ViewCreationToken) -> Rc<Mutex<Self>> {
        let (flatland, presentation_sender) = connect_to_flatland_and_presenter();
        let mut id_generator = scenic::flatland::IdGenerator::new();

        // Create view parameters.
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            create_proxy::<ui_comp::ParentViewportWatcherMarker>()
                .expect("failed to create parent viewport watcher channel");
        let view_bound_protocols =
            ui_comp::ViewBoundProtocols { ..ui_comp::ViewBoundProtocols::EMPTY };
        let mut view_identity = ui_views::ViewIdentityOnCreation::from(
            scenic::ViewRefPair::new().expect("failed to create view ref pair"),
        );

        // Create root transform ID.
        let root_transform_id = Self::create_transform(flatland.clone(), &mut id_generator);

        // Create the view and present.
        flatland
            .create_view2(
                &mut view_creation_token,
                &mut view_identity,
                view_bound_protocols,
                parent_viewport_watcher_request,
            )
            .expect("failed to create view");
        flatland
            .set_root_transform(&mut root_transform_id.clone())
            .expect("failed to set root transform");
        request_present(&presentation_sender).await;

        let this = Rc::new(Mutex::new(Self {
            flatland,
            _event_listener: OnceCell::new(),
            presentation_sender,
            id_generator,
            root_transform_id,
            logical_size: fmath::SizeU { width: 0, height: 0 },
            device_pixel_ratio: 0.,
            connected_to_display: false,
        }));

        let task = fasync::Task::local(Self::listen_for_view_events(
            this.clone(),
            parent_viewport_watcher,
        ));
        this.lock()._event_listener.set(task).expect("set event listener task more than once");

        this
    }

    /// Polls continuously for events reported to the view (parent viewport updates,
    /// touch/mouse/keyboard input, etc.).
    async fn listen_for_view_events(
        this: Rc<Mutex<Self>>,
        parent_viewport_watcher: ui_comp::ParentViewportWatcherProxy,
    ) {
        loop {
            futures::select! {
                maybe_parent_status = parent_viewport_watcher.get_status() => {
                    match maybe_parent_status {
                        Err(_) => {
                            panic!("error from parent viewport watcher on get_status");
                        }
                        Ok(parent_status) => {
                            info!("received parent status update");
                            this.lock().update_parent_status(parent_status);
                        }
                    }
                }
                maybe_layout_info = parent_viewport_watcher.get_layout() => {
                    match maybe_layout_info {
                        Err(_) => {
                            panic!("error from parent viewport watcher on get_layout");
                        }
                        Ok(ui_comp::LayoutInfo { logical_size, device_pixel_ratio, .. }) => {
                            this.lock().update_view_parameters(logical_size, device_pixel_ratio);
                        }
                    }
                }
            }
        }
    }

    /// Creates a flatland transform and returns its `TransformId`.
    fn create_transform(
        flatland: FlatlandPtr,
        id_generator: &mut scenic::flatland::IdGenerator,
    ) -> ui_comp::TransformId {
        let flatland_transform_id = id_generator.next_transform_id();

        flatland
            .create_transform(&mut flatland_transform_id.clone())
            .expect("failed to create transform");

        flatland_transform_id
    }

    /// Helper method to update our book keeping on our view's spatial parameters.
    fn update_view_parameters(
        &mut self,
        logical_size: Option<fmath::SizeU>,
        device_pixel_ratio: Option<fmath::VecF>,
    ) {
        if let Some(size) = logical_size {
            self.logical_size = size;
        }

        if let Some(dpr) = device_pixel_ratio {
            assert!(dpr.x == dpr.y);
            self.device_pixel_ratio = dpr.x;
        }
    }

    /// Helper method to update our book keeping on the parent viewport's status.
    fn update_parent_status(&mut self, parent_status: ui_comp::ParentViewportStatus) {
        self.connected_to_display = match parent_status {
            ui_comp::ParentViewportStatus::ConnectedToDisplay => true,
            ui_comp::ParentViewportStatus::DisconnectedFromDisplay => false,
        };
    }
}
