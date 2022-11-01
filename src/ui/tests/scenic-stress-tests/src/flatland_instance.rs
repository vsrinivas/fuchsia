// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input_listener,
    fidl::endpoints::*,
    fidl_fuchsia_math as fmath, fidl_fuchsia_ui_composition as flatland,
    fidl_fuchsia_ui_pointer as fpointer, fidl_fuchsia_ui_views as fviews, fuchsia_async as fasync,
    fuchsia_component_test::ScopedInstance,
    fuchsia_scenic as scenic,
    futures::StreamExt,
    rand::{prelude::SliceRandom, rngs::SmallRng, Rng},
    std::sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
    tracing::debug,
};

pub const DISPLAY_WIDTH: u16 = 1024;
pub const DISPLAY_HEIGHT: u16 = 600;

fn get_next_global_id() -> u64 {
    static ID: AtomicU64 = AtomicU64::new(1);
    ID.fetch_add(1, Ordering::Relaxed)
}

pub fn clone_view_ref(view_ref: &fviews::ViewRef) -> fviews::ViewRef {
    scenic::duplicate_view_ref(&view_ref).expect("valid view_ref")
}

// Creates a flatland instance with a View and a solid fill rectangle, returning the ViewRef and
// root transform id.
async fn create_instance(
    mut token: fviews::ViewCreationToken,
    realm: &ScopedInstance,
) -> (flatland::FlatlandProxy, fviews::ViewRef, flatland::TransformId) {
    let flatland_instance = realm
        .connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()
        .expect("Failed to connect Flatland instance");
    let mut view_identity = fviews::ViewIdentityOnCreation::from(
        scenic::ViewRefPair::new().expect("failed to create ViewRefPair"),
    );
    let view_ref = clone_view_ref(&mut view_identity.view_ref);
    let (_, parent_viewport_watcher_request) =
        create_proxy::<flatland::ParentViewportWatcherMarker>().unwrap();
    flatland_instance
        .create_view2(
            &mut token,
            &mut view_identity,
            flatland::ViewBoundProtocols::EMPTY,
            parent_viewport_watcher_request,
        )
        .expect("Failure creating view");

    let mut root_transform = flatland::TransformId { value: get_next_global_id() };
    flatland_instance.create_transform(&mut root_transform).expect("fidl error");
    flatland_instance.set_root_transform(&mut root_transform).expect("fidl error");

    create_and_attach_rect(&flatland_instance, &mut root_transform);

    (flatland_instance, view_ref, root_transform)
}

// Creates a fullscreen viewport.
// Does not call present.
fn create_viewport(
    proxy: &flatland::FlatlandProxy,
    mut token: fviews::ViewportCreationToken,
    mut viewport_id: flatland::ContentId,
) {
    let (_, child_view_watcher) = create_proxy::<flatland::ChildViewWatcherMarker>()
        .expect("failed to create ChildViewWatcher endpoints");
    proxy
        .create_viewport(
            &mut viewport_id,
            &mut token,
            flatland::ViewportProperties {
                logical_size: Some(fmath::SizeU {
                    width: DISPLAY_WIDTH as u32,
                    height: DISPLAY_HEIGHT as u32,
                }),
                ..flatland::ViewportProperties::EMPTY
            },
            child_view_watcher,
        )
        .expect("Failure creating viewport");
}

// Creates a solid fill rectangle and attaches it as a child of |root_transform_id|.
// Does not call present.
fn create_and_attach_rect(
    proxy: &flatland::FlatlandProxy,
    root_transform_id: &mut flatland::TransformId,
) {
    let mut rect_transform_id = flatland::TransformId { value: get_next_global_id() };
    proxy.create_transform(&mut rect_transform_id).expect("fidl error");
    let mut rect_content_id = flatland::ContentId { value: get_next_global_id() };
    proxy.create_filled_rect(&mut rect_content_id).expect("fidl error");
    proxy.set_content(&mut rect_transform_id, &mut rect_content_id).expect("fidl error");
    proxy.add_child(root_transform_id, &mut rect_transform_id).expect("fidl error");
    proxy
        .set_solid_fill(
            &mut rect_content_id,
            &mut flatland::ColorRgba { red: 0.0, blue: 1.0, green: 0.0, alpha: 1.0 },
            &mut fmath::SizeU { width: DISPLAY_WIDTH as u32, height: DISPLAY_HEIGHT as u32 },
        )
        .expect("fidl error");
}

// Present and wait until the frame has been presented and we've receive the go-ahead for the next frame.
async fn safe_present(
    instance: &flatland::FlatlandProxy,
    stream: &mut flatland::FlatlandEventStream,
) {
    instance.present(flatland::PresentArgs::EMPTY).expect("Present call failed");
    let mut frame_presented = false;
    let mut next_frame_begun = false;
    loop {
        match stream
            .next()
            .await
            .expect("Received 'nothing' from flatland stream")
            .expect("Fidl error")
        {
            flatland::FlatlandEvent::OnNextFrameBegin { .. } => {
                debug!("OnNextFrameBegin");
                next_frame_begun = true;
            }
            flatland::FlatlandEvent::OnFramePresented { .. } => {
                debug!("OnFramePresented");
                frame_presented = true;
            }
            flatland::FlatlandEvent::OnError { error } => {
                panic!("error in safe_present: {:?}", error);
            }
        };
        if frame_presented && next_frame_begun {
            return;
        }
    }
}

// In a new task, indefinitely call Flatland::Present() for a given instance.
fn autopresent(
    proxy: Arc<flatland::FlatlandProxy>,
    mut stream: flatland::FlatlandEventStream,
) -> fasync::Task<()> {
    fasync::Task::spawn(async move {
        loop {
            safe_present(&*proxy, &mut stream).await
        }
    })
}

/// Represents a single flatland instance and all comprising elements.
///
/// The Root instance contains the elements needed to render a scene and inject input events.
/// The instances form a tree starting at the singular Root instance, where each Child instance's
/// view is attached below their parent's root_transform.
pub enum FlatlandInstance {
    Root {
        flatland_display: flatland::FlatlandDisplayProxy,
        root_instance: Arc<flatland::FlatlandProxy>,
        target_instance: Arc<flatland::FlatlandProxy>,
        target_root_transform: flatland::TransformId,
        child_instances: Vec<(FlatlandInstance, flatland::ContentId)>,
        _root_present_task: fasync::Task<()>,
        _target_present_task: fasync::Task<()>,
    },
    Child {
        instance: Arc<flatland::FlatlandProxy>,
        root_transform: flatland::TransformId,
        child_instances: Vec<(FlatlandInstance, flatland::ContentId)>,
        _present_task: fasync::Task<()>,
        _touch_listener_task: fasync::Task<()>,
    },
}

impl FlatlandInstance {
    // Setup the root of the scene graph. This is the resulting the view tree:
    //         root view
    //             |
    //  input injection target view
    //             |
    // [subtree will be placed here]
    //
    // Returns the view refs of the root view and the input injection target view.
    pub async fn new_root(realm: &ScopedInstance) -> (Self, fviews::ViewRef, fviews::ViewRef) {
        let fuchsia_scenic::flatland::ViewCreationTokenPair {
            view_creation_token: root_view_creation_token,
            viewport_creation_token: mut root_viewport_creation_token,
        } = fuchsia_scenic::flatland::ViewCreationTokenPair::new()
            .expect("failed to create token pair");

        let flatland_display = realm
            .connect_to_protocol_at_exposed_dir::<flatland::FlatlandDisplayMarker>()
            .expect("Failed to connect Flatland display. Called new_root more than once?");
        {
            // Hook up root view to display.
            let (_, child_view_watcher) = create_proxy::<flatland::ChildViewWatcherMarker>()
                .expect("failed to create ChildViewWatcher endpoints");
            flatland_display
                .set_content(&mut root_viewport_creation_token, child_view_watcher)
                .expect("Failure setting the display");
        }

        let (root_instance, root_view_ref, mut root_transform_id) =
            create_instance(root_view_creation_token, &realm).await;

        // Set up the input injection target view.
        let fuchsia_scenic::flatland::ViewCreationTokenPair {
            view_creation_token: target_view_creation_token,
            viewport_creation_token: target_viewport_creation_token,
        } = fuchsia_scenic::flatland::ViewCreationTokenPair::new()
            .expect("failed to create token pair");
        let (target_instance, target_view_ref, target_root_transform) =
            create_instance(target_view_creation_token, &realm).await;
        let mut viewport_content_id = flatland::ContentId { value: get_next_global_id() };
        create_viewport(
            &root_instance,
            target_viewport_creation_token,
            viewport_content_id.clone(),
        );

        // Attach target view to root.
        root_instance
            .set_content(&mut root_transform_id, &mut viewport_content_id)
            .expect("fidl error");

        // Make initial present calls and set up autopresenters.
        let mut root_stream = root_instance.take_event_stream();
        let root_instance = Arc::new(root_instance);
        safe_present(&root_instance, &mut root_stream).await;
        let _root_present_task = { autopresent(Arc::clone(&root_instance), root_stream) };

        let mut target_stream = target_instance.take_event_stream();
        let target_instance = Arc::new(target_instance);
        safe_present(&target_instance, &mut target_stream).await;
        let _target_present_task = { autopresent(Arc::clone(&target_instance), target_stream) };

        let root = Self::Root {
            flatland_display,
            root_instance,
            target_instance,
            target_root_transform,
            child_instances: Vec::new(),
            _root_present_task,
            _target_present_task,
        };

        (root, root_view_ref, target_view_ref)
    }

    // Create a child instance.
    async fn new_child(realm: &ScopedInstance) -> (Self, fviews::ViewportCreationToken) {
        let flatland_instance = realm
            .connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()
            .expect("Failed to connect Flatland instance");
        let fuchsia_scenic::flatland::ViewCreationTokenPair {
            mut view_creation_token,
            viewport_creation_token,
        } = fuchsia_scenic::flatland::ViewCreationTokenPair::new()
            .expect("failed to create token pair");
        let mut view_identity = fviews::ViewIdentityOnCreation::from(
            scenic::ViewRefPair::new().expect("failed to create ViewRefPair"),
        );
        let (_, parent_viewport_watcher) =
            create_proxy::<flatland::ParentViewportWatcherMarker>().unwrap();

        let (touch_source, touch_source_request) = create_proxy::<fpointer::TouchSourceMarker>()
            .expect("failed to create TouchSource channels");
        let protocols = flatland::ViewBoundProtocols {
            touch_source: Some(touch_source_request),
            ..flatland::ViewBoundProtocols::EMPTY
        };
        flatland_instance
            .create_view2(
                &mut view_creation_token,
                &mut view_identity,
                protocols,
                parent_viewport_watcher,
            )
            .expect("Failure creating view");

        let mut root_transform = flatland::TransformId { value: get_next_global_id() };
        flatland_instance.create_transform(&mut root_transform).expect("fidl error");
        flatland_instance.set_root_transform(&mut root_transform).expect("fidl error");

        let mut stream = flatland_instance.take_event_stream();
        let flatland_instance = Arc::new(flatland_instance);
        safe_present(&flatland_instance, &mut stream).await;
        let _present_task = { autopresent(Arc::clone(&flatland_instance), stream) };

        let child = Self::Child {
            instance: flatland_instance,
            root_transform,
            child_instances: Vec::new(),
            _present_task,
            _touch_listener_task: input_listener::autolisten_touch(touch_source),
        };

        (child, viewport_creation_token)
    }

    // Create a new instance and attach it as a child of self.
    pub async fn add_child(&mut self, realm: &ScopedInstance) {
        let (child, viewport_creation_token) = Self::new_child(realm).await;

        let (instance, root_transform, child_instances): (
            &flatland::FlatlandProxy,
            &mut flatland::TransformId,
            &mut Vec<(FlatlandInstance, flatland::ContentId)>,
        ) = match self {
            Self::Root { target_instance, target_root_transform, child_instances, .. } => {
                (&*target_instance, target_root_transform, child_instances)
            }
            Self::Child { instance, root_transform, child_instances, .. } => {
                (&*instance, root_transform, child_instances)
            }
        };

        let mut viewport_content_id = flatland::ContentId { value: get_next_global_id() };
        let mut viewport_transform_id = flatland::TransformId { value: get_next_global_id() };
        create_viewport(&*instance, viewport_creation_token, viewport_content_id.clone());
        instance.create_transform(&mut viewport_transform_id).expect("fidl error");
        instance.add_child(root_transform, &mut viewport_transform_id).expect("fidl error");
        instance
            .set_content(&mut viewport_transform_id, &mut viewport_content_id)
            .expect("fidl error");
        child_instances.push((child, viewport_content_id));
    }

    // Remove a random child instance from the scene graph (and kill it).
    pub async fn delete_child(&mut self, rng: &mut SmallRng) {
        let (instance, children) = match self {
            Self::Root { target_instance, child_instances, .. } => {
                (target_instance, child_instances)
            }
            Self::Child { instance, child_instances, .. } => (instance, child_instances),
        };

        let num_sessions = children.len();
        let index = rng.gen_range(0..num_sessions);
        let (_child_instance, mut viewport_content_id) = children.remove(index);
        instance.release_viewport(&mut viewport_content_id).await.expect("fidl error");
    }

    pub fn has_children(&self) -> bool {
        let children = match self {
            Self::Root { child_instances, .. } => child_instances,
            Self::Child { child_instances, .. } => child_instances,
        };
        !children.is_empty()
    }

    // Select a random child instance of self.
    pub fn get_random_child_mut(&mut self, rng: &mut SmallRng) -> &mut FlatlandInstance {
        let children = match self {
            Self::Root { child_instances, .. } => child_instances,
            Self::Child { child_instances, .. } => child_instances,
        };

        let (child, _) = children.choose_mut(rng).unwrap();
        child
    }
}
