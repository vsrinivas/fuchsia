// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl::endpoints::{create_proxy, create_request_stream};
use fidl_fuchsia_ui_app::{ViewProviderRequest, ViewProviderRequestStream};
use fidl_fuchsia_ui_gfx::{ColorRgba, Event as GfxEvent, ViewProperties};
use fidl_fuchsia_ui_input::{InputEvent, PointerEventPhase};
use fidl_fuchsia_ui_scenic::{
    Event, ScenicMarker, ScenicProxy, SessionListenerMarker, SessionListenerRequest,
    SessionListenerRequestStream, SessionMarker,
};
use fidl_fuchsia_ui_views::ViewToken;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_component::server::ServiceFs;
use fuchsia_scenic::{
    Circle, EntityNode, Material, Rectangle, Session, SessionPtr, ShapeNode, View,
};
use fuchsia_syslog::{self, fx_log_err, fx_log_info};
use futures::{StreamExt, TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::sync::Arc;

const INITIAL_CIRCLE_POS_X: f32 = 0.12;
const INITIAL_CIRCLE_POS_Y: f32 = 0.26;

/// The [BouncingBall] struct owns all the state required to display the bouncing balls.
struct BouncingBall {
    // Hold on to the view node to make sure Scenic won't garbage collect our scene graph.
    _view: View,
    bg_node: ShapeNode,
    circle_node: ShapeNode,

    view_width: f32,
    view_height: f32,

    // Position is in the range [0, 1] and then multiplied by (view_width_,
    // view_height_).
    circle_pos_x: f32,
    circle_pos_y: f32,
    circle_velocity_y: f32,

    // Circle's radius in logical pixels.
    circle_radius: f32,

    // Input.
    pointer_down: bool,
    pointer_id: u32,

    session: SessionPtr,
}

type BouncingBallPtr = Arc<Mutex<BouncingBall>>;

impl BouncingBall {
    /// Construct a new [BouncingBall].
    fn new(session: SessionPtr, view_token: ViewToken) -> Self {
        // View: Use |view_token| to create a View in the Session.
        let view = View::new(session.clone(), view_token, Some("bouncing_circle_view".into()));

        // Root Node.
        let root_node = EntityNode::new(session.clone());
        view.add_child(&root_node);

        // Background Material.
        let bg_material = Material::new(session.clone());
        // Pink A400
        bg_material.set_color(ColorRgba { red: 0xf5, green: 0x00, blue: 0x57, alpha: 0xff });

        // Background ShapeNode.
        let bg_node = ShapeNode::new(session.clone());
        bg_node.set_material(&bg_material);
        root_node.add_child(&bg_node);

        // Circle's Material.
        let circle_material = Material::new(session.clone());
        // Deep purple 500
        circle_material.set_color(ColorRgba { red: 0x67, green: 0x3a, blue: 0xb7, alpha: 0xff });

        // Circle's ShapeNode.
        let circle_node = ShapeNode::new(session.clone());
        circle_node.set_material(&circle_material);
        root_node.add_child(&circle_node);

        BouncingBall {
            _view: view,
            bg_node,
            circle_node,
            view_width: 0.0,
            view_height: 0.0,
            circle_pos_x: INITIAL_CIRCLE_POS_X,
            circle_pos_y: INITIAL_CIRCLE_POS_Y,
            circle_velocity_y: 0.0,
            circle_radius: 0.0,
            pointer_down: false,
            pointer_id: 0,
            session,
        }
    }

    /// Process a [fidl_fuchsia_ui_scenic::Event] event, such as if the window is resized, or a
    /// button is pressed.
    fn on_scenic_event(&mut self, event: Event) {
        match event {
            Event::Gfx(GfxEvent::ViewPropertiesChanged(event)) => {
                self.on_view_properties_changed(event.properties);
            }
            Event::Input(InputEvent::Pointer(event)) => match event.phase {
                PointerEventPhase::Down => {
                    self.pointer_down = true;
                    self.pointer_id = event.pointer_id;
                }
                PointerEventPhase::Up if event.pointer_id == self.pointer_id => {
                    self.pointer_down = false;
                }
                _ => {
                    // Unhandled event.
                }
            },
            _ => {
                // Unhandled event.
            }
        }
    }

    /// Process a [ViewProperties] event, which describes the new geometry of the view. This
    /// function is used to create new versions of all the shapes needed to render the example.
    fn on_view_properties_changed(&mut self, vp: ViewProperties) {
        self.view_width = (vp.bounding_box.max.x - vp.inset_from_max.x)
            - (vp.bounding_box.min.x + vp.inset_from_min.x);
        self.view_height = (vp.bounding_box.max.y - vp.inset_from_max.y)
            - (vp.bounding_box.min.y + vp.inset_from_min.y);

        // Position is relative to the View's origin system.
        let center_x = self.view_width * 0.5;
        let center_y = self.view_height * 0.5;

        // Background Shape.
        {
            let bg_shape = Rectangle::new(self.session.clone(), self.view_width, self.view_height);
            self.bg_node.set_shape(&bg_shape);

            // We release the Shape Resource here, but it continues to stay alive in Scenic because
            // it's being referenced by background ShapeNode. However, we no longer have a way to
            // reference it.
            //
            // Once the background ShapeNode no longer references this shape, because a new Shape
            // was set on it, this Shape will be destroyed internally in Scenic.
        }

        // Translate the background node.
        const BACKGROUND_ELEVATION: f32 = 0.0;
        self.bg_node.set_translation(center_x, center_y, -BACKGROUND_ELEVATION);

        // Circle Shape.
        {
            self.circle_radius = self.view_width.min(self.view_height) * 0.1;
            let circle_shape = Circle::new(self.session.clone(), self.circle_radius);
            self.circle_node.set_shape(&circle_shape);

            // We release the Shape Resource here, but it continues to stay alive in Scenic because
            // it's being referenced by circle's ShapeNode. However, we no longer have a way to
            // reference it.
            //
            // Once the background ShapeNode no longer references this shape, because a new Shape
            // was set on it, this Shape will be destroyed internally in Scenic.
        }

        // The commands won't actually get committed until session.present() is
        // called. However, since we're animating every frame, in this case we can
        // assume present() will be called shortly.
    }

    /// Advance the simulation of the bouncing balls by `t` seconds.
    fn on_present(&mut self, t: f32) {
        if self.pointer_down {
            // Move back to near initial position and velocity when there's a pointer
            // down event.
            self.circle_pos_x = INITIAL_CIRCLE_POS_X;
            self.circle_pos_y = INITIAL_CIRCLE_POS_Y;
            self.circle_velocity_y = 0.0;
            return;
        }

        const Y_ACCELERATION: f32 = 3.0;
        self.circle_velocity_y += Y_ACCELERATION * t;

        const CIRCLE_VELOCITY_X: f32 = 0.2;
        self.circle_pos_x += CIRCLE_VELOCITY_X * t;
        self.circle_pos_y += (self.circle_velocity_y * t).min(1.0);

        if self.circle_pos_y > 1.0 {
            // Bounce.
            self.circle_velocity_y *= -0.8;
            self.circle_pos_y = 1.0;
        }
        if self.circle_pos_y >= 0.999 && self.circle_velocity_y.abs() < 0.015 {
            // If the circle stops bouncing, start the simulation again.
            self.circle_pos_y = 0.0;
            self.circle_velocity_y = 0.0;
        }
        if self.circle_pos_x > 1.0 {
            // Wrap the x position.
            self.circle_pos_x = self.circle_pos_x % 1.0;
        }

        let circle_pos_x_absolute = self.circle_pos_x * self.view_width;
        let circle_pos_y_absolute = self.circle_pos_y * self.view_height - self.circle_radius;

        // Translate the circle's node.
        const CIRCLE_ELEVATION: f32 = 8.0;
        self.circle_node.set_translation(
            circle_pos_x_absolute,
            circle_pos_y_absolute,
            -CIRCLE_ELEVATION,
        );
    }
}

/// This function receives events from the session listener and routes them to the [BouncingBall]
/// appropriate handler method.
async fn session_listener_service(
    view: BouncingBallPtr,
    mut stream: SessionListenerRequestStream,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            SessionListenerRequest::OnScenicError { error, control_handle: _ } => {
                fx_log_err!("scenic error: {}", error);
            }
            SessionListenerRequest::OnScenicEvent { events, control_handle: _ } => {
                for event in events {
                    view.lock().on_scenic_event(event);
                }
            }
        }
    }

    Ok(())
}

/// This function loops forever to display the bouncing ball view.
async fn present_view(session: SessionPtr, view: BouncingBallPtr) -> Result<(), Error> {
    let mut last_presentation_time = 0;
    let mut next_presentation_time = 0;

    loop {
        // Apply all the commands we've enqueued by calling Present. For this first
        // frame we call `present` with a presentation_time = 0 which means it the
        // commands should be applied immediately. For future frames, we'll use the
        // timing information we receive to have precise presentation times.
        let info = session.lock().present(next_presentation_time).await?;

        let presentation_time = info.presentation_time;

        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let mut t = ((presentation_time - last_presentation_time) as f32) * SECONDS_PER_NANOSECOND;
        if last_presentation_time == 0 {
            t = 0.0;
        }
        last_presentation_time = presentation_time;

        view.lock().on_present(t);

        next_presentation_time = info.presentation_time + info.presentation_interval;
    }
}

// This function creates
async fn create_view(scenic: ScenicProxy, view_token: ViewToken) -> Result<(), Error> {
    // Create a Scenic Session and a Scenic SessionListener.
    let (session_proxy, session_request) = create_proxy::<SessionMarker>()?;
    let (listener_request, listener_stream) = create_request_stream::<SessionListenerMarker>()?;
    scenic.create_session(session_request, Some(listener_request))?;

    let session = Session::new(session_proxy);

    let bouncing_ball = Arc::new(Mutex::new(BouncingBall::new(session.clone(), view_token)));

    fasync::spawn(
        session_listener_service(bouncing_ball.clone(), listener_stream)
            .unwrap_or_else(|e| fx_log_err!("failed to listen for events: {:?}", e)),
    );

    fasync::spawn(
        present_view(session, bouncing_ball)
            .unwrap_or_else(|e| fx_log_err!("failed to present: {:?}", e)),
    );

    Ok(())
}

async fn view_provider_service(
    scenic: ScenicProxy,
    mut stream: ViewProviderRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        fx_log_info!("got a view provider event: {:?}", event);

        match event {
            ViewProviderRequest::CreateView {
                token,
                incoming_services: _,
                outgoing_services: _,
                control_handle: _,
            } => {
                fasync::spawn(
                    create_view(scenic.clone(), ViewToken { value: token })
                        .unwrap_or_else(|e| fx_log_err!("failed to spawn: {:?}", e)),
                );
            }
        }
    }

    Ok(())
}

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["bouncing_ball"]).expect("can't init logger");

    let mut executor = fasync::Executor::new().context("error creating executor")?;

    // Connect to Scenic.
    let scenic = connect_to_service::<ScenicMarker>()?;

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn(
            view_provider_service(scenic.clone(), stream)
                .unwrap_or_else(|e| fx_log_err!("failed to spawn {:?}", e)),
        )
    });
    fs.take_and_serve_directory_handle()?;

    let () = executor.run_singlethreaded(fs.collect());

    Ok(())
}
