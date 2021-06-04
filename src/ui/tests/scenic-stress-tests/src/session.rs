// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::*, fidl_fuchsia_ui_gfx as fgfx, fidl_fuchsia_ui_scenic as fscenic,
    fidl_fuchsia_ui_views as fviews, fuchsia_async as fasync, fuchsia_scenic as scenic,
    futures::StreamExt, log::debug, rand::prelude::SliceRandom, rand::rngs::SmallRng, rand::Rng,
    std::sync::Arc,
};

pub const DISPLAY_WIDTH: u16 = 1024;
pub const DISPLAY_HEIGHT: u16 = 600;

pub const MIN_CIRCLE_RADIUS: u16 = 5;
pub const MAX_CIRCLE_RADIUS: u16 = 100;

fn create_view_holder(
    session: scenic::SessionPtr,
    view_holder_token: fviews::ViewHolderToken,
) -> scenic::ViewHolder {
    let view_holder = scenic::ViewHolder::new(session, view_holder_token, None);

    let view_properties = fgfx::ViewProperties {
        bounding_box: fgfx::BoundingBox {
            min: fgfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            max: fgfx::Vec3 { x: DISPLAY_HEIGHT as f32, y: DISPLAY_WIDTH as f32, z: 0.0 },
        },
        inset_from_min: fgfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
        inset_from_max: fgfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
        focus_change: true,
        downward_input: true,
    };
    view_holder.set_view_properties(view_properties);

    view_holder
}

// In a new task, indefinitely call Session::Present2() for a given session
fn autopresent(session: scenic::SessionPtr) -> fasync::Task<()> {
    fasync::Task::spawn(async move {
        let mut stream = {
            let session = session.lock();
            session.take_event_stream()
        };

        // Call present for the first time
        let future = {
            let mut session = session.lock();
            session.present2(0, 0)
        };
        future.await.unwrap();

        // Wait for a frame to be presented
        while let Some(Ok(_)) = stream.next().await {
            // Then call present
            let future = {
                let mut session = session.lock();
                session.present2(0, 0)
            };
            future.await.unwrap();
        }
    })
}

// In a new task, listen for session events and print them out
fn autolisten(listener: ServerEnd<fscenic::SessionListenerMarker>) -> fasync::Task<()> {
    fasync::Task::spawn(async move {
        let mut stream = listener.into_stream().unwrap();
        while let Some(Ok(request)) = stream.next().await {
            match request {
                fscenic::SessionListenerRequest::OnScenicEvent { events, control_handle: _ } => {
                    for event in events {
                        match event {
                            fscenic::Event::Gfx(e) => debug!("Received GFX event: {:#?}", e),
                            fscenic::Event::Input(e) => debug!("Received input event: {:#?}", e),
                            _ => {}
                        }
                    }
                }
                fscenic::SessionListenerRequest::OnScenicError { error, control_handle: _ } => {
                    panic!("Unexpected scenic error: {}", error);
                }
            }
        }
    })
}

fn create_session(
    scenic: Arc<fscenic::ScenicProxy>,
) -> (scenic::SessionPtr, fasync::Task<()>, fasync::Task<()>) {
    let (session_proxy, session_server) = create_proxy::<fscenic::SessionMarker>().unwrap();
    let (listener_client, listener_server) =
        create_endpoints::<fscenic::SessionListenerMarker>().unwrap();
    scenic
        .create_session2(session_server, Some(listener_client), None)
        .expect("Could not create new session");
    let session = scenic::Session::new(session_proxy);
    let present_task = autopresent(session.clone());
    let listener_task = autolisten(listener_server);

    (session, present_task, listener_task)
}

/// Contains all scenic elements needed to render a simple circle.
/// Describes the radius, color and translation of the circle.
pub struct Circle {
    shape_node: scenic::ShapeNode,
    _material: scenic::Material,
    _circle: scenic::Circle,
}

impl Circle {
    fn new(rng: &mut SmallRng, session: scenic::SessionPtr) -> Self {
        // Create a circle with random radius and translation
        let shape_node = scenic::ShapeNode::new(session.clone());
        let radius = rng.gen_range(MIN_CIRCLE_RADIUS, MAX_CIRCLE_RADIUS);
        let circle = scenic::Circle::new(session.clone(), radius as f32);
        let material = scenic::Material::new(session.clone());
        material.set_color(fgfx::ColorRgba { red: 255, blue: 0, green: 0, alpha: 255 });
        shape_node.set_shape(&circle);
        shape_node.set_material(&material);

        let x = rng.gen_range(0, DISPLAY_WIDTH);
        let y = rng.gen_range(0, DISPLAY_HEIGHT);
        shape_node.set_translation(x as f32, y as f32, 0.0);

        Self { shape_node, _material: material, _circle: circle }
    }
}

/// Represents a single Session and all comprising elements.
///
/// The root session contains the basic elements needed to render a complete scene.
/// Child sessions contain a simple view that is attached to a parent session's viewholder.
pub enum Session {
    Root {
        scenic: Arc<fscenic::ScenicProxy>,
        session: scenic::SessionPtr,
        compositor: scenic::DisplayCompositor,
        layer_stack: scenic::LayerStack,
        layer: scenic::Layer,
        renderer: scenic::Renderer,
        camera: scenic::Camera,
        light: scenic::AmbientLight,
        scene: scenic::Scene,
        circle: Circle,
        child_sessions: Vec<(Session, scenic::ViewHolder)>,
        _present_task: fasync::Task<()>,
        _listener_task: fasync::Task<()>,
    },
    Child {
        scenic: Arc<fscenic::ScenicProxy>,
        session: scenic::SessionPtr,
        view: scenic::View,
        circle: Circle,
        child_sessions: Vec<(Session, scenic::ViewHolder)>,
        _present_task: fasync::Task<()>,
        _listener_task: fasync::Task<()>,
    },
}

impl Session {
    // Setup the root session, including all elements needed to properly render the scene.
    pub fn initialize_as_root(
        rng: &mut SmallRng,
        scenic: Arc<fscenic::ScenicProxy>,
    ) -> (Self, u32, scenic::SessionPtr) {
        let (session, _present_task, _listener_task) = create_session(scenic.clone());
        let scene = scenic::Scene::new(session.clone());

        let light = scenic::AmbientLight::new(session.clone());
        light.set_color(fgfx::ColorRgb { red: 0.0, green: 0.0, blue: 0.0 });
        scene.add_ambient_light(&light);

        let camera = scenic::Camera::new(session.clone(), &scene);

        let renderer = scenic::Renderer::new(session.clone());
        renderer.set_camera(&camera);

        let layer = scenic::Layer::new(session.clone());
        layer.set_size(DISPLAY_WIDTH as f32, DISPLAY_HEIGHT as f32);
        layer.set_renderer(&renderer);

        let layer_stack = scenic::LayerStack::new(session.clone());
        layer_stack.add_layer(&layer);

        let compositor = scenic::DisplayCompositor::new(session.clone());
        compositor.set_layer_stack(&layer_stack);
        let compositor_id = compositor.id();

        let circle = Circle::new(rng, session.clone());
        scene.add_child(&circle.shape_node);

        let session_clone = session.clone();

        let root = Self::Root {
            scenic,
            session,
            compositor,
            layer_stack,
            layer,
            renderer,
            camera,
            light,
            scene,
            circle,
            child_sessions: vec![],
            _present_task,
            _listener_task,
        };

        (root, compositor_id, session_clone)
    }

    // Create the child session. Also return the viewholder token that can be used
    // to connect this session's view to a parent's viewholder.
    fn initialize_as_child(
        rng: &mut SmallRng,
        scenic: Arc<fscenic::ScenicProxy>,
    ) -> (Self, fviews::ViewHolderToken) {
        let (session, _present_task, _listener_task) = create_session(scenic.clone());

        let token_pair = scenic::ViewTokenPair::new().unwrap();
        let ref_pair = scenic::ViewRefPair::new().unwrap();

        let view = scenic::View::new3(
            session.clone(),
            token_pair.view_token,
            ref_pair.control_ref,
            ref_pair.view_ref,
            None,
        );

        let circle = Circle::new(rng, session.clone());
        view.add_child(&circle.shape_node);

        let child = Self::Child {
            scenic,
            session,
            view,
            circle,
            child_sessions: vec![],
            _present_task,
            _listener_task,
        };

        (child, token_pair.view_holder_token)
    }

    // Add a child session + viewholder to this session
    pub fn add_child(&mut self, rng: &mut SmallRng) {
        let scenic = match self {
            Self::Root { scenic, .. } => scenic.clone(),
            Self::Child { scenic, .. } => scenic.clone(),
        };

        let (child, view_holder_token) = Self::initialize_as_child(rng, scenic);

        // Create the view holder and attach it to the parent
        match self {
            Self::Root { session, scene, child_sessions, .. } => {
                let view_holder = create_view_holder(session.clone(), view_holder_token);
                scene.add_child(&view_holder);
                child_sessions.push((child, view_holder));
            }
            Self::Child { session, view, child_sessions, .. } => {
                let view_holder = create_view_holder(session.clone(), view_holder_token);
                view.add_child(&view_holder);
                child_sessions.push((child, view_holder));
            }
        };
    }

    // Remove a random child session + viewholder from the scene graph
    pub fn delete_child(&mut self, rng: &mut SmallRng) {
        let child_sessions = match self {
            Self::Root { child_sessions, .. } => child_sessions,
            Self::Child { child_sessions, .. } => child_sessions,
        };

        let num_sessions = child_sessions.len();
        let index = rng.gen_range(0, num_sessions);
        let (_, holder) = child_sessions.remove(index);
        holder.detach();
    }

    // true if the session has child sessions. false otherwise.
    pub fn has_children(&self) -> bool {
        let child_sessions = match self {
            Self::Root { child_sessions, .. } => child_sessions,
            Self::Child { child_sessions, .. } => child_sessions,
        };

        !child_sessions.is_empty()
    }

    // Select a random child session
    pub fn get_random_child_mut(&mut self, rng: &mut SmallRng) -> &mut Session {
        let child_sessions = match self {
            Self::Root { child_sessions, .. } => child_sessions,
            Self::Child { child_sessions, .. } => child_sessions,
        };

        let (session, _) = child_sessions.choose_mut(rng).unwrap();
        session
    }
}
