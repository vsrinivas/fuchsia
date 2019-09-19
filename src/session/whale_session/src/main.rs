// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![allow(dead_code)]

use {
    failure::Error,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_images as images,
    fidl_fuchsia_ui_gfx::{ColorRgb, ColorRgba, DisplayInfo, Quaternion, Vec3},
    fidl_fuchsia_ui_scenic::{ScenicMarker, ScenicProxy},
    fuchsia_async::{self as fasync, Interval},
    fuchsia_component::client::connect_to_service,
    fuchsia_scenic::{
        AmbientLight, Camera, DisplayCompositor, EntityNode, HostImage, HostMemory, Layer,
        LayerStack, Material, Rectangle, Renderer, Scene, Session, SessionPtr, ShapeNode,
    },
    fuchsia_zircon::{ClockId, Duration, Time},
    futures::{StreamExt, TryFutureExt},
    png,
    std::f32::consts::PI,
    std::fs::File,
    std::sync::{Arc, Mutex},
};

pub fn axis_angle(axis: Vec3, angle: f32) -> Quaternion {
    let half_angle = angle / 2.0;
    let sin_half_angle = half_angle.sin();
    Quaternion {
        x: axis.x * sin_half_angle,
        y: axis.y * sin_half_angle,
        z: axis.z * sin_half_angle,
        w: half_angle.cos(),
    }
}

type ContextPtr = Arc<Mutex<Context>>;

pub struct App {
    scenic: ScenicProxy,
    session: SessionPtr,
    context: ContextPtr,
    view: View,
}

struct Context {
    start_time: Time,
    presentation_time: Time,
    session: SessionPtr,
    display_info: DisplayInfo,
}

struct View {
    compositor: DisplayCompositor,
    layer_stack: LayerStack,
    layer: Layer,
    renderer: Renderer,
    scene: Scene,
    camera: Camera,
    bg_shape: ShapeNode,
    rect_shape: ShapeNode,
}

#[derive(Clone)]
struct Rgba {
    r: u8,
    g: u8,
    b: u8,
    a: u8,
}

impl App {
    /// Creates a new instance of this app.
    pub async fn new() -> Result<App, Error> {
        let scenic = connect_to_service::<ScenicMarker>()?;
        let display_info = scenic.get_display_info().await?;

        let session = App::make_session(&scenic)?;
        let context = Context::new(session.clone(), display_info)?;
        let view = View::new(context.clone())?;

        Ok(App { scenic, session, context, view })
    }

    /// Creates a new Scenic session.
    fn make_session(scenic: &ScenicProxy) -> Result<SessionPtr, Error> {
        let (session_proxy, session_request) = create_proxy()?;
        scenic.create_session(session_request, None)?;
        Ok(Session::new(session_proxy))
    }

    /// Sets the interval for updating the whale image.
    pub async fn run(&mut self) -> Result<(), Error> {
        self.run_update_timer(Duration::from_millis(10)).await;

        Ok(())
    }

    /// Runs the update timer.
    async fn run_update_timer(&mut self, duration: Duration) {
        let timer = Interval::new(duration);
        timer.map(move |_| self.update().unwrap()).collect::<()>().await;
    }

    fn present(&self) -> Result<(), Error> {
        fasync::spawn_local(
            self.session
                .lock()
                .present(self.context.lock().unwrap().presentation_time.into_nanos() as u64)
                .map_ok(|_| ())
                .unwrap_or_else(|e| eprintln!("present error: {:?}", e)),
        );

        Ok(())
    }

    fn update(&mut self) -> Result<(), Error> {
        self.context.lock().unwrap().presentation_time = Time::get(ClockId::Monotonic);

        self.view.update(self.context.clone())?;

        self.present()
    }
}

impl Context {
    fn new(session: SessionPtr, display_info: DisplayInfo) -> Result<ContextPtr, Error> {
        Ok(Arc::new(Mutex::new(Context {
            presentation_time: Time::get(ClockId::Monotonic),
            start_time: Time::get(ClockId::Monotonic),
            session,
            display_info,
        })))
    }
}

impl View {
    /// Sets up the Scenic stack, background, and whale shape.
    fn new(context: ContextPtr) -> Result<View, Error> {
        let ctx = context.lock().unwrap();
        let session = &ctx.session;
        let (width, height) =
            (ctx.display_info.width_in_px as f32, ctx.display_info.height_in_px as f32);

        let compositor = DisplayCompositor::new(session.clone());
        let layer_stack = LayerStack::new(session.clone());
        let layer = Layer::new(session.clone());
        let renderer = Renderer::new(session.clone());
        let scene = Scene::new(session.clone());
        let camera = Camera::new(session.clone(), &scene);

        compositor.set_layer_stack(&layer_stack);
        layer_stack.add_layer(&layer);
        layer.set_size(width, height);
        layer.set_renderer(&renderer);
        renderer.set_camera(&camera);

        let ambient_light = AmbientLight::new(session.clone());
        ambient_light.set_color(ColorRgb { red: 1.0, green: 1.0, blue: 1.0 });
        scene.add_ambient_light(&ambient_light);

        let root_node = EntityNode::new(session.clone());
        scene.add_child(&root_node);

        // Setup the background
        let bg_shape = View::make_background(session.clone(), width, height)?;
        root_node.add_child(&bg_shape);

        // Create the whale shape
        let rect_shape = View::make_whale_shape(session.clone())?;
        root_node.add_child(&rect_shape);

        Ok(View { compositor, layer_stack, layer, renderer, scene, camera, bg_shape, rect_shape })
    }

    /// Creates a white background.
    fn make_background(session: SessionPtr, width: f32, height: f32) -> Result<ShapeNode, Error> {
        let bg_material = Material::new(session.clone());
        bg_material.set_color(ColorRgba { red: 255, green: 255, blue: 255, alpha: 255 });
        let bg_rect = Rectangle::new(session.clone(), width, height);
        let bg_shape = ShapeNode::new(session.clone());
        bg_shape.set_shape(&bg_rect);
        bg_shape.set_material(&bg_material);
        bg_shape.set_translation(width / 2.0, height / 2.0, -50.0);

        Ok(bg_shape)
    }

    /// Puts a whale image on a rectangular shape.
    fn make_whale_shape(session: SessionPtr) -> Result<ShapeNode, Error> {
        let whale_material = View::make_whale_material(session.clone())?;
        let rect = Rectangle::new(session.clone(), 500.0, 436.0);
        let rect_shape = ShapeNode::new(session.clone());
        rect_shape.set_shape(&rect);
        rect_shape.set_material(&whale_material);
        rect_shape.set_translation(0.0, 0.0, -100.0);

        Ok(rect_shape)
    }

    /// Creates a whale image and colors it appropriately.
    fn make_whale_material(session: SessionPtr) -> Result<Material, Error> {
        let decoder = png::Decoder::new(File::open("/pkg/data/whale.png")?);
        let (info, mut reader) = decoder.read_info()?;
        let mut buf = vec![0; info.buffer_size()];
        reader.next_frame(&mut buf)?;

        let px_size_bytes = std::mem::size_of::<u8>() * 4; // RGBA

        let (width, height) = (info.width, info.height);
        let size_bytes = width as usize * height as usize * px_size_bytes;
        let image_info = images::ImageInfo {
            transform: images::Transform::Normal,
            width,
            height,
            stride: width * px_size_bytes as u32,
            pixel_format: images::PixelFormat::Bgra8,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::NonPremultiplied,
        };
        let host_memory = HostMemory::allocate(session.clone(), size_bytes)?;
        let host_image = HostImage::new(&host_memory, 0, image_info);

        // swizzle RGBA to BGRA
        for i in (0..size_bytes).step_by(px_size_bytes) {
            let (r, g, b, a) = (buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
            buf[i] = b;
            buf[i + 1] = g;
            buf[i + 2] = r;
            buf[i + 3] = a;
        }

        host_image.mapping().write(&buf);

        let material = Material::new(session);
        material.set_color(ColorRgba { red: 70, green: 150, blue: 207, alpha: 128 });
        material.set_texture(Some(&host_image));

        Ok(material)
    }

    /// Update the to look like it's bouncing.
    fn update(&self, context: ContextPtr) -> Result<(), Error> {
        const SPEED: f32 = 0.25;
        const SECONDS_PER_NANOSECOND: f32 = 1e-9;

        let ctx = context.lock().unwrap();

        let t = ((ctx.presentation_time.into_nanos() - ctx.start_time.into_nanos()) as f32
            * SECONDS_PER_NANOSECOND
            * SPEED)
            % 1.0;

        let (center_x, center_y) =
            (ctx.display_info.width_in_px as f32 / 2.0, ctx.display_info.height_in_px as f32 / 2.0);

        const BOUNCE_HEIGHT: f32 = 300.0;
        let y_bounce = ((2.0 * t - 1.0).powf(2.0) + 1.0) * BOUNCE_HEIGHT;
        self.rect_shape.set_translation(center_x, center_y - BOUNCE_HEIGHT + y_bounce, -100.0);

        let angle = t * PI * 2.0;
        let quat = axis_angle(Vec3 { x: 0.0, y: 0.0, z: 1.0 }, angle);
        self.rect_shape.set_rotation(quat.x, quat.y, quat.z, quat.w);

        Ok(())
    }
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut app = App::new().await?;
    app.run().await
}
